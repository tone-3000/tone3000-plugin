// Standalone audio device state (port of useAudioDevice.ts). Native pushes
// `audioDeviceChanged` on every device-manager change (our own setters,
// hot-plugs, vendor control panel edits) and we re-pull; mutations re-pull
// immediately so the settings panel reflects readback without waiting.
//
// Every action returns an error string ("" on success): the human-readable
// text JUCE's setAudioDeviceSetup reports, surfaced inline by the caller.
#pragma once

#include <juce_events/juce_events.h>

#include <optional>

#include "UiClock.h"
#include "backend/Backend.h"
#include "model/AudioDeviceState.h"

namespace t3k::ui {

class AudioDeviceStore : private Backend::Listener {
public:
  struct Listener {
    virtual ~Listener() = default;
    virtual void audioDeviceChanged() = 0;
  };

  explicit AudioDeviceStore(Backend& backend);
  ~AudioDeviceStore() override;

  // nullopt in hosted builds and while loading.
  const std::optional<AudioDeviceState>& state() const { return state_; }
  void refresh();

  void addListener(Listener* l) { listeners.add(l); }
  void removeListener(Listener* l) { listeners.remove(l); }

  juce::String setDeviceType(const juce::String& typeName);
  juce::String setInputDevice(const juce::String& name);
  juce::String setOutputDevice(const juce::String& name);
  juce::String setLinkedDevice(const juce::String& name);
  juce::String setInputChannels(const std::vector<int>& indices);
  juce::String setOutputPair(int pairIndex);
  juce::String setSampleRate(double rate);
  juce::String setBufferSize(int samples);
  juce::String setHearYourself(bool hear);
  juce::String playTestTone();
  juce::String openControlPanel();
  juce::String restartDevice();
  juce::String openMicSettings();
  juce::String setMidiInputEnabled(const juce::String& id, bool enabled);
  juce::String openBluetoothMidiPairing();

private:
  void audioDeviceChanged() override { refresh(); }
  template <typename Fn>
  juce::String run(Fn&& fn);

  Backend& backend_;
  std::optional<AudioDeviceState> state_;
  juce::ListenerList<Listener> listeners;
};

// Per-channel input peak levels for the channel picker's meters (port of
// useAudioInputLevels). Enables the native raw-device tap while alive, polls
// on the UiClock tick, and applies a simple falloff so short peaks stay
// readable.
class AudioInputLevels : private UiClock::Listener {
public:
  static constexpr float kFloorDb = -120;

  AudioInputLevels(Backend& backend, UiClock& clock, std::function<void()> onChange);
  ~AudioInputLevels() override;

  const std::vector<float>& levels() const { return displayed_; }

private:
  void tick() override;

  Backend& backend_;
  UiClock& clock_;
  std::function<void()> onChange_;
  std::vector<float> displayed_;
};

}  // namespace t3k::ui
