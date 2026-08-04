#include "StereoOffset.h"
#include <cmath>

void StereoOffset::prepare(double newSampleRate, int maxBlockSize) {
  sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

  const int maxDelaySamples = static_cast<int>(std::ceil(
      StereoOffsetParams::kMaxOffsetMs * 0.001 * sampleRate)) + 8;
  delayLine.setMaximumDelayInSamples(maxDelaySamples);
  delayLine.prepare({sampleRate, static_cast<juce::uint32>(juce::jmax(1, maxBlockSize)), 1});

  delaySamples.reset(sampleRate, kRampSeconds);
  delaySamples.setCurrentAndTargetValue(0.0f);

  running = false;
  engaged = false;
  delayLine.reset();
}

void StereoOffset::setTarget(const StereoOffsetParams& params, bool nowEngaged) {
  if (!running) {
    if (!nowEngaged)
      return;  // idle and staying idle
    // Engage from idle: start clean at 0 ms delay. The ramp never outruns
    // real time (kRampSeconds > max delay swing), so after this clear a read
    // can never reach back past the engage point: no stale audio, no gap.
    delayLine.reset();
    currentChannel = params.targetChannel;
    delaySamples.setCurrentAndTargetValue(0.0f);
    running = true;
  }

  engaged = nowEngaged;
  desiredChannel = params.targetChannel;
  offsetMs = params.offsetMs;

  // Disengaging or switching sides glides the delay to zero first;
  // process() completes the transition once it lands. Otherwise track knob
  // edits immediately.
  if (!engaged || desiredChannel != currentChannel)
    delaySamples.setTargetValue(0.0f);
  else
    retargetDelay();
}

void StereoOffset::retargetDelay() {
  const float totalMs = juce::jlimit(0.0f, StereoOffsetParams::kMaxOffsetMs, offsetMs);
  delaySamples.setTargetValue(static_cast<float>(totalMs * 0.001 * sampleRate));
}

void StereoOffset::process(juce::AudioBuffer<float>& buffer) {
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
      retargetDelay();
    }
  }
}
