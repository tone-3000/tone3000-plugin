#include "AudioRing.h"

#include <algorithm>
#include <cstring>

namespace t3k::usb {
namespace {

size_t roundUpPow2(size_t v) {
  size_t p = 1;
  while (p < v) p <<= 1;
  return p;
}

}  // namespace

AudioRing::AudioRing(size_t capacityFrames, int bytesPerFrame)
    : mask_(roundUpPow2(std::max<size_t>(capacityFrames, 2)) - 1),
      capacity_(mask_ + 1),
      bytesPerFrame_(bytesPerFrame),
      data_(capacity_ * static_cast<size_t>(bytesPerFrame)),
      lastFrame_(static_cast<size_t>(bytesPerFrame)) {}

size_t AudioRing::readAvailable() const {
  const uint64_t w = writeCount_.load(std::memory_order_acquire);
  const uint64_t r = readCount_.load(std::memory_order_relaxed);
  return static_cast<size_t>(std::min<uint64_t>(w - r, capacity_));
}

size_t AudioRing::writeAvailable() const {
  return capacity_ - readAvailable();
}

void AudioRing::writeDropOldest(const uint8_t* src, size_t frames) {
  // Overwrite ring: always accept, wrapping over un-consumed frames if the
  // consumer is lapped. The consumer notices and counts the overrun.
  const uint64_t w = writeCount_.load(std::memory_order_relaxed);
  size_t idx = static_cast<size_t>(w & mask_);
  size_t done = 0;
  while (done < frames) {
    const size_t chunk = std::min(frames - done, capacity_ - idx);
    std::memcpy(data_.data() + idx * bytesPerFrame_, src + done * bytesPerFrame_,
                chunk * bytesPerFrame_);
    done += chunk;
    idx = 0;
  }
  writeCount_.store(w + frames, std::memory_order_release);
}

size_t AudioRing::write(const uint8_t* src, size_t frames) {
  const uint64_t w = writeCount_.load(std::memory_order_relaxed);
  const uint64_t r = readCount_.load(std::memory_order_acquire);
  const size_t n = std::min<size_t>(frames, capacity_ - static_cast<size_t>(w - r));
  if (n == 0) return 0;
  size_t idx = static_cast<size_t>(w & mask_);
  size_t done = 0;
  while (done < n) {
    const size_t chunk = std::min(n - done, capacity_ - idx);
    std::memcpy(data_.data() + idx * bytesPerFrame_, src + done * bytesPerFrame_,
                chunk * bytesPerFrame_);
    done += chunk;
    idx = 0;
  }
  writeCount_.store(w + n, std::memory_order_release);
  return n;
}

size_t AudioRing::read(uint8_t* dst, size_t frames) {
  uint64_t r = readCount_.load(std::memory_order_relaxed);
  const uint64_t w = writeCount_.load(std::memory_order_acquire);
  if (w - r > capacity_) {
    // Lapped by the producer: the oldest frames are gone. Skip them.
    r = w - capacity_;
    overruns_.fetch_add(1, std::memory_order_relaxed);
  }
  const size_t n = std::min<size_t>(frames, static_cast<size_t>(w - r));
  size_t idx = static_cast<size_t>(r & mask_);
  size_t done = 0;
  while (done < n) {
    const size_t chunk = std::min(n - done, capacity_ - idx);
    std::memcpy(dst + done * bytesPerFrame_, data_.data() + idx * bytesPerFrame_,
                chunk * bytesPerFrame_);
    done += chunk;
    idx = 0;
  }
  if (n > 0) {
    std::memcpy(lastFrame_.data(), dst + (n - 1) * bytesPerFrame_, bytesPerFrame_);
    hasLastFrame_ = true;
  }
  readCount_.store(r + n, std::memory_order_release);
  return n;
}

size_t AudioRing::readOrRepeat(uint8_t* dst, size_t frames) {
  const size_t got = read(dst, frames);
  if (got == frames) return frames;
  underruns_.fetch_add(1, std::memory_order_relaxed);
  const size_t missing = frames - got;
  if (hasLastFrame_) {
    for (size_t i = 0; i < missing; ++i) {
      std::memcpy(dst + (got + i) * bytesPerFrame_, lastFrame_.data(), bytesPerFrame_);
    }
  } else {
    std::memset(dst + got * bytesPerFrame_, 0, missing * bytesPerFrame_);
  }
  return frames;
}

void AudioRing::clear() {
  readCount_.store(writeCount_.load(std::memory_order_acquire), std::memory_order_release);
  hasLastFrame_ = false;
}

}  // namespace t3k::usb
