#include "UacControl.h"

namespace t3k::usb {
namespace uac {
namespace {

void putLe16(CtrlRequest& r, uint16_t v) {
  r.data[0] = static_cast<uint8_t>(v & 0xff);
  r.data[1] = static_cast<uint8_t>(v >> 8);
}

void putLe24(CtrlRequest& r, uint32_t v) {
  r.data[0] = static_cast<uint8_t>(v & 0xff);
  r.data[1] = static_cast<uint8_t>((v >> 8) & 0xff);
  r.data[2] = static_cast<uint8_t>((v >> 16) & 0xff);
}

void putLe32(CtrlRequest& r, uint32_t v) {
  r.data[0] = static_cast<uint8_t>(v & 0xff);
  r.data[1] = static_cast<uint8_t>((v >> 8) & 0xff);
  r.data[2] = static_cast<uint8_t>((v >> 16) & 0xff);
  r.data[3] = static_cast<uint8_t>((v >> 24) & 0xff);
}

// Class request to an interface: host-to-device | class | interface.
constexpr uint8_t kTypeOutInterface = 0x21;
constexpr uint8_t kTypeInInterface = 0xA1;
// Class request to an endpoint (UAC1): host-to-device | class | endpoint.
constexpr uint8_t kTypeOutEndpoint = 0x22;
constexpr uint8_t kTypeInEndpoint = 0xA2;

}  // namespace

CtrlRequest uac2SetSampleRate(uint8_t acInterface, uint8_t clockSourceId, uint32_t rate) {
  CtrlRequest r;
  r.bmRequestType = kTypeOutInterface;
  r.bRequest = kReqSetCur;
  r.wValue = static_cast<uint16_t>(kCsSamFreq) << 8;
  r.wIndex = static_cast<uint16_t>(clockSourceId) << 8 | acInterface;
  r.wLength = 4;
  putLe32(r, rate);
  return r;
}

CtrlRequest uac2GetSampleRate(uint8_t acInterface, uint8_t clockSourceId) {
  CtrlRequest r;
  r.bmRequestType = kTypeInInterface;
  r.bRequest = kReqGetCur;
  r.wValue = static_cast<uint16_t>(kCsSamFreq) << 8;
  r.wIndex = static_cast<uint16_t>(clockSourceId) << 8 | acInterface;
  r.wLength = 4;
  return r;
}

CtrlRequest uac2GetSampleRateRange(uint8_t acInterface, uint8_t clockSourceId) {
  CtrlRequest r;
  r.bmRequestType = kTypeInInterface;
  r.bRequest = kReqGetRange;
  r.wValue = static_cast<uint16_t>(kCsSamFreq) << 8;
  r.wIndex = static_cast<uint16_t>(clockSourceId) << 8 | acInterface;
  r.wLength = 2 + 12 * 8;  // wNumSubRanges + up to 8 min/max/res triplets
  return r;
}

CtrlRequest uac1SetSampleRate(uint8_t endpointAddress, uint32_t rate) {
  CtrlRequest r;
  r.bmRequestType = kTypeOutEndpoint;
  r.bRequest = kReqSetCur;
  r.wValue = static_cast<uint16_t>(kCsSamplingFreq) << 8;
  r.wIndex = endpointAddress;
  r.wLength = 3;
  putLe24(r, rate);
  return r;
}

CtrlRequest uac1GetSampleRate(uint8_t endpointAddress) {
  CtrlRequest r;
  r.bmRequestType = kTypeInEndpoint;
  r.bRequest = kReqGetCur;
  r.wValue = static_cast<uint16_t>(kCsSamplingFreq) << 8;
  r.wIndex = endpointAddress;
  r.wLength = 3;
  return r;
}

CtrlRequest uac2SetMute(uint8_t entityId, uint8_t channel, bool mute) {
  CtrlRequest r;
  r.bmRequestType = kTypeOutInterface;
  r.bRequest = kReqSetCur;
  r.wValue = static_cast<uint16_t>(kCsMute) << 8;
  r.wIndex = static_cast<uint16_t>(entityId) << 8 | channel;
  r.wLength = 1;
  r.data[0] = mute ? 1 : 0;
  return r;
}

CtrlRequest uac2GetMute(uint8_t entityId, uint8_t channel) {
  CtrlRequest r;
  r.bmRequestType = kTypeInInterface;
  r.bRequest = kReqGetCur;
  r.wValue = static_cast<uint16_t>(kCsMute) << 8;
  r.wIndex = static_cast<uint16_t>(entityId) << 8 | channel;
  r.wLength = 1;
  return r;
}

CtrlRequest uac2SetVolume(uint8_t entityId, uint8_t channel, int16_t gainQ8) {
  CtrlRequest r;
  r.bmRequestType = kTypeOutInterface;
  r.bRequest = kReqSetCur;
  r.wValue = static_cast<uint16_t>(kCsVolume) << 8;
  r.wIndex = static_cast<uint16_t>(entityId) << 8 | channel;
  r.wLength = 2;
  putLe16(r, static_cast<uint16_t>(gainQ8));
  return r;
}

CtrlRequest uac2GetVolume(uint8_t entityId, uint8_t channel) {
  CtrlRequest r;
  r.bmRequestType = kTypeInInterface;
  r.bRequest = kReqGetCur;
  r.wValue = static_cast<uint16_t>(kCsVolume) << 8;
  r.wIndex = static_cast<uint16_t>(entityId) << 8 | channel;
  r.wLength = 2;
  return r;
}

CtrlRequest setInterface(uint8_t interfaceNumber, uint8_t altSetting) {
  CtrlRequest r;
  r.bmRequestType = 0x01;  // host-to-device | standard | interface
  r.bRequest = kReqSetInterface;
  r.wValue = altSetting;
  r.wIndex = interfaceNumber;
  r.wLength = 0;
  return r;
}

}  // namespace uac
}  // namespace t3k::usb
