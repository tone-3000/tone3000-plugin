// Standalone audio device snapshot (port of audioDevice.ts).
#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace t3k::ui {

struct MidiInputDevice {
  juce::String id;
  juce::String name;
  bool enabled = false;
};

struct AudioInputChannel {
  int index = 0;
  juce::String name;
  bool active = false;
};

enum class MicPermission { granted, denied, unknown };

struct AudioDeviceState {
  std::vector<juce::String> deviceTypes;
  juce::String currentType;
  bool separateIO = true;
  std::vector<juce::String> inputDevices;
  std::vector<juce::String> outputDevices;
  juce::String inputDevice;
  juce::String outputDevice;
  bool deviceOpen = false;
  std::vector<AudioInputChannel> inputChannels;
  std::vector<juce::String> outputPairs;
  int activeOutputPair = -1;
  std::vector<double> sampleRates;
  double sampleRate = 0;
  std::vector<int> bufferSizes;
  int bufferSize = 0;
  bool hasControlPanel = false;
  bool hearYourself = true;
  bool feedbackRisk = false;
  bool asioAvailable = false;
  MicPermission micPermission = MicPermission::granted;
  std::vector<MidiInputDevice> midiInputs;
  bool btMidiAvailable = false;
  bool bluetoothRoute = false;

  static AudioDeviceState parse(const juce::var& v);

  // Bluetooth tip (iOS): route is Bluetooth, or the session came up below 44.1 kHz.
  bool shouldShowBluetoothTip() const;
  juce::String bluetoothTipHeadline() const;
};

// Result of every audio settings mutation.
struct AudioDeviceResult {
  bool ok = false;
  juce::String error;
  static AudioDeviceResult parse(const juce::var& v);
};

}  // namespace t3k::ui
