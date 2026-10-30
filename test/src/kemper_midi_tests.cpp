#include "KemperMidi.h"

#include <gtest/gtest.h>
#include <limits>
#include <vector>

namespace {
using namespace t3k::capture::kemper;

std::vector<int> bytes(const Messages& messages) {
  std::vector<int> result;
  for (const auto& message : messages)
    for (int i = 0; i < message.getRawDataSize(); ++i)
      result.push_back(message.getRawData()[i]);
  return result;
}

TEST(KemperMidiTest, MatchesManufacturersFourControllerExample) {
  // MIDI Parameter Documentation 13 p.13: Wah Mix module A, value10000.
  EXPECT_EQ(bytes(encodeNrpn(1, 50, 4, 10000)),
            (std::vector<int>{0xb0, 99, 50, 0xb0, 98, 4, 0xb0, 6, 78, 0xb0, 38, 16}));
}

TEST(KemperMidiTest, EncodesFull14BitRangeOnRequestedChannel) {
  EXPECT_EQ(bytes(encodeNrpn(16, 127, 127, 16383)),
            (std::vector<int>{0xbf, 99, 127, 0xbf, 98, 127, 0xbf, 6, 127, 0xbf, 38, 127}));
  EXPECT_EQ(bytes(encodeNrpn(2, 10, 4, 0)),
            (std::vector<int>{0xb1, 99, 10, 0xb1, 98, 4, 0xb1, 6, 0, 0xb1, 38, 0}));
}

TEST(KemperMidiTest, InvalidAddressChannelAndValuesProduceNoBytes) {
  for (const int channel : {-1, 0, 17, 255})
    EXPECT_TRUE(encodeNrpn(channel, 10, 4, 0).empty());
  EXPECT_TRUE(encodeNrpn(1, -1, 4, 0).empty());
  EXPECT_TRUE(encodeNrpn(1, 128, 4, 0).empty());
  EXPECT_TRUE(encodeNrpn(1, 10, -1, 0).empty());
  EXPECT_TRUE(encodeNrpn(1, 10, 128, 0).empty());
  EXPECT_TRUE(encodeNrpn(1, 10, 4, -1).empty());
  EXPECT_TRUE(encodeNrpn(1, 10, 4, 16384).empty());
}

TEST(KemperMidiTest, GenericGainHasCorrectAddressAndMidpoint) {
  EXPECT_EQ(bytes(encodeGain(1, 0.0f)), bytes(encodeNrpn(1, 10, 4, 0)));
  EXPECT_EQ(bytes(encodeGain(1, 5.0f)), bytes(encodeNrpn(1, 10, 4, 8192)));
  EXPECT_EQ(bytes(encodeGain(1, 10.0f)), bytes(encodeNrpn(1, 10, 4, 16383)));
  EXPECT_TRUE(encodeGain(1, -0.1f).empty());
  EXPECT_TRUE(encodeGain(1, 10.1f).empty());
  EXPECT_TRUE(encodeGain(1, std::numeric_limits<float>::quiet_NaN()).empty());
  EXPECT_TRUE(encodeGain(1, std::numeric_limits<float>::infinity()).empty());
}

TEST(KemperMidiTest, EqValidatesEntireGroupAndEncodesIndependentAddresses) {
  const auto result = encodeEq(3, -5, 0, 5, 0);
  ASSERT_EQ(result.size(), 16u);
  Messages expected;
  for (const auto& group : {encodeNrpn(3, 11, 4, 0), encodeNrpn(3, 11, 5, 8192),
                            encodeNrpn(3, 11, 6, 16383), encodeNrpn(3, 11, 7, 8192)})
    expected.insert(expected.end(), group.begin(), group.end());
  EXPECT_EQ(bytes(result), bytes(expected));
  EXPECT_TRUE(encodeEq(1, 0, 0, 0, 5.1f).empty());
  EXPECT_TRUE(encodeEq(1, 0, std::numeric_limits<float>::quiet_NaN(), 0, 0).empty());
  EXPECT_TRUE(encodeEq(0, 0, 0, 0, 0).empty());
}

TEST(KemperMidiTest, CabinetSwitchUsesLiteralOneNotFullScale) {
  EXPECT_EQ(bytes(encodeCabinet(1, true)),
            (std::vector<int>{0xb0, 99, 12, 0xb0, 98, 2, 0xb0, 6, 0, 0xb0, 38, 1}));
  EXPECT_EQ(bytes(encodeCabinet(1, false)), bytes(encodeNrpn(1, 12, 2, 0)));
}

TEST(KemperMidiTest, ProgramChangeDoesNotConfuseOneBasedUiWithRawMidi) {
  EXPECT_EQ(bytes(encodeProgramChange(1, 0)), (std::vector<int>{0xc0, 0}));
  EXPECT_EQ(bytes(encodeProgramChange(16, 127)), (std::vector<int>{0xcf, 127}));
  EXPECT_TRUE(encodeProgramChange(1, 128).empty());
  EXPECT_TRUE(encodeProgramChange(1, -1).empty());
  EXPECT_TRUE(encodeProgramChange(17, 1).empty());
}

TEST(KemperMidiTest, DisconnectedControllerFailsWithoutOpeningOrSending) {
  Controller controller;
  EXPECT_FALSE(controller.isOpen());
  EXPECT_TRUE(controller.setGain(5).failed());
  EXPECT_TRUE(controller.setEq(0, 0, 0, 0).failed());
  EXPECT_TRUE(controller.setCabinet(true).failed());
  EXPECT_TRUE(controller.selectProgram(0).failed());
  EXPECT_TRUE(controller.connect({}, 1).failed());
  EXPECT_TRUE(controller.connect("unused-invalid-port", 0).failed());
  controller.close();
  EXPECT_FALSE(controller.isOpen());
}
}  // namespace
