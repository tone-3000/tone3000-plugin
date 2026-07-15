#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "BlockEq.h"
#include "BlockSpectrum.h"
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

// Fixed IDs for the insert/select placeholder blocks (pass-through, no audio effect).
// Each chain owns its own placeholder so block ids stay globally unique.
constexpr const char* INSERT_BLOCK_ID = "select-insert";
constexpr const char* INSERT_BLOCK_ID_RIGHT = "select-insert-right";

// Chain block data structure
struct ChainBlock {
  std::string id;  // Chain block UUID
  ChainBlockType type;

  // Tone metadata (full tone JSON stored for complete state persistence)
  int toneId;
  juce::String toneJson;  // Complete tone JSON from TONE3000 API
  int activeModelId;      // Currently active model ID (single source of truth)

  // Parsed-once copy of toneJson (full API payload — model switching needs
  // the model URLs) and the slim projection getChainState ships to the UI
  // (title/images/user/model names only). Both are ref-counted vars, so
  // serializing chain state is O(1) per block instead of a JSON re-parse.
  // Set together wherever toneJson is set — see setToneOnBlock.
  juce::var toneVar;
  juce::var toneSummary;

  // Model cache: stores downloaded model data by model ID
  std::map<int, std::vector<uint8_t>> modelCache;

  // State flags
  bool loaded;   // True when active model is loaded and ready
  bool enabled;  // True when block is enabled in processing chain

  // Set by the audio thread when NAM processing throws (the block is disabled
  // in the same breath). The message thread drains it in getChainState and
  // writes the log line there — string building/logging is not RT-safe.
  std::atomic<bool> rtProcessingFailed{false};

  // NAM-specific processing (runs at the fixed chain rate — see ChainDomain.h)
  std::unique_ptr<NamEngine> namEngine;
  juce::LinearSmoothedValue<float> namNormalizationSmoother;

  // IR-specific processing.
  // convolverMono: IR channel 0 loaded with Stereo::no — applies the same (left) kernel to
  //   every audio channel. Always present for a loaded IR; used as the mono fallback.
  // convolverStereo: IR loaded with Stereo::yes — audio ch0 ⊗ IR ch0, audio ch1 ⊗ IR ch1.
  //   Only created when the IR file actually has >= 2 channels (true stereo IR).
  std::unique_ptr<juce::dsp::Convolution> convolverMono;
  std::unique_ptr<juce::dsp::Convolution> convolverStereo;
  int irNumChannels{1};  // channels in the loaded IR file (1 or 2)
  juce::File irTempFile;
  juce::LinearSmoothedValue<float> irNormalizationSmoother;
  float irNormalizationGainLinear{1.0f};

  // Per-block controls (normalized 0..1)
  float inputGainNormalized{0.5f};  // 0.5 = unity gain; drives the block harder/softer
  juce::LinearSmoothedValue<float> inputGainSmoother;
  float outputGainNormalized{0.5f};  // 0.5 = unity gain
  juce::LinearSmoothedValue<float> outputGainSmoother;
  float mixNormalized{1.0f};  // 0 = dry, 1 = wet
  juce::LinearSmoothedValue<float> mixSmoother;

  // Per-block meter levels (dB, -60 floor). Written by the audio thread every
  // block, read by the UI via getMeterLevels(). Input is measured post
  // input-gain (what the model actually receives), output post gain+mix.
  std::atomic<float> inputMeterDb{-60.0f};
  std::atomic<float> outputMeterDb{-60.0f};

  // Post-block 6-band EQ (runs after output gain + mix). Flat by default, in
  // which case processing is skipped entirely (single branch per audio block).
  BlockEq eq;

  // Spectrum analyzer for the EQ editor backdrop. Only fed by the audio thread
  // while the UI has this block's EQ view open (atomic enabled flag).
  BlockSpectrum spectrum;

  // NAM slimmable / container (A2): 1.0 = full, 0.0 = lite (the tier boundary at
  // 0.5 belongs to full — see NamEngine::setSlimmableSize); only used when namIsSlimmable
  bool namIsSlimmable{false};
  double namSlimmableSize{1.0};

  ChainBlock(const std::string& blockId, ChainBlockType blockType)
      : id(blockId), type(blockType), toneId(0), activeModelId(0), loaded(false),
        enabled(true) {}
};
