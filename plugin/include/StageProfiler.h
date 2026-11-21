#pragma once
// Audio-thread stage profiler, compiled in only with -DT3K_PROFILE=ON and
// recording only when $T3K_PROFILE_OUT names a file. T3K_PROFILE_BLOCK opens
// one record per processBlock, T3K_PROFILE_STAGE times one stage inside it on
// whichever thread runs it (the audio thread or a DSP worker). Each thread
// pushes into its own preallocated SPSC ring (dropping records when it is
// full) and a background thread drains the rings to CSV, so the audio thread
// never locks or allocates. Read the CSV with script/profile-report.py.

#if T3K_PROFILE

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace t3k::prof {

struct Record {
  const char* stage;  // nullptr for the block record
  uint32_t block;
  uint32_t durNs;
  uint32_t frames;
  uint32_t deadlineNs;
  int index;
};

constexpr uint32_t kRingSize = 1u << 14;

struct Slot {
  std::atomic<bool> claimed{false};
  alignas(64) std::atomic<uint32_t> head{0};
  alignas(64) std::atomic<uint32_t> tail{0};
  std::array<Record, kRingSize> ring;
};

inline Slot gSlots[64];
inline std::atomic<uint32_t> gBlock{0};
inline const bool gEnabled = std::getenv("T3K_PROFILE_OUT") != nullptr;

inline void push(const Record& r) noexcept {
  thread_local Slot* slot = [] {
    for (auto& s : gSlots)
      if (bool expected = false; gEnabled && s.claimed.compare_exchange_strong(expected, true))
        return &s;
    return static_cast<Slot*>(nullptr);
  }();
  if (slot == nullptr)
    return;
  const uint32_t h = slot->head.load(std::memory_order_relaxed);
  if (h - slot->tail.load(std::memory_order_acquire) >= kRingSize)
    return;
  slot->ring[h & (kRingSize - 1)] = r;
  slot->head.store(h + 1, std::memory_order_release);
}

using Clock = std::chrono::steady_clock;

inline uint32_t elapsedNs(Clock::time_point start) noexcept {
  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
}

class Scope {
public:
  explicit Scope(const char* name, int idx = -1) noexcept : stage(name), index(idx) {}
  ~Scope() { push({stage, gBlock.load(std::memory_order_relaxed), elapsedNs(start), 0, 0, index}); }

private:
  const char* stage;
  int index;
  Clock::time_point start = Clock::now();
};

class BlockScope {
public:
  BlockScope(int numSamples, double sampleRate) noexcept
      : frames(static_cast<uint32_t>(numSamples)),
        deadlineNs(static_cast<uint32_t>(1.0e9 * numSamples / sampleRate)),
        block(gBlock.fetch_add(1, std::memory_order_relaxed) + 1) {}
  ~BlockScope() { push({nullptr, block, elapsedNs(start), frames, deadlineNs, -1}); }

private:
  uint32_t frames;
  uint32_t deadlineNs;
  uint32_t block;
  Clock::time_point start = Clock::now();
};

class Drain {
public:
  Drain() {
    const char* path = std::getenv("T3K_PROFILE_OUT");
    if (path == nullptr || (file = std::fopen(path, "w")) == nullptr)
      return;
    worker = std::thread([this] {
      while (!stop.load()) {
        drainAll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      drainAll();
    });
  }
  ~Drain() {
    if (file == nullptr)
      return;
    stop.store(true);
    worker.join();
    std::fclose(file);
  }

private:
  void drainAll() {
    for (auto& s : gSlots) {
      const uint32_t h = s.head.load(std::memory_order_acquire);
      for (uint32_t t = s.tail.load(std::memory_order_relaxed); t != h; ++t) {
        const Record& r = s.ring[t & (kRingSize - 1)];
        if (r.stage == nullptr)
          std::fprintf(file, "B,%u,%u,%u,%u\n", r.block, r.durNs, r.frames, r.deadlineNs);
        else
          std::fprintf(file, "S,%u,%s,%d,%u\n", r.block, r.stage, r.index, r.durNs);
      }
      s.tail.store(h, std::memory_order_release);
    }
    std::fflush(file);
  }

  std::FILE* file = nullptr;
  std::atomic<bool> stop{false};
  std::thread worker;
};

inline Drain gDrain;

}  // namespace t3k::prof

#define T3K_PROF_CAT_(a, b) a##b
#define T3K_PROF_CAT(a, b) T3K_PROF_CAT_(a, b)
#define T3K_PROFILE_STAGE(...) \
  const ::t3k::prof::Scope T3K_PROF_CAT(t3kStage, __LINE__)(__VA_ARGS__)
#define T3K_PROFILE_BLOCK(numSamples, sampleRate) \
  const ::t3k::prof::BlockScope t3kBlockScope(numSamples, sampleRate)

#else

#define T3K_PROFILE_STAGE(...)
#define T3K_PROFILE_BLOCK(numSamples, sampleRate)

#endif
