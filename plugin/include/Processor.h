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
#include "NAM/wavenet/model.h"
#include "ChainBlock.h"
#include "NamResampler.h"
#include "TunerDetector.h"

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
  // Replace the tone of an existing block in place. Keeps the block's chain
  // position and user params (enabled/gains/mix); the new tone's first model
  // is queued for background loading.
  bool swapTone(const std::string& blockId, const juce::String& toneJsonString);
  bool switchModel(const std::string& blockId, int modelId);
  bool removeChainBlock(const std::string& blockId);
  bool reorderChainBlocks(const std::vector<std::string>& newOrder);

  // TONE3000 OAuth access token. Updated by the UI after the Select flow and
  // again on every refresh. `fetchModelFromUrl` attaches it as a Bearer header
  // because the new TONE3000 model_url endpoints reject anonymous requests.
  void setAccessToken(const juce::String& token);
  juce::String getAccessToken() const;
  
  // Background loading (called by thread pool jobs)
  void loadToneInBackground(const std::string& blockId, const juce::String& toneJson, 
                            int firstModelId, const juce::String& modelUrl, 
                            const juce::String& modelName, ChainBlockType type);
  void switchModelInBackground(const std::string& blockId, int modelId, 
                               const juce::String& modelUrl, const juce::String& modelName);

  // Chain state for the UI. `knownRevision` is the last revision the caller
  // saw (-1 for "give me everything"); when the chain hasn't changed since,
  // a minimal { revision, unchanged: true } object is returned so the UI's
  // poll loop stays cheap.
  juce::var getChainState(int knownRevision) const;
  bool isChainValid() const;

  // Single entry point for all per-block user params. Supported params:
  // "enabled" (0/1), "inputGain", "outputGain", "mix" (normalized 0..1),
  // "namSlimmableSize" (0.5 lite .. 1.0 full). Returns false for unknown
  // blocks/params. Bumps the chain revision.
  bool setBlockParam(const std::string& blockId, const juce::String& param, double value);

  // All meter levels in one call: { input, output, blocks: { id: { in, out } } }.
  // Values in dB with a -60 floor. Designed to be polled once per UI frame.
  juce::var getMeterLevels() const;

  // Per-block post EQ. setBlockEqBand takes { type, freqHz, gainDb, q } for one
  // band — the mutation granularity a future undo stack wants. Both bump the
  // chain revision (EQ params are part of params.eq in getChainState).
  bool setBlockEqBand(const std::string& blockId, int bandIndex, const juce::var& bandVar);
  bool setBlockEqEnabled(const std::string& blockId, bool enabled);
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
  // Which chain the UI is currently editing (Left/Right). No-op argument outside {"left","right"}.
  void setActiveEditChain(const juce::String& side);

  // Latency management
  int calculateTotalLatency() const;
  void updateLatencyCompensation();

  // Meter level getters for UI
  float getInputMeterLevel() const;
  float getOutputMeterLevel() const;

  // Tuner: enabled by the UI while the tuner screen is visible. Reads the raw
  // (pre-gain, pre-gate) input so gating never starves the pitch detector.
  void setTunerEnabled(bool enabled) { tuner.setEnabled(enabled); }
  juce::var getTunerReading() { return tuner.getReading(); }

  // Location of the on-disk diagnostic log. Single source of truth shared by the
  // FileLogger setup and the UI's "copy/reveal logs" actions so they never drift.
  // macOS: ~/Library/Logs/TONE3000/TONE3000.log, Windows: %APPDATA%/TONE3000/TONE3000.log
  static juce::File getLogFile();

private:
  // Helper methods
  float computeIrNormalizationGain(const juce::File& irFile, size_t maxIrLength);
  juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
  
  // Tone loading helpers
  std::vector<uint8_t> fetchModelFromUrl(const juce::String& modelUrl);
  void loadModelData(ChainBlock& block, const std::vector<uint8_t>& modelData,
                     const juce::String& filename);

  /** max(maxBlockSize, getBlockSize(), 1); avoids NamResampler prepare(0, …)→runtime errors. */
  int computeEffectiveNamPrepareBlockSize() const noexcept;

  struct PreparedBlockModel {
    bool success = false;

    std::unique_ptr<NamResampler> namResampler;
    bool namIsSlimmable = false;
    int namLatencySamples = 0;

    std::unique_ptr<juce::dsp::Convolution> convolverMono;
    std::unique_ptr<juce::dsp::Convolution> convolverStereo;
    int irNumChannels = 1;
    juce::File irTempFile;
    float irNormalizationGainLinear = 1.0f;
  };

  /** CPU/file heavy; call without holding `chainMutex`. */
  PreparedBlockModel prepareBlockModelOffThread(ChainBlockType type, const std::vector<uint8_t>& modelData,
                                                const juce::String& filename,
                                                double namPersistedSlimmableSize);
  /** Short path under `chainMutex` only: swaps engines onto `block` and clears the opposite modality. */
  void applyPreparedModelToChainBlock(ChainBlock& block, PreparedBlockModel& prepared);

  // Run one chain (the per-block loop) over the supplied working buffer. The buffer may have
  // 1 channel (a single side in stereo mode) or 1-2 channels (mono mode). All per-channel work
  // is keyed on buffer.getNumChannels(). Must be called while holding `chainMutex`.
  void processChainOnBuffer(std::vector<std::unique_ptr<ChainBlock>>& blocks,
                            juce::AudioBuffer<float>& buffer);

  // Prepare every engine in a chain for the given sample rate / block size. Holds no lock.
  void prepareChain(std::vector<std::unique_ptr<ChainBlock>>& blocks, double sampleRate,
                    int samplesPerBlock);

  // The chain the UI edits/adds to right now (Left in mono mode, or the active side in stereo).
  std::vector<std::unique_ptr<ChainBlock>>& activeChain();

  // Find a block by id across both chains (ids are globally unique). Returns nullptr if absent.
  ChainBlock* findBlockById(const std::string& blockId);

  // State (de)serialization helpers for a single chain.
  void serializeChainToTree(const std::vector<std::unique_ptr<ChainBlock>>& blocks,
                            juce::ValueTree& chainState);
  void restoreChainFromTree(const juce::ValueTree& chainState,
                            std::vector<std::unique_ptr<ChainBlock>>& target,
                            const char* insertBlockId);

  // True when running as the standalone app with a mono input device selected.
  // Detected in prepareToPlay (device changes re-trigger it); processBlock then
  // duplicates channel 0 into channel 1 so the dry path is heard on both
  // speakers. Without this, a mono interface/mic only ever feeds the left
  // channel and an empty (or IR-only) chain plays back one-sided.
  std::atomic<bool> standaloneMonoInput{false};

  // Chain management
  std::vector<std::unique_ptr<ChainBlock>> chainBlocks;       // Left / primary chain
  std::vector<std::unique_ptr<ChainBlock>> rightChainBlocks;  // Right chain (stereo mode)
  std::atomic<bool> stereoEnabled{false};
  ChainSide activeEditSide{ChainSide::Left};
  juce::CriticalSection chainMutex;

  // Monotonic revision of everything getChainState() reports. Bumped on every
  // chain mutation (structure, params, load completion, stereo/side changes)
  // so the UI can cheaply skip resyncs when nothing changed.
  std::atomic<juce::uint32> chainRevision{1};
  void bumpChainRevision() { chainRevision.fetch_add(1); }

  // Queue a background download+prepare of a tone's model for `blockId`.
  // Shared by loadTone (new block) and swapTone (existing block).
  void queueToneLoad(const std::string& blockId, const juce::String& toneJson, int modelId,
                     const juce::String& modelUrl, const juce::String& modelName,
                     ChainBlockType type);

  // Pre-allocated mono scratch buffers for per-side processing in stereo mode.
  juce::AudioBuffer<float> stereoChainBufferL;
  juce::AudioBuffer<float> stereoChainBufferR;

  // TONE3000 OAuth access token (Bearer). Read by `fetchModelFromUrl` from any
  // thread; written by the UI thread via `setAccessToken`.
  juce::String accessToken;
  mutable juce::CriticalSection accessTokenMutex;
  
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

  // DC blocker; ProcessorDuplicator runs one filter instance per channel, so a
  // single duplicator handles mono and stereo buffers alike.
  juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>
      dcBlockerLeft;


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

  // Tuner pitch detection (fed from processBlock when enabled)
  TunerDetector tuner;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TONE3000Processor)
};
