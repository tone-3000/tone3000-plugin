#pragma once

// Lock-free single-producer/single-consumer audio ring buffer. One side is
// the USB reaping thread, the other is the audio callback (which runs on the
// same USB thread by design D3, or on a test thread on the host); nothing
// here locks or allocates after construction.
//
// Capacity is in frames and rounded up to a power of two so the index wrap
// is a mask, not a branch. Counters are free-running (they wrap at 2^64,
// which is ~12 million years at 48 kHz) and their difference is the fill
// level, so a full and an empty ring are never confused. Each counter has
// exactly one writer: the producer owns writeCount_, the consumer owns
// readCount_.
//
// Policy (plan M2.2), applied by the two policy methods:
//   output underrun: repeat the last frame and flag
//   input overrun:   drop the oldest frames and flag
//
// The input-side drop is implemented as an overwrite ring: the producer
// always writes (wrapping over un-consumed frames in a true overrun) and the
// consumer clamps its read pointer forward when it sees it has been lapped.
// That keeps single-writer discipline on both counters; the overrun flag
// marks the frames that were lost.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace t3k::usb {

class AudioRing {
 public:
  AudioRing(size_t capacityFrames, int bytesPerFrame);

  /** Producer: copy up to frames frames in, only as many as fit. Returns
      frames accepted; a shortfall is the caller's signal (playback should
      never silently drop, so this is the strict path). */
  size_t write(const uint8_t* src, size_t frames);

  /** Consumer: copy up to frames frames out. Returns frames produced. If the
      producer lapped the ring, the oldest frames are dropped and the overrun
      counter advances before copying. */
  size_t read(uint8_t* dst, size_t frames);

  /** Producer, input-side policy: overwrite as many frames as given, even
      over un-consumed data (freshest wins for capture). Always accepts.
      The dropped frames are counted on the consumer's next read. */
  void writeDropOldest(const uint8_t* src, size_t frames);

  /** Consumer, output-side policy: on underrun repeat the last frame read
      (holds the output level steady instead of clicking) and count an
      underrun. Always returns frames. */
  size_t readOrRepeat(uint8_t* dst, size_t frames);

  size_t readAvailable() const;
  size_t writeAvailable() const;
  size_t capacityFrames() const { return capacity_; }
  int bytesPerFrame() const { return bytesPerFrame_; }

  uint64_t underruns() const { return underruns_.load(std::memory_order_relaxed); }
  uint64_t overruns() const { return overruns_.load(std::memory_order_relaxed); }

  void clear();

 private:
  size_t mask_;
  size_t capacity_;
  int bytesPerFrame_;
  std::vector<uint8_t> data_;
  std::atomic<uint64_t> writeCount_{0};
  std::atomic<uint64_t> readCount_{0};
  std::atomic<uint64_t> underruns_{0};
  std::atomic<uint64_t> overruns_{0};
  // Last frame handed out, for the repeat-on-underrun policy. Only touched by
  // the consumer side.
  std::vector<uint8_t> lastFrame_;
  bool hasLastFrame_ = false;
};

}  // namespace t3k::usb
