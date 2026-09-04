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
#include "AutoOffset.h"
#include "ChainOversampler.h"
#include "RtWorkerPool.h"
#include "MidiMapper.h"
#include "NoiseGate.h"
#include "Spread.h"
#include "StereoOffset.h"
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
  // Load dropped local files (`files` = [{ name, data }], base64 bytes; one
  // entry for a single file, many for a folder) as one tone block, one model
  // per file. `targetInsertId` is either an insert slot (adds, consuming
  // that tile) or an existing tone block (swaps in place). Each file is
  // validated (.nam must be A2, .wav must be real audio) and stashed as a
  // content-addressed copy whose path becomes the model's model_url; then
  // the tone routes through loadTone / swapTone, so downstream behavior
  // (background load, cache, undo, duplication, persistence) is identical
  // to catalog tones. Unlike catalog tones the full model list stays in the
  // stored tone JSON (there is no API catalog to page the others back in
  // from). Returns { blockId } on success or { error } with a user-facing
  // message.
  juce::var loadLocalTone(const juce::String& title, const juce::var& files,
                          const std::string& targetInsertId = {});
  // Path-based sibling of loadLocalTone for files native already has on
  // disk: the tile menus' Load File / Load Folder pickers (the webview
  // drop path can't hand over paths, so it ships base64 instead). A
  // directory loads as one multi-model tone by the same rules as a folder
  // drop in the UI: majority extension picks NAM vs IR, capped at 300
  // files / 50 MB each, models in natural name order, title from the
  // folder name. A single file must be .nam or .wav; title is the file
  // name. Same return contract as loadLocalTone.
  juce::var loadLocalTonePath(const juce::File& source, const std::string& targetInsertId = {});

  /** URL sibling of loadLocalTonePath, for the iOS document picker.
      Files chosen from the Files app live outside the app sandbox and are
      readable only through the security scope JUCE's FileChooser bookmarked,
      so the bytes have to come through juce::URL rather than the raw path.
      Takes 1..N URLs because multi-select stands in for the folder route on
      iOS (a security-scoped directory cannot be enumerated through
      juce::URL); see pickLocalToneFile. Same return contract as
      loadLocalTone. Compiled on every platform so the DSP suite can test it;
      only the iOS editor calls it. */
  juce::var loadLocalToneUrls(const juce::Array<juce::URL>& sources,
                              const std::string& targetInsertId = {});
  // Age out local-model stash files unused for a week (runs once per
  // process, off-thread). Called from the constructor.
  static void cleanLocalModelStash();
  // Sweep "*_ir.wav" files leaked into the OS temp dir by older builds (the
  // IR loader wrote one per engine build and never deleted it; it now cleans
  // up after itself). Runs once per process, off-thread. Called from the
  // constructor.
  static void cleanLeakedIrTempFiles();
  // The sweep itself: deletes "*_ir.wav" files in `tempDir` untouched for
  // over an hour (the age guard protects a concurrent instance's in-flight
  // file). Returns how many files it deleted.
  static int sweepLeakedIrTempFiles(const juce::File& tempDir);
  // Resolve a persisted local-model `file://` URL to the stash file that
  // actually holds those bytes, given the current stash root. The URL stored
  // in a block's tone JSON is absolute, and that JSON is persisted in
  // presets, DAW/app state and undo snapshots; on iOS the app data
  // container's UUID rotates on every reinstall or app update, so every one
  // of those paths goes stale. Stash names are content hashes in a flat
  // folder, so the file name *is* the stable token: when the stored path no
  // longer exists, the same name under the current root is the same bytes.
  // A stored path that does exist is returned untouched, which is every
  // desktop case (the root never moves there). Empty File for a non-file URL.
  static juce::File resolveLocalModelFile(const juce::File& stashRoot,
                                          const juce::String& modelUrl);
  // The file name a URL names, percent-decoded. juce::URL::getFileName returns
  // the raw, still-escaped last path component, so a file picked as
  // "Deluxe Reverb 2.nam" reads back as "Deluxe%20Reverb%202.nam" and would
  // reach the title, the model name and the stash name in that form. Desktop
  // derives names from juce::File and never sees escapes; this keeps the URL
  // path identical.
  static juce::String localFileNameFromUrl(const juce::URL& url);
  // Replace the tone of an existing block in place. Keeps the block's chain
  // position and user params (enabled/gains/mix); the new tone's first model
  // is queued for background loading.
  bool swapTone(const std::string& blockId, const juce::String& toneJsonString);
  // Best-effort tone metadata re-sync: merge a fresh /tones/{id} API payload
  // into every non-local block holding that tone (both lanes). The stored
  // models array is preserved per block (native persists only the active
  // model, and its model_url backs retries/reloads); everything else (title,
  // artwork, counts, url, ...) is taken from the fresh payload. Purely
  // metadata: params, activeModelId and engines are untouched, and no undo
  // step is recorded. The revision only bumps when something actually
  // changed, so an identical payload is a true no-op. Returns whether any
  // block changed.
  bool refreshToneMetadata(const juce::String& toneJsonString);
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
  // Clone a tone block (model, EQ, gains, mix, every persisted setting)
  // into `side` at `index` (absolute lane index, inserts included). A clone
  // landing on an insert slot consumes it (paste into an empty tile);
  // anywhere else it splices in like a cross-lane drop. The clone gets a
  // fresh id and its own engines, loaded cache-first from the source's
  // in-memory model bytes (no network). Returns the new id, "" on failure.
  std::string duplicateChainBlock(const std::string& sourceBlockId, const juce::String& side,
                                  int index);
  // In-app block clipboard (Copy on a tone tile / Paste on an insert slot).
  // Copy stores a self-contained snapshot (the serializeBlockSettings tree +
  // the block's in-memory model bytes) on this instance, so paste keeps
  // working after the source block is gone: preset switches, undo, deleting
  // the copied block. Not undoable; bumps the revision so `canPasteBlock`
  // (getChainState) reaches the UI. Returns false for stale/insert ids.
  bool copyChainBlock(const std::string& blockId);
  // Rebuild a block from the clipboard snapshot into `side` at `index` (same
  // landing rules as duplicateChainBlock: an insert slot there is consumed,
  // anywhere else splices in). The paste gets a fresh id and loads its model
  // cache-first from the copied bytes. Returns the new id, "" on failure
  // (empty clipboard, or the right lane while mono).
  std::string pasteChainBlock(const juce::String& side, int index);

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

  // Per-block NAM A2 size, in NAM's slimmable-size domain (0..1, clamped;
  // 0.0 = lite, 1.0 = full; see ChainBlock::namSlimSize). Chain state like
  // mix or the active model, so presets/undo/duplication carry each block's
  // size. Not routed through setBlockParam because it retiers the block's
  // loaded engine in place: weight swaps are discontinuous and the retier
  // can hold chainMutex through a submodel prewarm, so it mute-splices the
  // chain like a structural edit (processBlock's try-lock keeps the audio
  // thread off the lock while the fade holds). Undoable; no-op sets never
  // dip the audio.
  bool setBlockSlimSize(const std::string& blockId, double slimSize);

  // Default NAM A2 size for newly added blocks (machine-wide user setting,
  // like multi-core), in the same slimmable-size domain. Existing blocks
  // keep their own per-block size; this only stamps blocks as loadTone
  // creates them, so it persists in the shared settings file rather than
  // sessions or presets.
  double getNamSlimSizeDefault() const { return namSlimSizeDefault.load(); }
  void setNamSlimSizeDefault(double slimSize);

  /** True while a chain-edit fade session holds the chain muted (including
      the deferred hold that waits out a restore's background loads). Lets
      tests (which pump audio far faster than the wall clock the release
      waiter runs on) block until the rig is actually audible again. */
  bool isChainEditFadeHeld() const { return chainEditFadePending.load(); }

  // Multi-core processing (machine-wide user setting).
  // When on, the chain stage spreads its independent work across the
  // RtWorkerPool realtime threads: in stereo mode the two lanes run
  // concurrently (the Right/branch lane on a worker, the other on the audio
  // thread), and with oversampling active each NAM block's phase instances
  // run concurrently too (see NamEngine). Off = everything runs sequentially
  // on the audio thread as before. Output is bit-identical either way; the
  // toggle trades single-core headroom for several busy cores, so it
  // persists per machine, never with presets/sessions.
  bool getMultiCoreEnabled() const { return multiCoreEnabled.load(); }
  // `persist` = false skips the settings-file write (tests toggling the
  // scheduling path must not touch the user's machine-wide preference).
  void setMultiCoreEnabled(bool enabled, bool persist = true);

  // All meter levels in one call: { input, output, blocks: { id: { in, out } },
  // cpu (0..1 audio-callback load), correlation (-1..1 stereo-image output
  // correlation, from whichever engine the mode runs) }. Levels in dB with a
  // -60 floor. Designed to be polled once per UI frame.
  juce::var getMeterLevels() const;

  // Per-block EQ (post-block by default, pre-model when its pre flag is on).
  // setBlockEqBand takes { type, freqHz, gainDb, q } for one band, the undo
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

  // Chain branching (stereo mode).
  // A single optional tap point: the *branch* lane takes its input from the
  // *trunk* lane's signal after one of the trunk's tone blocks, instead of
  // from its own channel input. Only the branch lane's input source changes
  // (its blocks are untouched), so reverting to independent chains is trivial.
  // `side` names the trunk lane; `afterBlockId` the tone block whose output
  // is tapped. Calling it while already branched re-points the tap (either
  // lane, one move, no clearing first). Undoable; cleared automatically
  // when the tapped block leaves the trunk lane. Turning stereo mode off
  // only makes the branch dormant; it re-engages when stereo comes back
  // (like the right lane itself).
  bool setChainBranch(const juce::String& side, const std::string& afterBlockId);
  bool clearChainBranch();

  // Which channels of a stereo source feed the plugin: both (default), or
  // one channel folded onto both. Interfaces usually expose stereo pairs
  // (line 1+2) even when only one jack is plugged in, so this lets the user
  // pick the channel that actually carries signal. Set from the faceplate
  // input-mode button (visible only when the source is actually stereo; see
  // stereoInputDetected). Saved with the plugin/session state but not with
  // presets: it's I/O routing, not tone.
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

  // Editor window scale, 1.0 = the 1024x578 design size. Written by the
  // editor whenever it is resized and read back when a new editor opens, so
  // the chosen size survives editor teardown. Saved with the plugin/session
  // state but deliberately not with presets: window size is a workstation
  // preference, not tone. Atomic: hosts may call get/setStateInformation off
  // the message thread.
  std::atomic<double> editorScale{1.0};

  // Extra window height (design px) for persistent chrome strips, currently
  // just the hint bar. Persisted like editorScale so the next editor opens at
  // the height the UI's first paint will actually need, instead of growing a
  // beat after launch. Dynamic strips (the standalone banner) are excluded.
  // Default 36 = the hint bar, which the UI shows by default: a fresh insert
  // then opens at its final height and never asks the host to resize. Some
  // hosts (LUNA) grant a post-open grow without re-laying-out their own
  // plugin-window chrome, which visibly misplaces it.
  std::atomic<int> editorExtraHeight{36};

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

  // Internal presets (ProcessorPresets.cpp).
  // A preset = chain snapshot (with embedded model bytes, so it loads
  // offline) + the faceplate parameter values (kPresetParameterIds). The
  // active preset { id, name } rides getChainState; it only ever changes
  // together with a revision bump. Preset files live in the shared user
  // presets folder (see PresetManager).
  juce::var getPresetList() const;    // { presets: [{ id, name, factory }] }
  juce::var savePreset(const juce::String& name);  // { id, name } or void var on failure
  bool loadPreset(const juce::String& presetId);   // undoable (chain part)
  bool renamePreset(const juce::String& presetId, const juce::String& newName);
  bool deletePreset(const juce::String& presetId);
  // Move a preset by `delta` steps within its browser section (negative =
  // earlier). The custom order is user-facing truth: prev/next stepping and
  // MIDI program-change numbers follow it (see loadPresetAtIndex).
  bool movePreset(const juce::String& presetId, int delta);
  // Back to the factory-default state: empty mono chain, every preset-managed
  // faceplate parameter at its default, no active preset. One undoable step,
  // mute-spliced like a preset load. Returns false (leaving the audio
  // untouched) when the state is already at default.
  bool resetToDefault();

  // Tuner: enabled by the UI while the tuner screen is visible. Reads the raw
  // (pre-gain, pre-gate) input so gating never starves the pitch detector.
  void setTunerEnabled(bool enabled) { tuner.setEnabled(enabled); }
  juce::var getTunerReading() { return tuner.getReading(); }

  // Auto balance: one-shot chain energy match.
  // startAutoBalance() arms a "listening" measurement: the audio thread
  // accumulates the raw chain outputs' energy (pre-balance, pre-pan, so the
  // result is the chains' true mismatch at any pan position; silence-gated)
  // until ~2 s of real signal has been heard, then pollAutoBalance()
  // (called from the message thread by the UI's poll loop) maps the measured
  // dB difference onto the outputBalance parameter (host-automatable, so
  // undo/presets come free).
  // One-shot measurement of real playing is the industry norm here:
  // continuous AGC fights the player's dynamics, and an injected noise burst
  // is both audible and unrepresentative for nonlinear amp chains.
  void startAutoBalance();
  void cancelAutoBalance();
  juce::var pollAutoBalance();  // { state: "idle"|"listening"|"done"|"timeout", matchedDb? }

  // Auto align: probe-based chain time alignment (stereo chain mode). One
  // press drives both chains with an identical internal sweep, output muted
  // for under half a second, and measures the inter-chain lag and relative
  // polarity to sub-sample precision; see AutoOffset.h for the rationale
  // and threading model. pollAutoOffset() runs the analysis and applies the
  // result (align offset + power, polarity flip) when it's trustworthy.
  void startAutoOffset();
  void cancelAutoOffset();
  juce::var pollAutoOffset();  // { state: "idle"|"listening"|"done"|"timeout", matchedMs?, polarityFlipped?, progress? }

  // Location of the on-disk diagnostic log. Single source of truth shared by the
  // FileLogger setup and the UI's "copy/reveal logs" actions so they never drift.
  // macOS: ~/Library/Logs/TONE3000/TONE3000.log, Windows: %APPDATA%/TONE3000/TONE3000.log
  static juce::File getLogFile();

  // Location of the shared machine-wide settings file (the multi-core
  // preference). Logged once at startup (see the constructor) so
  // a "settings don't persist" report can be diagnosed straight from the
  // log instead of guessed at.
  static juce::File getSettingsFile();

  // Web Inspector preference (macOS): right-click -> Inspect Element on the
  // plugin UI, off by default in release builds and flipped from Settings ->
  // Diagnostics. Machine-wide (it's a debugging aid, not tone state), so it
  // lives in the shared settings file. Applied to the live WKWebView by the
  // editor (see EditorWebViewSetup::setWebInspectorEnabled).
  static bool readPersistedWebInspectorEnabled();
  static void persistWebInspectorEnabled(bool enabled);

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
  // Cache-hit loads bypass fetchModelFromUrl, so this keeps the stash
  // honest for local models: recreates a missing stash copy from the cached
  // bytes and re-stamps an existing one's mtime (the GC's liveness signal).
  void refreshLocalStashCopy(const juce::String& modelUrl, const std::vector<uint8_t>& bytes);
  // Shared tail of loadLocalTone / loadLocalTonePath: dedupe the stashed
  // models by content id, synthesize the local tone JSON, and route it
  // through swapTone (existing tone tile) or loadTone (insert slot).
  juce::var finishLocalToneLoad(const juce::String& title,
                                const juce::Array<juce::var>& stashedModels,
                                const juce::String& firstError, int fileCount,
                                const std::string& targetInsertId);

  /** Largest frame count the chain stage can see per boundary callback at the
      base rate: the host max block size converted to 48 kHz frames (and never
      below the host size, so the direct 48k path is covered too). Floors at 1
      to avoid prepare(0). */
  int chainBaseBlockSize() const noexcept;

  /** Largest frame count the chain domain can see per callback: the base
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
    // IR convolvers are rate-independent and always built at the base rate.)
    int preparedBlockSize = 0;

    std::unique_ptr<NamEngine> namEngine;

    std::unique_ptr<juce::dsp::Convolution> convolverMono;
    std::unique_ptr<juce::dsp::Convolution> convolverStereo;
    int irNumChannels = 1;
    int irLengthBaseSamples = 0;  // base-rate kernel length (tail reporting)
    bool irIsLong = false;        // short/long classification (see ChainBlock.h)
    float irNormalizationGainLinear = 1.0f;
  };

  /** CPU/file heavy; call without holding `chainMutex`. NAM engines come out
      at the given slimmable size (the callers read it off the block, so the
      tier selection and prewarm run out here instead of under the apply
      lock). */
  PreparedBlockModel prepareBlockModelOffThread(ChainBlockType type, const std::vector<uint8_t>& modelData,
                                                const juce::String& filename, double namSlimSize);
  /** Short path under `chainMutex` only: swaps the new engines onto `block`
      and stamps `newType` (a tone swap may change the block's type; the old
      engine kept processing under the old type until this moment). The
      block's *old* engines end up back in `prepared`; the caller must let
      `prepared` die *after* releasing the lock, because engine destructors
      (NAM graphs, convolution state) are far too heavy to run while the audio
      thread may be blocked on chainMutex. */
  void applyPreparedModelToChainBlock(ChainBlock& block, ChainBlockType newType,
                                      PreparedBlockModel& prepared);

  /** Ask the audio thread to glide `blockId`'s wet path down to silence,
      then wait (bounded) for the fade, so the caller's change (engine swap,
      failure drop, block removal) never splices the waveform (audible
      click). `muteWetOnly` picks the fade shape (see ChainBlock.h): false
      crossfades the output toward the block's dry input, right when the
      change ENDS at bypass (removal, failure drop); true mutes just the wet
      term while the dry share of the user's mix holds, right for engine
      swaps, whose dry input was never audible (at 100% mix the bypass fade
      would blast the un-processed signal, e.g. a raw amp head with no cab).
      Returns immediately when the block isn't audibly processing or no
      audio callbacks are running (the change is inaudible then anyway).
      Must be called WITHOUT holding chainMutex. */
  void requestSwapFadeAndWait(const std::string& blockId, bool muteWetOnly = false);

  /** Same idea for structural edits that can't be expressed as one block's
      wet fade (reorder, cross-lane move): glide the whole chain output to
      silence and wait (bounded) so the edit splices in silently. The caller
      clears `chainEditFadePending` afterwards to glide back; use the
      ChainEditFade RAII below. Must be called WITHOUT holding chainMutex. */
  void requestChainEditFadeAndWait();

  /** Deferred release of the chain-edit fade for whole-chain restores
      (preset load, undo/redo): a restore queues its blocks' engines on the
      background loader, so releasing the mute at scope exit plays the raw
      dry input at full level for the whole reload window, the "preset
      switch pop". This instead keeps the chain muted until no block is
      `modelLoading` anymore (or a bounded cap expires; a network fetch
      shouldn't hold the rig silent), then glides back in on the ready rig.
      Called via ChainEditFade::releaseWhenChainLoadsSettle. */
  void releaseChainEditFadeWhenLoadsSettle();

  /** RAII wrapper for the chain-edit fade: fades the chain output to silence
      on construction, lets it glide back on scope exit (after the mutation,
      including early-error returns). Restores that queue background model
      loads call releaseWhenChainLoadsSettle() instead, handing the release
      to a background waiter that holds the mute until the loads land. */
  struct ChainEditFade {
    explicit ChainEditFade(TONE3000Processor& proc) : p(proc) {
      // Newer fade session: any deferred waiter still in flight stands down
      // (its generation is stale); this fade owns the release now.
      p.chainEditFadeHoldGeneration.fetch_add(1);
      p.requestChainEditFadeAndWait();
    }
    ~ChainEditFade() {
      if (!deferred)
        p.chainEditFadePending.store(false);
    }
    void releaseWhenChainLoadsSettle() {
      deferred = true;
      p.releaseChainEditFadeWhenLoadsSettle();
    }
    TONE3000Processor& p;
    bool deferred{false};
  };

  /** True while the audio thread is actively receiving callbacks (updated
      every processBlock). The fade handshakes skip their bounded waits when
      audio is stopped: nothing is audible, and blocking a click gesture on
      the message thread for the full timeout would feel sluggish. */
  bool isAudioActive() const {
    return juce::Time::currentTimeMillis() - lastAudioCallbackMs.load() < 150;
  }

  // Run one chain (the per-block loop) over the supplied working buffer. The buffer may have
  // 1 channel (a single side in stereo mode) or 1-2 channels (mono mode). All per-channel work
  // is keyed on buffer.getNumChannels(). Runs inside the chain domain (48 kHz; see
  // ChainDomain.h). Must be called while holding `chainMutex`.
  // `dryScratch` is the dry-copy scratch the per-block mix blends against,
  // per lane (see laneDryScratch) so the two lanes never share mutable state
  // and can run concurrently.
  // `beginIdx`/`endIdx` bound the block range [beginIdx, endIdx) so the
  // branched routing can split one lane around the tap point; the defaults
  // run the whole lane (endIdx -1 = blocks.size()). Whole-chain context
  // (lastNamIndex) is always computed over the full lane regardless of the
  // range; the range is a routing split, not a different chain.
  void processChainOnBuffer(std::vector<std::unique_ptr<ChainBlock>>& blocks,
                            juce::AudioBuffer<float>& buffer,
                            juce::AudioBuffer<float>& dryScratch, int beginIdx = 0,
                            int endIdx = -1);

  // Run two independent chain sections, the `worker*` one on an RtWorkerPool
  // thread and the `local*` one on the calling (audio) thread, when this
  // callback forked (rtParallelLanes); strictly sequentially otherwise. The
  // sections are lane-disjoint by construction (different Lane, buffer and
  // scratch), so both schedules produce bit-identical output. Caller holds
  // chainMutex; the worker inherits that protection because it runs entirely
  // inside this call.
  void processLanePair(Lane& workerBlocks, juce::AudioBuffer<float>& workerBuffer,
                       juce::AudioBuffer<float>& workerScratch, int workerBeginIdx,
                       Lane& localBlocks, juce::AudioBuffer<float>& localBuffer,
                       juce::AudioBuffer<float>& localScratch, int localBeginIdx);

  // The whole chain stage at the chain rate: lane L (and lane R in stereo
  // mode) over the given channel pointers. Called either directly (48k host)
  // or as the boundary resampler's encapsulated callback. `inputs`/`outputs`
  // both carry 2 pointers (on a mono host buffer the second one is the
  // scratch channel); processing is in place on `outputs` after an
  // input→output copy (skipped when they alias). Caller holds `chainMutex`.
  void processChainStage(float** inputs, float** outputs, int numFrames);

  // The post-chain stereo image at the host rate, over the two chain output
  // channels the stage above produced (`chR` is the scratch channel on a
  // mono host buffer): the mode's image engine (Spread / Align), the
  // auto-balance and auto-align taps, then the balance/pan matrix, which
  // becomes a mono fold when stereo chains run on a rig that can't
  // reproduce stereo (`stereoRig` false: mono bus, or a one-channel
  // standalone output device). Called once per chain-stage slice so the
  // fold stays correct even when a host exceeds its promised block size;
  // every processor it touches is a streaming engine, so slicing is
  // transparent. Caller holds `chainMutex` (it runs inside the chain-stage
  // slice loop).
  void processImageStage(float* chL, float* chR, int numFrames, bool stereoRig);

  // Prepare every engine in a chain for the current chain rate (see
  // chainSampleRate) and chain-domain block size. Holds no lock.
  void prepareChain(std::vector<std::unique_ptr<ChainBlock>>& blocks);

  // Recompute the longest loaded IR across both lanes into irTailBaseSamples
  // (base-rate samples; IRs always convolve at the base rate, see
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

  // Post-structural-change bookkeeping bundle: re-baseline both lanes
  // (normalizeLaneInserts), re-resolve the branch tap, then keep the lanes'
  // visible ends even while a branch is active by trimming the branch lane's
  // surplus *trailing* insert placeholders (trim-only; see the definition
  // for the geometry). Structural mutators call this instead of bare
  // normalizeLaneInserts + refreshBranchTapIndex. Caller holds chainMutex.
  void alignBranchLaneLengths();

  // Find a block by id across both chains (ids are globally unique). Returns nullptr if absent.
  ChainBlock* findBlockById(const std::string& blockId);

  // Shared tail of duplicateChainBlock/pasteChainBlock: land a freshly built
  // tone block in `side` at `index` (an insert slot there is consumed,
  // anywhere else splices in), restore the lane invariants, bump the revision
  // and queue the block's active model (cache-first). Returns the block's id.
  // Caller holds chainMutex and has already recorded history.
  std::string landToneBlock(std::unique_ptr<ChainBlock> block, const juce::String& side,
                            int index);

  // State (de)serialization helpers.
  // serializeBlockSettings/applyBlockSettings cover everything user-editable
  // on a block (identity, tone refs, gains, mix, EQ): the single source of
  // truth shared by plugin state persistence and undo/redo snapshots. Model
  // bytes are only included when `includeModelData` is set (project files).
  static juce::ValueTree serializeBlockSettings(const ChainBlock& block);
  void applyBlockSettings(ChainBlock& block, const juce::ValueTree& blockState);
  // Set a block's tone identity in one place: raw JSON, parsed var, and the
  // slim UI summary (see makeToneSummary) always stay in sync.
  static void setToneOnBlock(ChainBlock& block, int toneId, const juce::String& toneJson,
                             const juce::var& parsedTone);
  // Slim tone projection for getChainState: the UI renders only
  // id/title/format/gear/first image/user/model names/share url; shipping
  // the full API payload (model URLs, tags, counts…) per block per sync is
  // waste.
  static juce::var makeToneSummary(const juce::var& toneVar);
  static void serializeChainToTree(const std::vector<std::unique_ptr<ChainBlock>>& blocks,
                                   juce::ValueTree& chainState, bool includeModelData);

  // Undo/redo internals (ProcessorHistory.cpp).
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
  // background load. Caller must hold chainMutex, and must destroy the
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

  // Block clipboard (see copyChainBlock/pasteChainBlock). `settings` is a
  // serializeBlockSettings tree (invalid = nothing copied); the model bytes
  // ride separately so paste comes up without a network round trip, exactly
  // like duplicate. Guarded by chainMutex; in-memory only (deliberately not
  // part of the DAW session state).
  juce::ValueTree blockClipboardSettings;
  std::map<int, std::vector<uint8_t>> blockClipboardModelCache;

  // MIDI performance handlers (wired to midiMapper in the constructor,
  // both invoked on the message thread).
  // Program change n loads the nth preset in list order (user first, then
  // factory; the same order the preset browser shows and numbers).
  // Out-of-range programs are ignored.
  bool loadPresetAtIndex(int index);
  // Step the active preset through the list order, wrapping at the ends:
  // the MIDI twin of the preset bar's ‹ › buttons (mapped "presetPrevious" /
  // "presetNext" controls land here). With no active preset, a forward step
  // starts at the first preset and a backward step at the last.
  bool stepPreset(int delta);
  // Toggle the enabled flag of a lane's Nth tone block (0-based, insert
  // slots skipped). Positional so mappings survive tone swaps and preset
  // loads. No-op when the lane is shorter than N, and for the Right lane
  // outside stereo mode, so an inert lane is never edited invisibly.
  bool toggleBlockPower(int position, bool rightLane);

  // Preset internals (ProcessorPresets.cpp).
  // The faceplate parameters a preset carries. Explicitly scoped: rig
  // calibration (calibrateInput, inputCalibrationLevel) and the global
  // loudness target (targetLoudness) describe the user's setup, not the
  // tone, so they stay out of presets. (Per-block normalization rides the
  // chain snapshot itself.)
  static const std::vector<juce::String>& presetParameterIds();
  void setActivePreset(const juce::String& id, const juce::String& name);
  // True when nothing distinguishes the state from a fresh instance: no
  // active preset, mono mode, no tone blocks, every preset-managed parameter
  // at its default. Ships as getChainState's `atDefault`, which greys the
  // top bar's New button. Caller must hold chainMutex.
  bool isChainAtDefault() const;

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

  // Output-side twin: the standalone app with a one-channel output device.
  // The buffer can still be stereo then (the device only plays channel 0),
  // so this feeds stereoOutputDetected below; without it Spread would run
  // and the listener would hear half the double, and a stereo-chain rig
  // would play its Left chain alone instead of the mono sum.
  std::atomic<bool> standaloneMonoOutput{false};

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

  // Chain branch state (see the public setChainBranch).
  // All guarded by chainMutex (the audio thread reads them with the lock
  // held). `branchAfterBlockId` empty = no branch (today's independent
  // chains). `rtBranchTapIndex` is the id resolved to a trunk-lane index for
  // the RT path (-1 when no branch or while the branch lies dormant in mono
  // mode); refreshBranchTapIndex re-resolves it after every structural change
  // and clears the branch when the tapped block no longer lives in the trunk
  // lane (removed, moved across, stale restore).
  ChainSide branchSourceSide{ChainSide::Left};
  std::string branchAfterBlockId;
  int rtBranchTapIndex{-1};
  void refreshBranchTapIndex();

  // Global chain-edit fade (see ChainEditFade / requestChainEditFadeAndWait).
  // The gain rides the host-rate buffer right after the chain stage; the
  // smoother is audio-thread-only, the flags are the cross-thread handshake.
  std::atomic<bool> chainEditFadePending{false};
  std::atomic<bool> chainEditFadeDone{false};
  juce::LinearSmoothedValue<float> chainEditFadeGain;
  // Deferred-release bookkeeping (releaseChainEditFadeWhenLoadsSettle): the
  // generation stamps which fade session a background waiter belongs to
  // (a newer ChainEditFade invalidates in-flight waiters), and the deadline
  // caps how long a restore may hold the chain muted while models load.
  std::atomic<int> chainEditFadeHoldGeneration{0};
  std::atomic<juce::uint32> chainEditFadeHoldDeadlineMs{0};

  // Milliseconds timestamp of the last processBlock, for isAudioActive().
  std::atomic<juce::int64> lastAudioCallbackMs{0};

  // Output-stage gain (main level only; the balance trim lives in the
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

  // Chain-domain resampling boundary (see ChainDomain.h).
  // Engaged (non-null) only when the host rate differs from the chain base
  // rate; created/reset in prepareToPlay, so the audio thread never sees it
  // change.
  std::unique_ptr<ChainBoundaryResampler> chainBoundary;
  // Chain oversampler (see ChainOversampler.h).
  // Raises the chain rate to kChainBaseSampleRate × factor inside the
  // boundary. Factor 1 = transparent passthrough. The factor atomic is the
  // single source of truth for the live chain rate; it's only written inside
  // the re-prepare paths (prepareToPlay / applyOversamplingSettings) while
  // the chain is quiesced.
  ChainOversampler chainOversampler;
  std::atomic<int> chainOversampleFactor{1};
  // The chain stage behind the oversampler: the callable both invocation
  // paths (direct and boundary) share.
  void processOversampledChainStage(float** inputs, float** outputs, int numFrames);
  // The encapsulated callback, built once in the constructor (capturing only
  // `this`) so ProcessBlock never allocates a std::function per audio block.
  ChainBoundaryResampler::BlockProcessFunc chainStageFunc;
  // Boundary latency in host samples (0 at a 48k host). Constant per host
  // rate; chain edits never change reported latency.
  int chainBoundaryLatency = 0;
  // Second channel handed to the boundary when the host buffer is mono (the
  // boundary is a fixed 2-channel container). Silent in mono chain mode;
  // with stereo chains it becomes the Right lane's working channel: fed a
  // copy of the mono input per slice, and folded into channel 0 by the
  // image stage afterwards.
  juce::AudioBuffer<float> chainScratchChannel;
  // Per-callback routing state for processChainStage, set by processBlock
  // under chainMutex just before invoking the stage (audio thread only).
  // rtStereoChains follows the *mode* alone: stereo chains run on any rig
  // (a mono rig hears them summed; see processImageStage).
  int rtChainChannels = 2;
  bool rtStereoChains = false;
  // True when this callback's chain stage should fork the two lanes across
  // cores (see RtWorkerPool.h): multi-core enabled, workers healthy, stereo
  // chains active, and both sides of the parallel section actually carry
  // work. Resolved once per processBlock under chainMutex.
  bool rtParallelLanes = false;
  // Per-callback pool handle for NAM phase forks (see NamEngine::process):
  // &rtWorkerPool when multi-core is enabled and workers are up, nullptr
  // otherwise (phases run serially). Unlike the lane fork this doesn't need
  // stereo mode; a mono chain's oversampled NAM blocks fork too. Resolved
  // once per processBlock under chainMutex.
  RtWorkerPool* rtPhasePool = nullptr;

  // Multi-core processing (see the public getMultiCoreEnabled).
  // One pool serves both parallel sections: the stereo lane fork and the
  // NAM phase forks nested inside a lane. The threads live from
  // prepareToPlay to releaseResources regardless of the setting (parked
  // threads are ~free); the setting only gates dispatch, so toggling it is
  // glitch-free and instant.
  RtWorkerPool rtWorkerPool;
  static bool readPersistedMultiCoreEnabled();
  std::atomic<bool> multiCoreEnabled{readPersistedMultiCoreEnabled()};
  // Hosts hand the device's os_workgroup here (AU/Standalone on macOS);
  // forward it so the workers get scheduled with the audio deadline.
  void audioWorkgroupContextChanged(const juce::AudioWorkgroup& workgroup) override {
    rtWorkerPool.setAudioWorkgroup(workgroup);
  }
  // Does the lane do any audible processing from `beginIdx` on? Gates the
  // fork: dispatching an idle lane costs more than running its (empty) loop
  // inline. A block still gliding through its wet fade counts as work.
  static bool laneHasWork(const Lane& l, int beginIdx = 0) {
    for (size_t i = static_cast<size_t>(juce::jmax(0, beginIdx)); i < l.size(); ++i)
      if (l[i]->type != ChainBlockType::INSERT && l[i]->loaded)
        return true;
    return false;
  }

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
  // tracked across blocks so re-enabling resets the detector; a stale
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
    std::atomic<float>* spreadWobble = nullptr;
    std::atomic<float>* spreadWobbleEnabled = nullptr;
    std::atomic<float>* spreadCrossover = nullptr;
    std::atomic<float>* spreadCrossoverEnabled = nullptr;
    std::atomic<float>* spreadDiffuseEnabled = nullptr;
    std::atomic<float>* alignEnabled = nullptr;
    std::atomic<float>* alignOffset = nullptr;
    std::atomic<float>* alignWobble = nullptr;
    std::atomic<float>* alignWobbleEnabled = nullptr;
    std::atomic<float>* alignCrossover = nullptr;
    std::atomic<float>* alignCrossoverEnabled = nullptr;
    std::atomic<float>* alignDiffuseEnabled = nullptr;
    std::atomic<float>* chainPanLeft = nullptr;
    std::atomic<float>* chainPanRight = nullptr;
    std::atomic<float>* chainSoloLeft = nullptr;
    std::atomic<float>* chainSoloRight = nullptr;
    std::atomic<float>* chainInvertLeft = nullptr;
    std::atomic<float>* chainInvertRight = nullptr;
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

  // Oversampling live switching.
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
  float cacheSpreadOffset = 0.8125f;  // bipolar, 0.5 = center = 0 ms; default +15 ms R
  float cacheSpreadWobble = 0.25f;    // 0..1 of the ±1.2 ms wobble range
  bool cacheSpreadWobbleEnabled = true;
  float cacheSpreadCrossover = 0.5f;  // log map 32.5..520 Hz; 0.5 = 130 Hz
  bool cacheSpreadCrossoverEnabled = true;
  bool cacheSpreadDiffuseEnabled = true;
  bool cacheAlignEnabled = false;
  float cacheAlignOffset = 0.5f;   // bipolar, 0.5 = center = 0 ms
  float cacheAlignWobble = 0.25f;  // same span as spread's wobble
  bool cacheAlignWobbleEnabled = false;
  float cacheAlignCrossover = 0.5f;  // log map 32.5..520 Hz; 0.5 = 130 Hz
  bool cacheAlignCrossoverEnabled = false;
  bool cacheAlignDiffuseEnabled = false;
  float cacheChainPanLeft = 0.0f;
  float cacheChainPanRight = 1.0f;
  bool cacheChainSoloLeft = false;
  bool cacheChainSoloRight = false;
  bool cacheChainInvertLeft = false;
  bool cacheChainInvertRight = false;
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

  // Per-lane dry-copy scratch for the block mix stage (chain-domain sized;
  // see chainDomainBlockSize). One buffer per lane so the stereo lanes own
  // disjoint scratch and can process concurrently; each stays 2-channel
  // because mono mode runs a (possibly stereo) buffer through lane 0 alone.
  std::array<juce::AudioBuffer<float>, kNumLanes> laneDryScratch;
  double hostSampleRate = 48000.0;  // Default, updated dynamically in prepareToPlay

  // Default NAM A2 size for new blocks (see setNamSlimSizeDefault). Atomic:
  // written on the message thread, read wherever loadTone stamps a block.
  static double readPersistedNamSlimSizeDefault();
  std::atomic<double> namSlimSizeDefault{readPersistedNamSlimSizeDefault()};

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
  // updateStereoIoDetection().
  std::atomic<bool> stereoInputDetected{false};

  // True when the plugin can drive two distinct output channels (stereo bus
  // in a host, 2+ output channels in the standalone app). When false, Spread
  // stays idle no matter what its parameter says (the UI greys it out), and
  // stereo chains keep running but are summed to mono by the image stage
  // (see processImageStage; the UI shows a MONO chip on the pan rail).
  // Starts true so an editor opening before the first prepareToPlay doesn't
  // flash those states. Reported via getChainState; see
  // updateStereoIoDetection().
  std::atomic<bool> stereoOutputDetected{true};

  // Input channel mode (see InputMode). Atomic: written by the message
  // thread (faceplate button / state restore), read by the audio thread.
  std::atomic<int> inputMode{static_cast<int>(InputMode::Stereo)};
  bool isStandalone() const { return wrapperType == wrapperType_Standalone; }
  void updateStereoIoDetection();

  // Tuner pitch detection (fed from processBlock when enabled)
  TunerDetector tuner;

  // Auto balance measurement state.
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

  // Post-chain stereo image engines, one per chain mode (the modes are
  // exclusive; the inactive one is force-idled, and mode switches happen under
  // the chain-edit fade, so the hard stop is inaudible):
  //  - Mono mode: the Spread builds a stereo image from the single chain
  //    (an ADT-style double; see Spread.h). Its output correlation ships to
  //    the UI via getMeterLevels. Idle on rigs that can't reproduce stereo.
  //  - Stereo mode: Align time-aligns the two chains via StereoOffset (see
  //    StereoOffset.h). Runs on any rig; on a mono rig it shapes the chains
  //    right before they are summed (see processImageStage).
  Spread spread;
  StereoOffset stereoOffset;

  // Auto-align probe engine (sweep schedule + capture + estimation all live
  // in AutoOffset; the processor injects the probe, taps the chain outputs,
  // applies the mute stage, and writes the result to the parameters; see
  // the public startAutoOffset/pollAutoOffset above).
  AutoOffset autoOffset;

  // Post-chain image matrix gains (per-chain balance trim × constant-power
  // pan, or the mono fold; see imageMatrixGains in Processor.cpp), smoothed
  // so balance/pan moves don't zipper. LtoR = how much of the Left chain
  // lands in the Right output, etc. At the centered/hard-panned default the
  // matrix is the identity and the image stage skips the mix loop entirely;
  // the fold configuration is never the identity, so it always runs.
  juce::SmoothedValue<float> imageGainLtoL, imageGainLtoR, imageGainRtoL, imageGainRtoR;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TONE3000Processor)
};
