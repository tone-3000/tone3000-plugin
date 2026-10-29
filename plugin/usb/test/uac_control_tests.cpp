// L0: control-request byte sequences, compared against hand-computed hex
// (plan section 6.2). These bytes are the device-facing contract; a mistake
// here is a device that silently ignores the request.

#include <cstdint>

#include "UacControl.h"
#include "gtest/gtest.h"

namespace t3k::usb {
namespace {

void expectRequest(const CtrlRequest& r, uint8_t type, uint8_t req, uint16_t value,
                   uint16_t index, uint16_t length) {
  EXPECT_EQ(r.bmRequestType, type);
  EXPECT_EQ(r.bRequest, req);
  EXPECT_EQ(r.wValue, value);
  EXPECT_EQ(r.wIndex, index);
  EXPECT_EQ(r.wLength, length);
}

TEST(UacControl, Uac2SetSampleRate48k) {
  // Plan section 6.2: bmRequestType=0x21, bRequest=0x01 (SET_CUR),
  // wValue=0x0100 (SAM_FREQ_CONTROL << 8),
  // wIndex=(clockSourceEntityId << 8) | acInterfaceNumber, wLength=4,
  // data = uint32 LE 48000 = 80 BB 00 00.
  const CtrlRequest r = uac::uac2SetSampleRate(/*acInterface=*/0, /*clockSourceId=*/2, 48000);
  expectRequest(r, 0x21, 0x01, 0x0100, 0x0200, 4);
  EXPECT_EQ(r.data[0], 0x80);
  EXPECT_EQ(r.data[1], 0xBB);
  EXPECT_EQ(r.data[2], 0x00);
  EXPECT_EQ(r.data[3], 0x00);
}

TEST(UacControl, Uac2GetSampleRateReadback) {
  const CtrlRequest r = uac::uac2GetSampleRate(0, 2);
  expectRequest(r, 0xA1, 0x81, 0x0100, 0x0200, 4);
}

TEST(UacControl, Uac2GetSampleRateRangeCoversEightRanges) {
  const CtrlRequest r = uac::uac2GetSampleRateRange(0, 2);
  expectRequest(r, 0xA1, 0x82, 0x0100, 0x0200, 2 + 12 * 8);
}

TEST(UacControl, Uac1SetSampleRate44100) {
  // Plan section 6.2: bmRequestType=0x22, bRequest=0x01, wValue=0x0100,
  // wIndex=endpointAddress, wLength=3, data = 3-byte LE 44100 = 44 AC 00.
  const CtrlRequest r = uac::uac1SetSampleRate(0x01, 44100);
  expectRequest(r, 0x22, 0x01, 0x0100, 0x0001, 3);
  EXPECT_EQ(r.data[0], 0x44);
  EXPECT_EQ(r.data[1], 0xAC);
  EXPECT_EQ(r.data[2], 0x00);
}

TEST(UacControl, Uac1GetSampleRate) {
  const CtrlRequest r = uac::uac1GetSampleRate(0x82);
  expectRequest(r, 0xA2, 0x81, 0x0100, 0x0082, 3);
}

TEST(UacControl, Uac2MuteAndVolumeOnFeatureUnit) {
  // Plan section 6.2: bmRequestType=0x21, wValue=(controlSelector << 8) with
  // MUTE_CONTROL=0x01, VOLUME_CONTROL=0x02, wIndex=(entityId << 8) | channel,
  // wLength=1/2.
  const CtrlRequest mute = uac::uac2SetMute(/*entityId=*/3, /*channel=*/0, true);
  expectRequest(mute, 0x21, 0x01, 0x0100, 0x0300, 1);
  EXPECT_EQ(mute.data[0], 0x01);

  const CtrlRequest unmute = uac::uac2SetMute(3, 2, false);
  expectRequest(unmute, 0x21, 0x01, 0x0100, 0x0302, 1);
  EXPECT_EQ(unmute.data[0], 0x00);

  // -12 dB in 1/256 dB steps = -3072 = 0xF400 -> 00 F4 LE.
  const CtrlRequest vol = uac::uac2SetVolume(3, 0, -3072);
  expectRequest(vol, 0x21, 0x01, 0x0200, 0x0300, 2);
  EXPECT_EQ(vol.data[0], 0x00);
  EXPECT_EQ(vol.data[1], 0xF4);
}

TEST(UacControl, SetInterfaceStartsAndStopsStream) {
  const CtrlRequest start = uac::setInterface(2, 1);
  expectRequest(start, 0x01, 0x0B, 0x0001, 0x0002, 0);
  const CtrlRequest stop = uac::setInterface(2, 0);
  expectRequest(stop, 0x01, 0x0B, 0x0000, 0x0002, 0);
}

}  // namespace
}  // namespace t3k::usb
