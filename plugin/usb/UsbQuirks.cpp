#include "UsbQuirks.h"

namespace t3k::usb {

UsbQuirks& UsbQuirks::instance() {
  static UsbQuirks quirks;
  return quirks;
}

const DeviceQuirk* UsbQuirks::find(uint16_t vid, uint16_t pid) const {
  for (const DeviceQuirk& q : table_) {
    if (q.vid == vid && q.pid == pid) return &q;
  }
  return nullptr;
}

void UsbQuirks::add(const DeviceQuirk& quirk) {
  for (DeviceQuirk& q : table_) {
    if (q.vid == quirk.vid && q.pid == quirk.pid) {
      q = quirk;
      return;
    }
  }
  table_.push_back(quirk);
}

void UsbQuirks::resetToDefaults() {
  table_.clear();
  // Zoom AMS-22 (VID 0x1686, PID 0x066F) as observed by the probe dump
  // (plan M1.3): interfaces 3 (a second AudioControl block) and 4 (MIDI
  // over bulk) belong to nobody's audio path. MIDI is bulk, so leaving it
  // bound costs no isochronous bandwidth; interface 3 is never claimed.
  table_.push_back({0x1686, 0x066F,
                    /*settleDelayMsAfterSetInterface=*/0,
                    /*forceSubslot4=*/false,
                    /*ignoreInterfaces=*/(1u << 3) | (1u << 4),
                    "Zoom AMS-22: leave the extra AC block and MIDI unclaimed"});
}

}  // namespace t3k::usb
