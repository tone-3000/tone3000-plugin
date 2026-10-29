// L0: wire <-> float PCM conversion (plan pitfall 3: 24-bit packing mistakes
// produce garbage audio, so both packings are pinned bit-exactly).

#include <cstdint>
#include <limits>
#include <vector>

#include "PcmConvert.h"
#include "gtest/gtest.h"

namespace t3k::usb {
namespace {

TEST(PcmConvert, Packed24RoundTripIsBitExact) {
  const int32_t values[] = {0, 1, -1, 0x123456, -0x123456, 0x7FFFFF, -0x800000};
  for (int32_t v : values) {
    const float f = pcm::int24ToFloat(v);
    std::vector<uint8_t> wire(3);
    pcm::floatToWire(&f, wire.data(), 1, 1, 3);
    int32_t back = pcm::floatToInt24(pcm::int24ToFloat(v));
    std::vector<float> out(1);
    pcm::wireToFloat(wire.data(), out.data(), 1, 1, 3);
    EXPECT_EQ(pcm::floatToInt24(out[0]), v) << "value " << v;
    EXPECT_EQ(back, v);
  }
}

TEST(PcmConvert, Packed24ByteOrderIsLittleEndian) {
  // 0x123456 -> 56 34 12 on the wire.
  const float f = pcm::int24ToFloat(0x123456);
  uint8_t wire[3] = {};
  pcm::floatToWire(&f, wire, 1, 1, 3);
  EXPECT_EQ(wire[0], 0x56);
  EXPECT_EQ(wire[1], 0x34);
  EXPECT_EQ(wire[2], 0x12);
}

TEST(PcmConvert, Slot24In32IsLeftJustified) {
  // The 24-bit value sits in bytes 1..3 (word = value << 8); byte 0 is
  // padding. Devices read the slot at 32-bit scale (ALSA S32_LE), so a
  // low-3-bytes write would play as a half-rectified wave (plan: the AMS-22
  // "square output" and the EVO8 input fuzz were this bug).
  const float f = pcm::int24ToFloat(0x123456);
  uint8_t wire[4] = {0xAA, 0xAA, 0xAA, 0xAA};
  pcm::floatToWire(&f, wire, 1, 1, 4);
  EXPECT_EQ(wire[0], 0x00);  // padding byte is zeroed
  EXPECT_EQ(wire[1], 0x56);
  EXPECT_EQ(wire[2], 0x34);
  EXPECT_EQ(wire[3], 0x12);
  float out = 0;
  pcm::wireToFloat(wire, &out, 1, 1, 4);
  EXPECT_EQ(pcm::floatToInt24(out), 0x123456);
}

TEST(PcmConvert, NegativeValuesSignExtendInBothPackings) {
  // -1 = 0xFFFFFF packed / 0xFFFFFF00 slotted; the padding byte of a 4-byte
  // slot must not leak in as sign or magnitude.
  const float f = pcm::int24ToFloat(-1);
  uint8_t packed[3], slot[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  pcm::floatToWire(&f, packed, 1, 1, 3);
  pcm::floatToWire(&f, slot, 1, 1, 4);
  EXPECT_EQ(pcm::floatToInt24(pcm::int24ToFloat(-1)), -1);
  float back = 0;
  pcm::wireToFloat(slot, &back, 1, 1, 4);
  EXPECT_EQ(pcm::floatToInt24(back), -1);
  pcm::wireToFloat(packed, &back, 1, 1, 3);
  EXPECT_EQ(pcm::floatToInt24(back), -1);
}

TEST(PcmConvert, FloatClipsAtTheRails) {
  EXPECT_EQ(pcm::floatToInt24(2.0f), 0x7FFFFF);
  EXPECT_EQ(pcm::floatToInt24(-2.0f), -0x800000);
  EXPECT_EQ(pcm::floatToInt24(1.0f), 0x7FFFFF);  // +1.0 wraps without clipping
}

TEST(PcmConvert, InterleavedLayoutIsChannelMajor) {
  // Two frames of stereo: [L0 R0 L1 R1].
  const float in[4] = {0.25f, -0.25f, 0.5f, -0.5f};
  uint8_t wire[4 * 3];
  pcm::floatToWire(in, wire, 2, 2, 3);
  float out[4] = {};
  pcm::wireToFloat(wire, out, 2, 2, 3);
  for (int i = 0; i < 4; ++i) EXPECT_NEAR(out[i], in[i], 1e-6);
}

}  // namespace
}  // namespace t3k::usb

TEST(PcmConvert, NaNAndInfinitiesAreSafe) {
  // A NaN output from the DSP must never become full-scale wire garbage.
  EXPECT_EQ(t3k::usb::pcm::floatToInt24(std::numeric_limits<float>::quiet_NaN()), 0);
  EXPECT_EQ(t3k::usb::pcm::floatToInt24(std::numeric_limits<float>::infinity()), 8388607);
  EXPECT_EQ(t3k::usb::pcm::floatToInt24(-std::numeric_limits<float>::infinity()), -8388608);
}
