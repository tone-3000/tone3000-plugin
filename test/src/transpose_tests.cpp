// Unit tests for TransposeProcessor (the Transpose knob's pitch shifter),
// isolated from the full processor chain. See TransposeProcessor.h for the
// bypass-at-zero design and why it's a hard requirement here: the "empty,
// default chain is transparent with zero latency" invariant is pinned at
// the Processor level in processor_tests.cpp
// (ProcessorTest.EmptyChainAt48kIsTransparentWithZeroLatency), and this
// class is what has to deliver that.
#include "TransposeProcessor.h"
#include "test_helpers.h"

#include <cmath>

namespace {
constexpr int kBlock = 512;
}  // namespace

TEST(TransposeProcessorTest, BypassAtZeroIsBitExactPassthroughWithZeroLatency) {
  TransposeProcessor tp;
  tp.prepare(kFs, 2, kBlock);
  EXPECT_EQ(tp.getLatencySamples(), 0);

  juce::AudioBuffer<float> buffer(2, kBlock);
  const auto noise = makeNoise(kBlock, 7, 0.4f);
  buffer.copyFrom(0, 0, noise.data(), kBlock);
  buffer.copyFrom(1, 0, noise.data(), kBlock);

  juce::AudioBuffer<float> reference;
  reference.makeCopyOf(buffer);

  tp.setSemitones(0);  // default; explicit for readability
  tp.process(buffer);

  for (int ch = 0; ch < 2; ++ch)
    for (int i = 0; i < kBlock; ++i)
      EXPECT_EQ(buffer.getReadPointer(ch)[i], reference.getReadPointer(ch)[i])
          << "ch " << ch << " sample " << i;
}

TEST(TransposeProcessorTest, ActiveReportsPositiveLatencyAndBypassReturnsToZero) {
  TransposeProcessor tp;
  tp.prepare(kFs, 2, kBlock);
  EXPECT_EQ(tp.getLatencySamples(), 0);

  tp.setSemitones(3);
  const int active = tp.getLatencySamples();
  // ~20ms analysis window at kFs; loose bounds since the exact STFT latency
  // formula (inputLatency() + outputLatency()) is a library implementation
  // detail, not something this test should pin exactly.
  EXPECT_GT(active, static_cast<int>(kFs * 0.005)) << "> 5ms";
  EXPECT_LT(active, static_cast<int>(kFs * 0.1)) << "< 100ms";

  tp.setSemitones(0);
  EXPECT_EQ(tp.getLatencySamples(), 0) << "must return to zero-latency bypass";
}

// Regression test: TransposeProcessor::prepare() is always called with 2
// channels (Processor.cpp configures it for the ceiling), but the live
// per-block buffer can be genuinely mono when the host bus is mono (see
// ProcessorTest.StereoChainsFoldToMonoWithoutAStereoOutput). This used to
// read stretch's internal channel 1 pointer from a 1-channel
// juce::AudioBuffer, which is out-of-bounds and crashed.
TEST(TransposeProcessorTest, MonoBufferAgainstStereoConfiguredEngineDoesNotCrash) {
  TransposeProcessor tp;
  tp.prepare(kFs, 2, kBlock);
  tp.setSemitones(7);

  juce::AudioBuffer<float> mono(1, kBlock);
  const auto noise = makeNoise(kBlock, 99, 0.3f);
  mono.copyFrom(0, 0, noise.data(), kBlock);

  for (int i = 0; i < 50; ++i)
    tp.process(mono);

  for (int i = 0; i < kBlock; ++i)
    EXPECT_TRUE(std::isfinite(mono.getReadPointer(0)[i]));
}

TEST(TransposeProcessorTest, SurvivesSampleRateAndBlockSizeChanges) {
  TransposeProcessor tp;
  tp.prepare(44100.0, 2, 256);
  tp.setSemitones(5);
  {
    juce::AudioBuffer<float> buffer(2, 256);
    const auto noise = makeNoise(256, 1, 0.3f);
    buffer.copyFrom(0, 0, noise.data(), 256);
    buffer.copyFrom(1, 0, noise.data(), 256);
    tp.process(buffer);
  }

  // Sample-rate change mid-session (e.g. host switches audio device) is a
  // fresh prepare(), same as a real prepareToPlay call.
  tp.prepare(96000.0, 2, 1024);
  {
    juce::AudioBuffer<float> buffer(2, 1024);
    const auto noise = makeNoise(1024, 2, 0.3f);
    buffer.copyFrom(0, 0, noise.data(), 1024);
    buffer.copyFrom(1, 0, noise.data(), 1024);
    tp.process(buffer);
    for (int ch = 0; ch < 2; ++ch)
      for (int i = 0; i < 1024; ++i)
        EXPECT_TRUE(std::isfinite(buffer.getReadPointer(ch)[i]));
  }

  // A block bigger than prepare() sized the scratch buffer for (can happen
  // transiently, e.g. an offline bounce using a larger block than the
  // realtime device) must not crash - the scratch buffer grows to fit.
  {
    juce::AudioBuffer<float> buffer(2, 4096);
    const auto noise = makeNoise(4096, 3, 0.3f);
    buffer.copyFrom(0, 0, noise.data(), 4096);
    buffer.copyFrom(1, 0, noise.data(), 4096);
    tp.process(buffer);
    for (int ch = 0; ch < 2; ++ch)
      for (int i = 0; i < 4096; ++i)
        EXPECT_TRUE(std::isfinite(buffer.getReadPointer(ch)[i]));
  }
}

TEST(TransposeProcessorTest, ExtremeShiftsStayFiniteAndBounded) {
  for (int semis : {-12, 12}) {
    TransposeProcessor tp;
    tp.prepare(kFs, 2, kBlock);
    tp.setSemitones(semis);

    const auto noise = makeNoise(static_cast<int>(kFs), 555 + semis, 0.5f);
    juce::AudioBuffer<float> buffer(2, kBlock);
    for (int off = 0; off + kBlock <= static_cast<int>(noise.size()); off += kBlock) {
      buffer.copyFrom(0, 0, noise.data() + off, kBlock);
      buffer.copyFrom(1, 0, noise.data() + off, kBlock);
      tp.process(buffer);
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < kBlock; ++i) {
          const float s = buffer.getReadPointer(ch)[i];
          ASSERT_TRUE(std::isfinite(s)) << "semitones=" << semis;
          ASSERT_LT(std::abs(s), 10.0f) << "semitones=" << semis;
        }
    }
  }
}

// Flips the knob every block, including through 0 (the bypass boundary),
// exercising the reset()-on-bypassed->active transition (see
// TransposeProcessor::setSemitones) under worst-case churn.
TEST(TransposeProcessorTest, TogglingBetweenBypassAndActiveIsStable) {
  TransposeProcessor tp;
  tp.prepare(kFs, 2, kBlock);

  const auto noise = makeNoise(kBlock * 20, 42, 0.4f);
  juce::AudioBuffer<float> buffer(2, kBlock);
  int semis = 0;
  for (int off = 0; off + kBlock <= static_cast<int>(noise.size()); off += kBlock) {
    semis = (semis == 0) ? 7 : 0;
    tp.setSemitones(semis);
    buffer.copyFrom(0, 0, noise.data() + off, kBlock);
    buffer.copyFrom(1, 0, noise.data() + off, kBlock);
    tp.process(buffer);
    for (int ch = 0; ch < 2; ++ch)
      for (int i = 0; i < kBlock; ++i)
        ASSERT_TRUE(std::isfinite(buffer.getReadPointer(ch)[i]));
  }
}

namespace {
// Settled-tail dominant-frequency check: feeds a steady sine through a
// fixed shift and confirms energy has moved to the expected frequency
// rather than staying at the original - the actual pitch-shifting contract,
// not just "doesn't crash". Discards the first second (engine settle +
// latency) before measuring, well past the ~20ms window.
void expectPitchShiftedTo(int semitones, double inFreq, double expectedFreq) {
  TransposeProcessor tp;
  tp.prepare(kFs, 1, kBlock);
  tp.setSemitones(semitones);

  const int total = static_cast<int>(kFs * 3);  // 3 seconds
  const auto in = makeSine(total, inFreq, 0.5f, kFs);
  std::vector<float> out(static_cast<size_t>(total), 0.0f);

  juce::AudioBuffer<float> buffer(1, kBlock);
  for (int off = 0; off + kBlock <= total; off += kBlock) {
    buffer.copyFrom(0, 0, in.data() + off, kBlock);
    tp.process(buffer);
    std::copy(buffer.getReadPointer(0), buffer.getReadPointer(0) + kBlock,
              out.begin() + off);
  }

  constexpr int kSettleSamples = 48000;  // 1s
  constexpr int kWindow = 65536;
  const double atExpected = db(goertzelPower(out.data() + kSettleSamples, kWindow, expectedFreq, kFs));
  const double atOriginal = db(goertzelPower(out.data() + kSettleSamples, kWindow, inFreq, kFs));
  EXPECT_GT(atExpected, atOriginal + 20.0)
      << "semitones=" << semitones << ": expected energy to concentrate at " << expectedFreq
      << " Hz, not stay at the original " << inFreq << " Hz (at-expected=" << atExpected
      << " dB, at-original=" << atOriginal << " dB)";
}
}  // namespace

TEST(TransposeProcessorTest, OctaveUpDoublesFrequency) {
  expectPitchShiftedTo(/*semitones=*/12, /*inFreq=*/440.0, /*expectedFreq=*/880.0);
}

TEST(TransposeProcessorTest, OctaveDownHalvesFrequency) {
  expectPitchShiftedTo(/*semitones=*/-12, /*inFreq=*/440.0, /*expectedFreq=*/220.0);
}
