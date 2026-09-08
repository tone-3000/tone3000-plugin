#include "BlockPredelay.h"
#include <algorithm>
#include <cmath>

void BlockPredelay::prepare(double newSampleRate, float initialMs) {
  sampleRate = newSampleRate;
  // +2 samples of margin over the exact max-delay sample count for the
  // linear-interpolation lookahead tap at the boundary.
  capacity = static_cast<size_t>(std::ceil(kMaxDelayMs * 0.001 * sampleRate)) + 2;
  for (auto& r : ring) r.assign(capacity, 0.0f);
  writePos = 0;

  delaySamplesSmoothed.reset(sampleRate, kRampSeconds);
  const float clampedMs = juce::jlimit(0.0f, kMaxDelayMs, initialMs);
  delaySamplesSmoothed.setCurrentAndTargetValue(
      static_cast<float>(clampedMs * 0.001 * sampleRate));
}

void BlockPredelay::setDelayMs(float ms) {
  const float clampedMs = juce::jlimit(0.0f, kMaxDelayMs, ms);
  delaySamplesSmoothed.setTargetValue(static_cast<float>(clampedMs * 0.001 * sampleRate));
}

void BlockPredelay::process(juce::AudioBuffer<float>& buffer) {
  const int numChannels = std::min(buffer.getNumChannels(), 2);
  const int numSamples = buffer.getNumSamples();
  const float capacityF = static_cast<float>(capacity);

  for (int i = 0; i < numSamples; ++i) {
    const float delaySamples = delaySamplesSmoothed.getNextValue();

    for (int ch = 0; ch < numChannels; ++ch) {
      auto& r = ring[static_cast<size_t>(ch)];
      auto* data = buffer.getWritePointer(ch);

      r[writePos] = data[i];

      // Fractional read position, wrapped into [0, capacity). delaySamples
      // is always <= capacity - 2 (clamped to kMaxDelayMs in setDelayMs/
      // prepare, capacity sized for kMaxDelayMs + margin), so a single
      // wraparound addition is always enough — no loop/modulo needed here.
      float readPos = static_cast<float>(writePos) - delaySamples;
      if (readPos < 0.0f) readPos += capacityF;

      const size_t idx0 = static_cast<size_t>(readPos);
      const size_t idx1 = (idx0 + 1) % capacity;
      const float frac = readPos - static_cast<float>(idx0);

      data[i] = r[idx0] + frac * (r[idx1] - r[idx0]);
    }
    writePos = (writePos + 1) % capacity;
  }
}
