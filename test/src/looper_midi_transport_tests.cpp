#include <gtest/gtest.h>
#include "GlobalLooper.h"
#include <vector>

TEST(LooperMidiTransport, SameCommandRecordsLoopsThenReplacesWithoutOverdub) {
  GlobalLooper loop;
  loop.prepare(1000);
  std::vector<float> samples(200, 0.25f);
  loop.request(GlobalLooper::Command::toggleRecord);
  loop.process(samples.data(), nullptr, 200);
  EXPECT_EQ(loop.getState(), MonoLooper::State::recording);
  EXPECT_DOUBLE_EQ(loop.seconds(), 0.2);
  loop.request(GlobalLooper::Command::toggleRecord);
  std::fill(samples.begin(), samples.end(), 0);
  loop.process(samples.data(), nullptr, 200);
  EXPECT_EQ(loop.getState(), MonoLooper::State::playing);
  EXPECT_NEAR(samples[100], 0.25f, 1e-6);
  loop.request(GlobalLooper::Command::toggleRecord);
  std::fill(samples.begin(), samples.end(), 0.75f);
  loop.process(samples.data(), nullptr, 160);
  EXPECT_EQ(loop.getState(), MonoLooper::State::recording);
  EXPECT_DOUBLE_EQ(loop.seconds(), 0.16);
  EXPECT_FLOAT_EQ(samples[100], 0.75f); // no old loop mixed into the new take
  loop.request(GlobalLooper::Command::toggleRecord);
  std::fill(samples.begin(), samples.end(), 0);
  loop.process(samples.data(), nullptr, 160);
  EXPECT_EQ(loop.getState(), MonoLooper::State::playing);
  EXPECT_NEAR(samples[80], 0.75f, 1e-6);
}

TEST(LooperMidiTransport, FortySecondLimitLoopsButTouchRecordStillStops) {
  GlobalLooper loop;
  loop.prepare(1000);
  std::vector<float> samples(41000, 0.25f);
  loop.request(GlobalLooper::Command::toggleRecord);
  loop.process(samples.data(), nullptr, samples.size());
  EXPECT_DOUBLE_EQ(loop.seconds(), 40);
  EXPECT_EQ(loop.getState(), MonoLooper::State::playing);
  EXPECT_NEAR(samples[40500], 0.5f, 1e-6);
  loop.request(GlobalLooper::Command::record);
  loop.process(samples.data(), nullptr, samples.size());
  EXPECT_EQ(loop.getState(), MonoLooper::State::stopped);
}

TEST(LooperMidiTransport, QuickDoublePressFinishesEmptyTakeAndStopCancelsPendingPress) {
  GlobalLooper loop;
  loop.prepare(1000);
  std::vector<float> samples(200, 0.25f);
  loop.request(GlobalLooper::Command::toggleRecord);
  loop.request(GlobalLooper::Command::toggleRecord);
  loop.process(samples.data(), nullptr, 200);
  EXPECT_DOUBLE_EQ(loop.seconds(), 0);
  EXPECT_EQ(loop.getState(), MonoLooper::State::stopped);
  loop.request(GlobalLooper::Command::toggleRecord);
  loop.request(GlobalLooper::Command::stop);
  loop.process(samples.data(), nullptr, 200);
  EXPECT_EQ(loop.getState(), MonoLooper::State::stopped);
}

TEST(LooperMidiTransport, StoppedTakeIsReplacedAndRateChangeClearsTake) {
  GlobalLooper loop;
  loop.prepare(1000);
  std::vector<float> samples(200, 0.25f);
  loop.request(GlobalLooper::Command::record);
  loop.process(samples.data(), nullptr, 200);
  loop.request(GlobalLooper::Command::stop);
  loop.process(samples.data(), nullptr, 200);
  loop.request(GlobalLooper::Command::toggleRecord);
  loop.process(samples.data(), nullptr, 100);
  EXPECT_EQ(loop.getState(), MonoLooper::State::recording);
  EXPECT_DOUBLE_EQ(loop.seconds(), 0.1);
  loop.prepare(2000);
  EXPECT_DOUBLE_EQ(loop.seconds(), 0);
  EXPECT_EQ(loop.getState(), MonoLooper::State::stopped);
}
