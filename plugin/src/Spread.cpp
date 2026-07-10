#include "Spread.h"
#include <cmath>

namespace {
// One-pole coefficient for a given time constant.
float envCoeff(double sampleRate, float ms) {
  return std::exp(static_cast<float>(-1.0 / (sampleRate * ms * 0.001)));
}
}  // namespace

void Spread::prepare(double newSampleRate, int maxBlockSize) {
  sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

  const int maxDelaySamples = static_cast<int>(std::ceil(
      (SpreadParams::kMaxSpreadMs + SpreadParams::kMaxJitterMs) * 0.001 * sampleRate)) + 8;
  delayLine.setMaximumDelayInSamples(maxDelaySamples);
  delayLine.prepare({sampleRate, static_cast<juce::uint32>(juce::jmax(1, maxBlockSize)), 1});

  delaySamples.reset(sampleRate, kRampSeconds);
  delaySamples.setCurrentAndTargetValue(0.0f);

  fastAttack = envCoeff(sampleRate, 1.0f);
  fastRelease = envCoeff(sampleRate, 50.0f);
  slowCoeff = envCoeff(sampleRate, 200.0f);
  refractorySamples = static_cast<int>(0.06 * sampleRate);  // 60 ms per-note hold

  running = false;
  engaged = false;
  jitterOffsetMs = 0.0f;
  delayLine.reset();
  resetDetector();
}

void Spread::resetDetector() {
  envFast = envSlow = 0.0f;
  armed = true;
  refractorySamplesLeft = 0;
}

void Spread::setTarget(const SpreadParams& params, bool nowEngaged) {
  if (!running) {
    if (!nowEngaged)
      return;  // idle and staying idle
    // Engage from idle: start clean at 0 ms delay. The ramp never outruns
    // real time (kRampSeconds > max delay swing), so after this clear a read
    // can never reach back past the engage point — no stale audio, no gap.
    delayLine.reset();
    resetDetector();
    jitterOffsetMs = 0.0f;
    currentChannel = params.targetChannel;
    delaySamples.setCurrentAndTargetValue(0.0f);
    running = true;
  }

  engaged = nowEngaged;
  desiredChannel = params.targetChannel;
  spreadMs = params.spreadMs;
  jitterMs = params.jitterMs;

  // Disengaging or switching sides glides the delay to zero first;
  // process() completes the transition once it lands. Otherwise track knob
  // edits immediately (drags shouldn't wait for the next onset).
  if (!engaged || desiredChannel != currentChannel)
    delaySamples.setTargetValue(0.0f);
  else
    retargetDelay();
}

void Spread::retargetDelay() {
  const float totalMs = juce::jlimit(
      0.0f, SpreadParams::kMaxSpreadMs + SpreadParams::kMaxJitterMs, spreadMs + jitterOffsetMs);
  delaySamples.setTargetValue(static_cast<float>(totalMs * 0.001 * sampleRate));
}

void Spread::analyzeOnsets(const float* dryInput, int numSamples) {
  if (!running)
    return;

  bool onset = false;
  for (int i = 0; i < numSamples; ++i) {
    const float mag = std::abs(dryInput[i]);

    const float fastCoeff = mag > envFast ? fastAttack : fastRelease;
    envFast = fastCoeff * envFast + (1.0f - fastCoeff) * mag;
    envSlow = slowCoeff * envSlow + (1.0f - slowCoeff) * mag;

    if (refractorySamplesLeft > 0) {
      --refractorySamplesLeft;
      continue;
    }
    if (!armed) {
      // Hysteresis: re-arm only after the last note has actually decayed
      // (fast envelope back near the slow one). Sustained or compressed
      // material can't machine-gun retriggers.
      if (envFast < envSlow * kRearmRatio)
        armed = true;
      continue;
    }
    if (envFast > kOnsetFloor && envFast > envSlow * kOnsetRatio) {
      onset = true;
      armed = false;
      refractorySamplesLeft = refractorySamples;
    }
  }

  // One re-roll per block is plenty (a block is far shorter than the
  // refractory hold). Only meaningful when jitter is up and the engine is in
  // steady state (not mid glide-out).
  if (onset && jitterMs > 0.0f && engaged && desiredChannel == currentChannel) {
    jitterOffsetMs = (random.nextFloat() * 2.0f - 1.0f) * jitterMs;
    retargetDelay();
  }
}

void Spread::process(juce::AudioBuffer<float>& buffer) {
  if (!running || buffer.getNumChannels() < 2)
    return;

  const int numSamples = buffer.getNumSamples();
  float* data = buffer.getWritePointer(currentChannel);

  for (int i = 0; i < numSamples; ++i) {
    delayLine.pushSample(0, data[i]);
    data[i] = delayLine.popSample(0, delaySamples.getNextValue());
  }

  // Complete transitions that were gliding toward zero. At exactly 0 ms the
  // output equals the input, so both the side swap and going idle are
  // click-free by construction.
  if (!delaySamples.isSmoothing() && delaySamples.getCurrentValue() <= 0.0f) {
    if (!engaged) {
      running = false;
    } else if (desiredChannel != currentChannel) {
      currentChannel = desiredChannel;
      delayLine.reset();
      jitterOffsetMs = 0.0f;
      retargetDelay();
    }
  }
}
