#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "NamResampler.h"

// Chain block types
enum class ChainBlockType { NAM, IR, INSERT };

// Which chain is being processed/edited in stereo mode.
enum class ChainSide { Left, Right };

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

  // Model cache: stores downloaded model data by model ID
  std::map<int, std::vector<uint8_t>> modelCache;

  // State flags
  bool loaded;   // True when active model is loaded and ready
  bool enabled;  // True when block is enabled in processing chain

  // NAM-specific processing
  std::unique_ptr<NamResampler> namResampler;
  juce::LinearSmoothedValue<float> namNormalizationSmoother;
  int latencySamples = 0;

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
  float outputGainNormalized{0.5f};  // 0.5 = unity gain
  juce::LinearSmoothedValue<float> outputGainSmoother;
  float mixNormalized{1.0f};  // 0 = dry, 1 = wet
  juce::LinearSmoothedValue<float> mixSmoother;

  // NAM slimmable / container (A2): 1.0 = full, 0.5 = lite; only used when namIsSlimmable
  bool namIsSlimmable{false};
  double namSlimmableSize{1.0};

  ChainBlock(const std::string& blockId, ChainBlockType blockType)
      : id(blockId), type(blockType), toneId(0), activeModelId(0), loaded(false),
        enabled(true) {}
};
