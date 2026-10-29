#include "PcmConvert.h"

namespace t3k::usb::pcm {
namespace {

constexpr int32_t readPacked24(const uint8_t* p) {
  // Little-endian 24-bit signed; sign-extend from bit 23.
  uint32_t raw = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                 (static_cast<uint32_t>(p[2]) << 16);
  return static_cast<int32_t>(raw << 8) >> 8;
}

constexpr void writePacked24(int32_t v, uint8_t* p) {
  p[0] = static_cast<uint8_t>(v & 0xff);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xff);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xff);
}

// Subslot 4: 4-byte LE word, sample left-justified (value << 8); the 24-bit
// value lives in bytes 1..3 and byte 0 is padding. See PcmConvert.h for why
// (ALSA S32_LE convention; the low-3-bytes reading was a 256x scale error
// with wraparound fuzz, and the matching wrong write produced a
// half-rectified "square" output at the device's DAC).
constexpr int32_t readSlot24In32(const uint8_t* p) {
  const uint32_t raw = static_cast<uint32_t>(p[1]) | (static_cast<uint32_t>(p[2]) << 8) |
                       (static_cast<uint32_t>(p[3]) << 16);
  return static_cast<int32_t>(raw << 8) >> 8;
}

constexpr void writeSlot24In32(int32_t v, uint8_t* p) {
  p[0] = 0;  // padding byte
  p[1] = static_cast<uint8_t>(v & 0xff);
  p[2] = static_cast<uint8_t>((v >> 8) & 0xff);
  p[3] = static_cast<uint8_t>((v >> 16) & 0xff);
}

}  // namespace

void wireToFloat(const uint8_t* src, float* dst, size_t frames, int channels,
                 int subslotBytes) {
  const size_t samples = frames * static_cast<size_t>(channels);
  if (subslotBytes == 3) {
    for (size_t i = 0; i < samples; ++i) dst[i] = int24ToFloat(readPacked24(src + i * 3));
  } else {
    for (size_t i = 0; i < samples; ++i) dst[i] = int24ToFloat(readSlot24In32(src + i * 4));
  }
}

void floatToWire(const float* src, uint8_t* dst, size_t frames, int channels,
                 int subslotBytes) {
  const size_t samples = frames * static_cast<size_t>(channels);
  if (subslotBytes == 3) {
    for (size_t i = 0; i < samples; ++i) writePacked24(floatToInt24(src[i]), dst + i * 3);
  } else {
    for (size_t i = 0; i < samples; ++i) writeSlot24In32(floatToInt24(src[i]), dst + i * 4);
  }
}

}  // namespace t3k::usb::pcm
