#include "ProcessorBackend.h"

#include "NamArchitecture.h"
#include "WindowKeyEvents.h"

namespace t3k::ui {

ProcessorBackend::ProcessorBackend(TONE3000Processor& processor, juce::Component& peerHost)
    : processor_(processor), peerHost_(peerHost) {
  // Bespoke audio settings (standalone only): the controller listens to the
  // device manager and pushes changes; stores re-pull state on the event.
  if (StandaloneAudioSettings::isAvailable())
    audioSettings_ = std::make_unique<StandaloneAudioSettings>(processor_, [this] {
      listeners.call([](Listener& l) { l.audioDeviceChanged(); });
    });

  // MIDI map push (all builds): learn commits, removals and state restores
  // land as one event so the settings UI re-pulls instead of polling.
  processor_.midiMapper.onChanged = [this] {
    listeners.call([](Listener& l) { l.midiMapChanged(); });
  };
}

ProcessorBackend::~ProcessorBackend() {
  // The mapper outlives the editor (it's the processor's): detach our hook.
  processor_.midiMapper.onChanged = nullptr;
  // Tear down the audio settings controller before the listeners it reports to.
  audioSettings_.reset();
  // The tuner and block spectrum analyzers are only useful while the UI is
  // visible; stop feeding them when the editor goes away.
  processor_.setTunerEnabled(false);
  processor_.disableAllBlockSpectrums();
}

juce::RangedAudioParameter* ProcessorBackend::parameter(const juce::String& id) {
  return processor_.parameters.getParameter(id);
}

// Chain state / history
juce::var ProcessorBackend::getChainState(int knownRevision) {
  return processor_.getChainState(knownRevision);
}
juce::uint32 ProcessorBackend::chainRevision() { return processor_.getCurrentChainRevision(); }
bool ProcessorBackend::undoChain() { return processor_.undoChain(); }
bool ProcessorBackend::redoChain() { return processor_.redoChain(); }
bool ProcessorBackend::resetToDefault() { return processor_.resetToDefault(); }

// Chain mutations
std::string ProcessorBackend::loadTone(const juce::String& toneJson,
                                       const std::string& targetInsertId) {
  return processor_.loadTone(toneJson, targetInsertId);
}
juce::var ProcessorBackend::loadLocalTonePath(const juce::File& source,
                                              const std::string& targetInsertId) {
  return processor_.loadLocalTonePath(source, targetInsertId);
}
juce::var ProcessorBackend::loadLocalToneUrls(const juce::Array<juce::URL>& sources,
                                              const std::string& targetInsertId) {
  return processor_.loadLocalToneUrls(sources, targetInsertId);
}
bool ProcessorBackend::swapTone(const std::string& blockId, const juce::String& toneJson) {
  return processor_.swapTone(blockId, toneJson);
}
bool ProcessorBackend::refreshToneMetadata(const juce::String& toneJson) {
  return processor_.refreshToneMetadata(toneJson);
}
bool ProcessorBackend::switchModel(const std::string& blockId, int modelId, const juce::var& model) {
  return processor_.switchModel(blockId, modelId, model);
}
bool ProcessorBackend::retryModelLoad(const std::string& blockId) {
  return processor_.retryModelLoad(blockId);
}
bool ProcessorBackend::removeChainBlock(const std::string& blockId) {
  return processor_.removeChainBlock(blockId);
}
bool ProcessorBackend::reorderChainBlocks(const std::vector<std::string>& newOrder) {
  return processor_.reorderChainBlocks(newOrder);
}
bool ProcessorBackend::moveBlockToChain(const std::string& blockId, const juce::String& side,
                                        int index) {
  return processor_.moveBlockToChain(blockId, side, index);
}
std::string ProcessorBackend::duplicateChainBlock(const std::string& blockId,
                                                  const juce::String& side, int index) {
  return processor_.duplicateChainBlock(blockId, side, index);
}
bool ProcessorBackend::copyChainBlock(const std::string& blockId) {
  return processor_.copyChainBlock(blockId);
}
std::string ProcessorBackend::pasteChainBlock(const juce::String& side, int index) {
  return processor_.pasteChainBlock(side, index);
}
bool ProcessorBackend::swapChains() { return processor_.swapChains(); }
bool ProcessorBackend::setChainBranch(const juce::String& side, const std::string& afterBlockId) {
  return processor_.setChainBranch(side, afterBlockId);
}
bool ProcessorBackend::clearChainBranch() { return processor_.clearChainBranch(); }
void ProcessorBackend::setStereoMode(bool enabled) { processor_.setStereoMode(enabled); }
void ProcessorBackend::setInputMode(const juce::String& mode) {
  processor_.setInputMode(TONE3000Processor::inputModeFromString(mode));
}
void ProcessorBackend::setActiveEditChain(const juce::String& side) {
  processor_.setActiveEditChain(side);
}
void ProcessorBackend::setNamSlimSizeDefault(double slimSize) {
  processor_.setNamSlimSizeDefault(slimSize);
}
void ProcessorBackend::setMultiCore(bool enabled) { processor_.setMultiCoreEnabled(enabled); }
void ProcessorBackend::persistParamAsMachineDefault(const juce::String& id) {
  processor_.persistParameterAsMachineDefault(id);
}

// Per-block params / EQ / spectrum
bool ProcessorBackend::setBlockParam(const std::string& blockId, const juce::String& param,
                                     double value) {
  return processor_.setBlockParam(blockId, param, value);
}
bool ProcessorBackend::setBlockSlimSize(const std::string& blockId, double slimSize) {
  return processor_.setBlockSlimSize(blockId, slimSize);
}
bool ProcessorBackend::setBlockEqBand(const std::string& blockId, int bandIndex,
                                      const juce::var& band) {
  return band.isObject() && processor_.setBlockEqBand(blockId, bandIndex, band);
}
bool ProcessorBackend::setBlockEqEnabled(const std::string& blockId, bool enabled) {
  return processor_.setBlockEqEnabled(blockId, enabled);
}
bool ProcessorBackend::setBlockEqPre(const std::string& blockId, bool pre) {
  return processor_.setBlockEqPre(blockId, pre);
}
bool ProcessorBackend::resetBlockEq(const std::string& blockId) {
  return processor_.resetBlockEq(blockId);
}
bool ProcessorBackend::setBlockSpectrumEnabled(const std::string& blockId, bool enabled) {
  return processor_.setBlockSpectrumEnabled(blockId, enabled);
}
juce::var ProcessorBackend::getBlockSpectrum(const std::string& blockId) {
  return processor_.getBlockSpectrum(blockId);
}

// Presets
juce::var ProcessorBackend::getPresetList() { return processor_.getPresetList(); }
juce::var ProcessorBackend::savePreset(const juce::String& name) {
  return processor_.savePreset(name);
}
bool ProcessorBackend::loadPreset(const juce::String& presetId) {
  return processor_.loadPreset(presetId);
}
bool ProcessorBackend::renamePreset(const juce::String& presetId, const juce::String& newName) {
  return processor_.renamePreset(presetId, newName);
}
bool ProcessorBackend::deletePreset(const juce::String& presetId) {
  return processor_.deletePreset(presetId);
}
bool ProcessorBackend::movePreset(const juce::String& presetId, int delta) {
  return processor_.movePreset(presetId, delta);
}
bool ProcessorBackend::stepPreset(int delta) { return processor_.stepPreset(delta); }
juce::var ProcessorBackend::savePresetToFolder(const juce::File& folder, const juce::String& name) {
  return processor_.savePresetToFolder(folder, name);
}

// Library
void ProcessorBackend::setLibraryLocation(const juce::File& root, const juce::String& owner,
                                          const juce::Array<juce::File>& linkedDirs) {
  processor_.setLibraryLocation(root, owner, linkedDirs);
}
juce::String ProcessorBackend::libraryLinkProblem(const juce::File& dir) {
  return processor_.libraryLinkProblem(dir);
}
juce::var ProcessorBackend::getLibrary(bool fresh, const std::atomic<bool>* stop) {
  return processor_.getLibrary(fresh, stop);
}
juce::var ProcessorBackend::getSavedLibrary() { return processor_.getSavedLibrary(); }
juce::String ProcessorBackend::libraryFolderNameProblem(const juce::String& name) {
  return nam_arch::namedNotA2(name.trim())
             ? juce::String("The Library hides folders named A1, REVyHI or xSTD (A1 captures): pick another name")
             : juce::String();
}
juce::File ProcessorBackend::libraryCreateFolder(const juce::File& parent, const juce::String& name) {
  return processor_.libraryCreateFolder(parent, name);
}
juce::File ProcessorBackend::libraryRename(const juce::File& item, const juce::String& name) {
  return processor_.libraryRename(item, name);
}
bool ProcessorBackend::libraryRemove(const juce::File& item) { return processor_.libraryRemove(item); }
juce::File ProcessorBackend::libraryMove(const juce::File& item, const juce::File& folder) {
  return processor_.libraryMove(item, folder);
}
juce::File ProcessorBackend::libraryCopy(const juce::File& item, const juce::File& folder) {
  return processor_.libraryCopy(item, folder);
}
juce::File ProcessorBackend::libraryAddTone(const juce::File& folder, const juce::var& ref) {
  return processor_.libraryAddTone(folder, ref);
}
juce::File ProcessorBackend::libraryAddCapture(const juce::File& folder, const juce::File& source,
                                               const juce::String& name) {
  return processor_.libraryAddCapture(folder, source, name);
}
// The jobs' results come back through the processor (alive or not), never
// this backend, which goes with the editor.
void ProcessorBackend::loadLocalToneInFolderAsync(const juce::File& file, const std::string& targetInsertId,
                                                  std::function<void(juce::var)> done) {
  auto& processor = processor_;
  processor.runLibraryJob([file](const LocalLibrary&) { return TONE3000Processor::prepareLocalToneInFolder(file); },
                          [&processor, targetInsertId, done](juce::var prepared) {
                            done(processor.finishLocalToneInFolder(prepared, targetInsertId));
                          });
}

void ProcessorBackend::libraryImportFolderAsync(const juce::File& source, const juce::File& into,
                                                std::function<void(juce::File)> done) {
  processor_.runLibraryJob(
      [source, into](const LocalLibrary& library) { return juce::var(library.importFolder(source, into).getFullPathName()); },
      [done](juce::var landed) { done(landed.toString().isEmpty() ? juce::File() : juce::File(landed.toString())); });
}

void ProcessorBackend::libraryExportAsync(const juce::File& item, const juce::File& archive,
                                          std::function<void(bool)> done) {
  processor_.runLibraryJob([item, archive](const LocalLibrary& library) { return juce::var(library.exportArchive(item, archive)); },
                           [done](juce::var ok) { done(static_cast<bool>(ok)); });
}

void ProcessorBackend::libraryShareAsync(const juce::File& item, const juce::File& archive, const juce::var& siteRefs,
                                         std::function<void(juce::var)> done) {
  processor_.runLibraryJob(
      [item, archive, siteRefs](const LocalLibrary& library) { return library.shareArchive(item, archive, siteRefs); },
      [done](juce::var summary) { done(summary); });
}

void ProcessorBackend::libraryMoveAsync(const juce::File& item, const juce::File& folder,
                                        std::function<void(juce::File)> done) {
  processor_.libraryMoveAsync(item, folder, std::move(done));
}

void ProcessorBackend::libraryCopyAsync(const juce::File& item, const juce::File& folder,
                                        std::function<void(juce::File)> done) {
  processor_.libraryCopyAsync(item, folder, std::move(done));
}

void ProcessorBackend::libraryRemoveAsync(const juce::File& item, std::function<void(bool)> done) {
  processor_.libraryRemoveAsync(item, std::move(done));
}

void ProcessorBackend::libraryCopyFilesAsync(const juce::Array<juce::File>& files, const juce::File& folder,
                                             std::function<void(juce::var)> done) {
  processor_.libraryCopyFilesAsync(files, folder, std::move(done));
}

void ProcessorBackend::libraryImportAsync(const juce::File& archive, std::function<void(juce::File)> done) {
  auto& processor = processor_;
  processor.runLibraryJob(
      [archive](const LocalLibrary& library) { return juce::var(library.importArchive(archive).getFullPathName()); },
      [&processor, done](juce::var landed) {
        const auto imported = landed.toString().isEmpty() ? juce::File() : juce::File(landed.toString());
        processor.libraryImported(imported);
        done(imported);
      });
}

bool ProcessorBackend::libraryExport(const juce::File& item, const juce::File& archive) {
  return processor_.libraryExport(item, archive);
}
juce::File ProcessorBackend::libraryImport(const juce::File& archive) {
  return processor_.libraryImport(archive);
}

// Audio device settings (standalone only)
juce::var ProcessorBackend::getAudioDeviceState() {
  return withAudioSettings([](auto& s) { return s.getState(); });
}
juce::var ProcessorBackend::setAudioDeviceType(const juce::String& typeName) {
  return withAudioSettings([&](auto& s) { return s.setDeviceType(typeName); });
}
juce::var ProcessorBackend::setAudioDevice(const juce::String& kind, const juce::String& name) {
  return withAudioSettings([&](auto& s) { return s.setDevice(kind, name); });
}
juce::var ProcessorBackend::setAudioInputChannels(const juce::Array<juce::var>& indices) {
  return withAudioSettings([&](auto& s) { return s.setInputChannels(indices); });
}
juce::var ProcessorBackend::setAudioOutputPair(int pairIndex) {
  return withAudioSettings([&](auto& s) { return s.setOutputPair(pairIndex); });
}
juce::var ProcessorBackend::setAudioSampleRate(double rate) {
  return withAudioSettings([&](auto& s) { return s.setSampleRate(rate); });
}
juce::var ProcessorBackend::setAudioBufferSize(int samples) {
  return withAudioSettings([&](auto& s) { return s.setBufferSize(samples); });
}
juce::var ProcessorBackend::setHearYourself(bool hear) {
  return withAudioSettings([&](auto& s) { return s.setHearYourself(hear); });
}
juce::var ProcessorBackend::playTestTone() {
  return withAudioSettings([](auto& s) { return s.playTestTone(); });
}
juce::var ProcessorBackend::openAudioControlPanel() {
  return withAudioSettings([](auto& s) { return s.openControlPanel(); });
}
juce::var ProcessorBackend::restartAudioDevice() {
  return withAudioSettings([](auto& s) { return s.restartDevice(); });
}
juce::var ProcessorBackend::openMicSettings() {
  return withAudioSettings([](auto& s) { return s.openMicSettings(); });
}
juce::var ProcessorBackend::setMidiInputEnabled(const juce::String& id, bool enabled) {
  return withAudioSettings([&](auto& s) { return s.setMidiInputEnabled(id, enabled); });
}
juce::var ProcessorBackend::openBluetoothMidiPairing() {
  return withAudioSettings([](auto& s) { return s.openBluetoothMidiPairing(); });
}
void ProcessorBackend::setAudioInputMetering(bool enabled) {
  if (audioSettings_ != nullptr) audioSettings_->setInputMetering(enabled);
}
juce::var ProcessorBackend::getAudioInputLevels() {
  return withAudioSettings([](auto& s) { return s.getInputLevels(); });
}

// MIDI mapping
juce::var ProcessorBackend::getMidiMapState() { return processor_.midiMapper.getState(); }
void ProcessorBackend::setMidiChannelFilter(int channel) {
  processor_.midiMapper.setChannelFilter(channel);
}
void ProcessorBackend::startMidiLearn(const juce::String& targetId) {
  processor_.midiMapper.startLearn(targetId);
}
void ProcessorBackend::cancelMidiLearn() { processor_.midiMapper.cancelLearn(); }
bool ProcessorBackend::removeMidiMapping(const juce::String& targetId) {
  return processor_.midiMapper.removeMapping(targetId);
}
bool ProcessorBackend::setMidiCcMapping(const juce::String& targetId, int cc) {
  return processor_.midiMapper.setCcMapping(targetId, cc);
}

// Meters / tuner / auto-measure
juce::var ProcessorBackend::getMeterLevels() { return processor_.getMeterLevels(); }
void ProcessorBackend::setTunerEnabled(bool enabled) { processor_.setTunerEnabled(enabled); }
juce::var ProcessorBackend::getTunerReading() { return processor_.getTunerReading(); }
void ProcessorBackend::startAutoBalance() { processor_.startAutoBalance(); }
void ProcessorBackend::cancelAutoBalance() { processor_.cancelAutoBalance(); }
juce::var ProcessorBackend::pollAutoBalance() { return processor_.pollAutoBalance(); }
void ProcessorBackend::startAutoOffset() { processor_.startAutoOffset(); }
void ProcessorBackend::cancelAutoOffset() { processor_.cancelAutoOffset(); }
juce::var ProcessorBackend::pollAutoOffset() { return processor_.pollAutoOffset(); }

// Misc
juce::String ProcessorBackend::pluginVersion() { return JucePlugin_VersionString; }
juce::String ProcessorBackend::uniqueDeviceId() { return juce::SystemStats::getUniqueDeviceID(); }
void ProcessorBackend::setAccessToken(const juce::String& token) {
  processor_.setAccessToken(token);
}
void ProcessorBackend::copyToClipboard(const juce::String& text) {
  juce::SystemClipboard::copyTextToClipboard(text);
}

bool ProcessorBackend::copyLogs() {
  const juce::File logFile = TONE3000Processor::getLogFile();
  if (!logFile.existsAsFile()) return false;
  // Ship only the tail so we never dump a multi-MB file onto the clipboard.
  juce::String text = logFile.loadFileAsString();
  constexpr int maxChars = 200000;
  if (text.length() > maxChars) text = text.getLastCharacters(maxChars);
  juce::SystemClipboard::copyTextToClipboard(text);
  return true;
}

juce::String ProcessorBackend::revealLogs() {
  const juce::File logFile = TONE3000Processor::getLogFile();
  if (!logFile.existsAsFile()) return {};
  logFile.revealToUser();
  return logFile.getFullPathName();
}

bool ProcessorBackend::canOpenPresetsFolder() {
#if JUCE_IOS
  return false;
#else
  return true;
#endif
}

bool ProcessorBackend::openPresetsFolder() {
  const juce::File dir = processor_.getUserPresetsDir();
  // A fresh install has no folder until the first save; make it so the
  // user lands somewhere they can drop files into.
  if (!dir.isDirectory() && !dir.createDirectory()) return false;
  // startAsProcess opens the folder itself (Finder / Explorer / xdg-open);
  // revealToUser would select it inside its parent instead.
  return dir.startAsProcess();
}

bool ProcessorBackend::canOpenDateTimeSettings() {
#if JUCE_MAC || JUCE_WINDOWS
  return true;
#else
  // No reliable settings URI across Linux desktops; the UI hides the button.
  return false;
#endif
}

bool ProcessorBackend::openDateTimeSettings() {
#if JUCE_MAC
  // Ventura+ pane id; older systems fall back to the settings root.
  return juce::Process::openDocument(
      "x-apple.systempreferences:com.apple.Date-Time-Settings.extension", {});
#elif JUCE_WINDOWS
  return juce::Process::openDocument("ms-settings:dateandtime", {});
#else
  return false;
#endif
}

bool ProcessorBackend::forwardKeyToHost(HostKey key) {
  if (juce::JUCEApplicationBase::isStandaloneApp()) return false;
  if (auto* peer = peerHost_.getPeer())
    HostKeys::forwardKeyToHost(peer->getNativeHandle(),
                               key == HostKey::enter ? HostKeys::HostKey::enter : HostKeys::HostKey::space);
  return true;
}

}  // namespace t3k::ui
