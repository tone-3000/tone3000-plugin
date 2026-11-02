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
  juce::var loadLocalTonePath(const juce::File&, const std::string&) override;
  juce::var loadLocalToneUrls(const juce::Array<juce::URL>&, const std::string&) override;
  bool swapTone(const std::string&, const juce::String&) override { return true; }
  bool refreshToneMetadata(const juce::String&) override { return true; }
  bool switchModel(const std::string&, int, const juce::var&) override { return true; }
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
  void setTunerMuted(bool muted) override { tunerMuted_ = muted; }
  bool tunerMuted() const { return tunerMuted_; }
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
  std::vector<ChainMove> chainMoves_;
  std::vector<juce::String> machineDefaults_;
  juce::var meters_;
  juce::var tuner_;
  bool tunerMuted_ = false;
  juce::var autoMeasure_;
  juce::String version_;
  std::unique_ptr<MockSignal> signal_;
};

}  // namespace t3k::ui::testbed
