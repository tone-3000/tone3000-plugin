#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>
#include <vector>

namespace t3k::capture::kemper {

using Messages = std::vector<juce::MidiMessage>;

/** Kemper MIDI Parameter Documentation 13 (June 2025), pp. 12-19.
    Channel is 1-16. Invalid inputs return no messages; they are never clamped
    to another parameter/channel. These functions perform no hardware I/O. */
Messages encodeNrpn(int channel, int page, int index, int value14);
Messages encodeGain(int channel, float gain);  // Generic Gain, 0..10 (not Liquid Amp Model Gain).
Messages encodeEq(int channel, float bass, float middle, float treble, float presence);  // -5..5.
Messages encodeCabinet(int channel, bool enabled);
Messages encodeProgramChange(int channel, int program);  // Raw MIDI program 0..127.

/** Direct MIDI output only; Rig Manager is not required. Call on the message
    thread. Opening a port/sending bytes does not acknowledge hardware state.
    Nothing is transmitted on connect, and no Store command is ever sent. */
class Controller {
public:
  Controller() = default;
  juce::Result connect(const juce::String& identifier, int channel);
  void close();
  bool isOpen() const { return output_ != nullptr; }
  juce::Result setGain(float gain);
  juce::Result setEq(float bass, float middle, float treble, float presence);
  juce::Result setCabinet(bool enabled);
  juce::Result selectProgram(int program);

private:
  juce::Result send(const Messages&, const juce::String& invalidMessage);
  std::unique_ptr<juce::MidiOutput> output_;
  int channel_ = 1;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Controller)
};

}  // namespace t3k::capture::kemper
