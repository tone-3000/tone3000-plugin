#include "RoutingMixer.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace t3k::routing {
float RoutingMixer::Filter::process(float x, int ch) noexcept {
  const auto i = static_cast<size_t>(ch);
  if (!std::isfinite(x)) x = 0;
  const float y = c[0] * x + z1[i];
  z1[i] = c[1] * x - c[3] * y + z2[i];
  z2[i] = c[2] * x - c[4] * y;
  if (!std::isfinite(y) || !std::isfinite(z1[i]) || !std::isfinite(z2[i])) {
    z1[i] = z2[i] = 0; return 0;
  }
  return y;
}
void RoutingMixer::prepare(const Config& c, double rate,
                         const std::array<std::array<int, 2>, portCount>& packed) {
  const float ceiling = static_cast<float>(rate * 0.45);
  using Coeff = juce::dsp::IIR::ArrayCoefficients<float>;
  auto hz = [ceiling](float value) { return juce::jlimit(10.0f, ceiling, value); };
  for (size_t i = 0; i < portCount; ++i) {
    auto& r = routes_[i]; r = {}; r.output = c.outputs[i]; r.packed = packed[i];
    const auto& o = r.output;
    r.gain = juce::Decibels::decibelsToGain(o.volumeDb + (o.linkMaster ? c.masterVolumeDb : 0) - (o.pad12 ? 12 : 0));
    const int aux = i == 0 ? 0 : i == 1 ? 1 : i == 3 ? 2 : -1;
    r.aux = aux < 0 ? 0 : c.auxMix[static_cast<size_t>(aux)];
    r.pair = (stereoSource(o.source) && o.stereo) || (o.source >= Source::diStack && o.source <= Source::diMasterMono);
    auto set = [&](int f, const std::array<float, 6>& a) {
      r.filters[static_cast<size_t>(f)].c = {a[0] / a[3], a[1] / a[3], a[2] / a[3], a[4] / a[3], a[5] / a[3]};
    };
    set(0, Coeff::makeLowShelf(rate, hz(120), 0.7071f, juce::Decibels::decibelsToGain(o.bassDb)));
    set(1, Coeff::makePeakFilter(rate, hz(750), 0.7f, juce::Decibels::decibelsToGain(o.middleDb)));
    set(2, Coeff::makeHighShelf(rate, hz(3000), 0.7071f, juce::Decibels::decibelsToGain(o.trebleDb)));
    set(3, Coeff::makePeakFilter(rate, hz(5000), 0.7f, juce::Decibels::decibelsToGain(o.presenceDb)));
    if (o.lowCutHz > 0) set(4, Coeff::makeHighPass(rate, hz(o.lowCutHz)));
    if (o.highCutHz > 0) set(5, Coeff::makeLowPass(rate, hz(o.highCutHz)));
    peaks_[i].store(0);
  }
  fade_ = 0; step_ = 1.0f / static_cast<float>(std::max(1.0, rate * 0.01));
  target_.store(1); faded_.store(false);
}
float RoutingMixer::sourceSample(Source source, const Signals& s, int ch, int f) noexcept {
  const float di = s.di ? s.di[f] : 0;
  switch (source) {
    case Source::off: return 0;
    case Source::di: return di;
    case Source::stack: return s.stack.mono(f);
    case Source::modLeft: return s.mod.sample(0, f);
    case Source::modRight: return s.mod.sample(1, f);
    case Source::modMono: return s.mod.mono(f);
    case Source::modStereo: return s.mod.sample(ch, f);
    case Source::masterLeft: return s.master.sample(0, f);
    case Source::masterRight: return s.master.sample(1, f);
    case Source::masterMono: return s.master.mono(f);
    case Source::masterStereo: return s.master.sample(ch, f);
    case Source::wetFx: return s.wet.sample(ch, f);
    case Source::diStack: return ch == 0 ? di : s.stack.mono(f);
    case Source::diModLeft: return ch == 0 ? di : s.mod.sample(0, f);
    case Source::diModMono: return ch == 0 ? di : s.mod.mono(f);
    case Source::diMasterLeft: return ch == 0 ? di : s.master.sample(0, f);
    case Source::diMasterMono: return ch == 0 ? di : s.master.mono(f);
    case Source::returns: return s.returns.sample(ch, f);
  }
  return 0;
}
void RoutingMixer::process(const Signals& signals, const Signals* cabinetOff,
                          float* const* outputs, int outputCount, int frames,
                          bool allowed) {
  std::array<float, portCount> peak{};
  for (int f = 0; f < frames; ++f) {
    const float target = target_.load(std::memory_order_relaxed);
    fade_ += juce::jlimit(-step_, step_, target - fade_);
    for (size_t i = 0; i < portCount; ++i) {
      auto& r = routes_[i]; const auto& o = r.output;
      if (o.source == Source::off) continue;
      // An unavailable cabinet-free path is silent, never the cabinet-on path.
      if (o.cabinetOff && cabinetOff == nullptr) continue;
      const auto& s = o.cabinetOff ? *cabinetOff : signals;
      for (int ch = 0; ch < (r.pair ? 2 : 1); ++ch) {
        const int out = r.packed[static_cast<size_t>(ch)];
        if (out < 0 || out >= outputCount || outputs[out] == nullptr) continue;
        float x = sourceSample(o.source, s, ch, f);
        if (!r.pair) {
          if (o.source == Source::returns) x = s.returns.mono(f);
          else if (o.source == Source::modStereo) x = s.mod.mono(f);
          else if (o.source == Source::masterStereo) x = s.master.mono(f);
          else if (o.source == Source::wetFx) x = s.wet.mono(f);
        }
        x += r.aux * (r.pair ? s.aux.sample(ch, f) : s.aux.mono(f));
        for (auto& filter : r.filters) x = filter.process(x, ch);
        x *= allowed ? r.gain * fade_ : 0;
        if (!std::isfinite(x)) x = 0;
        outputs[out][f] += x;
        peak[i] = std::max(peak[i], std::abs(x));
      }
    }
  }
  for (size_t i = 0; i < portCount; ++i) peaks_[i].store(peak[i], std::memory_order_relaxed);
  faded_.store(fade_ <= 0.000001f, std::memory_order_relaxed);
}
} // namespace t3k::routing
