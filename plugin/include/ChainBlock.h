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

// Fixed ID for the insert/select placeholder block (pass-through, no audio effect)
constexpr const char* INSERT_BLOCK_ID = "select-insert";

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

  // IR-specific processing
  std::unique_ptr<juce::dsp::Convolution> convolverLeft;
  std::unique_ptr<juce::dsp::Convolution> convolverRight;
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
