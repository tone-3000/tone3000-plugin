// Fixture-driven ui::Backend for the testbed. State comes from one scenario
// entry of fixtures/scenarios.json; mutations acknowledge and bump the
// revision the way the processor would, so drive steps produce the same
// screens the plugin shows, with no audio engine behind them.
#pragma once

#include <memory>
#include <vector>

#include "MockSignal.h"
#include "backend/Backend.h"

namespace t3k::ui::testbed {

class MockBackend : public Backend {
public:
  explicit MockBackend(const juce::var& scenario);
  ~MockBackend() override;

  // Live pokes for drive steps.
  void setChain(juce::var chain);
  void setTuner(juce::var tuner) { tuner_ = std::move(tuner); }
  void setAutoMeasure(juce::var state) { autoMeasure_ = std::move(state); }
  void setDevice(juce::var device);
  // Live: meters, spectrum, tuner and input levels follow a moving signal
  // (MockSignal) instead of the scenario's frozen values.
  void setLive(bool live) { signal_ = live ? std::make_unique<MockSignal>() : nullptr; }

  juce::RangedAudioParameter* parameter(const juce::String& id) override;

  juce::var getChainState(int knownRevision) override;
  juce::uint32 chainRevision() override;
  bool undoChain() override { return true; }
  bool redoChain() override { return true; }
  bool resetToDefault() override { return true; }

  std::string loadTone(const juce::String&, const std::string&) override { return {}; }
  juce::var loadLocalTonePath(const juce::File&, const std::string& target) override;
  // The target the last local load was sent to (an insert, a tone block, or
  // a slot beside one: slotBefore / slotAfter).
  const std::string& lastLocalLoadTarget() const { return lastLocalLoadTarget_; }
  juce::String getLibraryView() override { return libraryView_; }
  void setLibraryView(const juce::String& json) override { libraryView_ = json; }
  // ...and the file or folder it loaded.
  const juce::File& lastLocalLoadFile() const { return lastLocalLoadFile_; }
  juce::var loadLocalToneInFolder(const juce::File& file, const std::string& target) override {
    return loadLocalTonePath(file, target);
  }
  juce::var loadLocalToneUrls(const juce::Array<juce::URL>&, const std::string&) override;
  bool swapTone(const std::string&, const juce::String&) override { return true; }
  bool refreshToneMetadata(const juce::String&) override { return true; }
  void relinkLocalFiles(const juce::File& from, const juce::File& to) override { relinks_.push_back({from, to}); }
  // Every relinkLocalFiles call so far: from, to.
  const std::vector<std::pair<juce::File, juce::File>>& relinks() const { return relinks_; }
  bool setLocalToneArt(const std::string& blockId, const juce::var& art) override {
    localToneArt_[blockId] = art;
    // What the block shows, merged as the processor merges: "clear" drops
    // the artwork, then what this one names is set.
    auto& look = localToneLook_[blockId];
    if (!look.isObject()) look = juce::var(new juce::DynamicObject());
    if (static_cast<bool>(art.getProperty("clear", false)))
      for (const char* key : {"image", "username", "url"}) look.getDynamicObject()->removeProperty(key);
    if (const auto* given = art.getDynamicObject())
      for (const auto& p : given->getProperties())
        if (p.name.toString() != "clear") look.getDynamicObject()->setProperty(p.name, p.value);
    ++localToneArtCalls_;
    if (artBumpsChain_) bumpChain();  // the processor's revision bump
    return true;
  }
  void setArtBumpsChain(bool bumps) { artBumpsChain_ = bumps; }
  int localToneArtCalls() const { return localToneArtCalls_; }
  // What a block shows after every setLocalToneArt so far (merged).
  juce::var localToneLook(const std::string& blockId) const {
    const auto it = localToneLook_.find(blockId);
    return it == localToneLook_.end() ? juce::var() : it->second;
  }
  // A new tone in the block (a load, a swap): its look starts over.
  void forgetLocalToneLook(const std::string& blockId) { localToneLook_.erase(blockId); }
  // The last setLocalToneArt per block (title / artwork / clear).
  juce::var localToneArt(const std::string& blockId) const {
    const auto it = localToneArt_.find(blockId);
    return it == localToneArt_.end() ? juce::var() : it->second;
  }
  bool switchModel(const std::string& blockId, int modelId, const juce::var&) override {
    lastSwitch_ = {blockId, modelId};
    return true;
  }
  // The last switchModel call: block id and model id.
  const std::pair<std::string, int>& lastSwitch() const { return lastSwitch_; }
  bool retryModelLoad(const std::string&) override { return true; }
  bool removeChainBlock(const std::string&) override { return true; }
  bool reorderChainBlocks(const std::vector<std::string>&) override { return true; }
  // Moves the block between lanes like the processor, and records the call
  // so self-tests can check what a cross-lane drop asked for.
  bool moveBlockToChain(const std::string& blockId, const juce::String& side, int index) override;
  struct ChainMove {
    std::string id;
    juce::String side;
    int index{0};
  };
  const std::vector<ChainMove>& chainMoves() const { return chainMoves_; }
  std::string duplicateChainBlock(const std::string&, const juce::String&, int) override { return {}; }
  bool copyChainBlock(const std::string&) override { return true; }
  std::string pasteChainBlock(const juce::String&, int) override { return {}; }
  bool swapChains() override { return true; }
  bool setChainBranch(const juce::String&, const std::string&) override { return true; }
  bool clearChainBranch() override { return true; }
  void setStereoMode(bool) override {}
  void setInputMode(const juce::String& mode) override;
  void setActiveEditChain(const juce::String&) override {}
  void setNamSlimSizeDefault(double slimSize) override;
  void setMultiCore(bool enabled) override;
  // Recorded so self-tests can check which Settings-page edits were stored
  // as machine defaults.
  void persistParamAsMachineDefault(const juce::String& id) override { machineDefaults_.push_back(id); }
  const std::vector<juce::String>& machineDefaults() const { return machineDefaults_; }

  bool setBlockParam(const std::string&, const juce::String&, double) override { return true; }
  bool setBlockSlimSize(const std::string& blockId, double slimSize) override;
  bool setBlockEqBand(const std::string&, int, const juce::var&) override { return true; }
  bool setBlockEqEnabled(const std::string&, bool) override { return true; }
  bool setBlockEqPre(const std::string&, bool) override { return true; }
  bool resetBlockEq(const std::string&) override { return true; }
  bool setBlockSpectrumEnabled(const std::string&, bool) override { return true; }
  juce::var getBlockSpectrum(const std::string&) override;

  juce::var getPresetList() override;
  juce::var savePreset(const juce::String& name) override;
  bool loadPreset(const juce::String& presetId) override;
  bool renamePreset(const juce::String&, const juce::String&) override { return true; }
  bool deletePreset(const juce::String&) override { return true; }
  // Reorders presets_ within the preset's section like the real store, and
  // records the call so self-tests can check what the browser asked for.
  bool movePreset(const juce::String& presetId, int delta) override;
  // Walks presets_ (no folders here): the list stepping the real store does.
  bool stepPreset(int delta) override;
  juce::var savePresetToFolder(const juce::File&, const juce::String& name) override { return savePreset(name); }
  // The scenario's "library" tree, or one built from presets_ (your library
  // with them in its Presets folder). Edits are recorded, not applied.
  void setLibraryLocation(const juce::File&, const juce::String&, const juce::Array<juce::File>&) override {}
  juce::String libraryLinkProblem(const juce::File&) override { return {}; }
  juce::var getLibrary(bool fresh = false, const std::atomic<bool>* stop = nullptr) override;
  // A drive's own tree (one too big for a fixture). Message thread, before
  // the store rescans.
  void setLibrary(juce::var library) { library_ = std::move(library); }
  juce::File libraryCreateFolder(const juce::File& parent, const juce::String& name) override {
    const auto folder = parent.getChildFile(juce::File::createLegalFileName(name));
    if (parent.isDirectory()) folder.createDirectory();
    return recordEdit("createFolder", folder);
  }
  juce::File libraryRename(const juce::File& item, const juce::String& name) override {
    // On disk too when it is there, as the processor's rename (a library
    // renamed takes its .t3kpictures along).
    const auto target = item.getSiblingFile(name);
    if (item.exists() && !target.exists()) item.moveFileTo(target);
    return recordEdit("rename", target);
  }
  bool libraryRemove(const juce::File& item) override { return recordEdit("remove", item) != juce::File(); }
  juce::File libraryMove(const juce::File& item, const juce::File& folder) override {
    return recordEdit("move", folder.getChildFile(item.getFileName()));
  }
  juce::File libraryCopy(const juce::File& item, const juce::File& folder) override {
    return recordEdit("copy", folder.getChildFile(item.getFileName()));
  }
  juce::File libraryAddTone(const juce::File& folder, const juce::var& ref) override {
    return recordEdit("addTone", folder.getChildFile(ref["tone"]["title"].toString() + ".t3ktone"));
  }
  juce::File libraryImportFolder(const juce::File& source, const juce::File& into) override {
    return recordEdit("importFolder", into.getChildFile(source.getFileName()));
  }
  juce::File libraryAddCapture(const juce::File& folder, const juce::File& source, const juce::String& name) override {
    // Copied for real into a real folder, as native does (kept links check
    // that the copy exists).
    const auto copy = folder.getChildFile(name + source.getFileExtension());
    if (folder.isDirectory()) source.copyFileTo(copy);
    return recordEdit("addCapture", copy);
  }
  // Native's bytes, as a file in a real folder: "in memory" unless the test
  // says not (then the store downloads it).
  juce::File libraryKeepModel(const std::string&, const juce::File& folder, const juce::String& name) override {
    if (!modelsInMemory_ || !folder.isDirectory()) return {};
    const auto file = folder.getChildFile(juce::File::createLegalFileName(name) + ".nam");
    file.replaceWithText("{\"kept\": true}");
    return recordEdit("keepModel", file);
  }
  void libraryDownloadModel(const juce::String& modelUrl, bool, const juce::File& folder, const juce::String& name,
                            std::function<void(juce::File)> done) override {
    downloads_.add(modelUrl);
    juce::MessageManager::callAsync([this, folder, name, done] {
      juce::File file;
      if (folder.isDirectory()) {
        file = folder.getChildFile(juce::File::createLegalFileName(name) + ".nam");
        file.replaceWithText("{\"downloaded\": true}");
        recordEdit("download", file);
      }
      if (done) done(file);
    });
  }
  void setModelsInMemory(bool inMemory) { modelsInMemory_ = inMemory; }
  const juce::StringArray& downloads() const { return downloads_; }
  // Records the share and what it was handed (shareRefs).
  void libraryShareAsync(const juce::File& item, const juce::File&, const juce::var& siteRefs,
                         std::function<void(juce::var)> done) override {
    shareRefs_ = siteRefs;
    recordEdit("share", item);
    auto* summary = new juce::DynamicObject();
    summary->setProperty("links", siteRefs.getDynamicObject() != nullptr ? siteRefs.getDynamicObject()->getProperties().size() : 0);
    done(juce::var(summary));
  }
  const juce::var& shareRefs() const { return shareRefs_; }
  bool libraryExport(const juce::File& item, const juce::File&) override {
    return recordEdit("export", item) != juce::File();
  }
  juce::File libraryImport(const juce::File& archive) override { return recordEdit("import", archive); }
  struct LibraryEdit {
    juce::String op;
    juce::File result;
  };
  const std::vector<LibraryEdit>& libraryEdits() const { return libraryEdits_; }
  struct Move {
    juce::String id;
    int delta{0};
  };
  const std::vector<Move>& presetMoves() const { return presetMoves_; }

  juce::var getAudioDeviceState() override { return device_; }
  juce::var setAudioDeviceType(const juce::String&) override { return okResult(); }
  juce::var setAudioDevice(const juce::String&, const juce::String&) override { return okResult(); }
  juce::var setAudioInputChannels(const juce::Array<juce::var>&) override { return okResult(); }
  juce::var setAudioOutputPair(int) override { return okResult(); }
  juce::var setAudioSampleRate(double) override { return okResult(); }
  juce::var setAudioBufferSize(int) override { return okResult(); }
  juce::var setHearYourself(bool) override { return okResult(); }
  juce::var playTestTone() override { return okResult(); }
  juce::var openAudioControlPanel() override { return okResult(); }
  juce::var restartAudioDevice() override { return okResult(); }
  juce::var openMicSettings() override { return okResult(); }
  juce::var setMidiInputEnabled(const juce::String&, bool) override { return okResult(); }
  juce::var openBluetoothMidiPairing() override { return okResult(); }
  void setAudioInputMetering(bool) override {}
  juce::var getAudioInputLevels() override;

  juce::var getMidiMapState() override { return midiMap_; }
  void setMidiChannelFilter(int channel) override;
  void startMidiLearn(const juce::String& targetId) override;
  void cancelMidiLearn() override;
  bool removeMidiMapping(const juce::String& targetId) override;
  bool setMidiCcMapping(const juce::String& targetId, int cc) override;

  juce::var getMeterLevels() override;
  void setTunerEnabled(bool) override {}
  juce::var getTunerReading() override { return signal_ != nullptr ? signal_->tuner() : tuner_; }
  void startAutoBalance() override {}
  void cancelAutoBalance() override {}
  juce::var pollAutoBalance() override { return autoMeasure_; }
  void startAutoOffset() override {}
  void cancelAutoOffset() override {}
  juce::var pollAutoOffset() override { return autoMeasure_; }

  juce::String pluginVersion() override { return version_; }
  juce::String uniqueDeviceId() override { return "testbed-device"; }
  void setAccessToken(const juce::String&) override {}
  void copyToClipboard(const juce::String&) override {}
  bool copyLogs() override { return true; }
  juce::String revealLogs() override { return "/tmp/TONE3000.log"; }
  bool canOpenPresetsFolder() override { return true; }
  bool openPresetsFolder() override { return true; }
  bool canOpenDateTimeSettings() override { return true; }
  bool openDateTimeSettings() override { return true; }
  bool forwardKeyToHost(HostKey) override { return false; }

private:
  static juce::var okResult();
  void bumpChain();
  void notifyMidiMapChanged();

  // Parameters need a processor for change gestures; this one has no audio.
  struct Params;
  std::unique_ptr<Params> params_;

  juce::var chain_;
  juce::var device_;
  juce::var midiMap_;
  juce::var presets_;
  std::vector<Move> presetMoves_;
  juce::var library_;
  std::vector<LibraryEdit> libraryEdits_;
  juce::var shareRefs_;
  bool modelsInMemory_ = true;
  juce::StringArray downloads_;
  std::string lastLocalLoadTarget_;
  juce::String libraryView_;
  std::map<std::string, juce::var> localToneArt_;
  int localToneArtCalls_ = 0;
  bool artBumpsChain_ = false;
  juce::File lastLocalLoadFile_;
  std::pair<std::string, int> lastSwitch_;
  std::vector<std::pair<juce::File, juce::File>> relinks_;
  std::map<std::string, juce::var> localToneLook_;
  juce::File recordEdit(const juce::String& op, const juce::File& result) {
    libraryEdits_.push_back({op, result});
    return result;
  }
  std::vector<ChainMove> chainMoves_;
  std::vector<juce::String> machineDefaults_;
  juce::var meters_;
  juce::var tuner_;
  juce::var autoMeasure_;
  juce::String version_;
  std::unique_ptr<MockSignal> signal_;
};

}  // namespace t3k::ui::testbed
