// The UI's only door to the audio engine. State blobs stay `juce::var`,
// exactly as the processor ships them (and as the testbed's JSON fixtures
// hold them); the model/ layer parses them into structs. Two
// implementations: ProcessorBackend (the plugin) and the testbed's
// MockBackend (fixture driven), so every view can render without audio.
//
// Every method is called on the message thread and completes inline. Async
// work (file choosers, HTTP) lives in services/.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <string>
#include <vector>

namespace t3k::ui {

class Backend {
public:
  virtual ~Backend() = default;

  // Push notifications from the processor. Chain changes are polled by
  // revision instead (see chainRevision), which is cheaper than an event per
  // edit.
  struct Listener {
    virtual ~Listener() = default;
    virtual void audioDeviceChanged() {}
    virtual void midiMapChanged() {}
  };
  void addListener(Listener* l) { listeners.add(l); }
  void removeListener(Listener* l) { listeners.remove(l); }

  // Parameters (APVTS ids; nullptr for an unknown id)
  virtual juce::RangedAudioParameter* parameter(const juce::String& id) = 0;

  // Chain state / history
  virtual juce::var getChainState(int knownRevision) = 0;
  virtual juce::uint32 chainRevision() = 0;
  virtual bool undoChain() = 0;
  virtual bool redoChain() = 0;
  virtual bool resetToDefault() = 0;

  // Chain mutations
  // Returns the new block id, "" on failure.
  virtual std::string loadTone(const juce::String& toneJson, const std::string& targetInsertId) = 0;
  // { blockId } or a user-facing { error }.
  virtual juce::var loadLocalTonePath(const juce::File& source, const std::string& targetInsertId) = 0;
  virtual juce::var loadLocalToneUrls(const juce::Array<juce::URL>& sources,
                                      const std::string& targetInsertId) = 0;
  virtual bool swapTone(const std::string& blockId, const juce::String& toneJson) = 0;
  virtual bool refreshToneMetadata(const juce::String& toneJson) = 0;
  virtual bool switchModel(const std::string& blockId, int modelId, const juce::var& model) = 0;
  virtual bool retryModelLoad(const std::string& blockId) = 0;
  virtual bool removeChainBlock(const std::string& blockId) = 0;
  virtual bool reorderChainBlocks(const std::vector<std::string>& newOrder) = 0;
  virtual bool moveBlockToChain(const std::string& blockId, const juce::String& side, int index) = 0;
  virtual std::string duplicateChainBlock(const std::string& blockId, const juce::String& side,
                                          int index) = 0;
  virtual bool copyChainBlock(const std::string& blockId) = 0;
  virtual std::string pasteChainBlock(const juce::String& side, int index) = 0;
  virtual bool swapChains() = 0;
  virtual bool setChainBranch(const juce::String& side, const std::string& afterBlockId) = 0;
  virtual bool clearChainBranch() = 0;
  virtual void setStereoMode(bool enabled) = 0;
  virtual void setInputMode(const juce::String& mode) = 0;
  virtual void setActiveEditChain(const juce::String& side) = 0;
  virtual void setNamSlimSizeDefault(double slimSize) = 0;
  virtual void setMultiCore(bool enabled) = 0;
  // Store a Settings-page parameter's current value (calibration,
  // oversampling) as the machine-wide default new instances start from. The
  // parameter itself is set through ParamBinding as usual; this is the
  // extra step a user edit takes that a host restore never does.
  virtual void persistParamAsMachineDefault(const juce::String& id) = 0;

  // Per-block params / EQ / spectrum
  virtual bool setBlockParam(const std::string& blockId, const juce::String& param, double value) = 0;
  virtual bool setBlockSlimSize(const std::string& blockId, double slimSize) = 0;
  virtual bool setBlockEqBand(const std::string& blockId, int bandIndex, const juce::var& band) = 0;
  virtual bool setBlockEqEnabled(const std::string& blockId, bool enabled) = 0;
  virtual bool setBlockEqPre(const std::string& blockId, bool pre) = 0;
  virtual bool resetBlockEq(const std::string& blockId) = 0;
  virtual bool setBlockSpectrumEnabled(const std::string& blockId, bool enabled) = 0;
  virtual juce::var getBlockSpectrum(const std::string& blockId) = 0;

  // Presets
  virtual juce::var getPresetList() = 0;
  virtual juce::var savePreset(const juce::String& name) = 0;
  virtual bool loadPreset(const juce::String& presetId) = 0;
  virtual bool renamePreset(const juce::String& presetId, const juce::String& newName) = 0;
  virtual bool deletePreset(const juce::String& presetId) = 0;
  virtual bool movePreset(const juce::String& presetId, int delta) = 0;

  // Audio device settings (standalone only; void var elsewhere)
  virtual juce::var getAudioDeviceState() = 0;
  virtual juce::var setAudioDeviceType(const juce::String& typeName) = 0;
  virtual juce::var setAudioDevice(const juce::String& kind, const juce::String& name) = 0;
  virtual juce::var setAudioInputChannels(const juce::Array<juce::var>& indices) = 0;
  virtual juce::var setAudioOutputPair(int pairIndex) = 0;
  virtual juce::var setAudioSampleRate(double rate) = 0;
  virtual juce::var setAudioBufferSize(int samples) = 0;
  virtual juce::var setHearYourself(bool hear) = 0;
  virtual juce::var playTestTone() = 0;
  virtual juce::var openAudioControlPanel() = 0;
  virtual juce::var restartAudioDevice() = 0;
  virtual juce::var openMicSettings() = 0;
  virtual juce::var setMidiInputEnabled(const juce::String& id, bool enabled) = 0;
  virtual juce::var openBluetoothMidiPairing() = 0;
  virtual void setAudioInputMetering(bool enabled) = 0;
  virtual juce::var getAudioInputLevels() = 0;

  // MIDI mapping (every build)
  virtual juce::var getMidiMapState() = 0;
  virtual void setMidiChannelFilter(int channel) = 0;
  virtual void startMidiLearn(const juce::String& targetId) = 0;
  virtual void cancelMidiLearn() = 0;
  virtual bool removeMidiMapping(const juce::String& targetId) = 0;
  virtual bool setMidiCcMapping(const juce::String& targetId, int cc) = 0;

  // Meters / tuner / auto-measure
  virtual juce::var getMeterLevels() = 0;
  virtual void setTunerEnabled(bool enabled) = 0;
  virtual void setTunerMuted(bool muted) = 0;
  virtual juce::var getTunerReading() = 0;
  virtual void startAutoBalance() = 0;
  virtual void cancelAutoBalance() = 0;
  virtual juce::var pollAutoBalance() = 0;
  virtual void startAutoOffset() = 0;
  virtual void cancelAutoOffset() = 0;
  virtual juce::var pollAutoOffset() = 0;

  // Misc
  virtual juce::String pluginVersion() = 0;
  virtual juce::String uniqueDeviceId() = 0;
  // Bearer token for native model downloads (kept in sync by ToneSession).
  virtual void setAccessToken(const juce::String& token) = 0;
  virtual void copyToClipboard(const juce::String& text) = 0;
  virtual bool copyLogs() = 0;
  virtual juce::String revealLogs() = 0;
  // Open the user presets folder in the OS file browser (creating it if no
  // preset has been saved yet). False on platforms with no file browser to
  // open (iOS); the UI hides the section.
  virtual bool canOpenPresetsFolder() = 0;
  virtual bool openPresetsFolder() = 0;
  // False when the platform has no settings URI (the UI hides the button).
  virtual bool canOpenDateTimeSettings() = 0;
  virtual bool openDateTimeSettings() = 0;
  // Hand a transport key no control consumed to the host DAW (its play/stop
  // or return-to-start shortcut); false in standalone, where there is none.
  enum class HostKey { space, enter };
  virtual bool forwardKeyToHost(HostKey key) = 0;

protected:
  juce::ListenerList<Listener> listeners;
};

}  // namespace t3k::ui
