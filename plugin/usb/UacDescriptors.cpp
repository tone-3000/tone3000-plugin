#include "UacDescriptors.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>

namespace t3k::usb {
namespace {

// Standard descriptor types (USB 2.0 chapter 9).
constexpr uint8_t kDescConfig = 0x02;
constexpr uint8_t kDescInterface = 0x04;
constexpr uint8_t kDescEndpoint = 0x05;
constexpr uint8_t kDescCsInterface = 0x24;
constexpr uint8_t kDescCsEndpoint = 0x25;

// Audio class-specific AC/AS subtypes (UAC1 and UAC2).
constexpr uint8_t kCsAcHeader = 0x01;
constexpr uint8_t kCsInputTerminal = 0x02;
constexpr uint8_t kCsOutputTerminal = 0x03;
constexpr uint8_t kCsMixerUnit = 0x04;
constexpr uint8_t kCsSelectorUnit = 0x05;
constexpr uint8_t kCsFeatureUnit = 0x06;
constexpr uint8_t kCsProcessingUnit = 0x07;
constexpr uint8_t kCsClockSource = 0x0A;
constexpr uint8_t kCsClockSelector = 0x0B;
constexpr uint8_t kCsAsGeneral = 0x01;
constexpr uint8_t kCsFormatType = 0x02;

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t le24(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16);
}
uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

bool AltSetting::hasDataEndpoint() const { return dataEndpoint() != nullptr; }

const EndpointInfo* AltSetting::dataEndpoint() const {
  for (const auto& e : endpoints) {
    if (e.isIsochronous() && !e.isFeedback()) return &e;
  }
  return nullptr;
}

const EndpointInfo* AltSetting::feedbackEndpoint() const {
  for (const auto& e : endpoints) {
    if (e.isFeedback()) return &e;
  }
  return nullptr;
}

UacVersion ConfigTree::uacVersion() const {
  // Protocol 0x20 on an AudioControl interface is the UAC2 marker; UAC1 uses
  // 0x00/0x01/0x02 there.
  for (const auto& a : altSettings) {
    if (a.isAudioControl()) {
      return a.ifaceProtocol == 0x20 ? UacVersion::uac2 : UacVersion::uac1;
    }
  }
  return UacVersion::uac2;
}

std::vector<const AltSetting*> ConfigTree::streamingAlts(bool out) const {
  std::vector<const AltSetting*> result;
  for (const auto& a : altSettings) {
    if (!a.isAudioStreaming() || !a.hasDataEndpoint()) continue;
    const EndpointInfo* ep = a.dataEndpoint();
    if (ep->isOut() == out) result.push_back(&a);
  }
  return result;
}

const AltSetting* ConfigTree::findAlt(bool out, uint32_t sampleRate,
                                      uint8_t channels) const {
  const AltSetting* fallback = nullptr;
  for (const AltSetting* a : streamingAlts(out)) {
    const FormatInfo& f = a->format;
    if (!f.present) continue;
    if (channels != 0 && f.channels != 0 && f.channels != channels) continue;
    // UAC2 (and UAC1 continuous) formats carry no fixed rate list here; the
    // rate is negotiated on the clock source / endpoint control. Accept the
    // altsetting and let the control path pin the rate.
    if (f.sampleRates.empty()) {
      if (fallback == nullptr) fallback = a;
      continue;
    }
    if (std::find(f.sampleRates.begin(), f.sampleRates.end(), sampleRate) !=
        f.sampleRates.end()) {
      return a;
    }
  }
  return fallback;
}

const Entity* ConfigTree::rateClockSource() const {
  for (const auto& e : entities) {
    if (e.kind == Entity::Kind::clockSource && e.clockFrequencyControl) return &e;
  }
  for (const auto& e : entities) {
    if (e.kind == Entity::Kind::clockSource) return &e;
  }
  return nullptr;
}

const EndpointInfo* ConfigTree::feedbackEndpointFor(bool out) const {
  for (const AltSetting* a : streamingAlts(out)) {
    if (const EndpointInfo* fb = a->feedbackEndpoint()) return fb;
  }
  return nullptr;
}

bool parseConfig(const uint8_t* data, size_t len, ConfigTree& out) {
  if (data == nullptr || len < 9) return false;

  // Parse state: which interface/altsetting a class-specific descriptor
  // belongs to.
  int curIface = -1;
  int curAlt = -1;
  uint8_t curClass = 0, curSubClass = 0, curProtocol = 0;
  AltSetting* cur = nullptr;
  bool uac2Entities = true;  // set by the AC header's bcdADC

  size_t pos = 0;
  while (pos + 2 <= len) {
    const uint8_t bLength = data[pos];
    const uint8_t bType = data[pos + 1];
    if (bLength < 2 || pos + bLength > len) break;  // truncated tail
    const uint8_t* d = data + pos;

    switch (bType) {
      case kDescConfig:
        if (bLength >= 9) {
          out.configValue = d[5];
          out.busPowered = (d[7] & 0x40) == 0;
          out.maxPowerMa = static_cast<uint16_t>(d[8]) *
                           (out.busPowered ? 2 : 2);  // USB 2.0: units of 2 mA
        }
        break;

      case kDescInterface:
        if (bLength >= 9) {
          curIface = d[2];
          curAlt = d[3];
          curClass = d[5];
          curSubClass = d[6];
          curProtocol = d[7];
          out.altSettings.emplace_back();
          cur = &out.altSettings.back();
          cur->interfaceNumber = static_cast<uint8_t>(curIface);
          cur->altSetting = static_cast<uint8_t>(curAlt);
          cur->ifaceClass = curClass;
          cur->ifaceSubClass = curSubClass;
          cur->ifaceProtocol = curProtocol;
        }
        break;

      case kDescEndpoint:
        if (bLength >= 7 && cur != nullptr) {
          EndpointInfo e;
          e.address = d[2];
          e.attributes = d[3];
          e.maxPacketSize = le16(d + 4);
          e.interval = d[6];
          cur->endpoints.push_back(e);
        }
        break;

      case kDescCsInterface:
        if (cur == nullptr || bLength < 3) break;
        if (cur->isAudioControl()) {
          Entity ent;
          ent.bSubtype = d[2];
          switch (d[2]) {
            case kCsAcHeader: {
              // bcdADC tells UAC1 (1.xx) from UAC2 (2.xx); the entity layouts
              // below branch on it. The header itself is not an entity.
              if (bLength >= 5) uac2Entities = d[4] >= 2;
              break;
            }
            case kCsInputTerminal:
              if (bLength >= 8) {
                ent.kind = Entity::Kind::inputTerminal;
                ent.id = d[3];
                ent.terminalType = le16(d + 4);
                // UAC2 Input Terminal (17 bytes): id, type, assoc, bCSourceID,
                // channels, ... The clock source lives at offset 7 and
                // bNrChannels at offset 8. UAC1 carries it at offset 7.
                if (uac2Entities && bLength >= 8) {
                  ent.cSourceId = d[7];
                  if (bLength >= 9) ent.channels = d[8];
                } else if (!uac2Entities && bLength >= 8) {
                  ent.channels = d[7];
                }
              }
              break;
            case kCsOutputTerminal:
              if (bLength >= 8) {
                ent.kind = Entity::Kind::outputTerminal;
                ent.id = d[3];
                ent.terminalType = le16(d + 4);
                // bSourceID: offset 7 in both UAC1 and UAC2. UAC2 adds
                // bCSourceID at offset 8 (the 12-byte form).
                ent.sourceId = d[7];
                if (uac2Entities && bLength >= 9) ent.cSourceId = d[8];
              }
              break;
            case kCsMixerUnit:
            case kCsSelectorUnit: {
              // UAC2 mixer/selector: bUnitID then bNrInPins + baSourceID[].
              // Kept as `other`; the channel-resolution pass walks them.
              if (bLength >= 5) {
                ent.kind = Entity::Kind::other;
                ent.id = d[3];
                const uint8_t pins = d[4];
                for (uint8_t i = 0; i < pins && 5 + i < bLength; ++i)
                  ent.pins.push_back(d[5 + i]);
              }
              break;
            }
            case kCsProcessingUnit:
              if (bLength >= 5) {
                ent.kind = Entity::Kind::other;
                ent.id = d[3];
                ent.sourceId = d[4];
              }
              break;
            case kCsFeatureUnit:
              if (bLength >= 6) {
                ent.kind = Entity::Kind::featureUnit;
                ent.id = d[3];
                ent.sourceId = d[4];
                ent.pins.push_back(d[4]);
              }
              break;
            case kCsClockSource:
              if (bLength >= 6) {
                ent.kind = Entity::Kind::clockSource;
                ent.id = d[3];
                ent.clockType = d[4] & 0x3;
                ent.clockFrequencyControl = (d[5] & 0x3) != 0;
              }
              break;
            case kCsClockSelector:
              if (bLength >= 5) {
                ent.kind = Entity::Kind::clockSelector;
                ent.id = d[3];
                const uint8_t pins = d[4];
                for (uint8_t i = 0; i < pins && 5 + i < bLength; ++i) {
                  ent.pins.push_back(d[5 + i]);
                }
              }
              break;
            default:
              ent.kind = Entity::Kind::other;
              if (bLength >= 4) ent.id = d[3];
              break;
          }
          // The AC header is bookkeeping, not an entity.
          if (ent.bSubtype != kCsAcHeader &&
              (ent.kind != Entity::Kind::other || bLength >= 4)) {
            out.entities.push_back(ent);
          }
        } else if (cur->isAudioStreaming()) {
          switch (d[2]) {
            case kCsAsGeneral:
              if (bLength >= 7) {
                cur->terminalLink = d[3];
                cur->bDelay = d[4];
              }
              break;
            case kCsFormatType:
              if (bLength >= 6 && d[3] == 0x01) {  // FORMAT_TYPE_I
                FormatInfo& f = cur->format;
                f.present = true;
                // UAC2 Type I: bFormatType, bSubslotSize, bBitResolution
                // (the AMS-22 emits exactly these 6 bytes; a longer form adds
                // bmChannelConfig/iChannelNames after them). UAC1 Type I has
                // bNrChannels first and 3-byte rates after bSamFreqType.
                const bool uac2 = cur->ifaceProtocol == 0x20;
                if (uac2) {
                  f.subslotBytes = d[4];
                  f.bitResolution = d[5];
                } else if (bLength >= 8) {
                  f.channels = d[4];
                  f.subslotBytes = d[5];
                  f.bitResolution = d[6];
                }
                if (!uac2 && bLength >= 10) {
                  const uint8_t freqType = d[7];
                  if (freqType == 0) {
                    f.continuous = true;
                    if (bLength >= 14) {
                      f.minRate = le24(d + 8);
                      f.maxRate = le24(d + 11);
                    }
                  } else {
                    for (uint8_t i = 0; i < freqType; ++i) {
                      const size_t off = 8 + static_cast<size_t>(i) * 3;
                      if (off + 3 <= bLength) f.sampleRates.push_back(le24(d + off));
                    }
                  }
                }
              }
              break;
            default:
              break;
          }
        }
        break;

      default:
        break;  // IAD, CS endpoint, string, vendor: skip by bLength
    }

    pos += bLength;
  }

  // UAC2 Type I formats omit the channel count: it lives on the terminal the
  // AS interface links to (bTerminalLink). A capture AS links to the USB
  // streaming output terminal, so follow its source to the input terminal
  // that owns bNrChannels. This makes multichannel devices (the EVO8 is
  // 6-in/6-out) describe correctly instead of reporting 0 ch. UAC1 already
  // carries bNrChannels in the format descriptor, so leave it alone.
  if (uac2Entities)
  for (auto& alt : out.altSettings) {
    if (!alt.isAudioStreaming() || !alt.format.present || alt.terminalLink == 0)
      continue;
    const auto findEntity = [&out](uint8_t id) -> const Entity* {
      for (const auto& ent : out.entities)
        if (ent.id == id) return &ent;
      return nullptr;
    };
    const Entity* e = findEntity(alt.terminalLink);
    // Walk OT -> feature/mixer/selector/processing -> input terminal. The
    // EVO8's capture runs OT 22 -> mixer 60 -> mic terminal 1, for example.
    for (int depth = 0; e != nullptr && depth < 8; ++depth) {
      if (e->kind == Entity::Kind::inputTerminal) break;
      const Entity* next = nullptr;
      if (e->sourceId != 0) {
        next = findEntity(e->sourceId);
      } else if (e->pins.size() == 1) {
        next = findEntity(e->pins[0]);
      }
      if (next == nullptr) break;
      e = next;
    }
    if (e != nullptr && e->kind == Entity::Kind::inputTerminal && e->channels > 0) {
      alt.format.channels = e->channels;
    } else if (alt.format.channels == 0) {
      // Vendor DSP graphs (the EVO8's extension units) can be opaque after a
      // mixer. The stream cannot carry more channels than the largest input
      // terminal, which is a sound upper bound for the AS description.
      uint8_t maxChannels = 0;
      for (const auto& ent : out.entities)
        if (ent.kind == Entity::Kind::inputTerminal && ent.channels > maxChannels)
          maxChannels = ent.channels;
      if (maxChannels > 0) alt.format.channels = maxChannels;
    }
  }

  return true;
}

bool parseRateRanges(const uint8_t* data, size_t len, std::vector<uint32_t>& rates) {
  if (data == nullptr) return false;
  // UAC2 GET_RANGE responses open with wNumSubRanges (2 bytes) followed by
  // min/max/resolution triplets; some test fixtures are bare triplets. Accept
  // either framing.
  size_t off0 = 0;
  if (len >= 14 && (len - 2) % 12 == 0) {
    off0 = 2;
  } else if (len < 12 || len % 12 != 0) {
    return false;
  }
  std::set<uint32_t> unique;
  for (size_t off = off0; off + 12 <= len; off += 12) {
    const uint32_t min = le32(data + off);
    const uint32_t max = le32(data + off + 4);
    const uint32_t res = le32(data + off + 8);
    unique.insert(min);
    unique.insert(max);
    if (res > 0 && max >= min && (max - min) / res <= 16) {
      for (uint32_t r = min; r <= max; r += res) unique.insert(r);
    }
  }
  rates.assign(unique.begin(), unique.end());
  return !rates.empty();
}

std::string ConfigTree::describe() const {
  std::string s;
  char line[160];
  std::snprintf(line, sizeof(line),
                "config value=%u %s maxPower=%umA uac=%s\n", configValue,
                busPowered ? "bus-powered" : "self-powered", maxPowerMa,
                uacVersion() == UacVersion::uac2 ? "2.0" : "1.0");
  s += line;

  for (const auto& e : entities) {
    const char* kind = "other";
    switch (e.kind) {
      case Entity::Kind::inputTerminal: kind = "input terminal"; break;
      case Entity::Kind::outputTerminal: kind = "output terminal"; break;
      case Entity::Kind::clockSource: kind = "clock source"; break;
      case Entity::Kind::clockSelector: kind = "clock selector"; break;
      case Entity::Kind::featureUnit: kind = "feature unit"; break;
      default: break;
    }
    std::snprintf(line, sizeof(line),
                  "  entity id=%u %-15s subtype=0x%02x terminalType=0x%04x source=%u "
                  "clock=%u clockType=%u freqControl=%d pins=%zu\n",
                  e.id, kind, e.bSubtype, e.terminalType, e.sourceId, e.cSourceId,
                  e.clockType, e.clockFrequencyControl ? 1 : 0, e.pins.size());
    s += line;
  }

  for (const auto& a : altSettings) {
    std::snprintf(line, sizeof(line), "  iface %u alt %u class=%u/%u/0x%02x\n",
                  a.interfaceNumber, a.altSetting, a.ifaceClass, a.ifaceSubClass,
                  a.ifaceProtocol);
    s += line;
    for (const auto& ep : a.endpoints) {
      std::snprintf(line, sizeof(line),
                    "    ep 0x%02x attr=0x%02x maxPacket=%u interval=%u%s%s\n", ep.address,
                    ep.attributes, ep.maxPacketSize, ep.interval,
                    ep.isFeedback() ? " [feedback]" : "",
                    ep.isImplicitFeedbackData() ? " [implicit-fb data]" : "");
      s += line;
    }
    if (a.format.present) {
      std::snprintf(line, sizeof(line),
                    "    format: %u ch subslot=%u bits=%u", a.format.channels,
                    a.format.subslotBytes, a.format.bitResolution);
      s += line;
      if (!a.format.sampleRates.empty()) {
        s += " rates=";
        for (uint32_t r : a.format.sampleRates) {
          std::snprintf(line, sizeof(line), "%u ", r);
          s += line;
        }
      } else if (a.format.continuous) {
        std::snprintf(line, sizeof(line), " rates=%u..%u (continuous)", a.format.minRate,
                      a.format.maxRate);
        s += line;
      } else {
        s += " rates=(clock source)";
      }
      s += "\n";
    }
  }
  return s;
}

}  // namespace t3k::usb
