#include "Transpose.h"

#include <cmath>

// Signalsmith Stretch (MIT) - Geraint Luff / Signalsmith Audio Ltd.
// https://github.com/Signalsmith-Audio/signalsmith-stretch
// Kept out of Transpose.h on purpose: it is a large header-only template
// and the rest of the plugin only needs the wrapper's API.
#include <signalsmith-stretch/signalsmith-stretch.h>

struct Transpose::Impl {
  using Engine = signalsmith::stretch::SignalsmithStretch<float>;
  // The STFT hop as a fraction of the window: 4x overlap is Signalsmith's own
  // preset ratio; it costs CPU, not latency or resolution.
  static constexpr int kOverlap = 4;

  // One engine per window, built in prepare(); a Window change is a swap.
  // Seeded (not std::random_device) so a given input renders identically
  // every run; the engine's randomness only ever enters for time stretches
  // beyond what a fixed 1:1 ratio can reach.
  std::array<Engine, kWindowMs.size()> engines{Engine(1), Engine(2), Engine(3)};
  Engine* engine = &engines[static_cast<size_t>(Window::balanced)];
  juce::AudioBuffer<float> scratch;  // stretch can't run in place

  double sampleRate = 0.0;
  Params params;

  void pushFrequencyMap() {
    const float semitones = static_cast<float>(params.semitones) + params.cents / 100.0f;
    // Signalsmith takes the tonality limit as a fraction of the sample rate.
    const float tonality = params.tonalityHz > 0.0f && sampleRate > 0.0
                               ? static_cast<float>(params.tonalityHz / sampleRate)
                               : 0.0f;
    engine->setTransposeSemitones(semitones, tonality);
  }
};

Transpose::Transpose() : impl_(std::make_unique<Impl>()) {}
Transpose::~Transpose() = default;

void Transpose::prepare(double sampleRate, int maxBlockSamples) {
  auto& s = *impl_;
  s.sampleRate = sampleRate;
  for (size_t w = 0; w < kWindowMs.size(); ++w) {
    const int blockSamples = latencySamples(static_cast<Window>(w), sampleRate);
    s.engines[w].configure(kMaxChannels, blockSamples, blockSamples / Impl::kOverlap);
    // The class promises "latency == window": the STFT is symmetric, so its
    // analysis and synthesis halves sum back to the block.
    jassert(s.engines[w].inputLatency() + s.engines[w].outputLatency() == blockSamples);
  }
  s.engine = &s.engines[static_cast<size_t>(s.params.window)];
  s.scratch.setSize(kMaxChannels, juce::jmax(1, maxBlockSamples));
  s.pushFrequencyMap();
  reset();
}

void Transpose::reset() { impl_->engine->reset(); }

void Transpose::setParams(const Params& p) {
  auto& s = *impl_;
  if (p.window != s.params.window) {
    s.params.window = p.window;
    s.engine = &s.engines[static_cast<size_t>(p.window)];
    s.engine->reset();
    s.pushFrequencyMap();  // the new engine has a stale map
  }
  // Exact compares on purpose: these are parameter values, and a change of
  // any size must reach the engine.
  if (p.semitones != s.params.semitones || !juce::exactlyEqual(p.cents, s.params.cents) ||
      !juce::exactlyEqual(p.tonalityHz, s.params.tonalityHz)) {
    s.params.semitones = p.semitones;
    s.params.cents = p.cents;
    s.params.tonalityHz = p.tonalityHz;
    s.pushFrequencyMap();
  }
}

void Transpose::process(juce::AudioBuffer<float>& buffer) {
  auto& s = *impl_;
  const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);
  const int numSamples = buffer.getNumSamples();
  if (s.sampleRate <= 0.0 || numChannels <= 0 || numSamples <= 0) return;

  // The engine is always stereo-configured; a mono buffer feeds both lanes
  // and only its own lane is read back, so no pointer ever leaves the buffer.
  // Blocks larger than the scratch run in slices, so this never allocates.
  const int slice = s.scratch.getNumSamples();
  for (int offset = 0; offset < numSamples; offset += slice) {
    const int n = juce::jmin(slice, numSamples - offset);
    const float* in[kMaxChannels];
    for (int ch = 0; ch < kMaxChannels; ++ch)
      in[ch] = buffer.getReadPointer(juce::jmin(ch, numChannels - 1), offset);
    s.engine->process(in, n, s.scratch.getArrayOfWritePointers(), n);
    for (int ch = 0; ch < numChannels; ++ch) buffer.copyFrom(ch, offset, s.scratch, ch, 0, n);
  }
}

int Transpose::latencySamples() const {
  return impl_->engine->inputLatency() + impl_->engine->outputLatency();
}
