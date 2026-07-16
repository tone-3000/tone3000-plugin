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
#include "Spread.h"
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
  // "enabled" (0/1), "inputGain", "outputGain", "mix" (normalized 0..1),
  // "namSlimmableSize" (0.0 lite .. 1.0 full). Returns false for unknown
  // blocks/params. Continuous params defer their revision bump to the end of
  // the gesture (see deferredRevisionBump) so drag-rate calls never force
  // full chain resyncs.
  bool setBlockParam(const std::string& blockId, const juce::String& param, double value);

  // All meter levels in one call: { input, output, blocks: { id: { in, out } } }.
  // Values in dB with a -60 floor. Designed to be polled once per UI frame.
  juce::var getMeterLevels() const;

  // Per-block post EQ. setBlockEqBand takes { type, freqHz, gainDb, q } for
  // one band — the undo stack's mutation granularity. Band drags defer their
  // revision bump like continuous block params (see deferredRevisionBump).
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
  // Which lane the next loadTone inserts into ("left"/"right"). The UI sets
  // this before launching the Select flow so the choice survives the OAuth
  // redirect. Not a view mode and not part of undo history.
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

  // Tuner: enabled by the UI while the tuner screen is visible. Reads the raw
  // (pre-gain, pre-gate) input so gating never starves the pitch detector.
  void setTunerEnabled(bool enabled) { tuner.setEnabled(enabled); }
  juce::var getTunerReading() { return tuner.getReading(); }

  // ── Auto balance: one-shot L/R output energy match ──
  // startAutoBalance() arms a "listening" measurement: the audio thread
  // accumulates pre-output-gain L/R energy (silence-gated) until ~2 s of real
  // signal has been heard, then pollAutoBalance() — called from the message
  // thread by the UI's poll loop — maps the measured dB difference onto the
  // outputBalance parameter (host-automatable, so undo/presets come free).
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
  float computeIrNormalizationGain(const juce::File& irFile, size_t maxIrLength);
  juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
  
  // Tone loading helpers
  std::vector<uint8_t> fetchModelFromUrl(const juce::String& modelUrl);

  /** Largest frame count the chain domain can see per callback: the host max
      block size converted to 48 kHz frames (and never below the host size, so
      the direct 48k path is covered too). Floors at 1 to avoid prepare(0). */
  int chainDomainBlockSize() const noexcept;

  struct PreparedBlockModel {
    bool success = false;

    std::unique_ptr<NamEngine> namEngine;
    bool namIsSlimmable = false;

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
  /** Short path under `chainMutex` only: swaps the new engines onto `block`.
      The block's *old* engines end up back in `prepared` — the caller must let
      `prepared` die *after* releasing the lock, because engine destructors
      (NAM graphs, convolution state) are far too heavy to run while the audio
      thread may be blocked on chainMutex. */
  void applyPreparedModelToChainBlock(ChainBlock& block, PreparedBlockModel& prepared);

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

  // Prepare every engine in a chain for the fixed chain rate (kChainSampleRate)
  // and the current chain-domain block size. Holds no lock.
  void prepareChain(std::vector<std::unique_ptr<ChainBlock>>& blocks);

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
  void reconcileChainFromTree(const juce::ValueTree& chainState, Lane& target,
                              const char* insertBlockId, Lane& retired);
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
  // Engaged (non-null) only when the host rate differs from kChainSampleRate;
  // created/reset in prepareToPlay, so the audio thread never sees it change.
  std::unique_ptr<ChainBoundaryResampler> chainBoundary;
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

  // Raw APVTS parameter atomics, resolved once in the constructor. The audio
  // thread reads these every block; getRawParameterValue is a string-keyed
  // map lookup and has no business on the RT path.
  struct ParamRefs {
    std::atomic<float>* inputLevel = nullptr;
    std::atomic<float>* outputLevel = nullptr;
    std::atomic<float>* inputBalance = nullptr;
    std::atomic<float>* outputBalance = nullptr;
    std::atomic<float>* spreadEnabled = nullptr;
    std::atomic<float>* spreadAmount = nullptr;
    std::atomic<float>* spreadJitter = nullptr;
    std::atomic<float>* chainPanLeft = nullptr;
    std::atomic<float>* chainPanRight = nullptr;
    std::atomic<float>* toneBass = nullptr;
    std::atomic<float>* toneMid = nullptr;
    std::atomic<float>* toneTreble = nullptr;
    std::atomic<float>* gateThreshold = nullptr;
    std::atomic<float>* gateEnabled = nullptr;
    std::atomic<float>* toneEqEnabled = nullptr;
    std::atomic<float>* toneEqPre = nullptr;
    std::atomic<float>* targetLoudness = nullptr;
    std::atomic<float>* normalize = nullptr;
    std::atomic<float>* calibrateInput = nullptr;
    std::atomic<float>* inputCalibrationLevel = nullptr;
  } paramRefs;
  void resolveParamRefs();

  // Per-block cached values (refreshed once per processBlock from paramRefs).
  float cacheInputLevel = 0.5f;
  float cacheOutputLevel = 0.5f;
  float cacheInputBalance = 0.5f;
  float cacheOutputBalance = 0.5f;
  bool cacheSpreadEnabled = false;
  float cacheSpreadAmount = 0.5f;  // bipolar: 0.5 = center = off
  float cacheSpreadJitter = 0.0f;
  float cacheChainPanLeft = 0.0f;
  float cacheChainPanRight = 1.0f;
  float cacheBassTone = 5.0f;
  float cacheMidTone = 5.0f;
  float cacheTrebleTone = 5.0f;
  float cacheGateThreshold = -80.0f;
  bool cacheGateEnabled = true;
  bool cacheToneEqEnabled = true;
  bool cacheToneEqPre = false;  // tone stack before (true) or after (false) the chain
  float cacheTargetLoudness = -18.0f;
  bool cacheNormalize = true;
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

  // Stereo chain-pan blend gains (constant-power), smoothed so pan moves
  // don't zipper. LtoR = how much of the Left chain lands in the Right
  // output, etc. At the hard-panned default the blend is the identity and
  // processBlock skips the mix loop entirely.
  juce::SmoothedValue<float> panGainLtoL, panGainLtoR, panGainRtoL, panGainRtoR;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TONE3000Processor)
};
