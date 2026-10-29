// L0: quirk registry lookups (plan M2.5).

#include "UsbQuirks.h"
#include "gtest/gtest.h"

namespace t3k::usb {
namespace {

TEST(UsbQuirks, SeededWithTheAms22) {
  UsbQuirks& quirks = UsbQuirks::instance();
  quirks.resetToDefaults();
  const DeviceQuirk* q = quirks.find(0x1686, 0x066F);
  ASSERT_NE(q, nullptr);
  EXPECT_NE(q->ignoreInterfaces & (1u << 3), 0u);  // extra AC block
  EXPECT_NE(q->ignoreInterfaces & (1u << 4), 0u);  // MIDI
  EXPECT_FALSE(q->forceSubslot4);
}

TEST(UsbQuirks, UnknownDeviceHasNoQuirk) {
  UsbQuirks& quirks = UsbQuirks::instance();
  quirks.resetToDefaults();
  EXPECT_EQ(quirks.find(0x1234, 0x5678), nullptr);
  EXPECT_EQ(quirks.find(0x1686, 0x9999), nullptr);
}

TEST(UsbQuirks, AddReplacesAndExtends) {
  UsbQuirks& quirks = UsbQuirks::instance();
  quirks.resetToDefaults();
  DeviceQuirk custom{0x1234, 0x0001, /*settleDelayMs=*/25, /*forceSubslot4=*/true,
                     /*ignoreInterfaces=*/0, "test device"};
  quirks.add(custom);
  const DeviceQuirk* q = quirks.find(0x1234, 0x0001);
  ASSERT_NE(q, nullptr);
  EXPECT_EQ(q->settleDelayMsAfterSetInterface, 25);
  EXPECT_TRUE(q->forceSubslot4);

  custom.settleDelayMsAfterSetInterface = 50;
  quirks.add(custom);  // same key: replace, not duplicate
  EXPECT_EQ(quirks.find(0x1234, 0x0001)->settleDelayMsAfterSetInterface, 50);
  quirks.resetToDefaults();
  EXPECT_EQ(quirks.find(0x1234, 0x0001), nullptr);
}

}  // namespace
}  // namespace t3k::usb
