#pragma once

// Parser for USB Audio Class configuration descriptors: interfaces,
// alternate settings, endpoints, the AudioControl entity graph (terminals,
// clock sources/selectors, feature units) and the AudioStreaming format
// descriptors (subslot size, bit resolution, sample rates). Understands both
// UAC1 (the fallback path) and UAC2 (the AMS-22's class).
//
// Pure C++ over a raw descriptor blob (the bytes of
// UsbDeviceConnection.getRawDescriptors() from the probe's dump mode), so
// the whole parser runs on the host against captured fixtures. Unknown
// class-specific subtypes are skipped by their bLength, never rejected: real
// devices carry plenty of vendor extensions.

#include <cstdint>
#include <string>
#include <vector>

#include "UsbAudioTypes.h"

namespace t3k::usb {

struct EndpointInfo {
  uint8_t address = 0;     // bEndpointAddress (incl. direction bit)
  uint8_t attributes = 0;  // bmAttributes
  uint16_t maxPacketSize = 0;
  uint8_t interval = 0;

  bool isOut() const { return (address & 0x80) == 0; }
  uint8_t transferType() const { return attributes & 0x3; }
  uint8_t syncType() const { return (attributes >> 2) & 0x3; }
  uint8_t usageType() const { return (attributes >> 4) & 0x3; }
  bool isIsochronous() const { return transferType() == 0x1; }
  bool isBulk() const { return transferType() == 0x2; }
  /** Explicit feedback endpoint: isochronous with usage type 01 (UAC2 also
      allows interrupt feedback; the quirk registry covers those devices). */
  bool isFeedback() const { return isIsochronous() && usageType() == 0x1; }
  /** Sync type Synchronous: the device clock follows the USB frame clock. */
  bool isSynchronous() const { return isIsochronous() && syncType() == 0x3; }
  /** Implicit feedback data endpoint (UAC2 usage type 10): pacing comes from
      SOF timestamps / actual lengths, no feedback endpoint exists. */
  bool isImplicitFeedbackData() const { return isIsochronous() && usageType() == 0x2; }
};

struct Entity {
  enum class Kind : uint8_t {
    inputTerminal,
    outputTerminal,
    clockSource,
    clockSelector,
    featureUnit,
    other,
  };
  Kind kind = Kind::other;
  uint8_t id = 0;
  uint16_t terminalType = 0;  // terminals
  uint8_t sourceId = 0;       // output terminals / feature units
  uint8_t cSourceId = 0;      // UAC2: which clock source feeds this entity
  uint8_t channels = 0;       // input terminals: bNrChannels (UAC2 rates/format omit it)
  uint8_t clockType = 0;      // clock sources: bits 1:0 of bmAttributes
  bool clockFrequencyControl = false;  // bmControls bit 0 (UAC2)
  std::vector<uint8_t> pins;           // selector/feature inputs
  uint8_t bSubtype = 0;                // raw CS subtype, for diagnostics
};

struct FormatInfo {
  bool present = false;
  uint8_t channels = 0;
  uint8_t subslotBytes = 0;
  uint8_t bitResolution = 0;
  /** UAC1: tSamFreq list. UAC2: empty until a GET_RANGE readback fills it
      (rates live on the clock source's SAM_FREQ control, not here). */
  std::vector<uint32_t> sampleRates;
  bool continuous = false;
  uint32_t minRate = 0, maxRate = 0;
};

struct AltSetting {
  uint8_t interfaceNumber = 0;
  uint8_t altSetting = 0;
  uint8_t ifaceClass = 0, ifaceSubClass = 0, ifaceProtocol = 0;
  std::vector<EndpointInfo> endpoints;
  FormatInfo format;
  /** AS general: bTerminalLink, the AC entity this stream attaches to. */
  uint8_t terminalLink = 0;
  uint8_t bDelay = 0;

  bool isAudioControl() const { return ifaceClass == 1 && ifaceSubClass == 1; }
  bool isAudioStreaming() const { return ifaceClass == 1 && ifaceSubClass == 2; }
  bool hasDataEndpoint() const;
  const EndpointInfo* dataEndpoint() const;
  const EndpointInfo* feedbackEndpoint() const;
};

struct ConfigTree {
  uint8_t configValue = 1;
  uint16_t maxPowerMa = 0;
  bool busPowered = true;
  std::vector<AltSetting> altSettings;
  std::vector<Entity> entities;

  UacVersion uacVersion() const;
  /** All altsettings of one streaming interface with a data endpoint. */
  std::vector<const AltSetting*> streamingAlts(bool out) const;
  /** The active altsetting for a direction whose format covers the rate.
      For UAC2 (one altsetting per direction, rate set via the clock source)
      this returns that altsetting and ignores the rate. */
  const AltSetting* findAlt(bool out, uint32_t sampleRate, uint8_t channels) const;
  /** UAC2 clock source carrying the SAM_FREQ control (the rate knob). */
  const Entity* rateClockSource() const;
  /** Explicit feedback endpoint for a direction, or nullptr (implicit). */
  const EndpointInfo* feedbackEndpointFor(bool out) const;

  /** Human-readable dump, matching what probe 'dump' mode prints. */
  std::string describe() const;
};

/** Parse a full configuration descriptor blob (starting at the config
    descriptor, as getRawDescriptors() returns it). Returns false on a
    malformed header; partial trees are still filled. */
bool parseConfig(const uint8_t* data, size_t len, ConfigTree& out);

/** Parse the UAC2 GET_RANGE response of SAM_FREQ_CONTROL: triplets of
    (min, max, resolution) as uint32 LE. */
bool parseRateRanges(const uint8_t* data, size_t len, std::vector<uint32_t>& rates);

}  // namespace t3k::usb
