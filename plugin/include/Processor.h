#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "NAM/activations.h"
#include "NAM/convnet.h"
#include "NAM/lstm.h"
#include "NAM/util.h"
#include "NAM/wavenet/model.h"
#include "ChainBlock.h"
#include "ChainDomain.h"
#include "ChainHistory.h"
#include "ChainOversampler.h"
#include "MidiMapper.h"
#include "NoiseGate.h"
#include "Spread.h"
#include "PresetManager.h"
#include "TunerDetector.h"

class TONE3000Processor;

class TONE3000Processor : public juce::AudioProcessor,
                          private juce::AudioProcessorValueTreeState::Listener,
                          private juce::AsyncUpdater {
public:
  TONE3000Processor();
  ~TONE3000Processor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;

  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
  using AudioProcessor::processBlock;

  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override;

  const juce::String getName() const override;
  double getTailLengthSeconds() const override;

  bool acceptsMidi() const override;
  bool producesMidi() const override;
  bool isMidiEffect() const override;
  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int) override;
  const juce::String getProgramName(int) override;
  void changeProgramName(int, const juce::String&) override;

  void getStateInformation(juce::MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;

  juce::AudioProcessorValueTreeState parameters;

  juce::AudioProcessorValueTreeState& getParameters() { return parameters; }

  // MIDI CC/note → parameter mapping with Learn (see MidiMapper). Fed every
  // processBlock; serializes with plugin state so maps travel with DAW
  // sessions and the standalone's saved session alike.
  MidiMapper midiMapper{parameters};

  // Chain management methods
  // Load a tone into an insert slot. `targetInsertId` is the insert block the
  // user clicked (it survives the OAuth redirect in the UI's sessionStorage);
  // the new tone block takes that slot's position. When the id is absent or
  // stale (undone away mid-flow), the active lane's first insert is used.
  std::string loadTone(const juce::String& toneJsonString,
                       const std::string& targetInsertId = {});
  // Replace the tone of an existing block in place. Keeps the block's chain
  // position and user params (enabled/gains/mix); the new tone's first model
  // is queued for background loading.
  bool swapTone(const std::string& blockId, const juce::String& toneJsonString);
  // Switch the block's active model. Native only stores the active model, so
  // `modelData` (JSON object with id/name/model_url, paged in from the API by
  // the UI) is required and becomes the tone's new sole stored model.
  bool switchModel(const std::string& blockId, int modelId, const juce::var& modelData);
  // Retry a failed model download (block.loadFailed): clears the flag and
  // re-queues the block's active model through the background loader. The
  // simplest recovery when tone3000.com was unreachable mid-load.
  bool retryModelLoad(const std::string& blockId);
  bool removeChainBlock(const std::string& blockId);
  bool reorderChainBlocks(const std::vector<std::string>& newOrder);
  // Move a block into the other lane at the given index (stereo mode drag
  // across chains). Engines move with the block; insert slots can't move.
  bool moveBlockToChain(const std::string& blockId, const juce::String& side, int index);

  // TONE3000 OAuth access token. Updated by the UI after the Select flow and
  // again on every refresh. `fetchModelFromUrl` attaches it as a Bearer header
  // because the new TONE3000 model_url endpoints reject anonymous requests.
  void setAccessToken(const juce::String& token);
  juce::String getAccessToken() const;
  
  // Background loading (called by thread pool jobs)
  void loadToneInBackground(const std::string& blockId, int firstModelId,
                            const juce::String& modelUrl, const juce::String& modelName,
                            ChainBlockType type);
  void switchModelInBackground(const std::string& blockId, int modelId, 
                               const juce::String& modelUrl, const juce::String& modelName);

  // Chain state for the UI. `knownRevision` is the last revision the caller
  // saw (-1 for "give me everything"); when the chain hasn't changed since,
  // a minimal { revision, unchanged: true } object is returned so the UI's
  // poll loop stays cheap.
  juce::var getChainState(int knownRevision) const;
  // Current chain revision, promoting any settled continuous-gesture edit
  // into a real bump first. The editor's push timer watches this to emit a
  // `chainChanged` event to the webview, so the UI resyncs immediately after
  // mutations instead of fast-polling.
  juce::uint32 getCurrentChainRevision() const;
  // Single entry point for all per-block user params. Supported params:
  // "enabled" (0/1), "normalize" (0/1), "inputGain", "outputGain", "mix"
  // (normalized 0..1). Returns false for unknown blocks/params. Continuous
  // params defer their revision bump to the end of the gesture (see
  // deferredRevisionBump) so drag-rate calls never force full chain resyncs.
  bool setBlockParam(const std::string& blockId, const juce::String& param, double value);

  // ── Global NAM A2 size (machine-wide user setting) ──
  // One tier for every A2 NAM block: false = lite, true = full. Deliberately
  // NOT part of chains/presets/undo — it describes the user's machine (CPU
  // budget), not the tone — so it persists in the shared TONE3000 settings
  // file and applies across every instance's loads. Setting it retiers all
  // loaded NAM engines in place (under the chain-edit fade) and bumps the
  // chain revision so the UI resyncs.
  bool getNamFullSize() const { return namFullSize.load(); }
  void setNamFullSize(bool full);

  // All meter levels in one call: { input, output, blocks: { id: { in, out } },
  // cpu (0..1 audio-callback load) }. Values in dB with a -60 floor. Designed
  // to be polled once per UI frame.
  juce::var getMeterLevels() const;

  // Per-block EQ (post-block by default, pre-model when its pre flag is on).
  // setBlockEqBand takes { type, freqHz, gainDb, q } for one band — the undo
  // stack's mutation granularity. Band drags defer their revision bump like
  // continuous block params (see deferredRevisionBump).
  bool setBlockEqBand(const std::string& blockId, int bandIndex, const juce::var& bandVar);
  bool setBlockEqEnabled(const std::string& blockId, bool enabled);
  bool setBlockEqPre(const std::string& blockId, bool pre);
  bool resetBlockEq(const std::string& blockId);

  // Per-block spectrum for the EQ editor backdrop. The UI enables a block's
  // analyzer while its EQ view is open and polls getBlockSpectrum (~30 Hz);
  // when disabled the audio thread does no analyzer work for that block.
  bool setBlockSpectrumEnabled(const std::string& blockId, bool enabled);
  juce::var getBlockSpectrum(const std::string& blockId);
  // Editor teardown: the webview can't send per-block disables while dying.
  void disableAllBlockSpectrums();

  // Stereo mode: two independent Left/Right chains.
  void setStereoMode(bool enabled);
  bool isStereoMode() const { return stereoEnabled.load(); }

  // Standalone input channel mode. Interfaces usually expose stereo pairs
  // (line 1+2) even when only one jack is plugged in, so the standalone app
  // lets the user pick which channel actually carries signal — the industry
  // Which channels of a stereo source feed the plugin: both (default), or
  // one folded onto both — a mono take of a stereo bus/interface pair. Set
  // from the faceplate input-mode button (visible only when the source is
  // actually stereo; see stereoInputDetected). Saved with the plugin/session
  // state but deliberately not with presets: it's I/O routing, not tone.
  enum class InputMode { Stereo = 0, Left = 1, Right = 2 };
  void setInputMode(InputMode mode);
  InputMode getInputMode() const { return static_cast<InputMode>(inputMode.load()); }
  static InputMode inputModeFromString(const juce::String& s) {
    if (s == "left") return InputMode::Left;
    if (s == "right") return InputMode::Right;
    return InputMode::Stereo;
  }
  static juce::String inputModeToString(InputMode mode) {
    switch (mode) {
      case InputMode::Left: return "left";
      case InputMode::Right: return "right";
      case InputMode::Stereo: break;
    }
    return "stereo";
  }

  // Editor window scale, 1.0 = the 1024x600 design size. Written by the
  // editor whenever it is resized and read back when a new editor opens, so
  // the chosen size survives editor teardown. Saved with the plugin/session
  // state but deliberately not with presets: window size is a workstation
  // preference, not tone. Atomic: hosts may call get/setStateInformation off
  // the message thread.
  std::atomic<double> editorScale{1.0};

  // Which lane loadTone falls back to ("left"/"right") when no valid target
  // insert id is supplied. The UI sets this before launching the Select flow
  // so the choice survives the OAuth redirect. Not a view mode and not part
  // of undo history.
  void setActiveEditChain(const juce::String& side);
  // Swap the Left and Right chains wholesale (stereo mode only). Undoable.
  bool swapChains();

  // Undo/redo over chain edits (structure, tones, params, EQ, stereo mode).
  // Snapshot-based: every mutator captures the pre-mutation chain settings
  // (see ChainHistory); undo/redo restore by reconciling against the live
  // chains so loaded engines are reused whenever the tone/model still match.
  // Both return false when there is nothing to undo/redo.
  bool undoChain();
  bool redoChain();

  // ── Internal presets (ProcessorPresets.cpp) ──
  // A preset = chain snapshot (with embedded model bytes, so it loads
  // offline) + the faceplate parameter values (kPresetParameterIds). The
  // active preset { id, name } rides getChainState — it only ever changes
  // together with a revision bump. Preset files live in the shared user
  // presets folder (see PresetManager).
  juce::var getPresetList() const;    // { presets: [{ id, name, factory }] }
  juce::var savePreset(const juce::String& name);  // { id, name } or void var on failure
  bool loadPreset(const juce::String& presetId);   // undoable (chain part)
  bool renamePreset(const juce::String& presetId, const juce::String& newName);
  bool deletePreset(const juce::String& presetId);
  // Move a preset one step up/down within its browser section. The custom
  // order is user-facing truth: prev/next stepping and MIDI program-change
  // numbers follow it (see loadPresetAtIndex).
  bool movePreset(const juce::String& presetId, int delta);

  // Tuner: enabled by the UI while the tuner screen is visible. Reads the raw
  // (pre-gain, pre-gate) input so gating never starves the pitch detector.
  void setTunerEnabled(bool enabled) { tuner.setEnabled(enabled); }
  juce::var getTunerReading() { return tuner.getReading(); }

  // ── Auto balance: one-shot chain energy match ──
  // startAutoBalance() arms a "listening" measurement: the audio thread
  // accumulates the raw chain outputs' energy (pre-balance, pre-pan, so the
  // result is the chains' true mismatch at any pan position; silence-gated)
  // until ~2 s of real signal has been heard, then pollAutoBalance() —
  // called from the message thread by the UI's poll loop — maps the measured
  // dB difference onto the outputBalance parameter (host-automatable, so
  // undo/presets come free).
  // One-shot measurement of real playing is the industry norm here:
  // continuous AGC fights the player's dynamics, and an injected noise burst
  // is both audible and unrepresentative for nonlinear amp chains.
  void startAutoBalance();
  void cancelAutoBalance();
  juce::var pollAutoBalance();  // { state: "idle"|"listening"|"done"|"timeout", matchedDb? }

  // Location of the on-disk diagnostic log. Single source of truth shared by the
  // FileLogger setup and the UI's "copy/reveal logs" actions so they never drift.
  // macOS: ~/Library/Logs/TONE3000/TONE3000.log, Windows: %APPDATA%/TONE3000/TONE3000.log
  static juce::File getLogFile();

private:
  // One chain of blocks. Two of these make up `lanes` (declared below).
  using Lane = std::vector<std::unique_ptr<ChainBlock>>;

  // Helper methods
  // Attenuation-only unit-energy gain for an IR file, matched to what the
  // convolver actually runs (rate-corrected, same length cap). `maxIrFileSamples`
  // is in the file's own sample rate, like the cap passed to loadImpulseResponse.
  float computeIrNormalizationGain(const juce::File& irFile, size_t maxIrFileSamples);
  juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
  
  // Tone loading helpers
  std::vector<uint8_t> fetchModelFromUrl(const juce::String& modelUrl);

  /** Largest frame count the chain stage can see per boundary callback at the
      base rate: the host max block size converted to 48 kHz frames (and never
      below the host size, so the direct 48k path is covered too). Floors at 1
      to avoid prepare(0). */
  int chainBaseBlockSize() const noexcept;

  /** Largest frame count the chain domain can see per callback — the base
      block size times the current oversampling factor. */
  int chainDomainBlockSize() const noexcept;

  /** The effective chain rate: kChainBaseSampleRate × oversampling factor.
      Safe from any thread (the factor is atomic; it only changes inside the
      full re-prepare paths, never under the audio thread's feet). */
  double chainSampleRate() const noexcept {
    return kChainBaseSampleRate * chainOversampleFactor.load(std::memory_order_relaxed);
  }

  struct PreparedBlockModel {
    bool success = false;

    // The chain-domain block size the engines were prepared for. Prepares can
    // race prepareToPlay at startup (restore-time loads run first), so the
    // apply step re-prepares when this is smaller than the live domain size.
    // (An oversampling *factor* change in flight is handled separately: NAM
    // engines carry their phase count and get re-queued by the apply step;
    // IR convolvers are rate-independent — always built at the base rate.)
    int preparedBlockSize = 0;

    std::unique_ptr<NamEngine> namEngine;

    std::unique_ptr<juce::dsp::Convolution> convolverMono;
    std::unique_ptr<juce::dsp::Convolution> convolverStereo;
    int irNumChannels = 1;
    int irLengthBaseSamples = 0;  // base-rate kernel length (tail reporting)
    bool irIsLong = false;        // short/long classification (see ChainBlock.h)
    juce::File irTempFile;
    float irNormalizationGainLinear = 1.0f;
  };

  /** CPU/file heavy; call without holding `chainMutex`. NAM engines come out
      at the current global A2 tier (see setNamFullSize). */
  PreparedBlockModel prepareBlockModelOffThread(ChainBlockType type, const std::vector<uint8_t>& modelData,
                                                const juce::String& filename);
  /** Short path under `chainMutex` only: swaps the new engines onto `block`
      and stamps `newType` (a tone swap may change the block's type — the old
      engine kept processing under the old type until this moment). The
      block's *old* engines end up back in `prepared` — the caller must let
      `prepared` die *after* releasing the lock, because engine destructors
      (NAM graphs, convolution state) are far too heavy to run while the audio
      thread may be blocked on chainMutex. */
  void applyPreparedModelToChainBlock(ChainBlock& block, ChainBlockType newType,
                                      PreparedBlockModel& prepared);

  /** Ask the audio thread to glide `blockId`'s wet mix down to bypass, then
      wait (bounded) for the fade, so the caller's change (engine swap,
      failure drop, block removal) never splices the waveform (audible
      click). Returns immediately when the block isn't audibly processing or
      no audio callbacks are running (the change is inaudible then anyway).
      Must be called WITHOUT holding chainMutex. */
  void requestSwapFadeAndWait(const std::string& blockId);

  /** Same idea for structural edits that can't be expressed as one block's
      wet fade (reorder, cross-lane move): glide the whole chain output to
      silence and wait (bounded) so the edit splices in silently. The caller
      clears `chainEditFadePending` afterwards to glide back — use the
      ChainEditFade RAII below. Must be called WITHOUT holding chainMutex. */
  void requestChainEditFadeAndWait();

  /** RAII wrapper for the chain-edit fade: fades the chain output to silence
      on construction, lets it glide back on scope exit (after the mutation,
      including early-error returns). */
  struct ChainEditFade {
    explicit ChainEditFade(TONE3000Processor& proc) : p(proc) {
      p.requestChainEditFadeAndWait();
    }
    ~ChainEditFade() { p.chainEditFadePending.store(false); }
    TONE3000Processor& p;
  };

  /** True while the audio thread is actively receiving callbacks (updated
      every processBlock). The fade handshakes skip their bounded waits when
      audio is stopped — nothing is audible, and blocking a click gesture on
      the message thread for the full timeout would feel sluggish. */
  bool isAudioActive() const {
    return juce::Time::currentTimeMillis() - lastAudioCallbackMs.load() < 150;
  }

  // Run one chain (the per-block loop) over the supplied working buffer. The buffer may have
  // 1 channel (a single side in stereo mode) or 1-2 channels (mono mode). All per-channel work
  // is keyed on buffer.getNumChannels(). Runs inside the chain domain (48 kHz — see
  // ChainDomain.h). Must be called while holding `chainMutex`.
  void processChainOnBuffer(std::vector<std::unique_ptr<ChainBlock>>& blocks,
                            juce::AudioBuffer<float>& buffer);

  // The whole chain stage at the chain rate: lane L (and lane R in stereo
  // mode) over the given channel pointers. Called either directly (48k host)
  // or as the boundary resampler's encapsulated callback. `inputs`/`outputs`
  // both carry 2 pointers; processing is in place on `outputs` after an
  // input→output copy (skipped when they alias). Caller holds `chainMutex`.
  void processChainStage(float** inputs, float** outputs, int numFrames);

  // Prepare every engine in a chain for the current chain rate (see
  // chainSampleRate) and chain-domain block size. Holds no lock.
  void prepareChain(std::vector<std::unique_ptr<ChainBlock>>& blocks);

  // Recompute the longest loaded IR across both lanes into irTailBaseSamples
  // (base-rate samples — IRs always convolve at the base rate, see
  // ChainBlock::irBaseRateIsland). Called wherever the set of live IR engines
  // can change: model apply, block removal, snapshot restore. Caller must
  // hold chainMutex; getTailLengthSeconds reads the atomic lock-free.
  void refreshIrTailLength();
  std::atomic<int> irTailBaseSamples{0};

  // The chain the UI edits/adds to right now (Left in mono mode, or the active side in stereo).
  std::vector<std::unique_ptr<ChainBlock>>& activeChain();

  // Enforce the lane's insert-slot invariant after any structural change:
  // insertCount == max(kMinLaneSlots - toneCount, 1). Shortfalls append fresh
  // placeholders (UUID ids) at the end; overshoot trims inserts from the end
  // so slots the user positioned earlier in the lane stay put. Inserts own no
  // engines, so add/remove is trivially cheap. Caller holds chainMutex.
  void normalizeLaneInserts(Lane& l);

  // Find a block by id across both chains (ids are globally unique). Returns nullptr if absent.
  ChainBlock* findBlockById(const std::string& blockId);

  // State (de)serialization helpers.
  // serializeBlockSettings/applyBlockSettings cover everything user-editable
  // on a block (identity, tone refs, gains, mix, EQ) — the single source of
  // truth shared by plugin state persistence and undo/redo snapshots. Model
  // bytes are only included when `includeModelData` is set (project files).
  static juce::ValueTree serializeBlockSettings(const ChainBlock& block);
  void applyBlockSettings(ChainBlock& block, const juce::ValueTree& blockState);
  // Set a block's tone identity in one place: raw JSON, parsed var, and the
  // slim UI summary (see makeToneSummary) always stay in sync.
  static void setToneOnBlock(ChainBlock& block, int toneId, const juce::String& toneJson,
                             const juce::var& parsedTone);
  // Slim tone projection for getChainState: the UI renders only
  // id/title/format/gear/first image/user/model names — shipping the full
  // API payload (model URLs, tags, counts…) per block per sync is waste.
  static juce::var makeToneSummary(const juce::var& toneVar);
  static void serializeChainToTree(const std::vector<std::unique_ptr<ChainBlock>>& blocks,
                                   juce::ValueTree& chainState, bool includeModelData);

  // ── Undo/redo internals (ProcessorHistory.cpp) ──
  // Snapshot both chains + stereo mode as a ValueTree. History snapshots stay
  // settings-only; presets embed the model bytes so they load offline.
  // Caller must hold chainMutex.
  juce::ValueTree captureChainSnapshot(bool includeModelData = false) const;
  // Record the pre-mutation state before a chain edit. `coalesceKey` groups a
  // continuous gesture (knob/EQ drags) into a single undo step; pass an empty
  // string for discrete edits. Caller must hold chainMutex.
  void pushChainHistory(const juce::String& coalesceKey = {});
  // Restore a snapshot by reconciling against the live chains: blocks whose
  // tone/model still match keep their loaded engines (undoing a knob tweak
  // never reloads a model); everything else is rebuilt and queued for a
  // background load. Caller must hold chainMutex — and must destroy the
  // returned retired blocks *after* releasing it (engine teardown is heavy).
  [[nodiscard]] Lane restoreChainSnapshot(const juce::ValueTree& snapshot);
  void reconcileChainFromTree(const juce::ValueTree& chainState, Lane& target, Lane& retired);
  // Queue a background download+prepare of `block`'s active model, resolving
  // url/name from its tone JSON. Used by undo/redo when a restored block's
  // model isn't cached in memory anymore. When the model can't even be
  // resolved from the stored tone JSON (corrupt/legacy state), the block is
  // flagged loadFailed so the UI shows the retry affordance instead of an
  // eternal loader.
  void queueActiveModelLoad(ChainBlock& block);

  ChainHistory chainHistory;

  // ── MIDI performance handlers (wired to midiMapper in the constructor,
  //    both invoked on the message thread) ──
  // Program change n loads the nth preset in list order (factory first, then
  // user, both alphabetical — the same order the preset browser shows).
  // Out-of-range programs are ignored.
  bool loadPresetAtIndex(int index);
  // Toggle the enabled flag of the chain's Nth tone block (0-based, insert
  // slots skipped; Left lane in stereo). Positional so mappings survive tone
  // swaps and preset loads. No-op when the chain is shorter than N.
  bool toggleBlockPower(int position);

  // ── Preset internals (ProcessorPresets.cpp) ──
  // The faceplate parameters a preset carries. Explicitly scoped: rig
  // calibration (calibrateInput, inputCalibrationLevel) and the global
  // loudness target (targetLoudness) describe the user's setup, not the
  // tone, so they stay out of presets. (Per-block normalization rides the
  // chain snapshot itself.)
  static const std::vector<juce::String>& presetParameterIds();
  void setActivePreset(const juce::String& id, const juce::String& name);

  PresetManager presetManager;
  // Shown in the preset pill; guarded by chainMutex (written on the message
  // thread, read by getChainState).
  juce::String activePresetId;
  juce::String activePresetName;

  // True when running as the standalone app with a mono input device selected.
  // Detected in prepareToPlay (device changes re-trigger it); processBlock then
  // duplicates channel 0 into channel 1 so the dry path is heard on both
  // speakers. Without this, a mono interface/mic only ever feeds the left
  // channel and an empty (or IR-only) chain plays back one-sided.
  std::atomic<bool> standaloneMonoInput{false};

  // The two chains: lanes[0] = Left/primary (the only lane in mono mode),
  // lanes[1] = Right (stereo mode). Kept as one array so per-lane logic
  // (find, meters, serialization, reconciliation) is written once.
  std::array<Lane, kNumLanes> lanes;
  Lane& lane(ChainSide side) { return lanes[static_cast<size_t>(laneIndex(side))]; }
  const Lane& lane(ChainSide side) const { return lanes[static_cast<size_t>(laneIndex(side))]; }

  std::atomic<bool> stereoEnabled{false};
  // Which lane loadTone inserts into. Set by the UI before launching the
  // Select flow (the choice must survive the OAuth redirect); not a view mode.
  ChainSide pendingAddSide{ChainSide::Left};
  juce::CriticalSection chainMutex;

  // ── Global chain-edit fade (see ChainEditFade / requestChainEditFadeAndWait) ──
  // The gain rides the host-rate buffer right after the chain stage; the
  // smoother is audio-thread-only, the flags are the cross-thread handshake.
  std::atomic<bool> chainEditFadePending{false};
  std::atomic<bool> chainEditFadeDone{false};
  juce::LinearSmoothedValue<float> chainEditFadeGain;

  // Milliseconds timestamp of the last processBlock, for isAudioActive().
  std::atomic<juce::int64> lastAudioCallbackMs{0};

  // Crossfade for the mono-double seed (spread in mono mode): 0 = channel 1
  // keeps its own chain output, 1 = channel 1 mirrors channel 0 (the
  // double). On true stereo sources the two chain outputs differ, so the
  // spread power switch must glide this instead of hard-copying (pop).
  juce::SmoothedValue<float> spreadDoubleBlend;

  // Output-stage gain (main level only — the balance trim lives in the
  // post-chain image matrix, pre-pan), smoothed so knob moves glide instead
  // of stepping once per block. Audio thread only.
  juce::SmoothedValue<float> outputGainSmoother;

  // Monotonic revision of everything getChainState() reports. Bumped on every
  // chain mutation (structure, params, load completion, stereo/side changes)
  // so the UI can cheaply skip resyncs when nothing changed. Mutable + const:
  // getChainState() promotes settled gesture edits into a bump (see below).
  mutable std::atomic<juce::uint32> chainRevision{1};
  void bumpChainRevision() const { chainRevision.fetch_add(1); }

  // Deferred revision bump for continuous gestures (knob/EQ drags). Bumping
  // per drag tick would make every 500 ms poll re-serialize and re-ship the
  // whole chain mid-gesture; instead the mutator records "params changed at
  // <now>" and getChainState() converts it into a real bump once the gesture
  // has been quiet for kGestureSettleMs. The dragging view holds its own
  // optimistic value, so nothing is stale meanwhile.
  static constexpr juce::int64 kGestureSettleMs = 400;
  mutable std::atomic<juce::int64> pendingParamBumpAt{0};  // 0 = nothing pending
  void deferredRevisionBump() { pendingParamBumpAt.store(juce::Time::currentTimeMillis()); }

  // Queue a background download+prepare of a tone's model for `blockId`.
  // Shared by loadTone (new block) and swapTone (existing block).
  void queueToneLoad(const std::string& blockId, int modelId, const juce::String& modelUrl,
                     const juce::String& modelName, ChainBlockType type);

  // Flip a block's loadFailed flag (with a revision bump) after a background
  // download/prepare failure, so the UI can offer a retry instead of showing
  // loading dots forever. No-op if the block is gone.
  void markBlockLoadFailed(const std::string& blockId);

  // ── Chain-domain resampling boundary (see ChainDomain.h) ──
  // Engaged (non-null) only when the host rate differs from the chain base
  // rate; created/reset in prepareToPlay, so the audio thread never sees it
  // change.
  std::unique_ptr<ChainBoundaryResampler> chainBoundary;
  // ── Chain oversampler (see ChainOversampler.h) ──
  // Raises the chain rate to kChainBaseSampleRate × factor inside the
  // boundary. Factor 1 = transparent passthrough. The factor atomic is the
  // single source of truth for the live chain rate; it's only written inside
  // the re-prepare paths (prepareToPlay / applyOversamplingSettings) while
  // the chain is quiesced.
  ChainOversampler chainOversampler;
  std::atomic<int> chainOversampleFactor{1};
  // The chain stage behind the oversampler — the callable both invocation
  // paths (direct and boundary) share.
  void processOversampledChainStage(float** inputs, float** outputs, int numFrames);
  // The encapsulated callback, built once in the constructor (capturing only
  // `this`) so ProcessBlock never allocates a std::function per audio block.
  ChainBoundaryResampler::BlockProcessFunc chainStageFunc;
  // Boundary latency in host samples (0 at a 48k host). Constant per host
  // rate — chain edits never change reported latency.
  int chainBoundaryLatency = 0;
  // Silent second channel handed to the boundary when the host buffer is
  // mono (the boundary is a fixed 2-channel container).
  juce::AudioBuffer<float> chainScratchChannel;
  // Per-callback routing state for processChainStage, set by processBlock
  // under chainMutex just before invoking the stage (audio thread only).
  int rtChainChannels = 2;
  bool rtStereoChains = false;

  // TONE3000 OAuth access token (Bearer). Read by `fetchModelFromUrl` from any
  // thread; written by the UI thread via `setAccessToken`.
  juce::String accessToken;
  mutable juce::CriticalSection accessTokenMutex;
  
  // Thread pool for background model loading
  juce::ThreadPool loadingThreadPool;

  int maxBlockSize = 0;
  bool eqParamsDirty = true;
  // Tracks the tone stack's power switch across blocks so re-enabling can
  // reset the filters (stale biquad state would otherwise ring).
  bool toneEqWasEnabled = true;

  // Input-stage noise gate (post input gain, host rate). The power switch is
  // tracked across blocks so re-enabling resets the detector — a stale
  // envelope must never decide the first block after power-on.
  NoiseGate inputGate;
  bool gateWasEnabled = true;

  // Raw APVTS parameter atomics, resolved once in the constructor. The audio
  // thread reads these every block; getRawParameterValue is a string-keyed
  // map lookup and has no business on the RT path.
  struct ParamRefs {
    std::atomic<float>* inputLevel = nullptr;
    std::atomic<float>* outputLevel = nullptr;
    std::atomic<float>* outputBalance = nullptr;
    std::atomic<float>* spreadEnabled = nullptr;
    std::atomic<float>* spreadOffset = nullptr;
    std::atomic<float>* spreadJitter = nullptr;
    std::atomic<float>* chainPanLeft = nullptr;
    std::atomic<float>* chainPanRight = nullptr;
    std::atomic<float>* toneBass = nullptr;
    std::atomic<float>* toneMid = nullptr;
    std::atomic<float>* toneTreble = nullptr;
    std::atomic<float>* gateThreshold = nullptr;
    std::atomic<float>* gateEnabled = nullptr;
    std::atomic<float>* toneEqEnabled = nullptr;
    std::atomic<float>* targetLoudness = nullptr;
    std::atomic<float>* calibrateInput = nullptr;
    std::atomic<float>* inputCalibrationLevel = nullptr;
    std::atomic<float>* osEnabled = nullptr;
    std::atomic<float>* osFactor = nullptr;
  } paramRefs;
  void resolveParamRefs();

  /** The oversampling factor the osEnabled/osFactor parameters currently
      request: 1 when disabled, else 2/4/8. */
  int resolvedOversampleFactor() const;

  // ── Oversampling live switching ──
  // The osEnabled/osFactor listeners bounce to the message thread (relays
  // fire from UI/host threads), where applyOversamplingSettings re-rates the
  // whole chain domain under the chain-edit fade: linear engines re-prepare
  // in place, NAM engines rebuild off-thread from the model cache.
  void parameterChanged(const juce::String& parameterID, float newValue) override;
  void handleAsyncUpdate() override;
  void applyOversamplingSettings();

  // Per-block cached values (refreshed once per processBlock from paramRefs).
  float cacheInputLevel = 0.5f;
  float cacheOutputLevel = 0.5f;
  float cacheOutputBalance = 0.5f;
  bool cacheSpreadEnabled = false;
  float cacheSpreadOffset = 0.75f;  // bipolar, 0.5 = center = off; default 12 ms R
  float cacheSpreadJitter = 0.5f;   // default 2 ms
  float cacheChainPanLeft = 0.0f;
  float cacheChainPanRight = 1.0f;
  float cacheBassTone = 5.0f;
  float cacheMidTone = 5.0f;
  float cacheTrebleTone = 5.0f;
  float cacheGateThreshold = -80.0f;
  bool cacheGateEnabled = true;
  bool cacheToneEqEnabled = true;
  float cacheTargetLoudness = -18.0f;
  bool cacheCalibrateInput = false;
  float cacheInputCalibrationLevel = 12.0f;

  void updateEqCoefficients();
  void updateCachedParameters();
  void processToneStack(juce::AudioBuffer<float>& buffer);

  // DC blocker; ProcessorDuplicator runs one filter instance per channel, so a
  // single duplicator handles mono and stereo buffers alike.
  juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>
      dcBlocker;

  juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>
      bassFilter;
  juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>
      midFilter;
  juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>
      trebleFilter;

  juce::AudioBuffer<float> tempDryBuffer;  // chain-domain scratch (sized to chainDomainBlockSize)
  double hostSampleRate = 48000.0;  // Default, updated dynamically in prepareToPlay

  // ── Global NAM A2 size (see setNamFullSize) ──
  // Seeded from the shared settings file at construction so DSP restores at
  // the right tier even before any editor opens; other instances pick up a
  // change on their next launch. Atomic: written on the message thread, read
  // by loader threads when a model prepares.
  static bool readPersistedNamFullSize();
  /** The preference as the size NamEngine::setSlimmableSize expects
      (0.0 = lite, 1.0 = full — the tier boundary at 0.5 belongs to full). */
  double namSlimmableSizeValue() const { return namFullSize.load() ? 1.0 : 0.0; }
  std::atomic<bool> namFullSize{readPersistedNamFullSize()};

  // Audio-callback load (timed around processBlock); ships to the UI as the
  // `cpu` field of getMeterLevels for the hint-bar readout.
  juce::AudioProcessLoadMeasurer loadMeasurer;

  // Meter level tracking, per channel (mono sources report L == R).
  mutable std::atomic<float> inputMeterLevelL{-60.0f};
  mutable std::atomic<float> inputMeterLevelR{-60.0f};
  mutable std::atomic<float> outputMeterLevelL{-60.0f};
  mutable std::atomic<float> outputMeterLevelR{-60.0f};

  // True when the plugin is being fed a real stereo source (stereo bus in a
  // host, or a stereo input device in the standalone app). A capability flag:
  // the input-mode selection doesn't affect it. Drives the UI's input-mode
  // button and dual input meters. Reported via getChainState; see
  // updateStereoInputDetection().
  std::atomic<bool> stereoInputDetected{false};

  // Input channel mode (see InputMode). Atomic: written by the message
  // thread (faceplate button / state restore), read by the audio thread.
  std::atomic<int> inputMode{static_cast<int>(InputMode::Stereo)};
  bool isStandalone() const { return wrapperType == wrapperType_Standalone; }
  void updateStereoInputDetection();

  // Tuner pitch detection (fed from processBlock when enabled)
  TunerDetector tuner;

  // ── Auto balance measurement state ──
  // Lock-free audio↔message thread handshake. The audio thread only touches
  // the accumulators while `autoBalanceState == Listening`; the message
  // thread only reads them after flipping the state to Measured, so the two
  // sides never race on the same phase.
  enum class AutoBalanceState : int { Idle = 0, Listening, Measured, TimedOut };
  std::atomic<int> autoBalanceState{static_cast<int>(AutoBalanceState::Idle)};
  // Sum of squares per channel. Written by the audio thread only; plain
  // doubles are safe because the message thread reads them strictly after
  // observing state == Measured (the atomic state store/load provides the
  // release/acquire ordering). The sample counters are atomic because the UI
  // polls them for progress while the audio thread is still accumulating.
  double autoBalanceSumL = 0.0, autoBalanceSumR = 0.0;
  std::atomic<juce::int64> autoBalanceSamples{0};
  std::atomic<juce::int64> autoBalanceElapsed{0};  // wall samples since arm, for the timeout
  float autoBalanceMatchedDb = 0.0f;               // result, valid in Measured
  void runAutoBalanceStage(const juce::AudioBuffer<float>& buffer, int numSamples);

  // Post-chain spread engine (one instance + one parameter set serves both
  // the mono double and the stereo chain shift — the modes are exclusive).
  // While the power switch is on it always runs (0 ms delay = identity), so
  // knob moves never hard-toggle DSP; all transitions glide through zero.
  Spread spread;

  // Post-chain image matrix gains (per-chain balance trim × constant-power
  // pan; see imageMatrixGains in Processor.cpp), smoothed so balance/pan
  // moves don't zipper. LtoR = how much of the Left chain lands in the
  // Right output, etc. At the centered/hard-panned default the matrix is
  // the identity and processBlock skips the mix loop entirely.
  juce::SmoothedValue<float> imageGainLtoL, imageGainLtoR, imageGainRtoL, imageGainRtoR;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TONE3000Processor)
};
