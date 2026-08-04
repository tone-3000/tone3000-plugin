// TunerDetector: YIN pitch detection over the ring buffer, as the tuner
// screen uses it (audio thread pushes, message thread polls).
#include "TunerDetector.h"

#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>

namespace {

constexpr double kFs = 48000.0;

juce::var readingFor(TunerDetector& tuner, const std::vector<float>& samples) {
  tuner.pushSamples(samples.data(), static_cast<int>(samples.size()));
  // getReading() caches analyses for 25 ms; step past that so every call
  // here analyzes the samples just pushed.
  juce::Thread::sleep(30);
  return tuner.getReading();
}

std::vector<float> sine(double freq, double seconds, float amplitude) {
  std::vector<float> out(static_cast<size_t>(kFs * seconds));
  for (size_t i = 0; i < out.size(); ++i)
    out[i] = amplitude *
             static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * freq *
                                         static_cast<double>(i) / kFs));
  return out;
}

TEST(TunerDetectorTest, DetectsGuitarStringPitches) {
  TunerDetector tuner;
  tuner.prepare(kFs);
  tuner.setEnabled(true);

  // Low E, A, and high E fundamentals; within a couple cents each
  // (1 cent at 110 Hz is ~0.06 Hz, so a 0.5% tolerance is generous but
  // catches octave and decimation errors outright).
  for (const double freq : {82.41, 110.0, 329.63}) {
    const auto reading = readingFor(tuner, sine(freq, 1.0, 0.25f));
    const double detected = reading.getProperty("frequency", 0.0);
    EXPECT_NEAR(detected, freq, freq * 0.005) << "at " << freq << " Hz";
    EXPECT_GT(static_cast<double>(reading.getProperty("confidence", 0.0)), 0.5);
  }
}

TEST(TunerDetectorTest, SilenceAndNoiseReportNoPitch) {
  TunerDetector tuner;
  tuner.prepare(kFs);
  tuner.setEnabled(true);

  const auto silent = readingFor(tuner, std::vector<float>(static_cast<size_t>(kFs), 0.0f));
  EXPECT_EQ(static_cast<double>(silent.getProperty("frequency", -1.0)), 0.0);

  juce::Random rng(7);
  std::vector<float> noise(static_cast<size_t>(kFs));
  for (auto& s : noise)
    s = rng.nextFloat() * 0.4f - 0.2f;
  const auto noisy = readingFor(tuner, noise);
  EXPECT_EQ(static_cast<double>(noisy.getProperty("frequency", -1.0)), 0.0);
}

}  // namespace
