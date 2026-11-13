#include "Metronome.h"
#include "Processor.h"
#include "test_helpers.h"

#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <vector>

namespace {

constexpr int kSilentGapSamples = 1500;
constexpr int kBlockSamples = 4800;
constexpr int kSmallBlock = 64, kDefaultBlock = 256, kLargeBlock = 480;
constexpr int kThreeSeconds = static_cast<int>(kFs * 3);
constexpr int kOneSecond = static_cast<int>(kFs);
constexpr float kFullGain = 1.0f, kHalfGain = 0.5f;
constexpr float kStopped = 0.0f;
constexpr float kDefaultBpm = 100.0f, kMaxBpm = 400.0f, kMinBpm = 20.0f;
constexpr float kDefaultVolume = 0.8f;
constexpr int kBlocksToReachTheFirstClick = 5;

double samplesPerBeat(float bpm) {
  return 60.0 * kFs / bpm;
}

std::vector<float> render(float bpm, float gain, int totalSamples, int blockSize) {
  Metronome metronome;
  metronome.prepare(kFs);
  juce::AudioBuffer<float> block(1, blockSize);
  std::vector<float> out;
  for (int rendered = 0; rendered < totalSamples; rendered += blockSize) {
    block.clear();
    metronome.process(block, bpm, gain);
    out.insert(out.end(), block.getReadPointer(0), block.getReadPointer(0) + blockSize);
  }
  return out;
}

std::vector<int> clickOnsets(const std::vector<float>& signal) {
  std::vector<int> onsets;
  int silentRun = kSilentGapSamples;
  for (int i = 0; i < static_cast<int>(signal.size()); ++i) {
    if (signal[i] == 0.0f) {
      ++silentRun;
      continue;
    }
    if (silentRun >= kSilentGapSamples)
      onsets.push_back(i);
    silentRun = 0;
  }
  return onsets;
}

float peak(const std::vector<float>& signal) {
  float p = 0.0f;
  for (float s : signal)
    p = std::max(p, std::abs(s));
  return p;
}

void expectSpacedByOneBeat(const std::vector<int>& onsets, float bpm) {
  ASSERT_GE(onsets.size(), 3u);
  for (size_t i = 1; i < onsets.size(); ++i)
    EXPECT_NEAR(onsets[i] - onsets[i - 1], samplesPerBeat(bpm), 1.0);
}

void process(Metronome& metronome, float bpm, int samples = kBlockSamples) {
  juce::AudioBuffer<float> block(1, samples);
  metronome.process(block, bpm, kFullGain);
}

Metronome preparedMetronome() {
  Metronome metronome;
  metronome.prepare(kFs);
  return metronome;
}

Metronome runningMetronome() {
  Metronome metronome = preparedMetronome();
  process(metronome, kDefaultBpm);
  return metronome;
}

void prepare(TONE3000Processor& proc) {
  proc.setPlayConfigDetails(2, 2, kFs, kBlockSamples);
  proc.prepareToPlay(kFs, kBlockSamples);
}

float processOneBlock(TONE3000Processor& proc) {
  juce::AudioBuffer<float> block(2, kBlockSamples);
  juce::MidiBuffer midi;
  block.clear();
  proc.processBlock(block, midi);
  return block.getMagnitude(0, block.getNumSamples());
}

float outputPeakAcrossTheFirstClick(TONE3000Processor& proc) {
  float highest = 0.0f;
  for (int i = 0; i < kBlocksToReachTheFirstClick; ++i)
    highest = std::max(highest, processOneBlock(proc));
  return highest;
}

void setParam(TONE3000Processor& proc, const char* id, float value) {
  auto* p = proc.parameters.getParameter(id);
  p->setValueNotifyingHost(p->convertTo0to1(value));
}

void setNormalised(TONE3000Processor& proc, const char* id, float normalised) {
  proc.parameters.getParameter(id)->setValueNotifyingHost(normalised);
}

float value(TONE3000Processor& proc, const char* id) {
  return proc.parameters.getRawParameterValue(id)->load();
}

void start(TONE3000Processor& proc) {
  setParam(proc, "metronomeEnabled", 1.0f);
}

float clickPeakAtVolume(float volume) {
  TONE3000Processor proc;
  prepare(proc);
  setParam(proc, "metronomeVolume", volume);
  start(proc);
  return outputPeakAcrossTheFirstClick(proc);
}

float clickPeakAtOutputLevel(float normalised) {
  TONE3000Processor proc;
  prepare(proc);
  setNormalised(proc, "outputLevel", normalised);
  start(proc);
  return outputPeakAcrossTheFirstClick(proc);
}

juce::MemoryBlock stateOf(TONE3000Processor& proc) {
  juce::MemoryBlock state;
  proc.getStateInformation(state);
  return state;
}

void restore(TONE3000Processor& proc, const juce::MemoryBlock& state) {
  proc.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
}

class MetronomeProcessorTest : public ::testing::Test {
protected:
  void SetUp() override { prepare(proc); }

  TONE3000Processor proc;
};

}  // namespace

TEST(MetronomeTest, ClicksOncePerBeatAt100Bpm) {
  const auto onsets = clickOnsets(render(kDefaultBpm, kFullGain, kThreeSeconds, kDefaultBlock));

  expectSpacedByOneBeat(onsets, kDefaultBpm);
}

TEST(MetronomeTest, ClicksOncePerBeatAtTheMaximumTempo) {
  const auto onsets = clickOnsets(render(kMaxBpm, kFullGain, kThreeSeconds, kDefaultBlock));

  expectSpacedByOneBeat(onsets, kMaxBpm);
}

TEST(MetronomeTest, FirstClickComesHalfABeatAfterStart) {
  const double halfABeat = Metronome::kLeadInBeats * samplesPerBeat(kDefaultBpm);

  const auto onsets = clickOnsets(render(kDefaultBpm, kFullGain, kThreeSeconds, kDefaultBlock));

  ASSERT_FALSE(onsets.empty());
  EXPECT_NEAR(onsets.front(), halfABeat, 2.0);
}

TEST(MetronomeTest, BeatsStartHalfABeatBeforeTheFirstClick) {
  const Metronome metronome = preparedMetronome();

  EXPECT_DOUBLE_EQ(metronome.beats(), -Metronome::kLeadInBeats);
}

TEST(MetronomeTest, BeatsAdvanceWithTheSamplesProcessed) {
  const float bpm = 120.0f;
  const double beatsInBlock = kBlockSamples * bpm / (60.0 * kFs);
  Metronome metronome = preparedMetronome();

  process(metronome, bpm);

  EXPECT_NEAR(metronome.beats(), -Metronome::kLeadInBeats + beatsInBlock, 1e-9);
}

TEST(MetronomeTest, ZeroBpmIsSilent) {
  const auto signal = render(kStopped, kFullGain, kOneSecond, kDefaultBlock);

  EXPECT_EQ(peak(signal), 0.0f);
}

TEST(MetronomeTest, GainScalesTheClick) {
  const float fullGainPeak = peak(render(kDefaultBpm, kFullGain, kOneSecond, kDefaultBlock));
  ASSERT_GT(fullGainPeak, 0.0f);

  const auto halfGain = render(kDefaultBpm, kHalfGain, kOneSecond, kDefaultBlock);

  EXPECT_NEAR(peak(halfGain), kHalfGain * fullGainPeak, 1e-6f);
}

TEST(MetronomeTest, FullGainClickReachesNearFullScale) {
  const auto signal = render(kDefaultBpm, kFullGain, kOneSecond, kDefaultBlock);

  EXPECT_GT(peak(signal), 0.9f);
}

TEST(MetronomeTest, BlockSizeDoesNotMoveTheBeats) {
  const int twoSeconds = kOneSecond * 2;
  const float awkwardBpm = 137.0f;

  const auto smallBlocks = clickOnsets(render(awkwardBpm, kFullGain, twoSeconds, kSmallBlock));
  const auto largeBlocks = clickOnsets(render(awkwardBpm, kFullGain, twoSeconds, kLargeBlock));

  EXPECT_EQ(smallBlocks, largeBlocks);
}

TEST(MetronomeTest, StoppingRearmsTheHalfBeatLeadIn) {
  Metronome metronome = runningMetronome();

  process(metronome, kStopped);

  EXPECT_DOUBLE_EQ(metronome.beats(), -Metronome::kLeadInBeats);
}

TEST_F(MetronomeProcessorTest, MetronomeStartsStopped) {
  EXPECT_FLOAT_EQ(value(proc, "metronomeEnabled"), 0.0f);
}

TEST_F(MetronomeProcessorTest, MetronomeDefaultsTo100Bpm) {
  EXPECT_FLOAT_EQ(value(proc, "metronomeBpm"), kDefaultBpm);
}

TEST_F(MetronomeProcessorTest, MetronomeDefaultsToLoudVolume) {
  EXPECT_FLOAT_EQ(value(proc, "metronomeVolume"), kDefaultVolume);
}

TEST_F(MetronomeProcessorTest, MetronomeBpmRangesFrom20To400) {
  auto* bpm = proc.parameters.getParameter("metronomeBpm");

  EXPECT_FLOAT_EQ(bpm->convertFrom0to1(0.0f), kMinBpm);
  EXPECT_FLOAT_EQ(bpm->convertFrom0to1(1.0f), kMaxBpm);
}

TEST_F(MetronomeProcessorTest, MetronomeBpmRoundsToWholeBeats) {
  setParam(proc, "metronomeBpm", 123.4f);

  EXPECT_FLOAT_EQ(value(proc, "metronomeBpm"), 123.0f);
}

TEST_F(MetronomeProcessorTest, StoppedMetronomeIsSilent) {
  EXPECT_EQ(outputPeakAcrossTheFirstClick(proc), 0.0f);
}

TEST_F(MetronomeProcessorTest, StartedMetronomeClicksOnTheOutput) {
  start(proc);

  EXPECT_GT(outputPeakAcrossTheFirstClick(proc), 0.0f);
}

TEST(MetronomeTest, VolumeFollowsASquareLaw) {
  const float fullVolumePeak = clickPeakAtVolume(1.0f);
  ASSERT_GT(fullVolumePeak, 0.0f);

  const float halfVolumePeak = clickPeakAtVolume(0.5f);

  EXPECT_NEAR(halfVolumePeak, 0.25f * fullVolumePeak, 1e-4f);
}

TEST(MetronomeTest, ClickIsTheSameAtTheLowestAndHighestOutputLevel) {
  const float lowestOutputPeak = clickPeakAtOutputLevel(0.0f);
  ASSERT_GT(lowestOutputPeak, 0.0f);

  const float highestOutputPeak = clickPeakAtOutputLevel(1.0f);

  EXPECT_FLOAT_EQ(highestOutputPeak, lowestOutputPeak);
}

TEST_F(MetronomeProcessorTest, BeatsStayStillWhileStopped) {
  processOneBlock(proc);

  EXPECT_DOUBLE_EQ(proc.getMetronomeBeats(), -Metronome::kLeadInBeats);
}

TEST_F(MetronomeProcessorTest, BeatsAdvanceWhileRunning) {
  const double beatsInBlock = kBlockSamples * kDefaultBpm / (60.0 * kFs);
  start(proc);

  processOneBlock(proc);

  EXPECT_NEAR(proc.getMetronomeBeats(), -Metronome::kLeadInBeats + beatsInBlock, 1e-6);
}

TEST_F(MetronomeProcessorTest, MetronomeStartsFromItsDefaultsOnAFreshInstance) {
  TONE3000Processor earlierSession;
  setParam(earlierSession, "metronomeBpm", 250.0f);
  setParam(earlierSession, "metronomeVolume", 0.1f);
  start(earlierSession);

  restore(proc, stateOf(earlierSession));

  EXPECT_FLOAT_EQ(value(proc, "metronomeBpm"), kDefaultBpm);
  EXPECT_FLOAT_EQ(value(proc, "metronomeVolume"), kDefaultVolume);
  EXPECT_FLOAT_EQ(value(proc, "metronomeEnabled"), 0.0f);
}

TEST_F(MetronomeProcessorTest, StateRestoredIntoARunningInstanceStopsTheMetronome) {
  setParam(proc, "metronomeBpm", 250.0f);
  start(proc);
  TONE3000Processor otherSession;

  restore(proc, stateOf(otherSession));

  EXPECT_FLOAT_EQ(value(proc, "metronomeBpm"), kDefaultBpm);
  EXPECT_FLOAT_EQ(value(proc, "metronomeEnabled"), 0.0f);
}
