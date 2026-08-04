// AutoOffset tests
//
// The auto-offset measurement (AutoOffset.h): cross-correlating the two raw
// chain outputs must find the true inter-chain lag (in both directions, and
// through chains voiced differently) while silence never dilutes the
// capture, and a misalignment beyond what the Offset knob can correct is
// rejected (low confidence) instead of producing a junk value. The last test
// closes the loop: the measured offset, applied through the real
// StereoOffset engine exactly like pollAutoOffset applies it, actually
// aligns the chains.
#include "AutoOffset.h"
#include "StereoOffset.h"
#include "test_helpers.h"

#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace {

constexpr int kBlock = 512;

// `x` delayed by `samples` (zero head), same length.
std::vector<float> delayed(const std::vector<float>& x, int samples) {
  std::vector<float> out(x.size(), 0.0f);
  for (size_t i = static_cast<size_t>(samples); i < x.size(); ++i)
    out[i] = x[i - static_cast<size_t>(samples)];
  return out;
}

// One-pole lowpass, a stand-in for "differently voiced chain".
std::vector<float> onePoleLp(const std::vector<float>& x, double fc) {
  const float a = static_cast<float>(std::exp(-2.0 * kPi * fc / kFs));
  std::vector<float> out(x.size());
  float state = 0.0f;
  for (size_t i = 0; i < x.size(); ++i) {
    state = (1.0f - a) * x[i] + a * state;
    out[i] = state;
  }
  return out;
}

// Arms an AutoOffset, streams l/r through capture() in blocks, and returns
// the analysis. Expects the capture to complete (2 s of signal available).
AutoOffset::Result measure(const std::vector<float>& l, const std::vector<float>& r) {
  AutoOffset ao;
  ao.prepare(kFs);
  ao.start();

  juce::AudioBuffer<float> buf(2, kBlock);
  for (size_t off = 0; off + kBlock <= l.size(); off += kBlock) {
    if (ao.state() != AutoOffset::State::Listening)
      break;
    for (int i = 0; i < kBlock; ++i) {
      buf.setSample(0, i, l[off + static_cast<size_t>(i)]);
      buf.setSample(1, i, r[off + static_cast<size_t>(i)]);
    }
    ao.capture(buf, kBlock);
  }

  EXPECT_EQ(ao.state(), AutoOffset::State::Captured) << "capture never completed";
  return ao.analyze();
}

TEST(AutoOffsetTest, MeasuresPureDelayRightChainLagging) {
  // Right chain lags by 150 samples → the correction delays the LEFT chain:
  // negative ms in the StereoOffset convention.
  const auto x = makeNoise(220 * kBlock, 42, 0.5f);
  const auto result = measure(delayed(x, 100), delayed(x, 250));

  EXPECT_NEAR(result.offsetMs, -150.0f * 1000.0f / static_cast<float>(kFs), 0.001f);
  EXPECT_GT(result.confidence, 0.9f);
}

TEST(AutoOffsetTest, MeasuresPureDelayLeftChainLagging) {
  // Left chain lags by 200 samples → delay the RIGHT chain: positive ms.
  const auto x = makeNoise(220 * kBlock, 43, 0.5f);
  const auto result = measure(delayed(x, 200), x);

  EXPECT_NEAR(result.offsetMs, 200.0f * 1000.0f / static_cast<float>(kFs), 0.001f);
  EXPECT_GT(result.confidence, 0.9f);
}

TEST(AutoOffsetTest, SurvivesDifferentlyVoicedChains) {
  // The two "chains" get very different tone (bright vs 1 kHz-lowpassed)
  // plus a 350-sample lag. The correlation peak must still land on the true
  // lag, within a hair: the lowpass has real group delay of its own
  // (~1/(2π·1 kHz) ≈ 7.6 samples at DC), and the energy-weighted peak
  // rightly absorbs a couple of samples of it.
  const auto x = makeNoise(220 * kBlock, 44, 0.5f);
  const auto l = delayed(x, 350);
  const auto r = onePoleLp(x, 1000.0);
  const auto result = measure(l, r);

  EXPECT_NEAR(result.offsetMs, 350.0f * 1000.0f / static_cast<float>(kFs), 0.25f);
  // White noise vs its 1 kHz-lowpassed copy shares under half its energy, so
  // ~0.5 is the honest ceiling here, still far above the 0.15 acceptance
  // threshold. Real chains (same instrument, both full-range) sit way higher.
  EXPECT_GT(result.confidence, 0.3f);
}

TEST(AutoOffsetTest, SilenceNeverCountsTowardTheCapture) {
  AutoOffset ao;
  ao.prepare(kFs);
  ao.start();

  // 1 s of silence: no progress, still listening.
  juce::AudioBuffer<float> buf(2, kBlock);
  buf.clear();
  for (int b = 0; b < static_cast<int>(kFs) / kBlock; ++b)
    ao.capture(buf, kBlock);
  EXPECT_EQ(ao.state(), AutoOffset::State::Listening);
  EXPECT_EQ(ao.progress(), 0.0f);

  // Keep feeding silence past the 15 s wall clock: times out, never Captured.
  for (int b = 0; b < 15 * static_cast<int>(kFs) / kBlock; ++b)
    ao.capture(buf, kBlock);
  EXPECT_EQ(ao.state(), AutoOffset::State::TimedOut);
}

TEST(AutoOffsetTest, GatedCaptureStillMeasuresTheLag) {
  // Phrases with silence in between: gated-out blocks splice the capture,
  // but both channels splice identically, so the measured lag survives.
  const auto x = makeNoise(440 * kBlock, 45, 0.5f);
  auto l = delayed(x, 120);
  auto r = std::vector<float>(x);
  // Mute every other 10-block stretch on both channels (below the -50 dBFS
  // block floor means exactly zero here).
  for (size_t i = 0; i < l.size(); ++i) {
    if ((i / (10 * kBlock)) % 2 == 1) {
      l[i] = 0.0f;
      r[i] = 0.0f;
    }
  }
  const auto result = measure(l, r);

  EXPECT_NEAR(result.offsetMs, 120.0f * 1000.0f / static_cast<float>(kFs), 0.05f);
  EXPECT_GT(result.confidence, 0.8f);
}

TEST(AutoOffsetTest, RejectsMisalignmentBeyondTheKnobRange) {
  // 40 ms of true lag sits outside the ±24 ms search window, so the best
  // in-window peak is noise-level: the confidence must fall below any sane
  // acceptance threshold (the processor rejects < 0.15).
  const auto x = makeNoise(220 * kBlock, 46, 0.5f);
  const int lag = static_cast<int>(std::round(0.040 * kFs));
  const auto result = measure(delayed(x, lag), x);

  EXPECT_LT(result.confidence, 0.15f);
}

TEST(AutoOffsetTest, EndToEndMeasureThenAlignThroughStereoOffset) {
  // The full loop, exactly as pollAutoOffset applies it: measure a
  // 300-sample left-chain lag, map the result onto the knob's normalized
  // value, run the misaligned pair through the real StereoOffset; the
  // residual lag between the outputs must be zero.
  constexpr int kLag = 300;
  const auto x = makeNoise(440 * kBlock, 47, 0.5f);
  const auto l = delayed(x, kLag);
  const auto& r = x;

  const auto result = measure(l, r);
  ASSERT_GT(result.confidence, 0.9f);

  const float norm = juce::jlimit(
      0.0f, 1.0f, 0.5f + result.offsetMs / (2.0f * StereoOffsetParams::kMaxOffsetMs));

  StereoOffset offset;
  offset.prepare(kFs, kBlock);
  juce::AudioBuffer<float> buf(2, kBlock);
  std::vector<float> outL, outR;
  for (size_t off = 0; off + kBlock <= l.size(); off += kBlock) {
    for (int i = 0; i < kBlock; ++i) {
      buf.setSample(0, i, l[off + static_cast<size_t>(i)]);
      buf.setSample(1, i, r[off + static_cast<size_t>(i)]);
    }
    offset.setTarget(StereoOffsetParams::fromNormalized(norm), true);
    offset.process(buf);
    for (int i = 0; i < kBlock; ++i) {
      outL.push_back(buf.getSample(0, i));
      outR.push_back(buf.getSample(1, i));
    }
  }

  // Well past the 40 ms delay glide-in; residual lag must be zero in both
  // directions (bestCorrelationLag only searches one way).
  const int start = static_cast<int>(kFs);
  EXPECT_EQ(bestCorrelationLag(outL, outR, start, 8 * kBlock, kLag), 0);
  EXPECT_EQ(bestCorrelationLag(outR, outL, start, 8 * kBlock, kLag), 0);
}

}  // namespace
