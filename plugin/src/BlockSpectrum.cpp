#include "BlockSpectrum.h"
#include <cmath>

BlockSpectrum::BlockSpectrum() {
  ring.resize(kRingSize, 0.0f);
  fftData.resize(kFftSize * 2, 0.0f);
  smoothedDb.resize(kNumBins, kMinDb);

  windowTable.resize(kFftSize);
  for (int i = 0; i < kFftSize; ++i)
    windowTable[static_cast<size_t>(i)] =
        0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * static_cast<float>(i) /
                                static_cast<float>(kFftSize - 1)));
}

void BlockSpectrum::prepare(double newSampleRate) {
  sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
  std::fill(ring.begin(), ring.end(), 0.0f);
  std::fill(smoothedDb.begin(), smoothedDb.end(), kMinDb);
}

void BlockSpectrum::pushSamples(const float* ch0, const float* ch1OrNull, int numSamples) {
  int pos = writePos.load(std::memory_order_relaxed);
  for (int i = 0; i < numSamples; ++i) {
    const float s = ch1OrNull != nullptr ? 0.5f * (ch0[i] + ch1OrNull[i]) : ch0[i];
    ring[static_cast<size_t>(pos)] = s;
    pos = (pos + 1) & (kRingSize - 1);
  }
  writePos.store(pos, std::memory_order_release);
}

juce::var BlockSpectrum::getSpectrum() {
  const auto now = static_cast<juce::int64>(juce::Time::getMillisecondCounter());
  if (now - lastAnalysisMs >= kAnalysisIntervalMs) {
    lastAnalysisMs = now;
    analyze();
  }

  juce::Array<juce::var> bins;
  bins.ensureStorageAllocated(kNumBins);
  for (int i = 0; i < kNumBins; ++i)
    bins.add(static_cast<double>(smoothedDb[static_cast<size_t>(i)]));
  return juce::var(bins);
}

void BlockSpectrum::analyze() {
  // Copy the most recent kFftSize samples out of the ring, windowed.
  const int endPos = writePos.load(std::memory_order_acquire);
  const int start = (endPos - kFftSize) & (kRingSize - 1);
  for (int i = 0; i < kFftSize; ++i)
    fftData[static_cast<size_t>(i)] =
        ring[static_cast<size_t>((start + i) & (kRingSize - 1))] *
        windowTable[static_cast<size_t>(i)];
  std::fill(fftData.begin() + kFftSize, fftData.end(), 0.0f);

  fft.performRealOnlyForwardTransform(fftData.data());

  // Hann coherent gain is 0.5; 2/N for single-sided amplitude → 4/N overall.
  const float ampScale = 4.0f / static_cast<float>(kFftSize);
  const double binHz = sampleRate / static_cast<double>(kFftSize);
  const double logRatio = std::log(kMaxFreqHz / kMinFreqHz);

  auto magnitudeAt = [&](int fftBin) -> float {
    fftBin = juce::jlimit(0, kFftSize / 2 - 1, fftBin);
    const float re = fftData[static_cast<size_t>(fftBin * 2)];
    const float im = fftData[static_cast<size_t>(fftBin * 2 + 1)];
    return std::sqrt(re * re + im * im);
  };

  for (int i = 0; i < kNumBins; ++i) {
    // Log-spaced bin edges matching the EQ graph's x axis.
    const double f0 = kMinFreqHz * std::exp(logRatio * static_cast<double>(i) / kNumBins);
    const double f1 = kMinFreqHz * std::exp(logRatio * static_cast<double>(i + 1) / kNumBins);
    const int b0 = static_cast<int>(std::ceil(f0 / binHz));
    const int b1 = static_cast<int>(std::floor(f1 / binHz));

    float magnitude;
    if (b1 >= b0) {
      // Peak across the covered FFT bins reads better than the average for
      // narrow-band content (a plain average buries resonances).
      magnitude = 0.0f;
      for (int b = b0; b <= b1; ++b)
        magnitude = std::max(magnitude, magnitudeAt(b));
    } else {
      // Output bin narrower than one FFT bin (low frequencies): interpolate.
      const double exact = (f0 + f1) * 0.5 / binHz;
      const int lower = static_cast<int>(exact);
      const float frac = static_cast<float>(exact - lower);
      magnitude = magnitudeAt(lower) * (1.0f - frac) + magnitudeAt(lower + 1) * frac;
    }

    const float amplitude = magnitude * ampScale;
    const float db = amplitude > 1.0e-9f
                         ? juce::jlimit(kMinDb, 0.0f, 20.0f * std::log10(amplitude))
                         : kMinDb;

    // Fast attack, slow release: classic analyzer ballistics at ~30 Hz polling.
    float& smoothed = smoothedDb[static_cast<size_t>(i)];
    const float coeff = db > smoothed ? 0.7f : 0.18f;
    smoothed += (db - smoothed) * coeff;
  }
}
