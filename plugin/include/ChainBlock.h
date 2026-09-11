#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "BlockEq.h"
#include "BlockPredelay.h"
#include "BlockSpectrum.h"
#include "ChainOversampler.h"
#include "NamEngine.h"

// Chain block types
enum class ChainBlockType { NAM, IR, INSERT };

inline juce::String chainBlockTypeToString(ChainBlockType type) {
  switch (type) {
    case ChainBlockType::NAM: return "nam";
    case ChainBlockType::INSERT: return "insert";
    case ChainBlockType::IR: break;
  }
  return "ir";
}

inline ChainBlockType chainBlockTypeFromString(const juce::String& s) {
  if (s == "nam") return ChainBlockType::NAM;
  if (s == "insert") return ChainBlockType::INSERT;
  return ChainBlockType::IR;
}

// Which chain is being processed/edited in stereo mode.
enum class ChainSide { Left, Right };

constexpr int kNumLanes = 2;
inline int laneIndex(ChainSide side) { return side == ChainSide::Right ? 1 : 0; }

// Wet-path fade time (see ChainBlock::wetFadeGain): every discontinuous
// per-block transition (engine swap, power toggle, block add/removal)
// glides the block's wet mix through bypass over this ramp instead of
// splicing the waveform (audible click). Also the ramp for the global
// chain-edit fade (reorder/cross-lane moves mute-splice the chain output).
constexpr double kWetFadeSeconds = 0.025;

// Minimum tiles per lane. A lane always presents at least this many blocks
// (tones + insert placeholders), and always at least one insert placeholder,
// so an empty lane shows kMinLaneSlots empty slots, and once the user has
// filled them all there is still one trailing empty slot to add into. The
// invariant (insertCount == max(kMinLaneSlots - toneCount, 1)) is enforced by
// TONE3000Processor::normalizeLaneInserts after every structural change, with
// one relaxation: while a stereo branch is active, the branch lane's surplus
// trailing inserts are trimmed below this baseline so its indented rail ends
// level with the trunk lane (see alignBranchLaneLengths).
constexpr int kMinLaneSlots = 5;

// Chain block data structure
struct ChainBlock {
  std::string id;  // Chain block UUID
  ChainBlockType type;

  // Tone metadata (full tone JSON stored for complete state persistence)
  int toneId;
  juce::String toneJson;  // Complete tone JSON from TONE3000 API
  int activeModelId;      // Currently active model ID (single source of truth)

  // Parsed-once copy of toneJson (full API payload; model switching needs
  // the model URLs) and the slim projection getChainState ships to the UI
  // (title/images/user/model names only). Both are ref-counted vars, so
  // serializing chain state is O(1) per block instead of a JSON re-parse.
  // Set together wherever toneJson is set; see setToneOnBlock.
  juce::var toneVar;
  juce::var toneSummary;

  // Model cache: stores downloaded model data by model ID
  std::map<int, std::vector<uint8_t>> modelCache;

  // State flags
  bool loaded;   // True when active model is loaded and ready
  bool enabled;  // True when block is enabled in processing chain

  // True when the last download/prepare of the active model failed (network
  // down, tone3000.com unreachable, bad model data). The UI swaps its loading
  // dots for a retry affordance targeting retryModelLoad. Runtime-only,
  // never persisted; cleared whenever a new load is queued.
  bool loadFailed{false};

  // True while a background download/prepare of the active model is in
  // flight. Split from `loaded` so a model switch/tone swap keeps the
  // previous engine processing (`loaded` stays true) while the replacement
  // downloads; the UI keys its loading affordances off this flag.
  // Runtime-only, never persisted.
  bool modelLoading{false};

  // One-shot: armed by loadTone (Select-flow) so the block's first
  // successful load sets the default mix from the actual model (long IR =
  // half wet, only known once the file arrives). Cleared on first apply;
  // never set by swaps/switches/restores, which keep the user's mix.
  // Runtime-only, never persisted.
  bool applyDefaultMixOnLoad{false};

  // Click-free wet-path fade (audio thread) + swap handshake.
  // `wetFadeGain` multiplies the block's wet mix and is the smoothing path
  // for every transition whose end state is bypass: the audio thread targets
  // it at 1 while the block wants to be heard (`enabled` and no swap
  // pending) and 0 otherwise, so power toggles glide through bypass, fresh
  // blocks fade in from bypass, and removals fade out before detaching.
  //
  // The handshake: another thread raises `swapFadePending` (engine swap,
  // failure drop, removal), the audio thread fades to silence and raises
  // `swapFadeDone`, and the requester then applies its change under the
  // chain lock (see requestSwapFadeAndWait: bounded wait; when no
  // callbacks are running the change applies directly, nothing is audible).
  //
  // Two fade shapes, picked by `swapMuteWet`:
  //  - false (bypass fade): wetFadeGain rides the mix and glides the
  //    post-mix Out Gain to unity in step, so the output crossfades toward
  //    the block's dry input at pass-through level. Right for transitions
  //    that END at bypass (power off, removal, failure drop, fresh-block
  //    fade-in; unity dry is what plays afterwards anyway).
  //  - true (wet mute): engine swaps end back at wet, and their dry input
  //    was never audible; at 100% mix crossfading through it blasts ~50 ms
  //    of the un-cabbed/un-ampped signal (a raw amp head into no cab is a
  //    loud bright burst). Instead `swapWetMuteGain` mutes just the wet
  //    term while the dry share of the user's mix holds steady: the old
  //    engine dips to silence, engines swap, the new one fades in from
  //    silence. wetFadeGain stays at 1 throughout.
  // Both gains are plain multipliers in the mix loop, so the shapes compose
  // (a power toggle mid-swap still glides to bypass through wetFadeGain).
  std::atomic<bool> swapFadePending{false};
  std::atomic<bool> swapFadeDone{false};
  std::atomic<bool> swapMuteWet{false};
  juce::LinearSmoothedValue<float> wetFadeGain;
  juce::LinearSmoothedValue<float> swapWetMuteGain{1.0f};

  // Set by the audio thread when NAM processing throws (the block is disabled
  // in the same breath). The message thread drains it in getChainState and
  // writes the log line there; string building/logging is not RT-safe.
  std::atomic<bool> rtProcessingFailed{false};

  // NAM-specific processing (runs at the chain rate; see ChainDomain.h)
  std::unique_ptr<NamEngine> namEngine;
  juce::LinearSmoothedValue<float> namNormalizationSmoother;

  // IR-specific processing.
  // convolverMono: IR channel 0 loaded with Stereo::no; applies the same (left) kernel to
  //   every audio channel. Always present for a loaded IR; used as the mono fallback.
  // convolverStereo: IR loaded with Stereo::yes; audio ch0 ⊗ IR ch0, audio ch1 ⊗ IR ch1.
  //   Only created when the IR file actually has >= 2 channels (true stereo IR).
  // The convolution engine is picked at load time by IR length: cab IRs use
  // JUCE's uniform zero-latency engine, reverb-length IRs the two-stage
  // non-uniform engine (also zero latency); see prepareBlockModelOffThread.
  std::unique_ptr<juce::dsp::Convolution> convolverMono;
  std::unique_ptr<juce::dsp::Convolution> convolverStereo;
  // Convolution always runs at kChainBaseSampleRate: when the chain is
  // oversampled this island decimates the block's wet path to the base rate
  // around the convolver and interpolates back (linear processing gains
  // nothing from oversampling; its CPU scales ~quadratically with the rate).
  // Bypass (zero-cost) at factor 1. See ChainOversampler.h.
  ChainOversampler irBaseRateIsland;
  int irNumChannels{1};  // channels in the loaded IR file (1 or 2)
  // Loaded IR length in base-rate samples (post trim + resample, read off
  // the built engine). Feeds refreshIrTailLength / getTailLengthSeconds so
  // hosts render real reverb tails.
  int irLengthBaseSamples{0};
  // The single short/long classification (kernel length vs the cutoff in
  // ProcessorModelLoader.cpp). Short = cab-like: -18 dB output pad
  // (spectrally concentrated kernels play back hot at unit energy), 100%
  // default mix. Long = reverb-like: no pad (diffuse kernels sit at ≈ dry
  // level at unit energy), 50% default mix. Shipped to the UI as `irLong`.
  bool irIsLong{false};
  juce::LinearSmoothedValue<float> irNormalizationSmoother;
  float irNormalizationGainLinear{1.0f};

  // Untouched, file-rate copy of the loaded IR (source of truth for the
  // waveform display and future Length/Decay/Curve editing) plus a
  // downsampled { min, max } per column for that display. Runtime-only:
  // never persisted, rebuilt from the cached model bytes on restore the
  // same way convolverMono itself is. Cleared when the block's IR is
  // swapped/removed (see ProcessorModelLoader.cpp's apply step).
  juce::AudioBuffer<float> irRawSamples;
  double irRawSampleRate{0.0};
  // Detected end of audible content within irRawSamples (file-rate samples):
  // -60 dB relative to peak, scanned backward in ~2ms pooled-RMS windows,
  // plus a margin (see computeIrContentLengthSamples). Single source of
  // truth for the waveform display's auto-fit trim and the length label -
  // shipped to the UI as irContentLengthMs (ms, at irRawSampleRate).
  int irContentLengthSamples{0};
  std::vector<std::pair<float, float>> irWaveformPeaks;

  // Predelay: delays the wet signal before it enters the convolver (see
  // BlockPredelay). Runs inside irBaseRateIsland, always at the base rate.
  // predelayNormalized is the persisted 0..1 knob value (same convention as
  // mixNormalized/inputGainNormalized); the engine is driven in real ms via
  // setDelayMs(predelayNormalized * BlockPredelay::kMaxDelayMs).
  BlockPredelay predelay;
  float predelayNormalized{0.0f};

  // IR envelope: a 2-segment Attack/Decay shape (Space Designer-style) over
  // the loaded IR, truncating it (+ short fade-out) and applying a gain
  // envelope in one pass - see TONE3000Processor::prepareIrShapeRebuild for
  // the exact formula. Rebuilds the convolver engine off-thread rather than
  // processing in real time (see rebuildIrShapeInBackground) - unlike
  // Predelay, this edits what the convolver *is*, not a real-time DSP stage.
  //
  // Init Level is the level at sample 0 (the origin point, not part of
  // either segment). The Attack segment runs from there up to unity/0dB -
  // pinned, not a knob: standard AD-envelope semantics (Attack always
  // reaches full level; only Decay's target level is adjustable) - then the
  // Decay segment continues from that peak to the truncated content's end
  // (Decay Length, Decay Level). Both adjustable levels are unipolar
  // attenuation-only (0..1, 1.0 = unity/0dB, 0.0 = genuine silence - see
  // prepareIrShapeRebuild's levelToDb for why that needs a small
  // float-safety floor internally but still lands on exact silence at the
  // sample the knob targets). Each segment has its own continuous curve
  // control (0.5 default = linear-in-dB/"exponential", sweeping
  // steeper-front-loaded below and back-loaded above - same kCurveMax
  // power-curve formula Length+Decay used before this became two segments).
  //
  // Length semantics: decayLengthNormalized is the TOTAL truncated length -
  // the real "End" position, exactly the old standalone Length knob's own
  // convention: a fraction of the block's full frozen detected content
  // (irContentLengthSamples, load-time). attackLengthNormalized is NOT an
  // independent segment length - it's a fraction *of that total*, marking
  // where the peak (the Attack/Decay boundary) sits within it, so it's
  // naturally bounded to [0, the total] by construction (a fraction of a
  // fraction), no separate clamp/edge-case needed, and dragging Attack
  // Length alone can never change the total window length - only Decay
  // Length does that. Defaults (attackLength 0.0, decayLength 1.0, every
  // level 1.0) reconstruct the pre-segment behavior exactly: no attack ramp,
  // decay spans the full content, flat/unity envelope - a genuine no-op,
  // matching the old lengthNormalized=1.0/level=unity defaults.
  // irIsLong/irNumChannels and irRawSamples/irContentLengthSamples/
  // irWaveformPeaks never change from an envelope edit - only the live
  // convolverMono/convolverStereo and irLengthBaseSamples do; the waveform
  // display's fixed window and the -18dB cab pad / default mix stay exactly
  // what they were at load.
  float initLevelNormalized{1.0f};
  float attackLengthNormalized{0.0f};
  float attackCurveNormalized{0.5f};
  float decayLengthNormalized{1.0f};
  float decayLevelNormalized{1.0f};
  float decayCurveNormalized{0.5f};

  // Bumped by setBlockIrDecay, captured by rebuildIrShapeInBackground's
  // caller as the generation it's targeting: a "latest wins" supersede
  // guard, since a rebuild must read *all seven* shaping params fresh off
  // the block rather than trusting a single stashed target value.
  std::atomic<int> irShapingGeneration{0};

  // Per-block loudness normalization toggle, NAM only (off = the capture's
  // true level, which is real information; IR normalization is always on
  // because an IR file's absolute level means nothing). On by default; part
  // of the chain state so presets carry their own gain staging. The UI
  // exposes it as an optional (=) header control behind an advanced
  // preference.
  bool normalizeEnabled{true};

  // Per-block NAM A2 size, stored in NAM's own slimmable-size domain (0..1;
  // 0.0 = lite, 1.0 = full, and the tier boundary belongs to the tier above,
  // so 0.5 already selects full). The value feeds
  // NamEngine::setSlimmableSize verbatim. Inert for IR blocks, like
  // normalizeEnabled. Part of the chain state so presets carry each block's
  // size; new blocks start at the machine-wide default
  // (TONE3000Processor::setNamSlimSizeDefault) and setBlockSlimSize retiers
  // the loaded engine in place.
  double namSlimSize{0.0};

  // Per-block controls (normalized 0..1)
  float inputGainNormalized{0.5f};  // 0.5 = unity gain; drives the block harder/softer
  juce::LinearSmoothedValue<float> inputGainSmoother;
  float outputGainNormalized{0.5f};  // 0.5 = unity gain
  juce::LinearSmoothedValue<float> outputGainSmoother;
  float mixNormalized{1.0f};  // 0 = dry, 1 = wet
  juce::LinearSmoothedValue<float> mixSmoother;

  // Per-block meter levels (dB, -60 floor). Written by the audio thread every
  // block, read by the UI via getMeterLevels(). Input is measured post
  // input-gain (what the model actually receives), output post mix + Out
  // Gain.
  std::atomic<float> inputMeterDb{-60.0f};
  std::atomic<float> outputMeterDb{-60.0f};

  // Per-block 6-band EQ: on the wet signal after the model by default
  // (before Out Gain and the mix), or between the input gain and the model
  // when its pre flag is on. Flat by default, in which case processing is
  // skipped entirely (single branch per audio block).
  BlockEq eq;

  // Spectrum analyzer for the EQ editor backdrop. Only fed by the audio thread
  // while the UI has this block's EQ view open (atomic enabled flag).
  BlockSpectrum spectrum;

  ChainBlock(const std::string& blockId, ChainBlockType blockType)
      : id(blockId), type(blockType), toneId(0), activeModelId(0), loaded(false),
        enabled(true) {}
};
