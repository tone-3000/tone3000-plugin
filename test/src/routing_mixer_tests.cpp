#include "RoutingMixer.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {
using namespace t3k::routing;
constexpr int frames = 4096;
constexpr double rate = 48000.0;
using Packing = std::array<std::array<int, 2>, portCount>;

Config blankConfig() {
  Config config;
  for (auto& output : config.outputs) output = Output{};
  return config;
}
Packing noPacking() {
  Packing result;
  for (auto& pair : result) pair = {-1, -1};
  return result;
}
struct Audio {
  std::vector<float> di, stackL, stackR, modL, modR, masterL, masterR, wetL, wetR, retL, retR, auxL, auxR;
  explicit Audio(int count = frames)
      : di(count, 0.17f), stackL(count, 0.2f), stackR(count, 0.6f),
        modL(count, -0.3f), modR(count, 0.7f), masterL(count, 0.8f), masterR(count, -0.2f),
        wetL(count, 0.031f), wetR(count, -0.047f), retL(count, 0.09f), retR(count, -0.11f),
        auxL(count, 0.13f), auxR(count, -0.05f) {}
  Signals signals() const {
    return {di.data(), {{stackL.data(), stackR.data()}}, {{modL.data(), modR.data()}},
            {{masterL.data(), masterR.data()}}, {{wetL.data(), wetR.data()}},
            {{retL.data(), retR.data()}}, {{auxL.data(), auxR.data()}}};
  }
};
struct Render {
  std::vector<std::vector<float>> audio;
  std::vector<float*> pointers;
  Render(int channels, int count = frames) : audio(channels, std::vector<float>(count, 0)) {
    for (auto& channel : audio) pointers.push_back(channel.data());
  }
  void clear() { for (auto& channel : audio) std::fill(channel.begin(), channel.end(), 0); }
};
void expectTail(const std::vector<float>& values, float expected, float tolerance = 0.00002f) {
  ASSERT_GE(values.size(), 1024u);
  for (size_t i = values.size() - 512; i < values.size(); ++i)
    ASSERT_NEAR(values[i], expected, tolerance) << "frame " << i;
}

TEST(RoutingMixerTest, SourcesExposeIndependentStageAndDiProcessedPairSignals) {
  const Audio input;
  const auto signals = input.signals();
  struct Expected { Source source; float left, right; };
  const Expected cases[]{
    {Source::off,0,0}, {Source::di,0.17f,0.17f}, {Source::stack,0.4f,0.4f},
    {Source::modLeft,-0.3f,-0.3f}, {Source::modRight,0.7f,0.7f}, {Source::modMono,0.2f,0.2f},
    {Source::modStereo,-0.3f,0.7f}, {Source::masterLeft,0.8f,0.8f},
    {Source::masterRight,-0.2f,-0.2f}, {Source::masterMono,0.3f,0.3f},
    {Source::masterStereo,0.8f,-0.2f}, {Source::wetFx,0.031f,-0.047f},
    {Source::diStack,0.17f,0.4f}, {Source::diModLeft,0.17f,-0.3f},
    {Source::diModMono,0.17f,0.2f}, {Source::diMasterLeft,0.17f,0.8f},
    {Source::diMasterMono,0.17f,0.3f}, {Source::returns,0.09f,-0.11f}
  };
  ASSERT_EQ(std::size(cases), sources().size());
  for (const auto& item : cases) {
    EXPECT_NEAR(RoutingMixer::sourceSample(item.source, signals, 0, 0), item.left, 0.000001f) << sourceName(item.source);
    EXPECT_NEAR(RoutingMixer::sourceSample(item.source, signals, 1, 0), item.right, 0.000001f) << sourceName(item.source);
  }
}

TEST(RoutingMixerTest, StereoPolarityIsPreservedAndMonoUsesAnAverage) {
  Audio input;
  std::fill(input.masterL.begin(), input.masterL.end(), 0.75f);
  std::fill(input.masterR.begin(), input.masterR.end(), -0.75f);
  auto config = blankConfig();
  config.outputs[0].source = Source::masterStereo;
  config.outputs[0].left = 0; config.outputs[0].right = 1;
  auto packing = noPacking(); packing[0] = {0,1};
  RoutingMixer mixer;
  mixer.prepare(config, rate, packing);
  Render stereo(2);
  mixer.process(input.signals(), nullptr, stereo.pointers.data(), 2, frames, true);
  expectTail(stereo.audio[0], 0.75f);
  expectTail(stereo.audio[1], -0.75f);
  config.outputs[0].stereo = false;
  mixer.prepare(config, rate, packing);
  stereo.clear();
  mixer.process(input.signals(), nullptr, stereo.pointers.data(), 2, frames, true);
  expectTail(stereo.audio[0], 0.0f);
  expectTail(stereo.audio[1], 0.0f);
}

TEST(RoutingMixerTest, DiMasterSplitCannotBeAccidentallyFoldedIntoMono) {
  const Audio input;
  auto config = blankConfig();
  config.outputs[0].source = Source::diMasterMono;
  config.outputs[0].stereo = false; // split pair must retain distinct DI/processed channels
  config.outputs[0].left = 0; config.outputs[0].right = 1;
  auto packing = noPacking(); packing[0] = {0,1};
  RoutingMixer mixer; mixer.prepare(config, rate, packing);
  Render output(2);
  mixer.process(input.signals(), nullptr, output.pointers.data(), 2, frames, true);
  expectTail(output.audio[0], 0.17f);
  expectTail(output.audio[1], 0.3f);
}

TEST(RoutingMixerTest, RawDiIsIndependentOfProcessedInputGainAndUnlinkedMaster) {
  Audio input;
  for (int i = 0; i < frames; ++i)
    input.di[static_cast<size_t>(i)] = 0.29f * std::sin(static_cast<float>(i) * 0.017f);
  const auto original = input.di;
  auto config = blankConfig();
  config.inputGainDb = 24; config.reampSensitivityDb = -24; config.masterVolumeDb = -60;
  config.outputs[0].source = Source::di; config.outputs[0].left = 0;
  config.outputs[0].linkMaster = false;
  auto packing = noPacking(); packing[0] = {0,-1};
  RoutingMixer mixer; mixer.prepare(config, rate, packing);
  Render output(1);
  mixer.process(input.signals(), nullptr, output.pointers.data(), 1, frames, true);
  for (int i = 1024; i < frames; ++i)
    ASSERT_NEAR(output.audio[0][static_cast<size_t>(i)], original[static_cast<size_t>(i)], 0.000001f);
  EXPECT_EQ(input.di, original);
}

TEST(RoutingMixerTest, SixDestinationsUseTheirOwnSourcesAndDoNotLeakIntoUnusedChannels) {
  const Audio input;
  auto config = blankConfig();
  const Source source[]{Source::di, Source::stack, Source::modRight,
                         Source::modStereo, Source::diMasterMono, Source::returns};
  const std::array<std::array<int,2>,6> physical{{{0,-1},{1,-1},{2,-1},{3,4},{5,6},{7,8}}};
  auto packing = noPacking();
  for (size_t i = 0; i < portCount; ++i) {
    config.outputs[i].source = source[i]; config.outputs[i].left = physical[i][0];
    config.outputs[i].right = physical[i][1]; packing[i] = physical[i];
  }
  ASSERT_TRUE(validate(config, 4, 10).isEmpty()) << validate(config, 4, 10).joinIntoString("; ").toStdString();
  RoutingMixer mixer; mixer.prepare(config, rate, packing);
  Render output(10);
  mixer.process(input.signals(), nullptr, output.pointers.data(), 10, frames, true);
  const float expected[]{0.17f,0.4f,0.7f,-0.3f,0.7f,0.17f,0.3f,0.09f,-0.11f,0};
  for (int channel = 0; channel < 10; ++channel)
    expectTail(output.audio[static_cast<size_t>(channel)], expected[channel]);
}

TEST(RoutingMixerTest, MissingWetFxAndCabinetFreeSignalsNeverSubstituteMaster) {
  const Audio input;
  auto signals = input.signals(); signals.wet = {};
  auto config = blankConfig();
  config.outputs[0].source = Source::wetFx; config.outputs[0].left = 0; config.outputs[0].right = 1;
  config.outputs[1].source = Source::masterStereo; config.outputs[1].cabinetOff = true;
  config.outputs[1].left = 2; config.outputs[1].right = 3;
  auto packing = noPacking(); packing[0] = {0,1}; packing[1] = {2,3};
  RoutingMixer mixer; mixer.prepare(config, rate, packing);
  Render output(4);
  mixer.process(signals, nullptr, output.pointers.data(), 4, frames, true);
  for (const auto& channel : output.audio) expectTail(channel, 0);
  Audio noCab;
  std::fill(noCab.masterL.begin(), noCab.masterL.end(), 0.21f);
  std::fill(noCab.masterR.begin(), noCab.masterR.end(), -0.13f);
  const auto alternate = noCab.signals();
  output.clear();
  mixer.process(signals, &alternate, output.pointers.data(), 4, frames, true);
  expectTail(output.audio[2], 0.21f); expectTail(output.audio[3], -0.13f);
}

TEST(RoutingMixerTest, SparsePhysicalMappingAndAdditiveOutputsAreRespected) {
  const Audio input;
  auto config = blankConfig(); config.outputs[0].source = Source::masterStereo;
  config.outputs[0].left = 10; config.outputs[0].right = 14;
  auto packing = noPacking(); packing[0] = {1,3};
  RoutingMixer mixer; mixer.prepare(config, rate, packing);
  Render output(4);
  for (auto& channel : output.audio) std::fill(channel.begin(), channel.end(), 0.1f);
  mixer.process(input.signals(), nullptr, output.pointers.data(), 4, frames, true);
  expectTail(output.audio[0], 0.1f); expectTail(output.audio[1], 0.9f);
  expectTail(output.audio[2], 0.1f); expectTail(output.audio[3], -0.1f);
}

TEST(RoutingMixerTest, MasterLinkPadAndIndependentRouteTrimHaveTheSpecifiedDbGain) {
  const Audio input;
  auto config = blankConfig(); config.masterVolumeDb = -12;
  auto packing = noPacking();
  for (size_t i = 0; i < 3; ++i) {
    config.outputs[i].source = Source::masterLeft; config.outputs[i].left = static_cast<int>(i);
    packing[i] = {static_cast<int>(i),-1};
  }
  config.outputs[0].volumeDb = -6;
  config.outputs[1].volumeDb = -3; config.outputs[1].linkMaster = true; config.outputs[1].pad12 = true;
  RoutingMixer mixer; mixer.prepare(config, rate, packing);
  Render output(3);
  mixer.process(input.signals(), nullptr, output.pointers.data(), 3, frames, true);
  expectTail(output.audio[0], 0.8f * std::pow(10.0f, -6.0f / 20));
  expectTail(output.audio[1], 0.8f * std::pow(10.0f, -27.0f / 20));
  expectTail(output.audio[2], 0.8f);
  EXPECT_NEAR(mixer.peak(Port::monitor), 0.8f * std::pow(10.0f, -27.0f / 20), 0.00001f);
}

TEST(RoutingMixerTest, AuxiliaryMixReachesOnlyMainMonitorAndPhones) {
  const Audio input;
  auto config = blankConfig(); config.auxMix = {0.5f, 0.25f, 1.0f};
  auto packing = noPacking();
  for (size_t i = 0; i < portCount; ++i) {
    config.outputs[i].source = Source::masterLeft; config.outputs[i].left = static_cast<int>(i);
    packing[i] = {static_cast<int>(i),-1};
  }
  RoutingMixer mixer; mixer.prepare(config, rate, packing);
  Render output(6);
  mixer.process(input.signals(), nullptr, output.pointers.data(), 6, frames, true);
  const float expected[]{0.82f,0.81f,0.8f,0.84f,0.8f,0.8f};
  for (int channel = 0; channel < 6; ++channel) expectTail(output.audio[static_cast<size_t>(channel)], expected[channel]);
}

TEST(RoutingMixerTest, MonitoringMuteAndFadesDoNotAlterSourceAudio) {
  const Audio input;
  auto config = blankConfig(); config.outputs[0].source = Source::masterLeft;
  auto packing = noPacking(); packing[0] = {0,-1};
  RoutingMixer mixer; mixer.prepare(config, rate, packing);
  Render output(1);
  mixer.process(input.signals(), nullptr, output.pointers.data(), 1, frames, true);
  EXPECT_GT(output.audio[0][0], 0); EXPECT_LT(output.audio[0][0], 0.01f);
  expectTail(output.audio[0], 0.8f);
  mixer.fadeOut(); output.clear();
  mixer.process(input.signals(), nullptr, output.pointers.data(), 1, frames, true);
  EXPECT_TRUE(mixer.faded()); expectTail(output.audio[0], 0);
  mixer.prepare(config, rate, packing); output.clear();
  mixer.process(input.signals(), nullptr, output.pointers.data(), 1, frames, false);
  expectTail(output.audio[0], 0);
  output.clear(); mixer.process(input.signals(), nullptr, output.pointers.data(), 1, frames, true);
  expectTail(output.audio[0], 0.8f);
}

TEST(RoutingMixerTest, NonfiniteSamplesCannotPermanentlyPoisonAValidRoute) {
  Audio input;
  input.masterL[1000] = std::numeric_limits<float>::quiet_NaN();
  input.masterL[2000] = std::numeric_limits<float>::infinity();
  auto config = blankConfig(); config.outputs[0].source = Source::masterLeft;
  auto packing = noPacking(); packing[0] = {0,-1};
  RoutingMixer mixer; mixer.prepare(config, rate, packing);
  Render output(1);
  mixer.process(input.signals(), nullptr, output.pointers.data(), 1, frames, true);
  for (float value : output.audio[0]) EXPECT_TRUE(std::isfinite(value));
  expectTail(output.audio[0], 0.8f);
}

TEST(RoutingPlanTest, AllDestinationsRejectChannelCollisionsAndMissingRightChannels) {
  auto config = blankConfig();
  for (size_t i = 0; i < portCount; ++i) {
    auto& output = config.outputs[i]; output.source = Source::masterStereo;
    output.stereo = true; output.left = static_cast<int>(i * 2); output.right = output.left + 1;
  }
  ASSERT_TRUE(validate(config, 4, 12).isEmpty());
  for (size_t i = 0; i < portCount; ++i) {
    auto duplicate = config;
    duplicate.outputs[i].left = config.outputs[(i + 1) % portCount].left;
    EXPECT_FALSE(validate(duplicate, 4, 12).isEmpty()) << portName(static_cast<Port>(i));
    auto incomplete = config; incomplete.outputs[i].right = -1;
    EXPECT_FALSE(validate(incomplete, 4, 12).isEmpty());
  }
  auto split = config; split.outputs[0].source = Source::diMasterMono;
  split.outputs[0].stereo = false; split.outputs[0].right = -1;
  EXPECT_FALSE(validate(split, 4, 12).isEmpty());
}

TEST(RoutingPlanTest, ConfigurationSurvivesActualJsonSerialization) {
  auto config = blankConfig(); config.enabled = true;
  config.inputDevice = "Interface USB / Input"; config.outputDevice = "Interface USB / Output";
  config.inputSource = InputSource::returnReamp; config.inputChannels = {0,1,2,3};
  config.stereoInputRight = 4; config.diInput = 5; config.returnInputs = {6,7}; config.auxInputs = {8,9};
  config.inputGainDb = 3.5f; config.reampSensitivityDb = -4.5f; config.masterVolumeDb = -8;
  config.auxMix = {0.25f,0.5f,0.75f};
  config.stackEnd = {"amp-L","amp-R"}; config.modEnd = {"mod-L","mod-R"};
  config.wetStart = {"delay-L","delay-R"}; config.cabinet = {"cab-L","cab-R"};
  for (size_t i = 0; i < portCount; ++i) {
    auto& output = config.outputs[i]; output.source = Source::masterStereo;
    output.stereo = true; output.left = static_cast<int>(i * 2); output.right = output.left + 1;
    output.volumeDb = -static_cast<float>(i); output.linkMaster = i % 2 != 0;
    output.pad12 = i == 0; output.cabinetOff = i == 1;
    output.bassDb = 2; output.middleDb = -1; output.trebleDb = 3; output.presenceDb = -2;
    output.lowCutHz = 80; output.highCutHz = 12000;
  }
  const auto text = juce::JSON::toString(toVar(config));
  Config restored;
  const auto loaded = fromVar(juce::JSON::parse(text), restored);
  ASSERT_TRUE(loaded.wasOk()) << loaded.getErrorMessage().toStdString();
  EXPECT_EQ(juce::JSON::toString(toVar(restored)), text);
  EXPECT_TRUE(sameDeviceTopology(config, restored));
  restored.inputGainDb += 1; EXPECT_TRUE(sameDeviceTopology(config, restored));
  restored.outputs[0].left = 30; EXPECT_FALSE(sameDeviceTopology(config, restored));
}

TEST(RoutingPlanTest, InvalidSavedRangesFailWithoutReplacingExistingConfiguration) {
  auto valid = blankConfig(); valid.outputs[0].source = Source::masterLeft; valid.outputs[0].left = 0;
  const auto baseline = juce::JSON::toString(toVar(valid));
  auto expectRejected = [&](const juce::var& data) {
    Config destination = valid;
    EXPECT_TRUE(fromVar(data, destination).failed());
    EXPECT_EQ(juce::JSON::toString(toVar(destination)), baseline);
  };
  auto data = toVar(valid); data.getDynamicObject()->setProperty("schema", 99); expectRejected(data);
  data = toVar(valid); data.getDynamicObject()->setProperty("inputSource", 4); expectRejected(data);
  data = toVar(valid); data.getDynamicObject()->setProperty("inputGainDb", 25); expectRejected(data);
  data = toVar(valid); data.getDynamicObject()->setProperty("reampSensitivityDb", -25); expectRejected(data);
  data = toVar(valid); data.getDynamicObject()->setProperty("masterVolumeDb", std::numeric_limits<double>::infinity()); expectRejected(data);
  auto badOutput = [&](const char* key, juce::var value) {
    auto record = toVar(valid);
    record["outputs"].getArray()->getReference(0).getDynamicObject()->setProperty(key, value);
    expectRejected(record);
  };
  badOutput("source", 999); badOutput("left", 128); badOutput("volumeDb", 13);
  badOutput("bassDb", -13); badOutput("lowCutHz", 10); badOutput("highCutHz", 20001);
  auto crossed = valid; crossed.outputs[0].lowCutHz = 4000; crossed.outputs[0].highCutHz = 1000;
  EXPECT_FALSE(validate(crossed, 4, 4).isEmpty());
  auto nonfinite = valid; nonfinite.outputs[0].presenceDb = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(validate(nonfinite, 4, 4).isEmpty());
}

// Frequency-response checks measure settled audio, independent of the IIR coefficient implementation.
double rmsAfterWarmup(const std::vector<float>& samples) {
  const size_t begin = samples.size() / 2;
  double energy = 0;
  for (size_t i = begin; i < samples.size(); ++i) energy += static_cast<double>(samples[i]) * samples[i];
  return std::sqrt(energy / static_cast<double>(samples.size() - begin));
}
double responseAt(double frequency, const Output& control) {
  constexpr int count = 48000;
  Audio input(count);
  for (int i = 0; i < count; ++i)
    input.masterL[static_cast<size_t>(i)] = 0.1f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * frequency * i / rate));
  auto config = blankConfig(); config.outputs[0] = control;
  config.outputs[0].source = Source::masterLeft; config.outputs[0].left = 0;
  auto packing = noPacking(); packing[0] = {0,-1};
  RoutingMixer mixer; mixer.prepare(config, rate, packing);
  Render result(1, count);
  mixer.process(input.signals(), nullptr, result.pointers.data(), 1, count, true);
  return rmsAfterWarmup(result.audio[0]) / rmsAfterWarmup(input.masterL);
}
TEST(RoutingMixerTest, OutputCutsRejectOutOfBandEnergyAndKeepPassbandNearUnity) {
  Output lowCut; lowCut.lowCutHz = 200;
  EXPECT_LT(responseAt(25, lowCut), 0.03);
  EXPECT_NEAR(responseAt(2000, lowCut), 1.0, 0.02);
  Output highCut; highCut.highCutHz = 2000;
  EXPECT_LT(responseAt(12000, highCut), 0.04);
  EXPECT_NEAR(responseAt(200, highCut), 1.0, 0.02);
}
TEST(RoutingMixerTest, OutputEqChangesTheIntendedFrequencyBands) {
  Output bass; bass.bassDb = 6;
  EXPECT_NEAR(20 * std::log10(responseAt(25, bass)), 6.0, 0.5);
  EXPECT_NEAR(20 * std::log10(responseAt(2000, bass)), 0.0, 0.3);
  Output middle; middle.middleDb = -6;
  EXPECT_NEAR(20 * std::log10(responseAt(750, middle)), -6.0, 0.2);
  Output treble; treble.trebleDb = 6;
  EXPECT_GT(20 * std::log10(responseAt(12000, treble)), 5.5);
}

TEST(RoutingMixerTest, MonoReturnWithUnassignedRightKeepsUnityWhenFoldedToMono) {
  const Audio input;
  auto signals = input.signals(); signals.returns.channel[1] = nullptr;
  EXPECT_FLOAT_EQ(signals.returns.mono(0), 0.09f);
  auto config = blankConfig(); config.outputs[0].source = Source::returns;
  config.outputs[0].stereo = false; config.outputs[0].left = 0;
  auto packing = noPacking(); packing[0] = {0,-1};
  RoutingMixer mixer; mixer.prepare(config, rate, packing);
  Render output(1);
  mixer.process(signals, nullptr, output.pointers.data(), 1, frames, true);
  expectTail(output.audio[0], 0.09f);
}
TEST(RoutingMixerTest, MonoAuxiliaryInputWithUnassignedRightKeepsUnity) {
  const Audio input;
  auto signals = input.signals(); signals.aux.channel[1] = nullptr;
  std::vector<float> silence(frames, 0.0f);
  signals.master = {{silence.data(), nullptr}};
  auto config = blankConfig(); config.outputs[0].source = Source::masterMono;
  config.outputs[0].left = 0; config.auxMix[0] = 1.0f;
  auto packing = noPacking(); packing[0] = {0,-1};
  RoutingMixer mixer; mixer.prepare(config, rate, packing);
  Render output(1);
  mixer.process(signals, nullptr, output.pointers.data(), 1, frames, true);
  expectTail(output.audio[0], 0.13f);
}
juce::var branchedState(bool stereo = true, const char* side = "left", const char* anchor = "L-amp") {
  auto state = juce::JSON::parse(R"({
    "stereoEnabled":true,
    "chain":[{"kind":"nam","blockId":"L-boost"},{"kind":"nam","blockId":"L-amp"},
             {"kind":"ir","blockId":"L-cab"},{"kind":"insert","blockId":"insert-marker"},
             {"kind":"nam","blockId":"L-post"}],
    "chainRight":[{"kind":"nam","blockId":"R-drive"},{"kind":"nam","blockId":"R-amp"},
                  {"kind":"ir","blockId":"R-cab"}],
    "branch":{"side":"left","afterBlockId":"L-amp"}
  })");
  state.getDynamicObject()->setProperty("stereoEnabled", stereo);
  state["branch"].getDynamicObject()->setProperty("side", side);
  state["branch"].getDynamicObject()->setProperty("afterBlockId", anchor);
  return state;
}
TEST(RoutingPlanTest, IndependentRenderedLanesExposeOnlyTheirOwnAudibleBlocks) {
  auto state = branchedState(); state.getDynamicObject()->removeProperty("branch");
  EXPECT_EQ(effectivePath(state, 0), (std::vector<std::string>{"L-boost","L-amp","L-cab","L-post"}));
  EXPECT_EQ(effectivePath(state, 1), (std::vector<std::string>{"R-drive","R-amp","R-cab"}));
}
TEST(RoutingPlanTest, BranchPathsIncludeTheSharedPrefixFromEitherTrunkSide) {
  const auto leftTrunk = branchedState(true, "left", "L-amp");
  EXPECT_EQ(effectivePath(leftTrunk, 1),
            (std::vector<std::string>{"L-boost","L-amp","R-drive","R-amp","R-cab"}));
  const auto rightTrunk = branchedState(true, "right", "R-amp");
  EXPECT_EQ(effectivePath(rightTrunk, 0),
            (std::vector<std::string>{"R-drive","R-amp","L-boost","L-amp","L-cab","L-post"}));
  EXPECT_EQ(effectivePath(rightTrunk, 1), (std::vector<std::string>{"R-drive","R-amp","R-cab"}));
}
TEST(RoutingPlanTest, BlocksAfterTheAnchorAreUnreachableFromTheOtherBranch) {
  const auto state = branchedState(true, "left", "L-amp");
  const auto rightPath = effectivePath(state, 1);
  EXPECT_EQ(std::find(rightPath.begin(), rightPath.end(), "L-cab"), rightPath.end());
  EXPECT_EQ(std::find(rightPath.begin(), rightPath.end(), "L-post"), rightPath.end());
  EXPECT_NE(std::find(rightPath.begin(), rightPath.end(), "L-amp"), rightPath.end());
  const auto ownTrunk = effectivePath(state, 0);
  EXPECT_NE(std::find(ownTrunk.begin(), ownTrunk.end(), "L-cab"), ownTrunk.end());
}
TEST(RoutingPlanTest, MonoRenderingUsesPrimaryLaneIdsEvenForRenderedRightChannel) {
  const auto state = branchedState(false, "right", "R-amp");
  const std::vector<std::string> primary{"L-boost","L-amp","L-cab","L-post"};
  EXPECT_EQ(effectivePath(state, 0), primary); EXPECT_EQ(effectivePath(state, 1), primary);
}
TEST(RoutingPlanTest, MissingBranchAnchorDoesNotInventPrefixAndInvalidLaneHasNoPath) {
  const auto state = branchedState(true, "left", "deleted-anchor");
  EXPECT_EQ(effectivePath(state, 1), (std::vector<std::string>{"R-drive","R-amp","R-cab"}));
  EXPECT_TRUE(effectivePath(state, -1).empty()); EXPECT_TRUE(effectivePath(state, 2).empty());
}
TEST(RoutingPlanTest, FractionalSavedSelectorsAndChannelsAreRejectedRatherThanTruncated) {
  auto config = blankConfig();
  config.outputs[0].source = Source::masterLeft; config.outputs[0].left = 0;
  auto reject = [&](const juce::var& record) {
    Config unchanged = config;
    EXPECT_TRUE(fromVar(record, unchanged).failed());
    EXPECT_EQ(juce::JSON::toString(toVar(unchanged)), juce::JSON::toString(toVar(config)));
  };
  auto record = toVar(config); record.getDynamicObject()->setProperty("schema", 1.5); reject(record);
  record = toVar(config); record.getDynamicObject()->setProperty("inputSource", 0.5); reject(record);
  record = toVar(config); record.getDynamicObject()->setProperty("diInput", 0.5); reject(record);
  record = toVar(config); record["inputChannels"].getArray()->set(0, 0.5); reject(record);
  record = toVar(config); record["returnInputs"].getArray()->set(0, -1.5); reject(record);
  record = toVar(config); record["outputs"].getArray()->getReference(0).getDynamicObject()->setProperty("source", 7.5); reject(record);
  record = toVar(config); record["outputs"].getArray()->getReference(0).getDynamicObject()->setProperty("left", 0.5); reject(record);
}

} // namespace
