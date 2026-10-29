#pragma once

// Builders for the class-specific USB control requests that drive an audio
// device: sample-rate selection on the UAC2 clock source (or the UAC1
// endpoint), feature-unit mute/volume, and the standard SET_INTERFACE that
// starts and stops a stream. Every builder returns the exact byte fields of
// the setup packet plus the payload; the unit tests pin them against
// hand-computed hex (plan section 6.2).
//
// The shell issues these with USBDEVFS_CONTROL on the raw fd (native) or
// UsbDeviceConnection.controlTransfer (Java); the plan prefers native once
// the fd is in native code.

#include <cstdint>

namespace t3k::usb {

struct CtrlRequest {
  uint8_t bmRequestType = 0;
  uint8_t bRequest = 0;
  uint16_t wValue = 0;
  uint16_t wIndex = 0;
  uint16_t wLength = 0;
  uint8_t data[8] = {};
};

namespace uac {

// Standard request codes (USB 2.0 chapter 9).
constexpr uint8_t kReqSetInterface = 0x0B;
// Class request codes (UAC1/UAC2 chapter 5).
constexpr uint8_t kReqSetCur = 0x01;
constexpr uint8_t kReqGetCur = 0x81;
constexpr uint8_t kReqGetRange = 0x82;
// Control selectors.
constexpr uint8_t kCsSamFreq = 0x01;       // UAC2 clock source
constexpr uint8_t kCsSamplingFreq = 0x01;  // UAC1 endpoint
constexpr uint8_t kCsMute = 0x01;
constexpr uint8_t kCsVolume = 0x02;

/** UAC2 SET_CUR on the clock source's SAM_FREQ_CONTROL: rate as uint32 LE. */
CtrlRequest uac2SetSampleRate(uint8_t acInterface, uint8_t clockSourceId, uint32_t rate);
/** UAC2 GET_CUR readback of the same control. */
CtrlRequest uac2GetSampleRate(uint8_t acInterface, uint8_t clockSourceId);
/** UAC2 GET_RANGE of SAM_FREQ_CONTROL (min/max/resolution triplets behind a
    wNumSubRanges header). wLength covers 8 ranges. */
CtrlRequest uac2GetSampleRateRange(uint8_t acInterface, uint8_t clockSourceId);

/** UAC1 SET_CUR on the endpoint's SAMPLING_FREQ_CONTROL: rate as 3-byte LE. */
CtrlRequest uac1SetSampleRate(uint8_t endpointAddress, uint32_t rate);
/** UAC1 GET_CUR readback of the same control. */
CtrlRequest uac1GetSampleRate(uint8_t endpointAddress);

/** UAC2 feature unit mute (channel 0 = master). */
CtrlRequest uac2SetMute(uint8_t entityId, uint8_t channel, bool mute);
CtrlRequest uac2GetMute(uint8_t entityId, uint8_t channel);
/** UAC2 feature unit volume in 1/256 dB steps (0 = 0 dB, e.g. -12 dB =
    -3072). */
CtrlRequest uac2SetVolume(uint8_t entityId, uint8_t channel, int16_t gainQ8);
CtrlRequest uac2GetVolume(uint8_t entityId, uint8_t channel);

/** Standard SET_INTERFACE: altsetting 0 stops the endpoint, the active
    altsetting starts it. Always return to altsetting 0 when stopping (the
    plan's pitfall 2; some devices stop responding otherwise). */
CtrlRequest setInterface(uint8_t interfaceNumber, uint8_t altSetting);

}  // namespace uac
}  // namespace t3k::usb
