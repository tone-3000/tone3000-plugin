#include "RoutingState.h"
#include "chain_test_helpers.h"

#include <gtest/gtest.h>
#include <cstring>

namespace {
constexpr int kBlock = 256;

void setParameter(TONE3000Processor& p, const char* name, float value) {
  auto* parameter = p.parameters.getParameter(name);
  ASSERT_NE(parameter, nullptr);
  parameter->setValueNotifyingHost(value);
}
void neutral(TONE3000Processor& p) {
  p.setMultiCoreEnabled(false, false);
  setParameter(p, "gateEnabled", 0.0f);
  setParameter(p, "toneEqEnabled", 0.0f);
  setParameter(p, "pitchEnabled", 0.0f);
  setParameter(p, "osEnabled", 0.0f);
}
juce::ValueTree parse(const juce::MemoryBlock& bytes) {
  if (bytes.getSize() <= 4 || std::memcmp(bytes.getData(), "T3KB", 4) != 0)
    return {};
  return juce::ValueTree::readFromData(static_cast<const char*>(bytes.getData()) + 4,
                                      bytes.getSize() - 4);
}
juce::MemoryBlock encode(const juce::ValueTree& tree) {
  juce::MemoryBlock result;
  juce::MemoryOutputStream stream(result, false);
  stream.write("T3KB", 4);
  tree.writeToStream(stream);
  return result;
}
juce::ValueTree findBlock(const juce::ValueTree& tree, const char* id) {
  const auto snapshot = tree.getChildWithName("ChainSnapshot");
  for (const char* key : {"ChainBlocks", "RightChainBlocks"}) {
    const auto lane = snapshot.getChildWithName(key);
    for (int i = 0; i < lane.getNumChildren(); ++i)
      if (lane.getChild(i)["id"].toString() == id)
        return lane.getChild(i);
  }
  return {};
}
juce::ValueTree withGear(juce::ValueTree block, const char* gear) {
  auto tone = juce::JSON::parse(block["toneJson"].toString());
  tone.getDynamicObject()->setProperty("gear", gear);
  block.setProperty("toneJson", juce::JSON::toString(tone), nullptr);
  return block;
}
juce::ValueTree snapshot(bool stereo, bool includeNam) {
  juce::ValueTree result("ChainSnapshot"), left("ChainBlocks"), right("RightChainBlocks");
  result.setProperty("stereoEnabled", stereo, nullptr);
  if (includeNam)
    left.appendChild(makeNamBlockTree("amp", 1, 100), nullptr);
  else {
    auto pre = makeIrBlockTree("pre", 1, 100, "cab-ir-test.wav", "pedal");
    pre.setProperty("mix", 0.4f, nullptr);
    left.appendChild(pre, nullptr);
  }
  left.appendChild(makeIrBlockTree("cab-anchor", 2, 200, "cab-ir-test.wav", "cab"), nullptr);
  auto fx = makeIrBlockTree("post-fx", 3, 300, "reverb-ir-mono-test.wav", "space");
  fx.setProperty("mix", 0.5f, nullptr);
  left.appendChild(fx, nullptr);
  if (stereo) {
    right.appendChild(makeIrBlockTree("right-cab", 4, 400, "cab-ir-test.wav", "cab"), nullptr);
    auto rightFx = makeIrBlockTree("right-post-fx", 5, 500, "reverb-ir-mono-test.wav", "space");
    rightFx.setProperty("mix", 0.35f, nullptr);
    right.appendChild(rightFx, nullptr);
  }
  result.appendChild(left, nullptr);
  result.appendChild(right, nullptr);
  return result;
}
juce::MemoryBlock realProcessorState(bool stereo, bool includeNam) {
  ChainTestProcessor source;
  neutral(source);
  source.setPlayConfigDetails(2, 2, kFs, kBlock);
  source.prepareToPlay(kFs, kBlock);
  source.restoreFromTree(snapshot(stereo, includeNam));
  EXPECT_TRUE(waitForChainLoaded(source));
  if (stereo) {
    EXPECT_TRUE(source.setChainBranch("left", "cab-anchor"));
    source.setInputMode(TONE3000Processor::InputMode::Right);
  }
  juce::MemoryBlock result;
  source.getStateInformation(result);
  return result;
}
juce::MemoryBlock minimalState(const juce::ValueTree& block) {
  juce::ValueTree tree("TONE3000State"), snap("ChainSnapshot"), lane("ChainBlocks");
  tree.setProperty("schemaVersion", 1, nullptr);
  lane.appendChild(block, nullptr);
  snap.appendChild(lane, nullptr);
  tree.appendChild(snap, nullptr);
  return encode(tree);
}
juce::MemoryBlock stereoCabState(bool branch = false, bool rightTrunk = false) {
  auto tree = parse(minimalState(makeIrBlockTree("left-cab", 1, 100, "cab-ir-test.wav", "cab")));
  auto snap = tree.getChildWithName("ChainSnapshot");
  snap.setProperty("stereoEnabled", true, nullptr);
  snap.getChildWithName("ChainBlocks").appendChild(
      makeIrBlockTree("left-after", 2, 200, "cab-ir-test.wav", "cab"), nullptr);
  juce::ValueTree right("RightChainBlocks");
  right.appendChild(makeIrBlockTree("right-cab", 3, 300, "cab-ir-test.wav", "cab"), nullptr);
  right.appendChild(makeIrBlockTree("right-after", 4, 400, "cab-ir-test.wav", "cab"), nullptr);
  snap.appendChild(right, nullptr);
  if (branch) {
    snap.setProperty("branchSide", rightTrunk ? "right" : "left", nullptr);
    snap.setProperty("branchAfterBlockId", rightTrunk ? "right-cab" : "left-cab", nullptr);
  }
  return encode(tree);
}
void expectFailurePreserves(const juce::MemoryBlock& original,
                             const std::array<std::string, 2>& ids) {
  const auto before = original;
  const juce::MemoryBlock sentinel("unchanged", 9);
  auto output = sentinel;
  const auto result = t3k::routing::cabinetBypassState(original, ids, output);
  EXPECT_TRUE(result.failed());
  EXPECT_EQ(original, before);
  EXPECT_EQ(output, sentinel);
}
float maxDifference(const std::vector<float>& a, const std::vector<float>& b) {
  EXPECT_EQ(a.size(), b.size());
  float result = 0.0f;
  for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
    result = std::max(result, std::abs(a[i] - b[i]));
  return result;
}

TEST(CabinetRoutingTest, RealStateOnlyChangesSelectedCabinetsAndRetainsBranchAnchor) {
  const auto original = realProcessorState(true, true);
  const auto originalCopy = original;
  juce::MemoryBlock bypassed;
  const auto result = t3k::routing::cabinetBypassState(original,
      {"cab-anchor", "right-cab"}, bypassed);
  ASSERT_TRUE(result.wasOk()) << result.getErrorMessage().toStdString();
  EXPECT_EQ(original, originalCopy);

  const auto actual = parse(bypassed);
  auto expected = parse(original).createCopy();
  auto firstCab = findBlock(expected, "cab-anchor");
  auto secondCab = findBlock(expected, "right-cab");
  ASSERT_TRUE(firstCab.isValid());
  ASSERT_TRUE(secondCab.isValid());
  firstCab.setProperty("enabled", false, nullptr);
  secondCab.setProperty("enabled", false, nullptr);
  EXPECT_TRUE(actual.isEquivalentTo(expected))
      << "Only cabinet power flags may differ; models, params, UUIDs, branch and input mode must survive";
  EXPECT_EQ(actual["inputMode"].toString(), "right");
  EXPECT_TRUE(static_cast<bool>(findBlock(actual, "post-fx")["enabled"]));
  EXPECT_TRUE(static_cast<bool>(findBlock(actual, "right-post-fx")["enabled"]));
  EXPECT_EQ(findBlock(actual, "amp").getChildWithName("ModelCache").getNumChildren(),
            findBlock(parse(original), "amp").getChildWithName("ModelCache").getNumChildren());

  ChainTestProcessor restored;
  neutral(restored);
  restored.setPlayConfigDetails(2, 2, kFs, kBlock);
  restored.prepareToPlay(kFs, kBlock);
  restored.setStateInformation(bypassed.getData(), static_cast<int>(bypassed.getSize()));
  ASSERT_TRUE(waitForChainLoaded(restored));
  const auto state = restored.getChainState(-1);
  ASSERT_TRUE(state["branch"].isObject());
  EXPECT_EQ(state["branch"]["afterBlockId"].toString(), "cab-anchor");
  EXPECT_EQ(state["branch"]["side"].toString(), "left");
  EXPECT_EQ(restored.getInputMode(), TONE3000Processor::InputMode::Right);
}

TEST(CabinetRoutingTest, RejectsCombinedAmpAndUnclassifiedNamWithoutMutation) {
  const auto combined = minimalState(withGear(makeNamBlockTree("combined", 1, 100), "amp-cab"));
  expectFailurePreserves(combined, {"combined"});
  const auto amp = minimalState(withGear(makeNamBlockTree("amp-only", 1, 100), "amp"));
  expectFailurePreserves(amp, {"amp-only"});
  const auto unclassified = minimalState(makeNamBlockTree("unknown", 1, 100));
  expectFailurePreserves(unclassified, {"unknown"});
}

TEST(CabinetRoutingTest, AcceptsExplicitSeparateCabinetNamWithoutChangingItsModel) {
  const auto original = minimalState(withGear(makeNamBlockTree("separate-cab", 1, 100), "cab"));
  juce::MemoryBlock bypassed;
  const auto result = t3k::routing::cabinetBypassState(original, {"separate-cab"}, bypassed);
  ASSERT_TRUE(result.wasOk()) << result.getErrorMessage().toStdString();
  const auto originalBlock = findBlock(parse(original), "separate-cab");
  const auto actual = findBlock(parse(bypassed), "separate-cab");
  EXPECT_FALSE(static_cast<bool>(actual["enabled"]));
  EXPECT_EQ(actual["toneJson"].toString(), originalBlock["toneJson"].toString());
  EXPECT_EQ(actual["activeModelId"], originalBlock["activeModelId"]);
  EXPECT_TRUE(actual.getChildWithName("ModelCache").isEquivalentTo(originalBlock.getChildWithName("ModelCache")));
}

TEST(CabinetRoutingTest, PartialStaleSelectionsAndAliasedFailuresAreAtomic) {
  const auto original = minimalState(makeIrBlockTree("cab", 1, 100, "cab-ir-test.wav", "cab"));
  expectFailurePreserves(original, {"missing"});
  expectFailurePreserves(stereoCabState(), {"left-cab", "missing"});
  expectFailurePreserves(original, {});
  expectFailurePreserves(original, {""});
  auto aliased = stereoCabState();
  const auto before = aliased;
  EXPECT_TRUE(t3k::routing::cabinetBypassState(aliased, {"left-cab", "missing"}, aliased).failed());
  EXPECT_EQ(aliased, before);
}

TEST(CabinetRoutingTest, IndependentStereoRequiresACabinetOnEachOwnLane) {
  const auto original = stereoCabState();
  expectFailurePreserves(original, {"right-cab", "right-cab"});
  expectFailurePreserves(original, {"left-cab", "left-cab"});
  expectFailurePreserves(original, {"left-cab", ""});
  expectFailurePreserves(original, {"", "right-cab"});
  juce::MemoryBlock bypassed;
  ASSERT_TRUE(t3k::routing::cabinetBypassState(original, {"left-cab", "right-cab"}, bypassed).wasOk());
  EXPECT_FALSE(static_cast<bool>(findBlock(parse(bypassed), "left-cab")["enabled"]));
  EXPECT_FALSE(static_cast<bool>(findBlock(parse(bypassed), "right-cab")["enabled"]));
  EXPECT_TRUE(static_cast<bool>(findBlock(parse(bypassed), "left-after")["enabled"]));
  EXPECT_TRUE(static_cast<bool>(findBlock(parse(bypassed), "right-after")["enabled"]));
}

TEST(CabinetRoutingTest, SharedPrefixIsAcceptedButTrunkAfterTheAnchorIsUnreachable) {
  for (const bool rightTrunk : {false, true}) {
    const auto original = stereoCabState(true, rightTrunk);
    const std::string shared = rightTrunk ? "right-cab" : "left-cab";
    juce::MemoryBlock bypassed;
    ASSERT_TRUE(t3k::routing::cabinetBypassState(original, {shared, shared}, bypassed).wasOk());
    auto expected = parse(original).createCopy();
    findBlock(expected, shared.c_str()).setProperty("enabled", false, nullptr);
    EXPECT_TRUE(parse(bypassed).isEquivalentTo(expected));
    if (rightTrunk)
      expectFailurePreserves(original, {"right-after", "right-cab"});
    else
      expectFailurePreserves(original, {"left-cab", "left-after"});
  }
}

TEST(CabinetRoutingTest, MonoUsesOnlyThePrimaryCabinetAndKeepsDormantRightAssignment) {
  const auto original = minimalState(makeIrBlockTree("cab", 1, 100, "cab-ir-test.wav", "cab"));
  juce::MemoryBlock bypassed;
  EXPECT_TRUE(t3k::routing::cabinetBypassState(original, {"cab", "dormant-stale-right"}, bypassed).wasOk());
  EXPECT_FALSE(static_cast<bool>(findBlock(parse(bypassed), "cab")["enabled"]));
  expectFailurePreserves(original, {"", "cab"});
}

TEST(CabinetRoutingTest, RejectsBadFormatsUnsupportedSchemaAndMissingSnapshot) {
  expectFailurePreserves({}, {"cab"});
  expectFailurePreserves(juce::MemoryBlock("legacy-state", 12), {"cab"});
  expectFailurePreserves(juce::MemoryBlock("T3KB", 4), {"cab"});
  juce::ValueTree future("TONE3000State");
  future.setProperty("schemaVersion", 99, nullptr);
  expectFailurePreserves(encode(future), {"cab"});
  juce::ValueTree noSnapshot("TONE3000State");
  noSnapshot.setProperty("schemaVersion", 1, nullptr);
  expectFailurePreserves(encode(noSnapshot), {"cab"});
  expectFailurePreserves(encode(juce::ValueTree("WrongRoot")), {"cab"});
}

TEST(CabinetRoutingTest, ReapplyingBypassIsIdempotentAndDoesNotDeleteTheUuid) {
  const auto original = minimalState(makeIrBlockTree("cab", 1, 100, "cab-ir-test.wav", "cab"));
  juce::MemoryBlock first, second;
  ASSERT_TRUE(t3k::routing::cabinetBypassState(original, {"cab"}, first).wasOk());
  ASSERT_TRUE(t3k::routing::cabinetBypassState(first, {"cab"}, second).wasOk());
  const auto firstTree = parse(first);
  const auto secondTree = parse(second);
  EXPECT_TRUE(firstTree.isEquivalentTo(secondTree));
  juce::MemoryOutputStream canonical;
  canonical.write("T3KB", 4);
  firstTree.writeToStream(canonical);
  size_t firstDifferentByte = std::min(first.getSize(), second.getSize());
  const auto* a = static_cast<const unsigned char*>(first.getData());
  const auto* b = static_cast<const unsigned char*>(second.getData());
  for (size_t i = 0; i < std::min(first.getSize(), second.getSize()); ++i)
    if (a[i] != b[i]) { firstDifferentByte = i; break; }
  EXPECT_EQ(first.getSize(), canonical.getDataSize())
      << "Serialized states must not retain unused MemoryOutputStream capacity";
  EXPECT_EQ(first, second) << "First differing byte " << firstDifferentByte
      << "; logical written payload size " << canonical.getDataSize();
  EXPECT_EQ(findBlock(parse(second), "cab")["id"].toString(), "cab");
  EXPECT_FALSE(static_cast<bool>(findBlock(parse(second), "cab")["enabled"]));
}

TEST(CabinetRoutingTest, RealEngineBypassMatchesManualCabOffAndKeepsDownstreamFx) {
  const auto original = realProcessorState(false, false);
  juce::MemoryBlock bypassed;
  ASSERT_TRUE(t3k::routing::cabinetBypassState(original, {"cab-anchor"}, bypassed).wasOk());
  ChainTestProcessor clone, manual, noPostFx;
  for (auto* p : {&clone, &manual, &noPostFx}) {
    neutral(*p);
    p->setPlayConfigDetails(2, 2, kFs, kBlock);
    p->prepareToPlay(kFs, kBlock);
  }
  clone.setStateInformation(bypassed.getData(), static_cast<int>(bypassed.getSize()));
  manual.setStateInformation(original.getData(), static_cast<int>(original.getSize()));
  noPostFx.setStateInformation(bypassed.getData(), static_cast<int>(bypassed.getSize()));
  for (auto* p : {&clone, &manual, &noPostFx})
    ASSERT_TRUE(waitForChainLoaded(*p));
  ASSERT_TRUE(manual.setBlockParam("cab-anchor", "enabled", 0.0));
  ASSERT_TRUE(noPostFx.setBlockParam("post-fx", "enabled", 0.0));
  for (auto* p : {&clone, &manual, &noPostFx}) {
    p->prepareToPlay(kFs, kBlock);
    processStereo(*p, std::vector<float>(40 * kBlock, 0.0f), kBlock);
  }
  const auto input = makeNoise(200 * kBlock, 522, 0.2f);
  const auto [cloneL, cloneR] = processStereo(clone, input, kBlock);
  const auto [manualL, manualR] = processStereo(manual, input, kBlock);
  const auto [dryL, dryR] = processStereo(noPostFx, input, kBlock);
  EXPECT_LT(maxDifference(cloneL, manualL), 1.0e-6f);
  EXPECT_LT(maxDifference(cloneR, manualR), 1.0e-6f);
  EXPECT_GT(maxDifference(cloneL, dryL), 0.001f)
      << "Cabinet bypass must keep the downstream reverb, not tap before the cabinet and discard later effects";
}

TEST(CabinetRoutingTest, EqCoefficientCopyKeepsItsOwnHistoryAndRejectsOtherRates) {
  BlockEq donor, receiver, reference;
  for (auto* eq : {&donor, &receiver, &reference}) {
    eq->prepare(kFs);
    ASSERT_TRUE(eq->setBand(2, {BlockEq::BandType::Bell, 1200.0f, 6.0f, 1.4f}));
  }
  const auto prefix = makeNoise(700, 764, 0.2f);
  juce::AudioBuffer<float> a(2, 700), b(2, 700), different(2, 700);
  for (int ch = 0; ch < 2; ++ch) {
    a.copyFrom(ch, 0, prefix.data(), 700);
    b.copyFrom(ch, 0, prefix.data(), 700);
    different.clear(ch, 0, 700);
    different.setSample(ch, 699, 0.7f);
  }
  receiver.process(a); reference.process(b); donor.process(different);
  for (int block = 0; block < 5; ++block) {
    const BlockEq::Band settings{BlockEq::BandType::Bell, 900.0f + block * 100.0f,
                                 7.0f + block, 1.4f};
    ASSERT_TRUE(donor.setBand(2, settings));
    ASSERT_TRUE(reference.setBand(2, settings));
    ASSERT_TRUE(receiver.copySettingsFrom(donor));
    const auto noise = makeNoise(700, 12u + block, 0.2f);
    for (int ch = 0; ch < 2; ++ch) {
      a.copyFrom(ch, 0, noise.data(), 700);
      b.copyFrom(ch, 0, noise.data(), 700);
    }
    receiver.process(a); reference.process(b);
    for (int ch = 0; ch < 2; ++ch)
      EXPECT_EQ(std::memcmp(a.getReadPointer(ch), b.getReadPointer(ch), 700 * sizeof(float)), 0)
          << "Coefficient copy reset/copied donor history during a drag";
  }
  donor.prepare(44100.0);
  const auto before = receiver.toValueTree();
  EXPECT_FALSE(receiver.copySettingsFrom(donor));
  EXPECT_TRUE(receiver.toValueTree().isEquivalentTo(before));
}

TEST(CabinetRoutingTest, MirrorFollowsBlockGainMixPowerAndEqWithoutRecloningOrCabReenable) {
  const auto original = realProcessorState(false, false);
  juce::MemoryBlock bypassed;
  ASSERT_TRUE(t3k::routing::cabinetBypassState(original, {"cab-anchor", ""}, bypassed).wasOk());
  ChainTestProcessor source, mirror, manual;
  for (auto* p : {&source, &mirror, &manual}) {
    neutral(*p);
    p->setPlayConfigDetails(2, 2, kFs, kBlock);
    p->prepareToPlay(kFs, kBlock);
  }
  source.setStateInformation(original.getData(), static_cast<int>(original.getSize()));
  mirror.setStateInformation(bypassed.getData(), static_cast<int>(bypassed.getSize()));
  manual.setStateInformation(bypassed.getData(), static_cast<int>(bypassed.getSize()));
  for (auto* p : {&source, &mirror, &manual})
    ASSERT_TRUE(waitForChainLoaded(*p));
  mirror.followRoutingControlsFrom(&source, {"cab-anchor", "post-fx"});  // Mono ignores dormant Right role.
  for (auto* p : {&source, &mirror, &manual}) {
    p->prepareToPlay(kFs, kBlock);
    processStereo(*p, std::vector<float>(40 * kBlock, 0.0f), kBlock);
  }
  juce::AudioBuffer<float> mainBuffer(2, kBlock), actual(2, kBlock), expected(2, kBlock);
  juce::MidiBuffer midi;
  float worst = 0.0f;
  for (int block = 0; block < 35; ++block) {
    for (auto* p : {&source, &manual}) {
      ASSERT_TRUE(p->setBlockParam("pre", "inputGain", 0.52 + 0.002 * block));
      ASSERT_TRUE(p->setBlockParam("post-fx", "outputGain", 0.43 + 0.002 * block));
      ASSERT_TRUE(p->setBlockParam("post-fx", "mix", 0.2 + 0.01 * block));
      ASSERT_TRUE(p->setBlockEqBand("post-fx", 2,
          eqBandVar("bell", 1000.0 + 20.0 * block, 4.0 + 0.1 * block, 1.2)));
      if (block == 10 || block == 18)
        ASSERT_TRUE(p->setBlockParam("post-fx", "enabled", block == 10 ? 0.0 : 1.0));
    }
    // The primary cab may be enabled or disabled, but the explicit bypass
    // selection must never be overwritten by live enabled-state copying.
    ASSERT_TRUE(source.setBlockParam("cab-anchor", "enabled", block % 2 == 0 ? 1.0 : 0.0));
    const auto input = makeNoise(kBlock, 400u + block, 0.15f);
    for (int ch = 0; ch < 2; ++ch) {
      mainBuffer.copyFrom(ch, 0, input.data(), kBlock);
      actual.copyFrom(ch, 0, input.data(), kBlock);
      expected.copyFrom(ch, 0, input.data(), kBlock);
    }
    source.processBlock(mainBuffer, midi);
    mirror.processBlock(actual, midi);
    manual.processBlock(expected, midi);
    ASSERT_FALSE(mirror.routingMirrorNeedsRebuild());
    for (int ch = 0; ch < 2; ++ch)
      for (int i = 0; i < kBlock; ++i)
        worst = std::max(worst, std::abs(actual.getSample(ch, i) - expected.getSample(ch, i)));
  }
  EXPECT_LT(worst, 1.0e-6f);
  mirror.followRoutingControlsFrom(nullptr);
}

TEST(CabinetRoutingTest, MirrorImmediatelyMutesAStaleGraphAndLatchesUntilRebuild) {
  const auto original = realProcessorState(false, false);
  juce::MemoryBlock bypassed;
  ASSERT_TRUE(t3k::routing::cabinetBypassState(original, {"cab-anchor", ""}, bypassed).wasOk());
  ChainTestProcessor source, mirror;
  for (auto* p : {&source, &mirror}) {
    neutral(*p);
    p->setPlayConfigDetails(2, 2, kFs, kBlock);
    p->prepareToPlay(kFs, kBlock);
  }
  source.setStateInformation(original.getData(), static_cast<int>(original.getSize()));
  mirror.setStateInformation(bypassed.getData(), static_cast<int>(bypassed.getSize()));
  ASSERT_TRUE(waitForChainLoaded(source));
  ASSERT_TRUE(waitForChainLoaded(mirror));
  mirror.followRoutingControlsFrom(&source, {"cab-anchor", ""});
  t3k::routing::TapPlan plan;
  plan.enabled = true;
  plan.stackEnd[0] = plan.modEnd[0] = plan.wetStart[0] = "post-fx";
  mirror.configureRoutingTaps(plan);
  source.prepareToPlay(kFs, kBlock);
  mirror.prepareToPlay(kFs, kBlock);
  processStereo(source, std::vector<float>(40 * kBlock, 0.0f), kBlock);
  processStereo(mirror, std::vector<float>(40 * kBlock, 0.0f), kBlock);
  ASSERT_FALSE(mirror.routingMirrorNeedsRebuild());
  letAudioGoIdle();
  ASSERT_TRUE(source.removeChainBlock("pre"));
  const auto input = makeNoise(kBlock, 821, 0.2f);
  const auto [left, right] = processStereo(mirror, input, kBlock);
  EXPECT_TRUE(mirror.routingMirrorNeedsRebuild());
  for (const auto& channel : {left, right})
    for (const auto sample : channel) EXPECT_FLOAT_EQ(sample, 0.0f);
  EXPECT_EQ(mirror.routingTap(t3k::routing::Stage::stack).frames, 0);
  EXPECT_EQ(mirror.routingTap(t3k::routing::Stage::wetFx).frames, 0);
  const auto [again, ignored] = processStereo(mirror, input, kBlock);
  for (const auto sample : again) EXPECT_FLOAT_EQ(sample, 0.0f);
  EXPECT_TRUE(mirror.routingMirrorNeedsRebuild());
  mirror.followRoutingControlsFrom(nullptr);
}
}  // namespace
