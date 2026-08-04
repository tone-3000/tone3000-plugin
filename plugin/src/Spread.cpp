#include "Spread.h"
#include <cmath>

void Spread::prepare(double newSampleRate, int maxBlockSize) {
  sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
  msToSamples = static_cast<float>(sampleRate * 0.001);
  lagGain = juce::Decibels::decibelsToGain(kLagGainDb);

  const juce::dsp::ProcessSpec spec{sampleRate,
                                    static_cast<juce::uint32>(juce::jmax(1, maxBlockSize)), 1};

  crossover.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
  crossover.setCutoffFrequency(static_cast<float>(kCrossoverHz));
  crossover.prepare(spec);

  const int maxDelaySamples = static_cast<int>(std::ceil(
      (SpreadParams::kMaxOffsetMs + kWobbleMaxMs) * 0.001 * sampleRate)) + 8;
  delayLine.setMaximumDelayInSamples(maxDelaySamples);
  delayLine.prepare(spec);

  for (int i = 0; i < kNumAllpasses; ++i) {
    const double fc = kAllpassLowHz * std::pow(kAllpassHighHz / kAllpassLowHz,
                                               static_cast<double>(i) / (kNumAllpasses - 1));
    const double t = std::tan(juce::MathConstants<double>::pi * fc / sampleRate);
    allpasses[static_cast<size_t>(i)].a = static_cast<float>((t - 1.0) / (t + 1.0));
  }

  offsetCoeff = 1.0f - std::exp(static_cast<float>(-1.0 / (kOffsetSmoothSeconds * sampleRate)));
  wobbleCoeff = 1.0f - std::exp(static_cast<float>(
      -juce::MathConstants<double>::twoPi * kWobbleRateHz / sampleRate));

  // Wobble normalization, analytic. The noise shaper is two cascaded
  // one-poles with coefficient k (see Spread.h for why two); its impulse
  // response is h[n] = k²(n+1)aⁿ with a = 1-k, so the steady-state output
  // variance for unit-variance input is Σh² = k⁴(1+a²)/(1-a²)³. Uniform
  // [-1,1] noise has σ² = 1/3; scale so 3σ reaches the ±1 clamp. Only then
  // does the depth knob actually span the full ±kWobbleMaxMs at any sample
  // rate. (The spec pseudocode's fixed wobNorm = 3.0 "tune once" placeholder
  // was ~100× too small, which made the wobble inaudible.)
  const double k = wobbleCoeff, a = 1.0 - k;
  const double gainSq = k * k * k * k * (1.0 + a * a) /
                        ((1.0 - a * a) * (1.0 - a * a) * (1.0 - a * a));
  const double wobbleSigma = std::sqrt(gainSq / 3.0);
  wobbleNorm = static_cast<float>(1.0 / (3.0 * wobbleSigma));

  wobbleDepth.reset(sampleRate, kFadeSeconds);
  wetGain.reset(sampleRate, kFadeSeconds);

  running = false;
  engaged = false;
  resetDeck();
}

void Spread::resetDeck() {
  crossover.reset();
  delayLine.reset();
  for (auto& ap : allpasses) ap.z = 0.0f;
  wobbleState1 = wobbleState2 = 0.0f;
  corrLR = corrLL = corrRR = 0.0f;
  correlationOut.store(1.0f, std::memory_order_relaxed);
}

void Spread::setTarget(const SpreadParams& params, bool nowEngaged) {
  if (!running) {
    if (!nowEngaged)
      return;  // idle and staying idle
    // Engage from idle: clean deck, offset primed at the knob (no glide up
    // from a stale value; the wet fade-in covers the start), fade from dry.
    resetDeck();
    offsetStateMs = params.offsetMs;
    wobbleDepth.setCurrentAndTargetValue(params.wobbleDepth);
    wetGain.setCurrentAndTargetValue(0.0f);
    running = true;
  }

  engaged = nowEngaged;
  targetOffsetMs = params.offsetMs;
  wobbleDepth.setTargetValue(params.wobbleDepth);
  wetGain.setTargetValue(engaged ? 1.0f : 0.0f);
}

void Spread::forceIdle() {
  running = false;
  engaged = false;
  correlationOut.store(1.0f, std::memory_order_relaxed);
}

void Spread::process(juce::AudioBuffer<float>& buffer) {
  if (!running || buffer.getNumChannels() < 2)
    return;

  const int numSamples = buffer.getNumSamples();
  auto* l = buffer.getWritePointer(0);
  auto* r = buffer.getWritePointer(1);

  // Correlation block sums (means folded into the followers after the loop).
  float sumLR = 0.0f, sumLL = 0.0f, sumRR = 0.0f;

  for (int i = 0; i < numSamples; ++i) {
    // The deck is seeded from channel 0 (the mono chain output). With a true
    // stereo source in mono chain mode the channels differ, so each bypass
    // crossfade endpoint is that channel's own untouched signal; the fade
    // must land exactly on the input, never hard-copy ch0 onto ch1.
    const float xl = l[i];
    const float xr = r[i];

    float low = 0.0f, high = 0.0f;
    crossover.processSample(0, xl, low, high);

    delayLine.pushSample(0, high);

    // Wobble: white noise through two cascaded 0.3 Hz one-poles, a random
    // walk with no audio-rate residue (see Spread.h).
    wobbleState1 += wobbleCoeff * (random.nextFloat() * 2.0f - 1.0f - wobbleState1);
    wobbleState2 += wobbleCoeff * (wobbleState1 - wobbleState2);
    const float wobbleMs = kWobbleMaxMs * wobbleDepth.getNextValue() *
                           juce::jlimit(-1.0f, 1.0f, wobbleState2 * wobbleNorm);

    // Smooth the SIGNED offset; side and magnitude derive from the result so
    // zero-crossings pass through identity (spec routing note).
    offsetStateMs += offsetCoeff * (targetOffsetMs - offsetStateMs);
    const float tMs = std::abs(offsetStateMs);
    const bool lagOnR = offsetStateMs >= 0.0f;

    const float delayMs = juce::jlimit(0.0f, SpreadParams::kMaxOffsetMs + kWobbleMaxMs,
                                       tMs + wobbleMs);
    float lag = delayLine.popSample(0, delayMs * msToSamples);

    for (auto& ap : allpasses)
      lag = ap.process(lag);
    lag *= lagGain;

    // Center-identity blend: below kCenterBlendMs the lag path converges to
    // the dry high band, so the detent is exactly dual-mono (see header).
    const float blend = juce::jmin(tMs * (1.0f / kCenterBlendMs), 1.0f);
    lag = high + (lag - high) * blend;

    const float refOut = low + high;  // LR4 recombination: allpass-flat
    const float lagOut = low + lag;

    const float wet = wetGain.getNextValue();
    const float outL = xl + ((lagOnR ? refOut : lagOut) - xl) * wet;
    const float outR = xr + ((lagOnR ? lagOut : refOut) - xr) * wet;
    l[i] = outL;
    r[i] = outR;

    sumLR += outL * outR;
    sumLL += outL * outL;
    sumRR += outR * outR;
  }

  // ~300 ms one-pole over block means, then publish the normalized value.
  const float norm = 1.0f / static_cast<float>(numSamples);
  const float corrCoeff = 1.0f - std::exp(static_cast<float>(-numSamples / (kCorrSeconds * sampleRate)));
  corrLR += corrCoeff * (sumLR * norm - corrLR);
  corrLL += corrCoeff * (sumLL * norm - corrLL);
  corrRR += corrCoeff * (sumRR * norm - corrRR);
  const float energy = corrLL * corrRR;
  correlationOut.store(energy > kCorrFloor * kCorrFloor
                           ? juce::jlimit(-1.0f, 1.0f, corrLR / std::sqrt(energy))
                           : 1.0f,
                       std::memory_order_relaxed);

  // Disengage completes once the fade-out lands: output equals input again.
  if (!engaged && !wetGain.isSmoothing() && wetGain.getCurrentValue() <= 0.0f)
    forceIdle();
}
