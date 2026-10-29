#pragma once

// Data-driven device quirks (plan M2.5): a tiny table keyed by VID/PID
// instead of an unmaintainable compatibility list. Pure C++ so the lookup
// tests run on the host and the probe/Android shell share the same data.
//
// Seed entries come from what the probe actually observed for a device
// (plan M1.3 / M2.5); do not speculate. Notes in the table are for humans
// reading dumps.

#include <cstdint>
#include <vector>

namespace t3k::usb {

struct DeviceQuirk {
  uint16_t vid = 0;
  uint16_t pid = 0;
  /** Extra settle time after SET_INTERFACE before the first URB (some
      devices stop responding otherwise; plan section 6.2). */
  int settleDelayMsAfterSetInterface = 0;
  /** Some devices only ever present subslot 4 despite reporting 3. */
  bool forceSubslot4 = false;
  /** Interfaces to leave unclaimed even when they look audio-related
      (bit N = interface number N). */
  uint32_t ignoreInterfaces = 0;
  const char* note = "";
};

class UsbQuirks {
 public:
  static UsbQuirks& instance();

  const DeviceQuirk* find(uint16_t vid, uint16_t pid) const;
  void add(const DeviceQuirk& quirk);
  void resetToDefaults();

 private:
  UsbQuirks() { resetToDefaults(); }
  std::vector<DeviceQuirk> table_;
};

}  // namespace t3k::usb
