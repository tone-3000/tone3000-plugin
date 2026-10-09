#include <gtest/gtest.h>
#include "MonoLooper.h"
#include <vector>

TEST(MonoLooper, RecordsMonoAndRepeatsWithoutOverdub) {
  MonoLooper loop;
  loop.prepare(1000);
  std::vector<float> left(200, 0.2f), right(200, 0.6f);
  loop.record();
  loop.process(left.data(), right.data(), 200, 1, true);
  EXPECT_FLOAT_EQ(left[100], 0.2f); // monitor unchanged while recording
  EXPECT_DOUBLE_EQ(loop.seconds(), 0.2);
  loop.stop();
  loop.play();
  std::fill(left.begin(), left.end(), 0);
  std::fill(right.begin(), right.end(), 0);
  loop.process(left.data(), right.data(), 200, 1, true);
  EXPECT_NEAR(left[100], 0.8f, 1e-6);
  EXPECT_EQ(left, right);
  std::vector<float> second(200, 0), secondRight(200, 0);
  loop.process(second.data(), secondRight.data(), 200, 1, true);
  // Full-scale Mix reaches gain 2 after the initial 10 ms playback ramp.
  for (int i = 10; i < 200; ++i) EXPECT_FLOAT_EQ(left[i], second[i]);
}

TEST(MonoLooper, RecordReplacesTakeAndLimitsAtFortySeconds) {
  MonoLooper loop;
  loop.prepare(1000);
  std::vector<float> samples(41000, 0.25f);
  loop.record();
  loop.process(samples.data(), nullptr, samples.size(), 1, true);
  EXPECT_EQ(loop.getState(), MonoLooper::State::stopped);
  EXPECT_DOUBLE_EQ(loop.seconds(), 40);
  loop.record();
  EXPECT_DOUBLE_EQ(loop.seconds(), 0);
  std::fill(samples.begin(), samples.end(), 0.75f);
  loop.process(samples.data(), nullptr, 200, 1, true);
  EXPECT_DOUBLE_EQ(loop.seconds(), 0.2);
  loop.stop(); loop.play();
  std::fill(samples.begin(), samples.end(), 0);
  loop.process(samples.data(), nullptr, 200, 1, true);
  EXPECT_NEAR(samples[100], 1.5f, 1e-6);
}

TEST(MonoLooper, MixStopBypassEmptyPlayAndRateChanges) {
  MonoLooper loop;
  loop.prepare(1000); loop.play();
  EXPECT_EQ(loop.getState(), MonoLooper::State::stopped);
  std::vector<float> samples(200, 0.8f);
  loop.record(); loop.process(samples.data(), nullptr, 200, 1, true); loop.stop(); loop.play();
  std::fill(samples.begin(), samples.end(), 0.2f);
  loop.process(samples.data(), nullptr, 200, 0.5f, true);
  EXPECT_NEAR(samples[100], 1.0f, 1e-6);
  loop.stop();
  std::fill(samples.begin(), samples.end(), 0.2f);
  loop.process(samples.data(), nullptr, 200, 1, true);
  EXPECT_FLOAT_EQ(samples[100], 0.2f);
  loop.play();
  std::fill(samples.begin(), samples.end(), 0.2f);
  loop.process(samples.data(), nullptr, 200, 0, true);
  EXPECT_FLOAT_EQ(samples[100], 0.2f);
  loop.record(); loop.process(samples.data(), nullptr, 200, 1, false);
  EXPECT_EQ(loop.getState(), MonoLooper::State::stopped);
  loop.prepare(1000); EXPECT_DOUBLE_EQ(loop.seconds(), 0);
  loop.record(); loop.process(samples.data(), nullptr, 100, 1, true);
  loop.prepare(2000);
  EXPECT_DOUBLE_EQ(loop.seconds(), 0);
  EXPECT_EQ(loop.getState(), MonoLooper::State::stopped);
}

#include "GlobalLooper.h"
#include "chain_test_helpers.h"

TEST(GlobalLooper, MixAndPanAffectOnlyPlaybackAndCommandsKeepRecordDeletion) {
  GlobalLooper loop;
  loop.prepare(1000);
  std::vector<float> left(200, 0.8f), right(200, 0.8f);
  loop.request(GlobalLooper::Command::record);
  loop.process(left.data(), right.data(), 200);
  EXPECT_DOUBLE_EQ(loop.seconds(), 0.2);
  EXPECT_FLOAT_EQ(left[100], 0.8f);
  EXPECT_FLOAT_EQ(right[100], 0.8f);
  loop.setMix(0.5f); loop.setPan(-1);
  loop.request(GlobalLooper::Command::play);
  std::fill(left.begin(), left.end(), 0.2f);
  std::fill(right.begin(), right.end(), 0.2f);
  loop.process(left.data(), right.data(), 200);
  EXPECT_NEAR(left[100], 1.0f, 1e-6);
  EXPECT_NEAR(right[100], 0.2f, 1e-6);
  loop.setPan(1);
  std::fill(left.begin(), left.end(), 0.2f);
  std::fill(right.begin(), right.end(), 0.2f);
  loop.process(left.data(), right.data(), 200);
  EXPECT_NEAR(left[100], 0.2f, 1e-6);
  EXPECT_NEAR(right[100], 1.0f, 1e-6);
  // A quick Record -> Stop between callbacks still deletes the old take.
  loop.request(GlobalLooper::Command::record);
  loop.request(GlobalLooper::Command::stop);
  loop.process(left.data(), right.data(), 200);
  EXPECT_DOUBLE_EQ(loop.seconds(), 0);
  EXPECT_EQ(loop.getState(), MonoLooper::State::stopped);
}

TEST(GlobalLooper, FortySecondCutoffAndHostRateChanges) {
  GlobalLooper loop;
  loop.prepare(1000);
  std::vector<float> audio(41000, 0.25f);
  loop.request(GlobalLooper::Command::record);
  loop.process(audio.data(), nullptr, audio.size());
  EXPECT_DOUBLE_EQ(loop.seconds(), 40);
  EXPECT_EQ(loop.getState(), MonoLooper::State::stopped);
  loop.prepare(1000);
  EXPECT_DOUBLE_EQ(loop.seconds(), 40);
  loop.prepare(2000);
  EXPECT_DOUBLE_EQ(loop.seconds(), 0);
  loop.request(GlobalLooper::Command::play);
  loop.process(audio.data(), nullptr, 100);
  EXPECT_EQ(loop.getState(), MonoLooper::State::stopped);
}

TEST(GlobalLooperProcessor, PlaysPostChainAndSurvivesPresetRestoreWithMixAndPan) {
  ChainTestProcessor proc;
  proc.setPlayConfigDetails(2, 2, 48000, 480);
  proc.prepareToPlay(48000, 480);
  ASSERT_TRUE(proc.looperCommand("record"));
  juce::AudioBuffer<float> buffer(2, 480);
  juce::MidiBuffer midi;
  for (int b = 0; b < 10; ++b) {
    for (int i = 0; i < 480; ++i) {
      const float value = 0.2f * std::sin(2.0 * juce::MathConstants<double>::pi * (b * 480 + i) / 48.0);
      buffer.setSample(0, i, value); buffer.setSample(1, i, value);
    }
    proc.processBlock(buffer, midi);
  }
  EXPECT_NEAR(static_cast<double>(proc.getLooperState()["seconds"]), 0.1, 0.001);
  proc.setLooperMix(1); proc.setLooperPan(-1);
  ASSERT_TRUE(proc.looperCommand("play"));
  for (int b = 0; b < 10; ++b) { buffer.clear(); proc.processBlock(buffer, midi); }
  EXPECT_GT(buffer.getMagnitude(0, 0, 480), 0.05f);
  EXPECT_LT(buffer.getMagnitude(1, 0, 480), 0.01f);
  // Preset restoration changes the chains but leaves the global take/controls.
  juce::ValueTree snapshot("ChainSnapshot"), lane("ChainBlocks");
  snapshot.appendChild(lane, nullptr);
  proc.restoreFromTree(snapshot);
  EXPECT_EQ(proc.getLooperState()["state"].toString(), "Playing");
  EXPECT_NEAR(static_cast<double>(proc.getLooperState()["seconds"]), 0.1, 0.001);
  EXPECT_FLOAT_EQ(static_cast<float>(proc.getLooperState()["mix"]), 1);
  EXPECT_FLOAT_EQ(static_cast<float>(proc.getLooperState()["pan"]), -1);
  proc.setLooperPan(1);
  for (int b = 0; b < 10; ++b) { buffer.clear(); proc.processBlock(buffer, midi); }
  EXPECT_LT(buffer.getMagnitude(0, 0, 480), 0.01f);
  EXPECT_GT(buffer.getMagnitude(1, 0, 480), 0.05f);
  EXPECT_GE(proc.getTailLengthSeconds(), 40);
  juce::MemoryBlock saved;
  proc.getStateInformation(saved);
  ChainTestProcessor restarted;
  restarted.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  EXPECT_DOUBLE_EQ(static_cast<double>(restarted.getLooperState()["seconds"]), 0);
  EXPECT_FALSE(proc.looperCommand("overdub"));
  proc.releaseResources();
}

TEST(GlobalLooperProcessor, RetiredExperimentalBlockIsRemovedFromOldPresets) {
  ChainTestProcessor proc;
  juce::ValueTree snapshot("ChainSnapshot"), lane("ChainBlocks"), block("ChainBlock");
  block.setProperty("id", "old-looper", nullptr);
  block.setProperty("type", "looper", nullptr);
  lane.appendChild(block, nullptr); snapshot.appendChild(lane, nullptr);
  proc.restoreFromTree(snapshot);
  const auto state = proc.getChainState(-1);
  for (const auto& item : *state["chain"].getArray()) EXPECT_EQ(item["kind"].toString(), "insert");
}

TEST(GlobalLooperProcessor, MidiRecordingRequiresEnableAndTogglesToPlayback) {
  ChainTestProcessor proc;
  proc.setPlayConfigDetails(2, 2, 48000, 480);
  proc.prepareToPlay(48000, 480);
  ASSERT_TRUE(proc.midiMapper.setCcMapping("looperRecord", 6));
  juce::AudioBuffer<float> buffer(2, 480);
  juce::MidiBuffer press;
  press.addEvent(juce::MidiMessage::controllerEvent(1, 6, 127), 0);
  buffer.clear();
  proc.processBlock(buffer, press);
  EXPECT_EQ(proc.getLooperState()["state"].toString(), "Stopped");
  proc.setLooperMidiEnabled(true);
  proc.processBlock(buffer, press);
  EXPECT_EQ(proc.getLooperState()["state"].toString(), "Recording");
  juce::MidiBuffer release;
  release.addEvent(juce::MidiMessage::controllerEvent(1, 6, 0), 0);
  proc.processBlock(buffer, release);
  EXPECT_EQ(proc.getLooperState()["state"].toString(), "Recording");
  proc.processBlock(buffer, press);
  EXPECT_EQ(proc.getLooperState()["state"].toString(), "Playing");
  proc.processBlock(buffer, press);
  EXPECT_EQ(proc.getLooperState()["state"].toString(), "Recording");
  EXPECT_NEAR(static_cast<double>(proc.getLooperState()["seconds"]), 0.01, 1e-6);
  proc.setLooperMidiEnabled(false);
  proc.looperCommand("stop"); // same Stop request as disabling in the UI
  proc.processBlock(buffer, press);
  EXPECT_EQ(proc.getLooperState()["state"].toString(), "Stopped");
  proc.releaseResources();
}

TEST(GlobalLooper, MonoOutputIgnoresPanWithoutAttenuatingTheLoop) {
  GlobalLooper loop;
  loop.prepare(1000); loop.setMix(1); loop.setPan(1);
  std::vector<float> left(200, 0.8f), right(200, 0.8f);
  loop.request(GlobalLooper::Command::record);
  loop.process(left.data(), right.data(), 200, false);
  loop.request(GlobalLooper::Command::play);
  std::fill(left.begin(), left.end(), 0);
  std::fill(right.begin(), right.end(), 0);
  loop.process(left.data(), right.data(), 200, false);
  EXPECT_NEAR(left[100], 1.6f, 1e-6);
  EXPECT_NEAR(right[100], 1.6f, 1e-6);
}

TEST(GlobalLooperProcessor, RecordsAfterOutputAndPlaybackIgnoresLaterOutputChanges) {
  ChainTestProcessor proc;
  proc.parameters.getParameter("outputLevel")->setValueNotifyingHost(0.25f);
  proc.setPlayConfigDetails(2, 2, 48000, 480);
  proc.prepareToPlay(48000, 480);
  proc.setLooperMix(0.5f);
  proc.setLooperPan(-1);
  juce::AudioBuffer<float> buffer(2, 480);
  juce::MidiBuffer midi;
  auto feedGuitar = [&] {
    for (int i = 0; i < 480; ++i) {
      const float sample = 0.2f * std::sin(2.0 * juce::MathConstants<double>::pi * i / 48.0);
      buffer.setSample(0, i, sample); buffer.setSample(1, i, sample);
    }
    proc.processBlock(buffer, midi);
  };
  for (int b = 0; b < 20; ++b) feedGuitar();
  ASSERT_TRUE(proc.looperCommand("record"));
  for (int b = 0; b < 10; ++b) feedGuitar();
  const float recordedPeak = buffer.getMagnitude(0, 0, 480);
  ASSERT_GT(recordedPeak, 0.01f);
  ASSERT_TRUE(proc.looperCommand("play"));
  auto playSilence = [&] {
    // The live path includes a 5 Hz DC blocker. Its decay is still audible
    // after 100 ms, and raising Output amplifies that residual live signal.
    // Drain its documented 2-second tail before measuring loop playback.
    // Keep the strict tolerances: the loop itself must not follow Output.
    for (int b = 0; b < 200; ++b) { buffer.clear(); proc.processBlock(buffer, midi); }
    return buffer.getMagnitude(0, 0, 480);
  };
  const float before = playSilence();
  EXPECT_NEAR(before, recordedPeak, 1e-4f);
  proc.parameters.getParameter("outputLevel")->setValueNotifyingHost(1.0f);
  const float after = playSilence();
  EXPECT_NEAR(after, before, 1e-5f);
  EXPECT_LT(buffer.getMagnitude(1, 0, 480), 1e-5f);
  proc.releaseResources();
}
