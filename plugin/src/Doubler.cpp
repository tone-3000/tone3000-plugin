#include "Doubler.h"
#include <cmath>

namespace {
// One-pole coefficient for a given time constant.
float envCoeff(double sampleRate, float ms) {
  return std::exp(static_cast<float>(-1.0 / (sampleRate * ms * 0.001)));
}
}  // namespace

void Doubler::prepare(double newSampleRate, int maxBlockSize) {
  sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

  const int maxDelaySamples =
      static_cast<int>(std::ceil((kMaxSpreadMs + kMaxJitterMs) * 0.001 * sampleRate)) + 8;
  delayLine.setMaximumDelayInSamples(maxDelaySamples);
  delayLine.prepare({sampleRate, static_cast<juce::uint32>(juce::jmax(1, maxBlockSize)), 1});

  // ~15 ms glide between delay targets: fast enough to feel per-note, slow
  // enough that the interpolated delay reads as a subtle bend, not a click.
  delaySamples.reset(sampleRate, 0.015);
  delaySamples.setCurrentAndTargetValue(
      static_cast<float>(spreadMs * 0.001 * sampleRate));

  fastAttack = envCoeff(sampleRate, 1.0f);
  fastRelease = envCoeff(sampleRate, 60.0f);
  slowCoeff = envCoeff(sampleRate, 250.0f);
  refractorySamples = static_cast<int>(0.08 * sampleRate);  // 80 ms per-note hold

  reset();
}

void Doubler::reset() {
  delayLine.reset();
  envFast = envSlow = 0.0f;
  refractorySamplesLeft = 0;
  jitterOffsetMs = 0.0f;
  delaySamples.setCurrentAndTargetValue(
      static_cast<float>(spreadMs * 0.001 * sampleRate));
}

void Doubler::setParams(float spreadNorm, float jitterNorm) {
  spreadMs = juce::jlimit(0.0f, 1.0f, spreadNorm) * kMaxSpreadMs;
  jitterMs = juce::jlimit(0.0f, 1.0f, jitterNorm) * kMaxJitterMs;
  // Track spread edits immediately (knob drags shouldn't wait for the next
  // onset); the current jitter offset stays until a new note re-rolls it.
  retargetDelay();
}

void Doubler::retargetDelay() {
  const float totalMs =
      juce::jlimit(0.0f, kMaxSpreadMs + kMaxJitterMs, spreadMs + jitterOffsetMs);
  delaySamples.setTargetValue(static_cast<float>(totalMs * 0.001 * sampleRate));
}

void Doubler::process(juce::AudioBuffer<float>& buffer, int sourceChannel) {
  if (buffer.getNumChannels() < 2)
    return;

  const int numSamples = buffer.getNumSamples();
  // src may alias dst (in-place stereo offset); each sample is read before
  // its slot is overwritten, so the aliasing is safe.
  const float* src = buffer.getReadPointer(juce::jlimit(0, 1, sourceChannel));
  float* dst = buffer.getWritePointer(1);

  for (int i = 0; i < numSamples; ++i) {
    const float x = src[i];
    const float mag = std::abs(x);

    // Envelope followers (branchless one-poles except the attack/release pick).
    const float fastCoeff = mag > envFast ? fastAttack : fastRelease;
    envFast = fastCoeff * envFast + (1.0f - fastCoeff) * mag;
    envSlow = slowCoeff * envSlow + (1.0f - slowCoeff) * mag;

    if (refractorySamplesLeft > 0) {
      --refractorySamplesLeft;
    } else if (jitterMs > 0.01f && envFast > kOnsetFloor && envFast > envSlow * kOnsetRatio) {
      // New note/chord: roll a fresh double offset and glide to it.
      jitterOffsetMs = (random.nextFloat() * 2.0f - 1.0f) * jitterMs;
      retargetDelay();
      refractorySamplesLeft = refractorySamples;
    }

    delayLine.pushSample(0, x);
    dst[i] = delayLine.popSample(0, delaySamples.getNextValue());
  }
}
