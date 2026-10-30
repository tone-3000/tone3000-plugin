#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>
#include <map>
#include <optional>
#include <vector>

namespace t3k::capture::kemper {

enum class IoParameter;

using Messages = std::vector<juce::MidiMessage>;

/** Kemper MIDI Parameter Documentation 13 (June 2025), pp. 12-19.
    Channel is 1-16. Invalid inputs return no messages; they are never clamped
    to another parameter/channel. These functions perform no hardware I/O. */
Messages encodeNrpn(int channel, int page, int index, int value14);
Messages encodeGain(int channel, float gain);  // Generic Gain, 0..10 (not Liquid Amp Model Gain).
Messages encodeEq(int channel, float bass, float middle, float treble, float presence);  // -5..5.
Messages encodeCabinet(int channel, bool enabled);
Messages encodeProgramChange(int channel, int program);  // Raw MIDI program 0..127.

/** Read-only SysEx request. MIDI13's header table uses product0; its worked
    examples use product2. Both are selectable; default0 follows the table.
    Invalid fields produce no messages. No requests are sent automatically. */
Messages encodeRequestParameter(int page, int index, int productType = 0, int deviceId = 0x7f);
struct ParameterResponse {
  int page = 0, index = 0, value14 = 0, productType = 0, deviceId = 0;
  std::optional<int> morphedValue14;
};
std::optional<ParameterResponse> parseParameterResponse(const juce::MidiMessage&);
struct ParameterReadback {
  ParameterResponse value;
  double receivedAtMilliseconds = 0.0;
};

/** Direct MIDI I/O; Rig Manager is not required. Control methods run on the
    message thread. Opening a port/sending bytes does not acknowledge hardware
    state. Explicit read requests can populate a timestamped cache from actual
    replies. Nothing is transmitted on connect; no Store command is ever sent. */
class Controller : private juce::MidiInputCallback {
public:
  Controller() = default;
  ~Controller() override;
  juce::Result connect(const juce::String& identifier, int channel);
  void close();
  bool isOpen() const { return output_ != nullptr; }
  juce::Result setGain(float gain);
  juce::Result setEq(float bass, float middle, float treble, float presence);
  juce::Result setCabinet(bool enabled);
  juce::Result selectProgram(int program);
  juce::Result setIoParameter(IoParameter, int rawValue14);
  juce::Result enableInput(const juce::String& identifier);
  void disableInput();
  bool hasInput() const { return input_ != nullptr; }
  juce::Result requestParameter(int page, int index, int productType = 0, int deviceId = 0x7f);
  std::optional<ParameterReadback> cachedParameter(int page, int index) const;

private:
  juce::Result send(const Messages&, const juce::String& invalidMessage);
  void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;
  std::unique_ptr<juce::MidiOutput> output_;
  std::unique_ptr<juce::MidiInput> input_;
  mutable juce::CriticalSection readbackLock_;
  std::map<int, ParameterReadback> readback_;
  int channel_ = 1;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Controller)
};

}  // namespace t3k::capture::kemper
