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
#include "ChainHistory.h"
#include "Spread.h"
#include "NamResampler.h"
#include "PresetManager.h"
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
  // "namSlimmableSize" (0.0 lite .. 1.0 full). Returns false for unknown
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

  // Standalone input channel mode. Interfaces usually expose stereo pairs
  // (line 1+2) even when only one jack is plugged in, so the standalone app
  // lets the user pick which channel actually carries signal — the industry
  // pattern for amp-sim standalones. Mono modes fold the chosen channel onto
  // both up front; hosts always route explicitly, so this is standalone-only
  // and defaults to Input 1 (a guitar in the first input).
  enum class InputMode { Input1 = 0, Input2 = 1, Stereo = 2 };
  void setStandaloneInputMode(InputMode mode);
  static InputMode inputModeFromString(const juce::String& s) {
    if (s == "input2") return InputMode::Input2;
    if (s == "stereo") return InputMode::Stereo;
    return InputMode::Input1;
  }
  static juce::String inputModeToString(InputMode mode) {
    switch (mode) {
      case InputMode::Input2: return "input2";
      case InputMode::Stereo: return "stereo";
      case InputMode::Input1: break;
    }
    return "input1";
  }
  // Which chain the UI is currently editing (Left/Right). No-op argument outside {"left","right"}.
  // Pure view navigation — not part of undo history.
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

  // Latency management
  int calculateTotalLatency() const;
  void updateLatencyCompensation();

  // Meter level getters for UI (max of the two channels)
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

  // State (de)serialization helpers.
  // serializeBlockSettings/applyBlockSettings cover everything user-editable
  // on a block (identity, tone refs, gains, mix, EQ) — the single source of
  // truth shared by plugin state persistence and undo/redo snapshots. Model
  // bytes are only included when `includeModelData` is set (project files).
  static juce::ValueTree serializeBlockSettings(const ChainBlock& block);
  void applyBlockSettings(ChainBlock& block, const juce::ValueTree& blockState);
  static void serializeChainToTree(const std::vector<std::unique_ptr<ChainBlock>>& blocks,
                                   juce::ValueTree& chainState, bool includeModelData);
  void restoreChainFromTree(const juce::ValueTree& chainState,
                            std::vector<std::unique_ptr<ChainBlock>>& target,
                            const char* insertBlockId);

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
  // background load. Caller must hold chainMutex.
  void restoreChainSnapshot(const juce::ValueTree& snapshot);
  void reconcileChainFromTree(const juce::ValueTree& chainState,
                              std::vector<std::unique_ptr<ChainBlock>>& target,
                              const char* insertBlockId);
  // Queue a background download+prepare of `block`'s active model, resolving
  // url/name from its tone JSON. Used by undo/redo when a restored block's
  // model isn't cached in memory anymore.
  void queueActiveModelLoad(const ChainBlock& block);

  ChainHistory chainHistory;

  // ── Preset internals (ProcessorPresets.cpp) ──
  // The faceplate parameters a preset carries. Explicitly scoped: rig
  // calibration (calibrateInput, inputCalibrationLevel) and global loudness
  // preferences (normalize, targetLoudness) describe the user's setup, not
  // the tone, so they stay out of presets.
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
  // Tracks the tone stack's power switch across blocks so re-enabling can
  // reset the filters (stale biquad state would otherwise ring).
  bool toneEqWasEnabled = true;

  // cache
  float cacheInputLevel;
  float cacheOutputLevel;
  float cacheInputBalance = 0.5f;
  float cacheOutputBalance = 0.5f;
  bool cacheSpreadEnabled = false;
  float cacheSpreadAmount = 0.5f;  // bipolar: 0.5 = center = off
  float cacheSpreadJitter = 0.0f;
  float cacheChainPanLeft = 0.0f;
  float cacheChainPanRight = 1.0f;
  float cacheBassTone;
  float cacheMidTone;
  float cacheTrebleTone;
  float cacheGateThreshold;
  bool cacheGateEnabled = true;
  bool cacheToneEqEnabled = true;
  bool cacheToneEqPre = false;  // tone stack before (true) or after (false) the chain
  float cacheTargetLoudness;
  bool cacheNormalize;
  bool cacheCalibrateInput;
  float cacheInputCalibrationLevel;

  void updateEqCoefficients();
  void updateCachedParameters();
  void processToneStack(juce::AudioBuffer<float>& buffer);

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

  // Meter level tracking, per channel (mono sources report L == R).
  mutable std::atomic<float> inputMeterLevelL{-60.0f};
  mutable std::atomic<float> inputMeterLevelR{-60.0f};
  mutable std::atomic<float> outputMeterLevelL{-60.0f};
  mutable std::atomic<float> outputMeterLevelR{-60.0f};

  // True when the plugin is being fed a real stereo source (stereo bus in a
  // host, or a stereo input device in the standalone app with the input mode
  // set to stereo). Drives the UI's dual input meter + balance controls.
  // Reported via getChainState; see updateStereoInputDetection().
  std::atomic<bool> stereoInputDetected{false};

  // Standalone input channel mode (see InputMode). Atomic: written by the
  // message thread (settings UI / state restore), read by the audio thread.
  std::atomic<int> standaloneInputMode{static_cast<int>(InputMode::Input1)};
  bool isStandalone() const { return wrapperType == wrapperType_Standalone; }
  void updateStereoInputDetection();

  // Tuner pitch detection (fed from processBlock when enabled)
  TunerDetector tuner;

  // Post-chain spread engine (one instance + one parameter set serves both
  // the mono double and the stereo chain shift — the modes are exclusive).
  // While the power switch is on it always runs (0 ms delay = identity), so
  // knob moves never hard-toggle DSP; all transitions glide through zero.
  Spread spread;

  // Stereo chain-pan blend gains (constant-power), smoothed so pan moves
  // don't zipper. LtoR = how much of the Left chain lands in the Right
  // output, etc. At the hard-panned default the blend is the identity and
  // processBlock skips the mix loop entirely.
  juce::SmoothedValue<float> panGainLtoL, panGainLtoR, panGainRtoL, panGainRtoR;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TONE3000Processor)
};
