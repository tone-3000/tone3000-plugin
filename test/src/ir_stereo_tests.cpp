// IR stereo tests
//
// A two-channel IR on a mono chain runs in true stereo (L ⊗ IR_L, R ⊗ IR_R)
// unless the chain or the rig makes the signal mono, or the user picks mono
// for the block (setBlockIrStereo). getChainState reports which per block
// (`irImage`) and for the chain (`stereoIrActive`), by the audio loop's own
// rule. These pin the report to the audio, and the per-block choice to the
// chain state (save, undo, duplicate).
#include "Processor.h"
#include "chain_test_helpers.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace {

constexpr int kBlock = 512;
constexpr const char* kStereoIr = "reverb-ir-stereo-test.wav";
using InputMode = TONE3000Processor::InputMode;

juce::ValueTree chainOf(std::initializer_list<juce::ValueTree> blocks) {
  juce::ValueTree state("ChainSnapshot");
  juce::ValueTree lane("ChainBlocks");
  for (const auto& block : blocks)
    lane.appendChild(block, nullptr);
  state.appendChild(lane, nullptr);
  return state;
}

juce::ValueTree verb(const char* id = "blk-verb", int toneId = 1) {
  return makeIrBlockTree(id, toneId, toneId * 100, kStereoIr);
}

std::unique_ptr<ChainTestProcessor> makeRig(const juce::ValueTree& state,
                                            InputMode mode = InputMode::Stereo,
                                            int outChannels = 2) {
  auto proc = std::make_unique<ChainTestProcessor>();
  proc->setPlayConfigDetails(2, outChannels, kFs, kBlock);
  proc->prepareToPlay(kFs, kBlock);
  proc->restoreFromTree(state);
  EXPECT_TRUE(waitForChainLoaded(*proc));
  proc->setInputMode(mode);
  EXPECT_TRUE(waitForChainLoaded(*proc));
  proc->prepareToPlay(kFs, kBlock);
  return proc;
}

juce::var blockRow(TONE3000Processor& proc, const juce::String& blockId) {
  const juce::var state = proc.getChainState(-1);
  for (const auto* lane : {state["chain"].getArray(), state["chainRight"].getArray()}) {
    if (lane == nullptr)
      continue;
    for (const auto& item : *lane)
      if (item["blockId"].toString() == blockId)
        return item;
  }
  return {};
}

juce::String imageOf(TONE3000Processor& proc, const juce::String& blockId = "blk-verb") {
  return blockRow(proc, blockId)["irImage"].toString();
}

bool stereoIrActive(TONE3000Processor& proc) {
  return static_cast<bool>(proc.getChainState(-1)["stereoIrActive"]);
}

// The same signal on both inputs: the two outputs differ only if a block
// treats the channels differently.
float channelDiff(TONE3000Processor& proc) {
  const auto [l, r] = processStereo(proc, makeNoise(240 * kBlock, 3003, 0.1f));
  return settledMaxChannelDiff(l, r);
}

// The report and the audio agree: `stereo` and a stereo output together, or
// a mono reason and two identical channels.
void expectImage(TONE3000Processor& proc, const char* image) {
  const bool stereo = juce::String(image) == "stereo";
  EXPECT_EQ(imageOf(proc), image);
  EXPECT_EQ(stereoIrActive(proc), stereo);
  if (stereo)
    EXPECT_GT(channelDiff(proc), 1e-3f) << "a stereo IR should decorrelate the channels";
  else
    EXPECT_EQ(channelDiff(proc), 0.0f) << "a mono image must put the same signal on both sides";
}

// ---------------------------------------------------------------------------
// The report follows the audio
// ---------------------------------------------------------------------------

TEST(IrStereoTest, StereoFileOnMonoChainRunsStereo) {
  auto proc = makeRig(chainOf({verb()}));
  expectImage(*proc, "stereo");
}

TEST(IrStereoTest, NamBeforeKeepsStereo) {
  auto proc = makeRig(chainOf({makeNamBlockTree("blk-amp", 2, 200), verb()}));
  expectImage(*proc, "stereo");
  EXPECT_TRUE(blockRow(*proc, "blk-amp")["irImage"].isVoid()) << "NAM blocks carry no irImage";
}

TEST(IrStereoTest, NamAfterMakesItMono) {
  auto proc = makeRig(chainOf({verb(), makeNamBlockTree("blk-amp", 2, 200)}));
  expectImage(*proc, "mono-nam-after");
}

TEST(IrStereoTest, BypassedNamAfterKeepsStereo) {
  auto amp = makeNamBlockTree("blk-amp", 2, 200);
  amp.setProperty("enabled", false, nullptr);
  auto proc = makeRig(chainOf({verb(), amp}));
  expectImage(*proc, "stereo");
}

TEST(IrStereoTest, MonoFileIsMono) {
  auto proc = makeRig(chainOf({makeIrBlockTree("blk-verb", 1, 100)}));
  expectImage(*proc, "mono-file");
}

TEST(IrStereoTest, DualMonoMakesItMono) {
  auto proc = makeRig(chainOf({verb()}), InputMode::DualMono);
  ASSERT_TRUE(proc->dualMonoEngaged());
  expectImage(*proc, "mono-dual-mono");
}

TEST(IrStereoTest, StereoChainsMakeItMono) {
  juce::ValueTree state("ChainSnapshot");
  state.setProperty("stereoEnabled", true, nullptr);
  juce::ValueTree left("ChainBlocks"), right("RightChainBlocks");
  left.appendChild(verb("blk-verb", 1), nullptr);
  right.appendChild(verb("blk-verb-r", 2), nullptr);
  state.appendChild(left, nullptr);
  state.appendChild(right, nullptr);

  auto proc = makeRig(state);
  EXPECT_EQ(imageOf(*proc, "blk-verb-r"), "mono-stereo-chains");
  expectImage(*proc, "mono-stereo-chains");
}

// One output channel: only the left result is heard, whatever the buffer's
// second channel holds.
TEST(IrStereoTest, MonoOutputMakesItMono) {
  auto proc = makeRig(chainOf({verb()}), InputMode::Stereo, 1);
  EXPECT_EQ(imageOf(*proc), "mono-output");
  EXPECT_FALSE(stereoIrActive(*proc));
}

// A bypassed block keeps its state on the card, but puts nothing stereo out.
TEST(IrStereoTest, BypassedStereoIrIsNotActive) {
  auto block = verb();
  block.setProperty("enabled", false, nullptr);
  auto proc = makeRig(chainOf({block}));
  EXPECT_EQ(imageOf(*proc), "stereo");
  EXPECT_FALSE(stereoIrActive(*proc));
  EXPECT_EQ(channelDiff(*proc), 0.0f);
}

// The block's Out meter keeps the two sides apart ("outLR"); "out" stays the
// louder side, as the tiles and the mono card read it.
TEST(IrStereoTest, BlockMeterKeepsTheTwoSidesApart) {
  auto proc = makeRig(chainOf({verb()}));
  const auto sides = [&] {
    channelDiff(*proc);
    const juce::var levels = proc->getMeterLevels()["blocks"]["blk-verb"];
    const float l = static_cast<float>(levels["outLR"][0]), r = static_cast<float>(levels["outLR"][1]);
    EXPECT_FLOAT_EQ(static_cast<float>(levels["out"]), std::max(l, r));
    EXPECT_GT(std::min(l, r), -40.0f);
    return std::pair<float, float>(l, r);
  };

  const auto [stereoL, stereoR] = sides();
  EXPECT_NE(stereoL, stereoR) << "the IR's two kernels should meter apart";

  letAudioGoIdle();
  ASSERT_TRUE(proc->setBlockIrStereo("blk-verb", false));
  const auto [monoL, monoR] = sides();
  EXPECT_EQ(monoL, monoR);
}

// ---------------------------------------------------------------------------
// The per-block choice
// ---------------------------------------------------------------------------

TEST(IrStereoTest, ToggleSwitchesTheAudioAndTheReport) {
  auto proc = makeRig(chainOf({verb()}));
  expectImage(*proc, "stereo");

  letAudioGoIdle();
  ASSERT_TRUE(proc->setBlockIrStereo("blk-verb", false));
  expectImage(*proc, "mono-user");

  letAudioGoIdle();
  ASSERT_TRUE(proc->setBlockIrStereo("blk-verb", true));
  expectImage(*proc, "stereo");
}

// The choice never outranks a mono reason: the block reports why it is mono
// and the choice waits underneath.
TEST(IrStereoTest, MonoReasonOutranksTheChoice) {
  auto proc = makeRig(chainOf({verb(), makeNamBlockTree("blk-amp", 2, 200)}));
  ASSERT_TRUE(proc->setBlockIrStereo("blk-verb", false));
  EXPECT_EQ(imageOf(*proc), "mono-nam-after");

  ASSERT_TRUE(proc->setBlockParam("blk-amp", "enabled", 0.0));
  EXPECT_EQ(imageOf(*proc), "mono-user");
}

TEST(IrStereoTest, RejectsOtherBlocksAndSkipsNoOps) {
  auto proc = makeRig(chainOf({verb(), makeNamBlockTree("blk-amp", 2, 200)}));
  EXPECT_FALSE(proc->setBlockIrStereo("blk-amp", false));
  EXPECT_FALSE(proc->setBlockIrStereo("blk-missing", false));

  EXPECT_TRUE(proc->setBlockIrStereo("blk-verb", true)) << "already stereo";
  EXPECT_FALSE(static_cast<bool>(proc->getChainState(-1)["canUndo"]))
      << "a no-op must not leave an undo step";
}

TEST(IrStereoTest, ChoiceIsOneUndoStep) {
  auto proc = makeRig(chainOf({verb()}));
  ASSERT_TRUE(proc->setBlockIrStereo("blk-verb", false));

  ASSERT_TRUE(proc->undoChain());
  ASSERT_TRUE(waitForChainLoaded(*proc));
  EXPECT_EQ(imageOf(*proc), "stereo");
  EXPECT_FALSE(static_cast<bool>(proc->getChainState(-1)["canUndo"]));

  ASSERT_TRUE(proc->redoChain());
  ASSERT_TRUE(waitForChainLoaded(*proc));
  EXPECT_EQ(imageOf(*proc), "mono-user");
}

TEST(IrStereoTest, ChoiceSurvivesSaveAndRestore) {
  auto proc = makeRig(chainOf({verb()}));
  ASSERT_TRUE(proc->setBlockIrStereo("blk-verb", false));
  juce::MemoryBlock saved;
  proc->getStateInformation(saved);

  ChainTestProcessor reopened;
  reopened.setPlayConfigDetails(2, 2, kFs, kBlock);
  reopened.prepareToPlay(kFs, kBlock);
  reopened.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  ASSERT_TRUE(waitForChainLoaded(reopened));
  expectImage(reopened, "mono-user");
}

TEST(IrStereoTest, DuplicateCarriesTheChoice) {
  auto proc = makeRig(chainOf({verb()}));
  ASSERT_TRUE(proc->setBlockIrStereo("blk-verb", false));

  const std::string cloneId = proc->duplicateChainBlock("blk-verb", "left", 1);
  ASSERT_FALSE(cloneId.empty());
  ASSERT_TRUE(waitForChainLoaded(*proc));
  EXPECT_EQ(imageOf(*proc, juce::String(cloneId)), "mono-user");
}

}  // namespace
