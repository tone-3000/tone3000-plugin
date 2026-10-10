#include "AudioDeviceState.h"

#include "VarReader.h"

namespace t3k::ui {

using namespace var_reader;

AudioDeviceState AudioDeviceState::parse(const juce::var& v) {
  AudioDeviceState s;
  s.deviceTypes = strings(v, "deviceTypes");
  s.currentType = str(v, "currentType");
  s.separateIO = boolean(v, "separateIO", true);
  s.inputDevices = strings(v, "inputDevices");
  s.outputDevices = strings(v, "outputDevices");
  s.inputDevice = str(v, "inputDevice");
  s.outputDevice = str(v, "outputDevice");
  s.deviceOpen = boolean(v, "deviceOpen");
  s.inputChannels = list<AudioInputChannel>(v, "inputChannels", [](const juce::var& c) {
    return AudioInputChannel{integer(c, "index"), str(c, "name"), boolean(c, "active")};
  });
  s.outputPairs = strings(v, "outputPairs");
  s.activeOutputPair = integer(v, "activeOutputPair", -1);
  s.sampleRates = numbers(v, "sampleRates");
  s.sampleRate = num(v, "sampleRate");
  for (double size : numbers(v, "bufferSizes"))
    s.bufferSizes.push_back(static_cast<int>(size));
  s.bufferSize = integer(v, "bufferSize");
  s.hasControlPanel = boolean(v, "hasControlPanel");
  s.hearYourself = boolean(v, "hearYourself", true);
  s.feedbackRisk = boolean(v, "feedbackRisk");
  s.asioAvailable = boolean(v, "asioAvailable");
  const auto mic = str(v, "micPermission", "granted");
  s.micPermission = mic == "denied"    ? MicPermission::denied
                    : mic == "unknown" ? MicPermission::unknown
                                       : MicPermission::granted;
  s.midiInputs = list<MidiInputDevice>(v, "midiInputs", [](const juce::var& m) {
    return MidiInputDevice{str(m, "id"), str(m, "name"), boolean(m, "enabled")};
  });
  s.btMidiAvailable = boolean(v, "btMidiAvailable");
  s.bluetoothRoute = boolean(v, "bluetoothRoute");
  return s;
}

bool AudioDeviceState::shouldShowBluetoothTip() const {
  return deviceOpen && (bluetoothRoute || (sampleRate > 0 && sampleRate < 44100));
}

juce::String AudioDeviceState::bluetoothTipHeadline() const {
  const juce::String kHz = juce::String(juce::roundToInt(sampleRate / 1000)) + " kHz";
  const bool capped = sampleRate > 0 && sampleRate < 44100;
  if (bluetoothRoute)
    return capped ? "Bluetooth headphones are limiting audio to " + kHz + " and add latency."
                  : "Bluetooth headphones add latency.";
  return "This audio route is running at " + kHz + ", which limits fidelity and adds latency.";
}

AudioDeviceResult AudioDeviceResult::parse(const juce::var& v) {
  return {boolean(v, "ok", false), str(v, "error")};
}

}  // namespace t3k::ui
