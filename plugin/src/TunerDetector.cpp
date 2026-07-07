#include "TunerDetector.h"
#include <cmath>

TunerDetector::TunerDetector() {
  ring.resize(kRingSize, 0.0f);
  rawWindow.resize(kWindowSize * kDecimation, 0.0f);
  window.resize(kWindowSize, 0.0f);
  yin.resize(kWindowSize / 2, 0.0f);
}

void TunerDetector::prepare(double newSampleRate) {
  sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
  std::fill(ring.begin(), ring.end(), 0.0f);
  lastFrequency = 0.0;
  lastConfidence = 0.0;
  lastLevelDb = -120.0;
}

void TunerDetector::pushSamples(const float* samples, int numSamples) {
  int pos = writePos.load(std::memory_order_relaxed);
  for (int i = 0; i < numSamples; ++i) {
    ring[static_cast<size_t>(pos)] = samples[i];
    pos = (pos + 1) & (kRingSize - 1);
  }
  writePos.store(pos, std::memory_order_release);
}

juce::var TunerDetector::getReading() {
  const auto now = juce::Time::getMillisecondCounter();
  if (static_cast<juce::int64>(now) - lastAnalysisMs >= kAnalysisIntervalMs) {
    lastAnalysisMs = static_cast<juce::int64>(now);
    analyze();
  }

  auto* obj = new juce::DynamicObject();
  obj->setProperty("frequency", lastFrequency);
  obj->setProperty("confidence", lastConfidence);
  obj->setProperty("level", lastLevelDb);
  return juce::var(obj);
}

void TunerDetector::analyze() {
  const int rawCount = kWindowSize * kDecimation;

  // Copy the most recent rawCount samples out of the ring (newest last).
  const int endPos = writePos.load(std::memory_order_acquire);
  int start = (endPos - rawCount) & (kRingSize - 1);
  for (int i = 0; i < rawCount; ++i) {
    rawWindow[static_cast<size_t>(i)] = ring[static_cast<size_t>((start + i) & (kRingSize - 1))];
  }

  // Decimate by averaging groups of kDecimation samples. The averaging acts
  // as a crude low-pass, which is plenty for fundamentals below ~1.5 kHz.
  double sumSquares = 0.0;
  for (int i = 0; i < kWindowSize; ++i) {
    float acc = 0.0f;
    for (int d = 0; d < kDecimation; ++d)
      acc += rawWindow[static_cast<size_t>(i * kDecimation + d)];
    const float s = acc / static_cast<float>(kDecimation);
    window[static_cast<size_t>(i)] = s;
    sumSquares += static_cast<double>(s) * static_cast<double>(s);
  }

  const double rms = std::sqrt(sumSquares / kWindowSize);
  lastLevelDb = rms > 1.0e-9 ? 20.0 * std::log10(rms) : -120.0;

  if (lastLevelDb < kSilenceDb) {
    lastFrequency = 0.0;
    lastConfidence = 0.0;
    return;
  }

  const double decimatedRate = sampleRate / kDecimation;
  const int tauMax =
      juce::jmin(static_cast<int>(decimatedRate / kMinFrequency), kWindowSize / 2 - 1);
  const int tauMin = juce::jmax(2, static_cast<int>(decimatedRate / kMaxFrequency));

  // YIN difference function + cumulative mean normalized difference.
  const int integrationLength = kWindowSize - tauMax;
  yin[0] = 1.0f;
  double runningSum = 0.0;
  for (int tau = 1; tau <= tauMax; ++tau) {
    double diff = 0.0;
    for (int i = 0; i < integrationLength; ++i) {
      const double delta = static_cast<double>(window[static_cast<size_t>(i)]) -
                           static_cast<double>(window[static_cast<size_t>(i + tau)]);
      diff += delta * delta;
    }
    runningSum += diff;
    yin[static_cast<size_t>(tau)] =
        runningSum > 0.0 ? static_cast<float>(diff * tau / runningSum) : 1.0f;
  }

  // Absolute-threshold step: first tau under threshold, refined to its local minimum.
  int bestTau = -1;
  for (int tau = tauMin; tau <= tauMax; ++tau) {
    if (yin[static_cast<size_t>(tau)] < kYinThreshold) {
      while (tau + 1 <= tauMax && yin[static_cast<size_t>(tau + 1)] < yin[static_cast<size_t>(tau)])
        ++tau;
      bestTau = tau;
      break;
    }
  }

  if (bestTau < 0) {
    // Fall back to the global minimum if it is at least somewhat periodic.
    float minValue = 1.0f;
    for (int tau = tauMin; tau <= tauMax; ++tau) {
      if (yin[static_cast<size_t>(tau)] < minValue) {
        minValue = yin[static_cast<size_t>(tau)];
        bestTau = tau;
      }
    }
    if (minValue > 0.3f) {
      lastFrequency = 0.0;
      lastConfidence = 0.0;
      return;
    }
  }

  // Parabolic interpolation around the minimum for sub-sample precision.
  double refinedTau = bestTau;
  if (bestTau > tauMin && bestTau < tauMax) {
    const double y0 = yin[static_cast<size_t>(bestTau - 1)];
    const double y1 = yin[static_cast<size_t>(bestTau)];
    const double y2 = yin[static_cast<size_t>(bestTau + 1)];
    const double denom = y0 - 2.0 * y1 + y2;
    if (std::abs(denom) > 1.0e-12)
      refinedTau = bestTau + 0.5 * (y0 - y2) / denom;
  }

  lastFrequency = decimatedRate / refinedTau;
  lastConfidence = juce::jlimit(0.0, 1.0, 1.0 - static_cast<double>(yin[static_cast<size_t>(bestTau)]));
}
