#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>
#include <atomic>
#include <memory>
#include <vector>
#include <string>
#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "NAM/activations.h"
#include "NAM/convnet.h"
#include "NAM/lstm.h"
#include "NAM/util.h"
#include "NAM/wavenet.h"
#include "ChainBlock.h"
#include "NamResampler.h"

class TONE3000Processor;

class TONE3000Processor : public juce::AudioProcessor {
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

  // Chain management methods
  std::string loadTone(const juce::String& toneJsonString);
  bool switchModel(const std::string& blockId, int modelId);
  bool removeChainBlock(const std::string& blockId);
  bool reorderChainBlocks(const std::vector<std::string>& newOrder);
  
  // Background loading (called by thread pool jobs)
  void loadToneInBackground(const std::string& blockId, const juce::String& toneJson, 
                            int firstModelId, const juce::String& modelUrl, 
                            const juce::String& modelName, ChainBlockType type);
  void switchModelInBackground(const std::string& blockId, int modelId, 
                               const juce::String& modelUrl, const juce::String& modelName);

  // Chain status methods
  juce::var getChainStatus() const;
  bool isChainValid() const;

  // Per-block control setters
  void setBlockOutputGain(const std::string& blockId, float normalizedGain);
  void setBlockMix(const std::string& blockId, float normalizedMix);

  // Latency management
  int calculateTotalLatency() const;
  void updateLatencyCompensation();

  // Meter level getters for UI
  float getInputMeterLevel() const;
  float getOutputMeterLevel() const;

private:
  // Helper methods
  float computeIrNormalizationGain(const juce::File& irFile, size_t maxIrLength);
  juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
  
  // Tone loading helpers
  std::vector<uint8_t> fetchModelFromUrl(const juce::String& modelUrl);
  void loadModelData(ChainBlock& block, const std::vector<uint8_t>& modelData, 
                     const juce::String& filename);

  // Chain management
  std::vector<std::unique_ptr<ChainBlock>> chainBlocks;
  juce::CriticalSection chainMutex;
  
  // Thread pool for background model loading
  juce::ThreadPool loadingThreadPool;

  // Processing buffers (still used by chain processing)
  std::vector<double> inputBuffer, outputBuffer;
  int maxBlockSize = 0;
  bool eqParamsDirty = true;

  // cache
  float cacheInputLevel;
  float cacheOutputLevel;
  float cacheBassTone;
  float cacheMidTone;
  float cacheTrebleTone;
  float cacheGateThreshold;
  float cacheTargetLoudness;
  bool cacheNormalize;
  bool cacheCalibrateInput;
  float cacheInputCalibrationLevel;

  void updateEqCoefficients();
  void updateCachedParameters();

  juce::LinearSmoothedValue<float> normalizationGainSmoother;

  juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>
      dcBlockerLeft;
  juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>
      dcBlockerRight;



  juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>
      bassFilter;
  juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>
      midFilter;
  juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>
      trebleFilter;

  std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
  std::vector<float> namProcessInputFloatBuffer;
  juce::AudioBuffer<float> oversampleBuffer;
  juce::AudioBuffer<float> tempDryBuffer;
  std::vector<float> resampleInputBuffer;
  std::vector<float> resampleOutputBuffer;
  juce::LagrangeInterpolator upsampler;
  juce::LagrangeInterpolator downsampler;
  double modelSampleRate = 48000.0;
  double hostSampleRate = 48000.0;  // Default, updated dynamically in prepareToPlay
  bool bypassResampling = true;     // Default to bypass unless model requires specific rate

  // Meter level tracking
  mutable std::atomic<float> inputMeterLevel{-60.0f};
  mutable std::atomic<float> outputMeterLevel{-60.0f};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TONE3000Processor)
};
