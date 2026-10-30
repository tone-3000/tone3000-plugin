#include "RoutingTaps.h"
#include "chain_test_helpers.h"
#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <cstring>

namespace {
using t3k::routing::Stage;
using t3k::routing::TapPlan;
constexpr int blockSize = 256;

void parameter(TONE3000Processor& p, const char* id, float normalized) {
  auto* value = p.parameters.getParameter(id);
  ASSERT_NE(value, nullptr);
  value->setValueNotifyingHost(normalized);
}

void neutral(TONE3000Processor& p, int factor = 1) {
  p.setMultiCoreEnabled(false, false);
  parameter(p, "gateEnabled", 0.0f);
  parameter(p, "toneEqEnabled", 0.0f);
  parameter(p, "pitchEnabled", 0.0f);
  parameter(p, "osEnabled", factor == 1 ? 0.0f : 1.0f);
  parameter(p, "osFactor", factor == 8 ? 1.0f : factor == 4 ? 0.5f : 0.0f);
}

void seedMono(ChainTestProcessor& p, bool withInsert = false) {
  juce::ValueTree snapshot("ChainSnapshot"), left("ChainBlocks"), right("RightChainBlocks");
  snapshot.setProperty("stereoEnabled", false, nullptr);
  left.appendChild(makeIrBlockTree("stack", 1, 100), nullptr);
  if (withInsert) {
    juce::ValueTree insert("ChainBlock");
    insert.setProperty("id", "gap", nullptr);
    insert.setProperty("type", "insert", nullptr);
    left.appendChild(insert, nullptr);
  }
  left.appendChild(makeIrBlockTree("mod", 2, 101), nullptr);
  snapshot.appendChild(left, nullptr);
  snapshot.appendChild(right, nullptr);
  p.restoreFromTree(snapshot);
  ASSERT_TRUE(waitForChainLoaded(p));
}

using Channels = std::array<std::vector<float>, 2>;
struct Render {
  std::array<Channels, 3> stages;
  Channels master;
  std::array<std::array<bool, 2>, 3> available{};
};
Render render(TONE3000Processor& p, const std::vector<float>& l,
              const std::vector<float>& r, int block = blockSize) {
  EXPECT_EQ(l.size(), r.size());
  Render result;
  juce::AudioBuffer<float> buffer(2, block);
  juce::MidiBuffer midi;
  for (size_t offset = 0; offset + static_cast<size_t>(block) <= l.size(); offset += block) {
    buffer.copyFrom(0, 0, l.data() + offset, block);
    buffer.copyFrom(1, 0, r.data() + offset, block);
    p.processBlock(buffer, midi);
    for (int ch = 0; ch < 2; ++ch)
      result.master[static_cast<size_t>(ch)].insert(result.master[static_cast<size_t>(ch)].end(),
          buffer.getReadPointer(ch), buffer.getReadPointer(ch) + block);
    for (size_t stage = 0; stage < 3; ++stage) {
      const auto tap = p.routingTap(static_cast<Stage>(stage));
      result.available[stage] = tap.available;
      for (size_t ch = 0; ch < 2; ++ch) {
        auto& out = result.stages[stage][ch];
        if (tap.frames == block && tap.channels[ch] != nullptr)
          out.insert(out.end(), tap.channels[ch], tap.channels[ch] + block);
        else
          out.insert(out.end(), static_cast<size_t>(block), 0.0f);
      }
    }
  }
  return result;
}
void settle(TONE3000Processor& p, int count = 30) {
  const std::vector<float> zero(static_cast<size_t>(count * blockSize), 0.0f);
  render(p, zero, zero);
}
float difference(const std::vector<float>& a, const std::vector<float>& b) {
  EXPECT_EQ(a.size(), b.size());
  float result = 0.0f;
  for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
    result = std::max(result, std::abs(a[i] - b[i]));
  return result;
}
float peak(const std::vector<float>& a) {
  float result = 0.0f;
  for (const float x : a) result = std::max(result, std::abs(x));
  return result;
}
TapPlan monoRoles() {
  TapPlan plan;
  plan.enabled = true;
  plan.stackEnd[0] = "stack";
  plan.modEnd[0] = "mod";
  plan.wetStart[0] = "stack";
  return plan;
}

TEST(RoutingTapTest, ExactPostBlockStagesAndActualSerialWetBus) {
  ChainTestProcessor p;
  neutral(p);
  p.setPlayConfigDetails(2, 2, kFs, blockSize);
  p.prepareToPlay(kFs, blockSize);
  seedMono(p, true);
  ASSERT_TRUE(p.setBlockParam("stack", "mix", 0.25));
  ASSERT_TRUE(p.setBlockParam("mod", "mix", 0.5));
  // Non-unity post-mix gains are part of each tap and wet contribution.
  const double sixDb = 20.0 * std::log10(2.0);
  ASSERT_TRUE(p.setBlockParam("stack", "outputGain", 0.5 + sixDb / 48.0));
  ASSERT_TRUE(p.setBlockParam("mod", "outputGain", 0.5 - sixDb / 48.0));
  p.configureRoutingTaps(monoRoles());
  p.prepareToPlay(kFs, blockSize);
  settle(p);
  const auto input = makeNoise(20 * blockSize, 42, 0.2f);
  const auto out = render(p, input, input);
  for (const auto& available : out.available) {
    EXPECT_TRUE(available[0]);
    EXPECT_TRUE(available[1]);
  }
  EXPECT_GT(difference(out.stages[0][0], out.stages[1][0]), 0.01f);
  EXPECT_GT(peak(out.stages[2][0]), 0.0001f);
  // For these known serial mix/output gains, the residual dry coefficient
  // is (1-.25)*2*(1-.5)*.5=.375. This independent algebra rejects a
  // Master-minus-DI shortcut, omitted prior wet terms, and ignored Out Gain.
  float worst = 0.0f;
  for (size_t i = 0; i < input.size(); ++i) {
    const float expectedWet = out.stages[1][0][i] - input[i] * 0.375f;
    worst = std::max(worst, std::abs(expectedWet - out.stages[2][0][i]));
  }
  EXPECT_LT(worst, 2.0e-6f);
}

TEST(RoutingTapTest, BypassedEffectsPreserveZeroWetAndIdsNeverFallBackByPosition) {
  ChainTestProcessor p;
  neutral(p);
  p.setPlayConfigDetails(2, 2, kFs, blockSize);
  p.prepareToPlay(kFs, blockSize);
  seedMono(p);
  ASSERT_TRUE(p.setBlockParam("stack", "enabled", 0.0));
  ASSERT_TRUE(p.setBlockParam("mod", "enabled", 0.0));
  p.configureRoutingTaps(monoRoles());
  p.prepareToPlay(kFs, blockSize);
  settle(p);
  const auto input = makeNoise(8 * blockSize, 123, 0.2f);
  auto out = render(p, input, input);
  EXPECT_TRUE(out.available[2][0]);
  EXPECT_FLOAT_EQ(peak(out.stages[2][0]), 0.0f);
  EXPECT_FLOAT_EQ(difference(out.stages[0][0], input), 0.0f);
  letAudioGoIdle();
  ASSERT_TRUE(p.removeChainBlock("stack"));
  out = render(p, input, input);
  EXPECT_FALSE(out.available[0][0]);
  EXPECT_FALSE(out.available[2][0]);
  EXPECT_TRUE(out.available[1][0]);  // "mod" moved, but retained its UUID.
  EXPECT_FLOAT_EQ(peak(out.stages[0][0]), 0.0f);
  EXPECT_FLOAT_EQ(peak(out.stages[2][0]), 0.0f);
}

TEST(RoutingTapTest, StereoAndBranchRolesFollowTheirEffectivePaths) {
  ChainTestProcessor p;
  neutral(p);
  p.setMultiCoreEnabled(true, false);
  p.setPlayConfigDetails(2, 2, kFs, blockSize);
  p.prepareToPlay(kFs, blockSize);
  seedStereoChains(p, {"trunk", "tail"}, {"branch"});
  ASSERT_TRUE(waitForChainLoaded(p));
  ASSERT_TRUE(p.setBlockParam("branch", "mix", 0.4));
  TapPlan plan;
  plan.enabled = true;
  plan.stackEnd = {"trunk", "branch"};
  plan.modEnd = {"tail", "branch"};
  plan.wetStart = {"tail", "branch"};
  p.configureRoutingTaps(plan);
  p.prepareToPlay(kFs, blockSize);
  settle(p);
  const auto left = makeNoise(8 * blockSize, 41, 0.2f);
  const auto right = makeNoise(8 * blockSize, 93, 0.2f);
  auto independent = render(p, left, right);
  EXPECT_TRUE(independent.available[0][0]);
  EXPECT_TRUE(independent.available[0][1]);
  EXPECT_GT(difference(independent.stages[0][0], independent.stages[0][1]), 0.001f);

  letAudioGoIdle();
  ASSERT_TRUE(p.setChainBranch("left", "trunk"));
  plan.stackEnd[1] = "trunk";  // Shared ancestor is a valid branch role.
  plan.wetStart[1] = "trunk";
  p.configureRoutingTaps(plan);
  p.prepareToPlay(kFs, blockSize);
  settle(p);
  const auto branched = render(p, left, right);
  EXPECT_TRUE(branched.available[0][1]);
  EXPECT_FLOAT_EQ(difference(branched.stages[0][0], branched.stages[0][1]), 0.0f);
  EXPECT_GT(difference(branched.stages[1][0], branched.stages[1][1]), 0.0001f);

  // A block after the split on the trunk is not on the branch's path.
  plan.stackEnd[1] = "tail";
  p.configureRoutingTaps(plan);
  p.prepareToPlay(kFs, blockSize);
  const auto invalid = render(p, left, right);
  EXPECT_FALSE(invalid.available[0][1]);
  EXPECT_FLOAT_EQ(peak(invalid.stages[0][1]), 0.0f);
}

TEST(RoutingTapTest, DualMonoUsesPrimaryRolesForBothIndependentChannels) {
  ChainTestProcessor p;
  neutral(p);
  p.setPlayConfigDetails(2, 2, kFs, blockSize);
  p.prepareToPlay(kFs, blockSize);
  seedMono(p);
  p.setInputMode(TONE3000Processor::InputMode::DualMono);
  p.configureRoutingTaps(monoRoles());
  p.prepareToPlay(kFs, blockSize);
  settle(p);
  const auto left = makeNoise(8 * blockSize, 1, 0.2f);
  const std::vector<float> silent(left.size(), 0.0f);
  const auto out = render(p, left, silent);
  EXPECT_TRUE(out.available[0][0]);
  EXPECT_TRUE(out.available[0][1]);
  EXPECT_GT(peak(out.stages[0][0]), 0.001f);
  EXPECT_LT(peak(out.stages[0][1]), 1.0e-8f);
}

TEST(RoutingTapTest, AuxiliaryConvertersMatchMainClockAtEveryRateAndFactor) {
  for (const double rate : {44100.0, 48000.0, 96000.0}) {
    for (const int factor : {1, 2, 4, 8}) {
      SCOPED_TRACE("rate=" + std::to_string(rate) + " factor=" + std::to_string(factor));
      const int baseFrames = std::max(blockSize, static_cast<int>(std::ceil(blockSize * kFs / rate)));
      t3k::routing::detail::TapBuffers taps;
      taps.prepare(rate, blockSize, baseFrames, factor);
      ChainOversampler mainOs;
      mainOs.prepare(factor, baseFrames);
      std::unique_ptr<ChainBoundaryResampler> boundary;
      if (rate != kFs) {
        boundary = std::make_unique<ChainBoundaryResampler>(kFs);
        boundary->Reset(rate, blockSize);
      }
      std::array<std::array<bool, 2>, 3> available{};
      bool roleAvailable = true;
      ChainBoundaryResampler::BlockProcessFunc process = [&](float** ins, float** outs, int frames) {
        mainOs.process(ins, outs, frames, [&](float** osIn, float** osOut, int osFrames) {
          taps.beginOversampledSlice(osFrames);
          if (roleAvailable)
            for (size_t stage = 0; stage < 3; ++stage)
              for (int ch = 0; ch < 2; ++ch)
                std::memcpy(taps.osChannel(static_cast<Stage>(stage), ch), osIn[ch],
                            sizeof(float) * static_cast<size_t>(osFrames));
          for (int ch = 0; ch < 2; ++ch) {
            if (!roleAvailable)
              juce::FloatVectorOperations::clear(osOut[ch], osFrames);
            else if (osOut[ch] != osIn[ch])
              std::memcpy(osOut[ch], osIn[ch], sizeof(float) * static_cast<size_t>(osFrames));
          }
          taps.appendOversampledSlice(osFrames, available);
        });
      };
      juce::AudioBuffer<float> input(2, blockSize), output(2, blockSize);
      const auto noise = makeNoise(40 * blockSize, 667, 0.2f);
      float worst = 0.0f;
      for (int block = 0; block < 40; ++block) {
        roleAvailable = block % 7 != 3;
        for (auto& stage : available) stage.fill(roleAvailable);
        const int frames = block % 3 == 0 ? 63 : block % 3 == 1 ? 191 : blockSize;
        for (int ch = 0; ch < 2; ++ch)
          input.copyFrom(ch, 0, noise.data() + block * blockSize, frames, ch == 0 ? 1.0f : -0.7f);
        taps.beginHostSlice();
        float* inputChannels[2] = {input.getWritePointer(0), input.getWritePointer(1)};
        float* outputChannels[2] = {output.getWritePointer(0), output.getWritePointer(1)};
        if (boundary)
          boundary->ProcessBlock(inputChannels, outputChannels, frames, process);
        else
          process(inputChannels, outputChannels, frames);
        taps.finishHostSlice(frames, true);
        for (size_t stage = 0; stage < 3; ++stage) {
          const auto view = taps.view(static_cast<Stage>(stage));
          ASSERT_EQ(view.frames, frames);
          ASSERT_EQ(view.available[0], roleAvailable);
          ASSERT_EQ(view.available[1], roleAvailable);
          for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < frames; ++i) {
              const float expected = roleAvailable ? output.getSample(ch, i) : 0.0f;
              worst = std::max(worst, std::abs(view.channels[static_cast<size_t>(ch)][i] - expected));
            }
        }
      }
      EXPECT_LT(worst, 1.0e-7f);
    }
  }
}

TEST(RoutingTapTest, RealChainStageBusesAlignAcrossRatesAndOversampling) {
  for (const double rate : {44100.0, 48000.0, 96000.0}) {
    for (const int factor : {1, 2, 4, 8}) {
      SCOPED_TRACE("rate=" + std::to_string(rate) + " factor=" + std::to_string(factor));
      ChainTestProcessor p;
      neutral(p, factor);
      p.setPlayConfigDetails(2, 2, rate, blockSize);
      p.prepareToPlay(rate, blockSize);
      seedStereoChains(p, {"only"}, {});
      ASSERT_TRUE(waitForChainLoaded(p));
      TapPlan plan;
      plan.enabled = true;
      plan.stackEnd[0] = plan.modEnd[0] = plan.wetStart[0] = "only";
      p.configureRoutingTaps(plan);
      p.prepareToPlay(rate, blockSize);
      settle(p);
      const auto input = makeNoise(12 * blockSize, 431, 0.1f);
      const auto out = render(p, input, input);
      ASSERT_TRUE(out.available[0][0]);
      ASSERT_TRUE(out.available[1][0]);
      ASSERT_TRUE(out.available[2][0]);
      EXPECT_GT(peak(out.stages[0][0]), 0.0001f);
      EXPECT_FLOAT_EQ(difference(out.stages[0][0], out.stages[1][0]), 0.0f);
      EXPECT_FLOAT_EQ(difference(out.stages[0][0], out.stages[2][0]), 0.0f);
      EXPECT_FALSE(out.available[0][1]);
    }
  }
}

TEST(RoutingTapTest, TapsAreOptionalAndDoNotAlterTheMainSignal) {
  ChainTestProcessor reference, enabled, disabled;
  for (auto* p : {&reference, &enabled, &disabled}) {
    neutral(*p);
    p->setPlayConfigDetails(2, 2, kFs, blockSize);
    p->prepareToPlay(kFs, blockSize);
    seedMono(*p);
  }
  enabled.configureRoutingTaps(monoRoles());
  auto off = monoRoles();
  off.enabled = false;
  disabled.configureRoutingTaps(off);
  for (auto* p : {&reference, &enabled, &disabled}) {
    p->prepareToPlay(kFs, blockSize);
    settle(*p);
  }
  const auto input = makeNoise(12 * blockSize, 553, 0.2f);
  const auto a = render(reference, input, input);
  const auto b = render(enabled, input, input);
  const auto c = render(disabled, input, input);
  EXPECT_FLOAT_EQ(difference(a.master[0], b.master[0]), 0.0f);
  EXPECT_FLOAT_EQ(difference(a.master[0], c.master[0]), 0.0f);
  EXPECT_EQ(disabled.routingTap(Stage::stack).frames, 0);
  EXPECT_FALSE(disabled.routingTap(Stage::stack).available[0]);
}

TEST(RoutingTapTest, OversizedHostCallbacksDoNotExposePartialOrStaleTapBuffers) {
  ChainTestProcessor p;
  neutral(p);
  p.setPlayConfigDetails(2, 2, 44100.0, blockSize);
  p.prepareToPlay(44100.0, blockSize);
  seedMono(p);
  p.configureRoutingTaps(monoRoles());
  p.prepareToPlay(44100.0, blockSize);
  settle(p);
  const auto oversized = makeNoise(2 * blockSize, 11, 0.2f);
  render(p, oversized, oversized, 2 * blockSize);
  EXPECT_EQ(p.routingTap(Stage::stack).frames, 0);
  const auto normal = makeNoise(blockSize, 12, 0.2f);
  const auto out = render(p, normal, normal);
  EXPECT_TRUE(out.available[0][0]);
  EXPECT_EQ(p.routingTap(Stage::stack).frames, blockSize);
}

TEST(RoutingTapTest, ShadowReadsLiveGlobalControlsAndSelectedInputMode) {
  ChainTestProcessor source, shadow;
  neutral(source);
  source.setPlayConfigDetails(2, 2, kFs, blockSize);
  source.prepareToPlay(kFs, blockSize);
  juce::MemoryBlock state;
  source.getStateInformation(state);
  shadow.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  ASSERT_TRUE(waitForChainLoaded(shadow));
  shadow.followRoutingControlsFrom(&source);
  shadow.setRoutingIoOverride(true, true);
  shadow.setPlayConfigDetails(2, 2, kFs, blockSize);
  shadow.prepareToPlay(kFs, blockSize);
  source.setInputMode(TONE3000Processor::InputMode::Left);
  parameter(source, "inputLevel", 0.625f);
  parameter(source, "outputLevel", 0.4f);
  const auto left = makeNoise(16 * blockSize, 24, 0.2f);
  const auto right = makeNoise(16 * blockSize, 25, 0.2f);
  const auto a = render(source, left, right);
  const auto b = render(shadow, left, right);
  EXPECT_FLOAT_EQ(difference(a.master[0], b.master[0]), 0.0f);
  EXPECT_FLOAT_EQ(difference(a.master[1], b.master[1]), 0.0f);
  EXPECT_EQ(shadow.getInputMode(), TONE3000Processor::InputMode::Left);
  shadow.followRoutingControlsFrom(nullptr);
}

TEST(RoutingTapTest, LogicalIoOverrideUsesSelectedFeedInsteadOfPhysicalMasks) {
  ChainTestProcessor p;
  neutral(p);
  p.setPlayConfigDetails(2, 2, kFs, blockSize);
  p.setRoutingIoOverride(true, false);
  p.prepareToPlay(kFs, blockSize);
  auto state = p.getChainState(-1);
  EXPECT_FALSE(static_cast<bool>(state["stereoInput"]));
  EXPECT_TRUE(static_cast<bool>(state["stereoOutput"]));
  p.setRoutingIoOverride(true, true);
  p.prepareToPlay(kFs, blockSize);
  state = p.getChainState(-1);
  EXPECT_TRUE(static_cast<bool>(state["stereoInput"]));
  EXPECT_TRUE(static_cast<bool>(state["stereoOutput"]));
}
}  // namespace
