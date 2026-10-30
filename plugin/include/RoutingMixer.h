#pragma once
#include "RoutingPlan.h"
#include <array>
#include <atomic>

namespace t3k::routing {
struct StereoSignal {
  std::array<const float*, 2> channel{};
  float sample(int ch, int frame) const noexcept {
    return channel[static_cast<size_t>(ch)] ? channel[static_cast<size_t>(ch)][frame] : 0.0f;
  }
  float mono(int frame) const noexcept {
    const int count = (channel[0] != nullptr) + (channel[1] != nullptr);
    return count ? (sample(0, frame) + sample(1, frame)) / count : 0.0f;
  }
};
struct Signals {
  const float* di = nullptr;
  StereoSignal stack, mod, master, wet, returns, aux;
};

/** Quiescent-thread prepare, allocation-free callback mix. The configuration
    is immutable while processing. Physical channels are already packed by
    AudioDeviceManager; mappings are supplied once after driver readback. */
class RoutingMixer {
public:
  void prepare(const Config&, double rate, const std::array<std::array<int, 2>, portCount>& packed);
  void process(const Signals&, const Signals* cabinetOff, float* const* outputs,
               int outputCount, int frames, bool monitoringAllowed);
  void fadeOut() noexcept { target_.store(0.0f, std::memory_order_relaxed); }
  bool faded() const noexcept { return faded_.load(std::memory_order_relaxed); }
  float peak(Port p) const { return peaks_[static_cast<size_t>(p)].load(); }
  static float sourceSample(Source, const Signals&, int channel, int frame) noexcept;
private:
  struct Filter {
    std::array<float, 5> c{1, 0, 0, 0, 0};
    std::array<float, 2> z1{}, z2{};
    float process(float x, int ch) noexcept;
  };
  struct Route {
    Output output;
    std::array<int, 2> packed{-1, -1};
    std::array<Filter, 6> filters;
    float gain = 1, aux = 0;
    bool pair = false;
  };
  std::array<Route, portCount> routes_;
  std::array<std::atomic<float>, portCount> peaks_{};
  std::atomic<float> target_{1};
  std::atomic<bool> faded_{false};
  float fade_ = 0, step_ = 1;
};
} // namespace t3k::routing
