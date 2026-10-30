#include "CaptureDsp.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace {

using namespace t3k::capture;

std::vector<float> filter(const std::vector<float>& signal,
                          const std::vector<std::pair<size_t, float>>& taps,
                          size_t delay) {
  size_t tail = 0;
  for (const auto& tap : taps)
    tail = std::max(tail, tap.first);
  std::vector<float> result(signal.size() + delay + tail + 1024, 0.0f);
  for (const auto& [offset, gain] : taps)
    for (size_t i = 0; i < signal.size(); ++i)
      result[i + delay + offset] += signal[i] * gain;
  return result;
}

TEST(CaptureDspTest, SweepIsBoundedFadedAndDeterministic) {
  const auto sweep = makeSweep(1.0, -12.0f);
  ASSERT_EQ(sweep.size(), static_cast<size_t>(sampleRate));
  EXPECT_FLOAT_EQ(sweep.front(), 0.0f);
  EXPECT_FLOAT_EQ(sweep.back(), 0.0f);
  float peak = 0.0f;
  for (const float value : sweep)
    peak = std::max(peak, std::abs(value));
  EXPECT_LE(peak, std::pow(10.0f, -12.0f / 20.0f) + 1.0e-6f);
  EXPECT_EQ(sweep, makeSweep(1.0, -12.0f));
  EXPECT_TRUE(makeSweep(-1.0).empty());
  EXPECT_TRUE(makeSweep(121.0).empty());
  EXPECT_TRUE(makeSweep(std::numeric_limits<double>::quiet_NaN()).empty());
  EXPECT_TRUE(makeSweep(1.0, std::numeric_limits<float>::infinity()).empty());
}

TEST(CaptureDspTest, ResampleHasExactLengthAndSameRateIsBitExact) {
  const std::vector<float> samples{0.0f, -0.25f, 0.5f, -0.0f, 0.125f};
  const auto same = resample(samples, 48000.0, 48000.0);
  ASSERT_EQ(same.size(), samples.size());
  EXPECT_EQ(std::memcmp(samples.data(), same.data(), samples.size() * sizeof(float)), 0);
  EXPECT_EQ(resample(std::vector<float>(44100, 0.2f), 44100.0, 48000.0).size(), 48000u);
  EXPECT_EQ(resample(std::vector<float>(48000, 0.2f), 48000.0, 44100.0).size(), 44100u);
  EXPECT_EQ(resample(std::vector<float>(101, 0.2f), 44100.0, 48000.0).size(),
            static_cast<size_t>(std::llround(101.0 * 48000.0 / 44100.0)));
}

TEST(CaptureDspTest, ResamplePreservesSineAmplitudeAndRoundTripTiming) {
  std::vector<float> input(44100);
  constexpr double twoPi = 6.2831853071795864769;
  for (size_t i = 0; i < input.size(); ++i)
    input[i] = static_cast<float>(0.25 * std::sin(twoPi * 1000.0 * i / 44100.0));
  const auto converted = resample(input, 44100.0, 48000.0);
  const auto restored = resample(converted, 48000.0, 44100.0);
  ASSERT_EQ(converted.size(), 48000u);
  ASSERT_EQ(restored.size(), input.size());
  double energy = 0.0, errorEnergy = 0.0;
  float convertedPeak = 0.0f;
  for (size_t i = 256; i + 256 < restored.size(); ++i) {
    energy += static_cast<double>(restored[i]) * restored[i];
    const double error = restored[i] - input[i];
    errorEnergy += error * error;
  }
  for (size_t i = 256; i + 256 < converted.size(); ++i)
    convertedPeak = std::max(convertedPeak, std::abs(converted[i]));
  const double count = static_cast<double>(restored.size() - 512);
  EXPECT_NEAR(std::sqrt(energy / count), 0.25 / std::sqrt(2.0), 0.0003);
  EXPECT_NEAR(convertedPeak, 0.25f, 0.0003f);
  EXPECT_LT(std::sqrt(errorEnergy / count), 1.0e-5);
}

TEST(CaptureDspTest, ResampleDoesNotAddFilterDelayToImpulseOrToTrackPair) {
  std::vector<float> input(44100, 0.0f);
  constexpr size_t impulseIndex = 12345;
  input[impulseIndex] = 0.6f;
  const auto converted = resample(input, 44100.0, 48000.0);
  const auto restored = resample(converted, 48000.0, 44100.0);
  const auto peakIndex = [](const auto& signal) {
    return static_cast<size_t>(std::distance(
        signal.begin(), std::max_element(signal.begin(), signal.end(), [](float a, float b) {
          return std::abs(a) < std::abs(b);
        })));
  };
  ASSERT_FALSE(converted.empty());
  ASSERT_FALSE(restored.empty());
  EXPECT_EQ(peakIndex(converted),
            static_cast<size_t>(std::llround(impulseIndex * 48000.0 / 44100.0)));
  EXPECT_EQ(peakIndex(restored), impulseIndex);
  auto wet = input;
  for (auto& sample : wet)
    sample *= -0.4f;
  const auto convertedWet = resample(wet, 44100.0, 48000.0);
  ASSERT_EQ(convertedWet.size(), converted.size());
  EXPECT_EQ(peakIndex(convertedWet), peakIndex(converted));
  for (size_t i = 0; i < converted.size(); ++i)
    EXPECT_NEAR(convertedWet[i], converted[i] * -0.4f, 1.0e-7f);
}

TEST(CaptureDspTest, DownsampleRejectsUltrasonicAlias) {
  std::vector<float> input(48000);
  constexpr double twoPi = 6.2831853071795864769;
  for (size_t i = 0; i < input.size(); ++i)
    input[i] = static_cast<float>(0.25 * std::sin(twoPi * 23500.0 * i / 48000.0));
  const auto converted = resample(input, 48000.0, 44100.0);
  ASSERT_EQ(converted.size(), 44100u);
  double energy = 0.0;
  for (size_t i = 256; i + 256 < converted.size(); ++i)
    energy += static_cast<double>(converted[i]) * converted[i];
  EXPECT_LT(std::sqrt(energy / static_cast<double>(converted.size() - 512)), 0.0002);
}

TEST(CaptureDspTest, ResampleRejectsInvalidRatesAndNonfiniteSamples) {
  const std::vector<float> samples(100, 0.2f);
  EXPECT_TRUE(resample(samples, 0.0, 48000.0).empty());
  EXPECT_TRUE(resample(samples, 44100.0, -1.0).empty());
  EXPECT_TRUE(resample(samples, std::numeric_limits<double>::infinity(), 48000.0).empty());
  EXPECT_TRUE(resample(samples, 44100.0, std::numeric_limits<double>::quiet_NaN()).empty());
  auto invalid = samples;
  invalid[4] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_TRUE(resample(invalid, 44100.0, 48000.0).empty());
  EXPECT_TRUE(resample(invalid, 48000.0, 48000.0).empty());
}

TEST(CaptureDspTest, RecoversExactDelayOfScaledInvertedReturn) {
  std::vector<float> sweep(400, 0.0f);
  const auto body = makeSweep(0.5, -12.0f);
  sweep.insert(sweep.end(), body.begin(), body.end());
  const auto original = sweep;
  const auto returned = filter(sweep, {{0, -0.4f}}, 719);
  const auto alignment = estimateDelay(sweep, returned, 2048);
  ASSERT_TRUE(alignment.error.isEmpty()) << alignment.error.toStdString();
  EXPECT_EQ(alignment.offset, 719);
  EXPECT_GT(alignment.confidence, 0.99f);
  EXPECT_EQ(sweep, original);  // NAM reference is never normalized/edited.
}

TEST(CaptureDspTest, RecoversDelayDespiteClippingAndNoise) {
  const auto sweep = makeSweep(0.5, -6.0f);
  auto returned = filter(sweep, {{0, 4.0f}}, 271);
  uint32_t seed = 42;
  for (float& sample : returned) {
    seed = seed * 1664525u + 1013904223u;
    const float noise = static_cast<float>(static_cast<double>(seed) / UINT32_MAX - 0.5) * 0.02f;
    sample = std::clamp(sample, -0.2f, 0.2f) + noise;
  }
  const auto alignment = estimateDelay(sweep, returned, 1024);
  ASSERT_TRUE(alignment.error.isEmpty()) << alignment.error.toStdString();
  EXPECT_EQ(alignment.offset, 271);
  EXPECT_GT(alignment.confidence, 0.7f);
}

TEST(CaptureDspTest, AlignmentRejectsInvalidAndSilentInputs) {
  const auto sweep = makeSweep(0.1);
  EXPECT_FALSE(estimateDelay(sweep, std::vector<float>(sweep.size(), 0.0f), 512).error.isEmpty());
  EXPECT_FALSE(estimateDelay(sweep, sweep, -1).error.isEmpty());
  EXPECT_FALSE(estimateDelay(sweep, sweep, sampleRate * 31).error.isEmpty());
  EXPECT_FALSE(estimateDelay(std::vector<float>(8, 0.2f), sweep, 512).error.isEmpty());
  EXPECT_FALSE(estimateDelay(sweep, std::vector<float>(16, 0.2f), 512).error.isEmpty());
  auto invalid = sweep;
  invalid[42] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(estimateDelay(sweep, invalid, 512).error.isEmpty());
}

TEST(CaptureDspTest, RecoversKnownFirAndRemovesTransportDelay) {
  const auto sweep = makeSweep(1.0, -12.0f);
  const auto returned = filter(sweep, {{0, 0.7f}, {32, -0.21f}, {144, 0.14f}}, 623);
  const auto result = extractIr(sweep, returned, 512);
  ASSERT_TRUE(result.error.isEmpty()) << result.error.toStdString();
  ASSERT_EQ(result.samples.size(), 512u);
  EXPECT_NEAR(result.samples[0], 0.9f, 0.03f);
  EXPECT_NEAR(result.samples[32] / result.samples[0], -0.3f, 0.035f);
  EXPECT_NEAR(result.samples[144] / result.samples[0], 0.2f, 0.035f);
}

TEST(CaptureDspTest, RetainsEarlyArrivalBeforeStrongerReflection) {
  const auto sweep = makeSweep(1.0, -12.0f);
  const auto returned = filter(sweep, {{0, 0.3f}, {100, 0.7f}}, 123);
  const auto result = extractIr(sweep, returned, 256);
  ASSERT_TRUE(result.error.isEmpty()) << result.error.toStdString();
  ASSERT_EQ(result.samples.size(), 256u);
  EXPECT_GT(result.samples[0], 0.3f);
  EXPECT_NEAR(result.samples[100], 0.9f, 0.03f);
}

TEST(CaptureDspTest, IrRejectsSilenceMissingSweepInvalidLengthAndNonfiniteSamples) {
  const auto sweep = makeSweep(0.1, -12.0f);
  EXPECT_FALSE(extractIr(sweep, std::vector<float>(sweep.size(), 0.0f), 256).error.isEmpty());
  auto shortened = sweep;
  shortened.pop_back();
  EXPECT_FALSE(extractIr(sweep, shortened, 256).error.isEmpty());
  EXPECT_FALSE(extractIr(sweep, sweep, 0).error.isEmpty());
  EXPECT_FALSE(extractIr(sweep, sweep, sampleRate * 31).error.isEmpty());
  auto invalid = sweep;
  invalid[42] = std::numeric_limits<float>::infinity();
  EXPECT_FALSE(extractIr(sweep, invalid, 256).error.isEmpty());
}

}  // namespace
