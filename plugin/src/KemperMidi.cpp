#include "KemperMidi.h"
#include "KemperRouting.h"

#include <algorithm>
#include <cmath>

namespace t3k::capture::kemper {
namespace {

bool validChannel(int channel) {
  return channel >= 1 && channel <= 16;
}

int continuousValue(float value, float minimum, float maximum) {
  if (!std::isfinite(value) || value < minimum || value > maximum)
    return -1;
  // Kemper defines continuous parameters across the full 14-bit range.
  // round(16383/2) = 8192, so a bipolar control's centre is exact.
  return static_cast<int>(
      std::lround((static_cast<double>(value) - minimum) / (maximum - minimum) * 16383.0));
}

}  // namespace

Messages encodeNrpn(int channel, int page, int index, int value14) {
  if (!validChannel(channel) || page < 0 || page > 127 || index < 0 || index > 127 || value14 < 0 ||
      value14 > 16383)
    return {};
  // Always include the full address, including on successive changes. CC38
  // commits the value; the preceding CC6 alone must not be treated as a write.
  return {juce::MidiMessage::controllerEvent(channel, 99, page),
          juce::MidiMessage::controllerEvent(channel, 98, index),
          juce::MidiMessage::controllerEvent(channel, 6, value14 >> 7),
          juce::MidiMessage::controllerEvent(channel, 38, value14 & 0x7f)};
}

Messages encodeGain(int channel, float gain) {
  return encodeNrpn(channel, 10, 4, continuousValue(gain, 0.0f, 10.0f));
}

Messages encodeEq(int channel, float bass, float middle, float treble, float presence) {
  const float values[]{bass, middle, treble, presence};
  // Validate the complete group before producing any messages. A bad fourth
  // control must not leave the first three controls changed on the hardware.
  for (const float value : values)
    if (continuousValue(value, -5.0f, 5.0f) < 0)
      return {};
  Messages messages;
  messages.reserve(16);
  for (int i = 0; i < 4; ++i) {
    const auto parameter = encodeNrpn(channel, 11, 4 + i, continuousValue(values[i], -5.0f, 5.0f));
    messages.insert(messages.end(), parameter.begin(), parameter.end());
  }
  return messages;
}

Messages encodeCabinet(int channel, bool enabled) {
  // Switches use integer 0/1 in the LSB, not full-scale continuous values.
  return encodeNrpn(channel, 12, 2, enabled ? 1 : 0);
}

Messages encodeProgramChange(int channel, int program) {
  if (!validChannel(channel) || program < 0 || program > 127)
    return {};
  return {juce::MidiMessage::programChange(channel, program)};
}

Messages encodeRequestParameter(int page, int index, int productType, int deviceId) {
  if (page < 0 || page > 127 || index < 0 || index > 127 ||
      (productType != 0 && productType != 2) || deviceId < 0 || deviceId > 127)
    return {};
  const juce::uint8 data[]{0x00, 0x20, 0x33, static_cast<juce::uint8>(productType),
                          static_cast<juce::uint8>(deviceId), 0x41, 0x00,
                          static_cast<juce::uint8>(page), static_cast<juce::uint8>(index)};
  return {juce::MidiMessage::createSysExMessage(data, static_cast<int>(sizeof(data)))};
}

std::optional<ParameterResponse> parseParameterResponse(const juce::MidiMessage& message) {
  if (!message.isSysEx())
    return {};
  if (message.getRawDataSize() < 2 || message.getRawData()[message.getRawDataSize() - 1] != 0xf7)
    return {};
  const int size = message.getSysExDataSize();
  if (size != 11 && size != 13) // optional second14-bit value is the Morph B value
    return {};
  const auto* data = message.getSysExData();
  if (data == nullptr || data[0] != 0x00 || data[1] != 0x20 || data[2] != 0x33 ||
      (data[3] != 0 && data[3] != 2) || data[5] != 0x01 || data[6] != 0)
    return {};
  for (int i = 0; i < size; ++i)
    if (data[i] > 127)
      return {};
  ParameterResponse result;
  result.productType = data[3];
  result.deviceId = data[4];
  result.page = data[7];
  result.index = data[8];
  result.value14 = (data[9] << 7) | data[10];
  if (size == 13)
    result.morphedValue14 = (data[11] << 7) | data[12];
  return result;
}

Controller::~Controller() {
  close();
}

juce::Result Controller::connect(const juce::String& identifier, int channel) {
  if (!validChannel(channel))
    return juce::Result::fail("MIDI channel must be between 1 and 16.");
  if (identifier.isEmpty())
    return juce::Result::fail("Select a Kemper MIDI output first.");
  const auto devices = juce::MidiOutput::getAvailableDevices();
  const auto found =
      std::find_if(devices.begin(), devices.end(),
                   [&identifier](const auto& device) { return device.identifier == identifier; });
  if (found == devices.end())
    return juce::Result::fail(
        "The selected MIDI output is unavailable. Refresh devices and reconnect.");
  auto candidate = juce::MidiOutput::openDevice(identifier);
  if (candidate == nullptr)
    return juce::Result::fail(
        "Could not open the MIDI output. Check its connection and other applications.");
  disableInput(); // never show a previous device's cached values for a new output
  output_ = std::move(candidate);
  channel_ = channel;
  return juce::Result::ok();
}

void Controller::close() {
  disableInput();
  output_.reset();
}

juce::Result Controller::enableInput(const juce::String& identifier) {
  if (identifier.isEmpty())
    return juce::Result::fail("Select the Kemper MIDI input used for readback.");
  if (input_ != nullptr && input_->getIdentifier() == identifier)
    return juce::Result::ok();
  const auto devices = juce::MidiInput::getAvailableDevices();
  if (std::none_of(devices.begin(), devices.end(),
                   [&identifier](const auto& device) { return device.identifier == identifier; }))
    return juce::Result::fail("The selected MIDI input is unavailable. Refresh devices.");
  auto candidate = juce::MidiInput::openDevice(identifier, this);
  if (candidate == nullptr)
    return juce::Result::fail("Could not open the MIDI input. Check other applications and its connection.");
  disableInput();
  input_ = std::move(candidate);
  input_->start();
  return juce::Result::ok();
}

void Controller::disableInput() {
  if (input_ != nullptr) {
    input_->stop();
    input_.reset();
  }
  const juce::ScopedLock lock(readbackLock_);
  readback_.clear();
}

void Controller::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) {
  const auto response = parseParameterResponse(message);
  if (!response)
    return;
  const ParameterReadback value{*response, juce::Time::getMillisecondCounterHiRes()};
  const juce::ScopedLock lock(readbackLock_);
  readback_[response->page * 128 + response->index] = value;
}

std::optional<ParameterReadback> Controller::cachedParameter(int page, int index) const {
  if (page < 0 || page > 127 || index < 0 || index > 127)
    return {};
  const juce::ScopedLock lock(readbackLock_);
  const auto found = readback_.find(page * 128 + index);
  if (found == readback_.end())
    return {};
  return found->second;
}

juce::Result Controller::requestParameter(int page, int index, int productType, int deviceId) {
  if (input_ == nullptr)
    return juce::Result::fail("Open a MIDI input for readback before requesting hardware values.");
  return send(encodeRequestParameter(page, index, productType, deviceId),
              "Invalid readback address, product type or device ID.");
}

juce::Result Controller::send(const Messages& messages, const juce::String& invalidMessage) {
  if (messages.empty())
    return juce::Result::fail(invalidMessage);
  if (output_ == nullptr)
    return juce::Result::fail("Connect a Kemper MIDI output before sending settings.");
  for (const auto& message : messages)
    output_->sendMessageNow(message);
  // The OS MIDI API reports no device acknowledgement. This means queued/sent,
  // not that the Profiler accepted a parameter or that Rig Manager refreshed.
  return juce::Result::ok();
}

juce::Result Controller::setGain(float gain) {
  return send(encodeGain(channel_, gain), "Generic Gain must be a finite value between 0 and 10.");
}

juce::Result Controller::setEq(float bass, float middle, float treble, float presence) {
  return send(encodeEq(channel_, bass, middle, treble, presence),
              "Each amplifier EQ control must be a finite value between -5 and +5.");
}

juce::Result Controller::setCabinet(bool enabled) {
  return send(encodeCabinet(channel_, enabled), "Invalid MIDI channel.");
}

juce::Result Controller::selectProgram(int program) {
  return send(encodeProgramChange(channel_, program), "MIDI program must be between 0 and 127.");
}

juce::Result Controller::setIoParameter(IoParameter parameter, int rawValue14) {
  return send(encodeIoParameter(channel_, parameter, rawValue14),
              "Invalid hardware parameter/value. Continuous positions use0..16383; switches use0/1 and choices use their literal index.");
}

}  // namespace t3k::capture::kemper
