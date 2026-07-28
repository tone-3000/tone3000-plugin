#pragma once
// Shared helpers for the DSP test suite (dsp_tests.cpp / processor_tests.cpp).

#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <complex>
#include <random>
#include <vector>

constexpr double kPi = 3.14159265358979323846;
constexpr double kFs = 48000.0;  // == kChainBaseSampleRate

inline juce::File testFile(const char* name) {
  const auto f = juce::File(T3K_TEST_FILES_DIR).getChildFile(name);
  EXPECT_TRUE(f.existsAsFile()) << "missing test asset: " << f.getFullPathName().toStdString();
  return f;
}

// Hann-windowed DFT magnitude² at one frequency (relative to `fs`). A
// full-scale sine reads 1.0 → 0 dB.
inline double goertzelPower(const float* x, size_t n, double freq, double fs = kFs) {
  std::complex<double> acc{0.0, 0.0};
  for (size_t i = 0; i < n; ++i) {
    const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (n - 1));
    const double ph = -2.0 * kPi * freq / fs * static_cast<double>(i);
    acc += w * static_cast<double>(x[i]) * std::complex<double>(std::cos(ph), std::sin(ph));
  }
  const double mag = std::abs(acc) / (0.25 * static_cast<double>(n));
  return mag * mag;
}

inline double goertzelPower(const std::vector<float>& x, double freq, double fs = kFs) {
  return goertzelPower(x.data(), x.size(), freq, fs);
}

inline double db(double power) { return 10.0 * std::log10(std::max(power, 1e-30)); }

// The frequency a component at `f` lands on after sampling at `fs`.
inline double foldFrequency(double f, double fs = kFs) {
  f = std::fmod(f, fs);
  return f > fs / 2 ? fs - f : f;
}

inline std::vector<float> makeSine(int frames, double freq, float amplitude = 1.0f,
                                   double fs = kFs) {
  std::vector<float> x(static_cast<size_t>(frames));
  for (int i = 0; i < frames; ++i)
    x[static_cast<size_t>(i)] =
        amplitude * static_cast<float>(std::sin(2.0 * kPi * freq * i / fs));
  return x;
}

inline std::vector<float> makeNoise(int frames, unsigned seed, float amplitude = 1.0f) {
  std::vector<float> x(static_cast<size_t>(frames));
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> dist(-amplitude, amplitude);
  for (auto& s : x)
    s = dist(rng);
  return x;
}

// Lag (within [0, maxLag)) at which `out` best matches `in`, over the window
// [start, start + window) — a group-delay estimate.
inline int bestCorrelationLag(const std::vector<float>& out, const std::vector<float>& in,
                              int start, int window, int maxLag) {
  int bestLag = -1;
  double bestCorr = -1e30;
  for (int lag = 0; lag < maxLag; ++lag) {
    double corr = 0.0;
    for (int i = start; i < start + window; ++i)
      corr += static_cast<double>(out[static_cast<size_t>(i)]) *
              static_cast<double>(in[static_cast<size_t>(i - lag)]);
    if (corr > bestCorr) {
      bestCorr = corr;
      bestLag = lag;
    }
  }
  return bestLag;
}
