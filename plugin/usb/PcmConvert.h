#pragma once

// Interleaved PCM conversion between the USB wire format and float, for the
// two packings the AMS-22-class devices use (plan section 6.1):
//
//   subslot 3: packed 3 bytes per sample, little-endian 24-bit signed
//   subslot 4: 4 bytes per sample, sample left-justified in the 32-bit word
//              (value << 8): bytes 1..3 carry the 24-bit sample, byte 0 is
//              padding. This matches Linux snd-usb-audio, which exposes a
//              bSubslotSize-4 stream as SNDRV_PCM_FORMAT_S32_LE and therefore
//              carries 24-bit content at full 32-bit scale. Devices read the
//              word the same way (verified on Audient EVO8 and Zoom AMS-22:
//              right-justified writes came out as a half-rectified,
//              DC-lifted "square" wave at the analog output while the
//              byte-transparent digital loopback stayed clean).
//
// The plan's rule: read bSubslotSize from the descriptor dump, never guess.
// Both packings are covered by bit-exact unit tests.

#include <cstddef>
#include <cstdint>

namespace t3k::usb::pcm {

/** Wire (interleaved, subslotBytes per sample) -> float in [-1, 1). */
void wireToFloat(const uint8_t* src, float* dst, size_t frames, int channels,
                 int subslotBytes);

/** Float -> wire. Values outside [-1, 1) clip to the representable range so a
    DSP overshoot cannot wrap into a full-scale glitch. */
void floatToWire(const float* src, uint8_t* dst, size_t frames, int channels,
                 int subslotBytes);

/** Signed 24-bit value in [-2^23, 2^23) to float, both packings' common core. */
constexpr float int24ToFloat(int32_t v) {
  return static_cast<float>(v) * (1.0f / 8388608.0f);
}

/** Float to signed 24-bit, clipping at the rails. */
constexpr int32_t floatToInt24(float f) {
  // A NaN would fall through both comparisons and convert through undefined
  // behaviour; in practice that emits full-scale garbage (heard as white
  // noise). Silence is the safe wire value.
  if (f != f) return 0;
  const float scaled = f * 8388608.0f;
  if (scaled >= 8388607.0f) return 8388607;
  if (scaled <= -8388608.0f) return -8388608;
  return static_cast<int32_t>(scaled);
}

}  // namespace t3k::usb::pcm
