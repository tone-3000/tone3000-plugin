// ui::Backend over the real TONE3000Processor. Owns the standalone
// audio-settings controller (nullptr in hosts) and forwards its
// device-change pushes and the MIDI mapper's change hook to Backend
// listeners.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Backend.h"
#include "Processor.h"
#include "StandaloneAudioSettings.h"

namespace t3k::ui {

class ProcessorBackend : public Backend {
public:
  // `peerHost` is the editor: its window peer handle is what host-key
  // forwarding needs.
  ProcessorBackend(TONE3000Processor& processor, juce::Component& peerHost);
  ~ProcessorBackend() override;

  juce::RangedAudioParameter* parameter(const juce::String& id) override;

  juce::var getChainState(int knownRevision) override;
  juce::uint32 chainRevision() override;
  bool undoChain() override;
  bool redoChain() override;
  bool resetToDefault() override;

  std::string loadTone(const juce::String& toneJson, const std::string& targetInsertId) override;
  juce::var loadLocalTonePath(const juce::File& source, const std::string& targetInsertId) override;
  juce::var loadLocalToneInFolder(const juce::File& file, const std::string& targetInsertId) override {
    return processor_.loadLocalToneInFolder(file, targetInsertId);
  }
  juce::var loadLocalToneUrls(const juce::Array<juce::URL>& sources,
                              const std::string& targetInsertId) override;
  bool swapTone(const std::string& blockId, const juce::String& toneJson) override;
  bool refreshToneMetadata(const juce::String& toneJson) override;
  void relinkLocalFiles(const juce::File& from, const juce::File& to) override {
    processor_.relinkLocalFiles(from, to);
  }
  bool setLocalToneArt(const std::string& blockId, const juce::var& art) override {
    return processor_.setLocalToneArt(blockId, art);
  }
  bool switchModel(const std::string& blockId, int modelId, const juce::var& model) override;
  bool retryModelLoad(const std::string& blockId) override;
  bool removeChainBlock(const std::string& blockId) override;
  bool reorderChainBlocks(const std::vector<std::string>& newOrder) override;
  bool moveBlockToChain(const std::string& blockId, const juce::String& side, int index) override;
  std::string duplicateChainBlock(const std::string& blockId, const juce::String& side,
                                  int index) override;
  bool copyChainBlock(const std::string& blockId) override;
  std::string pasteChainBlock(const juce::String& side, int index) override;
  bool swapChains() override;
  bool setChainBranch(const juce::String& side, const std::string& afterBlockId) override;
  bool clearChainBranch() override;
  void setStereoMode(bool enabled) override;
  void setInputMode(const juce::String& mode) override;
  void setActiveEditChain(const juce::String& side) override;
  void setNamSlimSizeDefault(double slimSize) override;
  void setMultiCore(bool enabled) override;
  void persistParamAsMachineDefault(const juce::String& id) override;

  bool setBlockParam(const std::string& blockId, const juce::String& param, double value) override;
  bool setBlockSlimSize(const std::string& blockId, double slimSize) override;
  bool setBlockEqBand(const std::string& blockId, int bandIndex, const juce::var& band) override;
  bool setBlockEqEnabled(const std::string& blockId, bool enabled) override;
  bool setBlockEqPre(const std::string& blockId, bool pre) override;
  bool resetBlockEq(const std::string& blockId) override;
  bool setBlockSpectrumEnabled(const std::string& blockId, bool enabled) override;
  juce::var getBlockSpectrum(const std::string& blockId) override;

  juce::var getPresetList() override;
  juce::var savePreset(const juce::String& name) override;
  bool loadPreset(const juce::String& presetId) override;
  bool renamePreset(const juce::String& presetId, const juce::String& newName) override;
  bool deletePreset(const juce::String& presetId) override;
  bool movePreset(const juce::String& presetId, int delta) override;
  bool stepPreset(int delta) override;
  juce::var savePresetToFolder(const juce::File& folder, const juce::String& name) override;
  void setLibraryLocation(const juce::File& root, const juce::String& owner,
                          const juce::Array<juce::File>& linkedDirs) override;
  juce::String libraryLinkProblem(const juce::File& dir) override;
  juce::var getLibrary(bool fresh, const std::atomic<bool>* stop = nullptr) override;
  juce::var getSavedLibrary() override;
  juce::String getLibraryView() override { return processor_.getLibraryView(); }
  void setLibraryView(const juce::String& json) override { processor_.setLibraryView(json); }
  juce::File libraryCreateFolder(const juce::File& parent, const juce::String& name) override;
  juce::String libraryFolderNameProblem(const juce::String& name) override;
  juce::File libraryRename(const juce::File& item, const juce::String& name) override;
  bool libraryRemove(const juce::File& item) override;
  juce::File libraryMove(const juce::File& item, const juce::File& folder) override;
  juce::File libraryCopy(const juce::File& item, const juce::File& folder) override;
  juce::File libraryAddTone(const juce::File& folder, const juce::var& ref) override;
  juce::File libraryImportFolder(const juce::File& source, const juce::File& into) override {
    return processor_.libraryImportFolder(source, into);
  }
  juce::File libraryAddCapture(const juce::File& folder, const juce::File& source,
                               const juce::String& name) override;
  juce::File libraryKeepModel(const std::string& blockId, const juce::File& folder,
                              const juce::String& name) override {
    return processor_.libraryKeepModel(blockId, folder, name);
  }
  void libraryDownloadModel(const juce::String& modelUrl, bool ir, const juce::File& folder, const juce::String& name,
                            std::function<void(juce::File)> done) override {
    processor_.libraryDownloadModel(modelUrl, ir, folder, name, std::move(done));
  }
  bool libraryExport(const juce::File& item, const juce::File& archive) override;
  void loadLocalToneInFolderAsync(const juce::File& file, const std::string& targetInsertId,
                                  std::function<void(juce::var)> done) override;
  void libraryImportFolderAsync(const juce::File& source, const juce::File& into,
                                std::function<void(juce::File)> done) override;
  void libraryExportAsync(const juce::File& item, const juce::File& archive, std::function<void(bool)> done) override;
  void libraryShareAsync(const juce::File& item, const juce::File& archive, const juce::var& siteRefs,
                         std::function<void(juce::var)> done) override;
  void libraryMoveAsync(const juce::File& item, const juce::File& folder, std::function<void(juce::File)> done) override;
  void libraryCopyAsync(const juce::File& item, const juce::File& folder, std::function<void(juce::File)> done) override;
  void libraryRemoveAsync(const juce::File& item, std::function<void(bool)> done) override;
  void libraryCopyFilesAsync(const juce::Array<juce::File>& files, const juce::File& folder,
                             std::function<void(juce::var)> done) override;
  void libraryImportAsync(const juce::File& archive, std::function<void(juce::File)> done) override;
  juce::File libraryImport(const juce::File& archive) override;

  juce::var getAudioDeviceState() override;
  juce::var setAudioDeviceType(const juce::String& typeName) override;
  juce::var setAudioDevice(const juce::String& kind, const juce::String& name) override;
  juce::var setAudioInputChannels(const juce::Array<juce::var>& indices) override;
  juce::var setAudioOutputPair(int pairIndex) override;
  juce::var setAudioSampleRate(double rate) override;
  juce::var setAudioBufferSize(int samples) override;
  juce::var setHearYourself(bool hear) override;
  juce::var playTestTone() override;
  juce::var openAudioControlPanel() override;
  juce::var restartAudioDevice() override;
  juce::var openMicSettings() override;
  juce::var setMidiInputEnabled(const juce::String& id, bool enabled) override;
  juce::var openBluetoothMidiPairing() override;
  void setAudioInputMetering(bool enabled) override;
  juce::var getAudioInputLevels() override;

  juce::var getMidiMapState() override;
  void setMidiChannelFilter(int channel) override;
  void startMidiLearn(const juce::String& targetId) override;
  void cancelMidiLearn() override;
  bool removeMidiMapping(const juce::String& targetId) override;
  bool setMidiCcMapping(const juce::String& targetId, int cc) override;

  juce::var getMeterLevels() override;
  void setTunerEnabled(bool enabled) override;
  juce::var getTunerReading() override;
  void startAutoBalance() override;
  void cancelAutoBalance() override;
  juce::var pollAutoBalance() override;
  void startAutoOffset() override;
  void cancelAutoOffset() override;
  juce::var pollAutoOffset() override;

  juce::String pluginVersion() override;
  juce::String uniqueDeviceId() override;
  void setAccessToken(const juce::String& token) override;
  void copyToClipboard(const juce::String& text) override;
  bool copyLogs() override;
  juce::String revealLogs() override;
  bool canOpenPresetsFolder() override;
  bool openPresetsFolder() override;
  bool canOpenDateTimeSettings() override;
  bool openDateTimeSettings() override;
  bool forwardKeyToHost(HostKey key) override;

private:
  // Standalone-only calls resolve to void in hosts.
  template <typename Fn>
  juce::var withAudioSettings(Fn&& fn) {
    return audioSettings_ != nullptr ? fn(*audioSettings_) : juce::var();
  }

  TONE3000Processor& processor_;
  juce::Component& peerHost_;
  std::unique_ptr<StandaloneAudioSettings> audioSettings_;
};

}  // namespace t3k::ui
