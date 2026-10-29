// L0: the round-trip-latency self-measurement (plan section 4.1): probe
// generation, burst finding by cross-correlation, the peak/next-lobe sanity
// metric, and the median rule.

#include <cmath>
#include <random>

#include "RtlMeasurement.h"
#include "gtest/gtest.h"

namespace t3k::usb {
namespace {

TEST(RtlMeasurement, SweepProbeIsDeterministicAndBounded) {
  const auto a = rtl::makeSweep(48000.0, 2.0, -12.0f);
  const auto b = rtl::makeSweep(48000.0, 2.0, -12.0f);
  ASSERT_EQ(a.size(), 96u);  // 2 ms at 48 kHz
  EXPECT_EQ(a, b);
  const float amp = std::pow(10.0f, -12.0f / 20.0f);
  for (float v : a) EXPECT_LE(std::abs(v), amp * 1.0001f);
}

TEST(RtlMeasurement, MlsProbeIsDeterministic) {
  const auto a = rtl::makeMls(-12.0f);
  const auto b = rtl::makeMls(-12.0f);
  EXPECT_EQ(a.size(), 4096u);
  EXPECT_EQ(a, b);
  const float amp = std::pow(10.0f, -12.0f / 20.0f);
  for (float v : a) EXPECT_FLOAT_EQ(std::abs(v), amp);
}

TEST(RtlMeasurement, FindsExactLagsAndRtl) {
  // Reference probe at frame 5000, path at 5123 in separate captures:
  // true rtl = 123 samples = 2.5625 ms at 48 kHz.
  const double rate = 48000.0;
  std::mt19937 rng(7);
  std::normal_distribution<float> noise(0.0f, 1e-4f);
  for (const auto& probe : {rtl::makeSweep(rate, 2.0, -12.0f), rtl::makeMls(-12.0f)}) {
    std::vector<float> ref(48000, 0.0f), path(48000, 0.0f);
    for (size_t i = 0; i < ref.size(); ++i) {
      ref[i] = noise(rng);
      path[i] = noise(rng);
    }
    for (size_t i = 0; i < probe.size(); ++i) {
      ref[5000 + i] += probe[i];
      path[5123 + i] += 0.8f * probe[i];
    }
    const rtl::FindResult r = rtl::findBurst(ref.data(), ref.size(), probe.data(), probe.size());
    const rtl::FindResult p = rtl::findBurst(path.data(), path.size(), probe.data(), probe.size());
    EXPECT_TRUE(r.valid);
    EXPECT_TRUE(p.valid);
    EXPECT_EQ(r.lag, 5000);
    EXPECT_EQ(p.lag, 5123);
    EXPECT_GT(r.peakRatio, 3.0);
    EXPECT_GT(p.peakRatio, 3.0);
    EXPECT_NEAR(rtl::rtlMs(p.lag, r.lag, rate), 2.5625, 1e-9);
  }
}

TEST(RtlMeasurement, FindsTheBurstUnderHeavyNoise) {
  // Path 20 dB down in 10x noise: still exact, sanity ratio stays high.
  const double rate = 48000.0;
  const auto probe = rtl::makeSweep(rate, 2.0, -12.0f);
  std::mt19937 rng(11);
  std::normal_distribution<float> noise(0.0f, 1e-3f);
  std::vector<float> rec(48000);
  for (float& v : rec) v = noise(rng);
  for (size_t i = 0; i < probe.size(); ++i) rec[20000 + i] += 0.2f * probe[i];
  const rtl::FindResult r = rtl::findBurst(rec.data(), rec.size(), probe.data(), probe.size());
  EXPECT_TRUE(r.valid);
  EXPECT_EQ(r.lag, 20000);
  EXPECT_GT(r.peakRatio, 3.0);
}

TEST(RtlMeasurement, NoiseOnlyIsFlaggedGarbage) {
  std::mt19937 rng(3);
  std::normal_distribution<float> noise(0.0f, 1e-2f);
  std::vector<float> rec(24000);
  for (float& v : rec) v = noise(rng);
  const auto probe = rtl::makeSweep(48000.0, 2.0, -12.0f);
  const rtl::FindResult r = rtl::findBurst(rec.data(), rec.size(), probe.data(), probe.size());
  EXPECT_FALSE(r.valid);  // peak/next-lobe < 3.0 on pure noise
  EXPECT_LT(r.peakRatio, 3.0);
}

TEST(RtlMeasurement, DegenerateInputsAreRejected) {
  const auto probe = rtl::makeSweep(48000.0, 2.0, -12.0f);
  std::vector<float> tiny(4, 0.0f);
  EXPECT_FALSE(rtl::findBurst(tiny.data(), tiny.size(), probe.data(), probe.size()).valid);
  EXPECT_FALSE(rtl::findBurst(nullptr, 100, probe.data(), probe.size()).valid);
}

TEST(RtlMeasurement, MedianOfTrials) {
  EXPECT_DOUBLE_EQ(rtl::median({3.0, 1.0, 2.0}), 2.0);
  EXPECT_DOUBLE_EQ(rtl::median({4.0, 1.0, 2.0, 3.0}), 2.5);
  EXPECT_DOUBLE_EQ(rtl::median({42.0}), 42.0);
  EXPECT_DOUBLE_EQ(rtl::median({}), 0.0);
}

}  // namespace
}  // namespace t3k::usb
