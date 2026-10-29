// L0: AudioRing wrap and underrun/overrun semantics (plan M2.2 policy).

#include <cstdint>
#include <numeric>
#include <thread>
#include <vector>

#include "AudioRing.h"
#include "gtest/gtest.h"

namespace t3k::usb {
namespace {

// Frames carry a 4-byte sequence number so a read can prove ordering.
constexpr int kBpf = 4;

std::vector<uint8_t> seqFrames(uint64_t first, size_t count) {
  std::vector<uint8_t> out(count * kBpf);
  for (size_t i = 0; i < count; ++i) {
    const uint32_t v = static_cast<uint32_t>(first + i);
    out[i * 4 + 0] = static_cast<uint8_t>(v);
    out[i * 4 + 1] = static_cast<uint8_t>(v >> 8);
    out[i * 4 + 2] = static_cast<uint8_t>(v >> 16);
    out[i * 4 + 3] = static_cast<uint8_t>(v >> 24);
  }
  return out;
}

uint32_t frameValue(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

TEST(AudioRing, CapacityRoundsUpToPowerOfTwo) {
  AudioRing ring(100, kBpf);
  EXPECT_EQ(ring.capacityFrames(), 128u);
  AudioRing pow2(64, kBpf);
  EXPECT_EQ(pow2.capacityFrames(), 64u);
}

TEST(AudioRing, WriteReadSingleBlock) {
  AudioRing ring(16, kBpf);
  auto in = seqFrames(0, 8);
  EXPECT_EQ(ring.write(in.data(), 8), 8u);
  EXPECT_EQ(ring.readAvailable(), 8u);
  std::vector<uint8_t> out(8 * kBpf);
  EXPECT_EQ(ring.read(out.data(), 8), 8u);
  for (size_t i = 0; i < 8; ++i) EXPECT_EQ(frameValue(&out[i * 4]), i);
  EXPECT_EQ(ring.readAvailable(), 0u);
}

TEST(AudioRing, WrapAroundPreservesOrder) {
  AudioRing ring(8, kBpf);
  std::vector<uint8_t> out(3 * kBpf);
  uint64_t next = 0;
  // Drive 10 rounds of write-3/read-3 so the indices wrap many times.
  for (int round = 0; round < 10; ++round) {
    auto in = seqFrames(next, 3);
    ASSERT_EQ(ring.write(in.data(), 3), 3u);
    ASSERT_EQ(ring.read(out.data(), 3), 3u);
    for (size_t i = 0; i < 3; ++i) EXPECT_EQ(frameValue(&out[i * 4]), next + i);
    next += 3;
  }
}

TEST(AudioRing, StrictWriteReportsShortfall) {
  AudioRing ring(8, kBpf);
  auto in = seqFrames(0, 10);
  EXPECT_EQ(ring.write(in.data(), 10), 8u);  // capacity 8, no silent drop
  auto more = seqFrames(100, 1);
  EXPECT_EQ(ring.write(more.data(), 1), 0u);
}

TEST(AudioRing, ReadOrRepeatFillsUnderrunWithLastFrame) {
  AudioRing ring(8, kBpf);
  auto in = seqFrames(7, 2);
  ring.write(in.data(), 2);
  std::vector<uint8_t> out(5 * kBpf);
  EXPECT_EQ(ring.readOrRepeat(out.data(), 5), 5u);
  // First two frames are real (7, 8); the underrun repeats frame 8.
  EXPECT_EQ(frameValue(&out[0 * 4]), 7u);
  EXPECT_EQ(frameValue(&out[1 * 4]), 8u);
  for (size_t i = 2; i < 5; ++i) EXPECT_EQ(frameValue(&out[i * 4]), 8u);
  EXPECT_EQ(ring.underruns(), 1u);
}

TEST(AudioRing, ReadOrRepeatWithoutHistoryEmitsSilence) {
  AudioRing ring(8, kBpf);
  std::vector<uint8_t> out(2 * kBpf, 0xFF);
  EXPECT_EQ(ring.readOrRepeat(out.data(), 2), 2u);
  EXPECT_EQ(frameValue(&out[0]), 0u);
  EXPECT_EQ(frameValue(&out[4]), 0u);
}

TEST(AudioRing, DropOldestKeepsFreshestAndCountsOverrun) {
  AudioRing ring(8, kBpf);
  auto first = seqFrames(0, 8);   // fills the ring
  auto second = seqFrames(100, 8);  // laps it entirely
  ring.write(first.data(), 8);
  ring.writeDropOldest(second.data(), 8);
  std::vector<uint8_t> out(8 * kBpf);
  EXPECT_EQ(ring.read(out.data(), 8), 8u);
  for (size_t i = 0; i < 8; ++i) EXPECT_EQ(frameValue(&out[i * 4]), 100 + i);
  EXPECT_EQ(ring.overruns(), 1u);
}

TEST(AudioRing, PartialDropOldestSkipsOnlyTheLappedFrames) {
  AudioRing ring(8, kBpf);
  auto first = seqFrames(0, 8);
  auto second = seqFrames(100, 4);
  ring.write(first.data(), 8);
  ring.writeDropOldest(second.data(), 4);  // laps 4 of the 8
  std::vector<uint8_t> out(8 * kBpf);
  EXPECT_EQ(ring.read(out.data(), 8), 8u);
  // Frames 4..7 survived, then the 4 fresh ones.
  EXPECT_EQ(frameValue(&out[0 * 4]), 4u);
  EXPECT_EQ(frameValue(&out[3 * 4]), 7u);
  EXPECT_EQ(frameValue(&out[4 * 4]), 100u);
  EXPECT_EQ(frameValue(&out[7 * 4]), 103u);
  EXPECT_EQ(ring.overruns(), 1u);
}

TEST(AudioRing, ProducerConsumerStressIsConsistent) {
  // Cheap cross-thread smoke; the full 10^8-frame run is below. Each frame
  // carries its own index, so any lost or duplicated frame shows up.
  AudioRing ring(256, kBpf);
  constexpr uint64_t kFrames = 200000;
  std::thread producer([&] {
    for (uint64_t v = 0; v < kFrames;) {
      auto in = seqFrames(v, 1);
      if (ring.write(in.data(), 1) == 1) ++v;
    }
  });
  uint64_t expected = 0;
  std::thread consumer([&] {
    std::vector<uint8_t> out(kBpf);
    while (expected < kFrames) {
      if (ring.read(out.data(), 1) == 1) {
        ASSERT_EQ(frameValue(out.data()), expected);
        ++expected;
      }
    }
  });
  producer.join();
  consumer.join();
  EXPECT_EQ(expected, kFrames);
  EXPECT_EQ(ring.underruns(), 0u);
  EXPECT_EQ(ring.overruns(), 0u);
}

TEST(AudioRing, TenToTheEighthFramesStress) {
  // Plan M2.2's long stress: 10^8 frames through a lock-free SPSC ring with
  // a real producer and consumer thread. Every frame is checked, so a lost,
  // duplicated or torn frame fails the test.
  constexpr uint64_t kTotal = 100000000ull;
  constexpr size_t kBlock = 256;
  AudioRing ring(1024, kBpf);
  std::atomic<bool> failed{false};
  uint64_t consumed = 0;

  std::thread producer([&] {
    std::vector<uint8_t> block(kBlock * kBpf);
    uint64_t sent = 0;
    while (sent < kTotal) {
      const size_t n = static_cast<size_t>(std::min<uint64_t>(kBlock, kTotal - sent));
      for (size_t i = 0; i < n; ++i) {
        const uint32_t v = static_cast<uint32_t>(sent + i);
        block[i * 4 + 0] = static_cast<uint8_t>(v);
        block[i * 4 + 1] = static_cast<uint8_t>(v >> 8);
        block[i * 4 + 2] = static_cast<uint8_t>(v >> 16);
        block[i * 4 + 3] = static_cast<uint8_t>(v >> 24);
      }
      sent += ring.write(block.data(), n);
    }
  });

  std::thread consumer([&] {
    std::vector<uint8_t> out(kBlock * kBpf);
    while (consumed < kTotal) {
      const size_t want = static_cast<size_t>(std::min<uint64_t>(kBlock, kTotal - consumed));
      const size_t n = ring.read(out.data(), want);
      if (!failed.load(std::memory_order_relaxed)) {
        for (size_t i = 0; i < n; ++i) {
          if (frameValue(&out[i * 4]) != static_cast<uint32_t>(consumed + i)) {
            failed.store(true, std::memory_order_relaxed);
            break;
          }
        }
      }
      consumed += n;
    }
  });

  producer.join();
  consumer.join();
  EXPECT_FALSE(failed.load());
  EXPECT_EQ(consumed, kTotal);
  EXPECT_EQ(ring.underruns(), 0u);
  EXPECT_EQ(ring.overruns(), 0u);
}

}  // namespace
}  // namespace t3k::usb
