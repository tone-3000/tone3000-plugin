#pragma once
#include <juce_dsp/juce_dsp.h>
#include "ChainDomain.h"
#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>
#if JUCE_MAC
#include <dispatch/dispatch.h>
#else
#include <semaphore>
#endif

// A long IR's tail, convolved on its own thread and mixed back kIrTailSplit samples late.
constexpr int kIrTailSplit = 1024;
constexpr int kIrTailChunk = 128;

// std::counting_semaphore needs macOS 11; the build targets 10.15.
#if JUCE_MAC
struct IrTailWake {
  dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
  ~IrTailWake() { dispatch_release(semaphore); }
  void post() noexcept { dispatch_semaphore_signal(semaphore); }
  void wait() noexcept { dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER); }
};
#else
struct IrTailWake {
  std::counting_semaphore<> semaphore{0};
  void post() noexcept { semaphore.release(); }
  void wait() noexcept { semaphore.acquire(); }
};
#endif

class IrTail : private juce::Thread {
public:
  static constexpr int kRing = 16384;
  static constexpr int kMaxThreadedBlock = kIrTailSplit / 2;

  explicit IrTail(std::unique_ptr<juce::dsp::Convolution> tailConvolver)
      : juce::Thread("TONE3000 IR Tail"), convolver(std::move(tailConvolver)) {
    output.clear();
    if (!startRealtimeThread(RealtimeOptions{}.withPriority(5).withMaximumProcessingTimeMs(10.0)))
      startThread(Priority::high);
  }

  ~IrTail() override {
    signalThreadShouldExit();
    wake.post();
    stopThread(2000);
  }

  void reset() {
    claim(true);
    convolver->reset();
    output.clear();
    written.store(0, std::memory_order_relaxed);
    mixed.store(0, std::memory_order_relaxed);
    consumed = 0;
    ready.store(kIrTailSplit, std::memory_order_relaxed);
    release();
  }

  void push(const juce::dsp::AudioBlock<float>& block, bool threaded) noexcept {
    threadedTail.store(threaded, std::memory_order_relaxed);
    const int n = static_cast<int>(block.getNumSamples());
    const juce::int64 w = written.load(std::memory_order_relaxed);
    channels.store(static_cast<int>(block.getNumChannels()), std::memory_order_relaxed);
    for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
      forRing(w, n, [&](int at, int off, int count) {
        juce::FloatVectorOperations::copy(input.getWritePointer(int(ch), at),
                                          block.getChannelPointer(ch) + off, count);
      });
    written.store(w + n);
    if (threaded && w % kIrTailChunk + n >= kIrTailChunk && sleeping.exchange(false))
      wake.post();
  }

  void addTo(const juce::dsp::AudioBlock<float>& block, bool offline) noexcept {
    const int n = static_cast<int>(block.getNumSamples());
    const juce::int64 r = mixed.load(std::memory_order_relaxed);
    if (ready.load(std::memory_order_acquire) < r + n && claim(offline)) {
      while (processChunk()) {
      }
      release();
    }
    const int avail = static_cast<int>(
        juce::jlimit<juce::int64>(0, n, ready.load(std::memory_order_acquire) - r));
    for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
      forRing(r, avail, [&](int at, int off, int count) {
        juce::FloatVectorOperations::add(block.getChannelPointer(ch) + off,
                                         output.getReadPointer(int(std::min<size_t>(ch, 1)), at),
                                         count);
      });
    mixed.store(r + n, std::memory_order_relaxed);
    if (avail < n)
      missed.fetch_add(n - avail, std::memory_order_relaxed);
  }

private:
  void run() override {
    juce::ScopedNoDenormals noDenormals;
    juce::int64 polledWritten = 0;
    double polledAt = 0.0, burst = 0.0;
    while (!threadShouldExit()) {
      for (;;) {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const juce::int64 w = written.load(std::memory_order_relaxed);
        // Above real time (prefetch, catch-up) audio could overtake a chunk in flight here.
        const double gap = now - polledAt;
        burst =
            gap > 20.0
                ? 2.0 * kMaxThreadedBlock
                : std::clamp(burst + double(w - polledWritten) - gap * kChainBaseSampleRate / 500,
                             0.0, 2.0 * kMaxThreadedBlock);
        polledWritten = w;
        polledAt = now;
        if (burst > kMaxThreadedBlock || !threadedTail.load(std::memory_order_relaxed) ||
            ready.load(std::memory_order_acquire) - mixed.load(std::memory_order_relaxed) <
                2 * kIrTailChunk ||
            !claim(false))
          break;
        const bool more = processChunk();
        release();
        if (!more)
          break;
      }
      sleeping.store(true);
      if (written.load() / kIrTailChunk == polledWritten / kIrTailChunk && !threadShouldExit())
        wake.wait();
      sleeping.store(false);
    }
  }

  template <typename Fn>
  static void forRing(juce::int64 pos, int n, Fn&& fn) {
    const int at = static_cast<int>(pos & (kRing - 1));
    const int first = std::min(n, kRing - at);
    if (first > 0)
      fn(at, 0, first);
    if (n > first)
      fn(0, first, n - first);
  }

  bool claim(bool wait) noexcept {
    while (busy.exchange(true, std::memory_order_acquire))
      if (!wait)
        return false;
      else
        std::this_thread::yield();
    return true;
  }

  void release() noexcept { busy.store(false, std::memory_order_release); }

  bool processChunk() noexcept {
    const juce::int64 w = written.load(std::memory_order_acquire);
    if (w - consumed > kRing / 2) {
      convolver->reset();
      const juce::int64 from =
          std::max(ready.load(std::memory_order_relaxed), w + kIrTailSplit - kRing);
      forRing(from, int(w + kIrTailSplit - from),
              [&](int at, int, int count) { output.clear(at, count); });
      consumed = w;
      ready.store(w + kIrTailSplit, std::memory_order_release);
    }
    if (w - consumed < kIrTailChunk)
      return false;
    const int numChannels = channels.load(std::memory_order_relaxed);
    for (int ch = 0; ch < numChannels; ++ch)
      forRing(consumed, kIrTailChunk,
              [&](int at, int off, int count) { chunk.copyFrom(ch, off, input, ch, at, count); });
    juce::dsp::AudioBlock<float> block(chunk.getArrayOfWritePointers(), size_t(numChannels),
                                       size_t(kIrTailChunk));
    convolver->process(juce::dsp::ProcessContextReplacing<float>(block));
    for (int ch = 0; ch < numChannels; ++ch)
      forRing(consumed + kIrTailSplit, kIrTailChunk,
              [&](int at, int off, int count) { output.copyFrom(ch, at, chunk, ch, off, count); });
    consumed += kIrTailChunk;
    ready.store(consumed + kIrTailSplit, std::memory_order_release);
    return true;
  }

  std::unique_ptr<juce::dsp::Convolution> convolver;
  juce::AudioBuffer<float> input{2, kRing}, output{2, kRing}, chunk{2, kIrTailChunk};
  std::atomic<juce::int64> written{0}, mixed{0}, ready{kIrTailSplit}, missed{0};
  std::atomic<int> channels{2};
  std::atomic<bool> busy{false}, threadedTail{false}, sleeping{false};
  IrTailWake wake;
  juce::int64 consumed = 0;
};
