// Chain slot tests
//
// Adding beside a block (see plugin/docs/chain-slots.md):
// loadTone("before:<id>" / "after:<id>") splices a new block in beside
// another without consuming a slot, the lane's empty tail re-padding behind
// it, and is one undo step. Removing a block closes the lane up (the blocks
// after it move up), as the plugin always has.
//
// Structure only: the seeded blocks' (fake) model URLs never need to load.
#include "Processor.h"
#include "chain_test_helpers.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

// The lane as block ids, "_" for an empty slot.
std::vector<std::string> layout(TONE3000Processor& proc, const char* laneKey = "chain") {
  std::vector<std::string> out;
  const juce::var state = proc.getChainState(-1);  // held: the lane array lives in it
  if (const auto* lane = state[laneKey].getArray())
    for (const auto& item : *lane)
      out.push_back(item["kind"].toString() == "tone" ? item["blockId"].toString().toStdString() : "_");
  return out;
}

void seedMono(ChainTestProcessor& proc, const std::vector<juce::String>& ids) {
  juce::ValueTree state("ChainSnapshot");
  juce::ValueTree left("ChainBlocks");
  int toneId = 1, modelId = 100;
  for (const auto& id : ids)
    left.appendChild(makeIrBlockTree(id, toneId++, modelId++), nullptr);
  state.appendChild(left, nullptr);
  proc.restoreFromTree(state);
  ASSERT_TRUE(waitForChainLoaded(proc));
}

// A tone the loader accepts (its model never resolves; structure only).
juce::String toneJson(int id) {
  return "{\"id\":" + juce::String(id) + ",\"title\":\"Spliced\",\"format\":\"ir\",\"models\":[{\"id\":" +
         juce::String(id * 10) + ",\"name\":\"x\",\"model_url\":\"https://test.invalid/x.wav\"}]}";
}

using L = std::vector<std::string>;

TEST(ChainSlotTest, ByDefaultTheBlocksAfterARemovedOneMoveUp) {
  ChainTestProcessor proc;
  seedMono(proc, {"pedal", "amp", "cab"});
  ASSERT_TRUE(proc.removeChainBlock("pedal"));
  EXPECT_EQ(layout(proc), (L{"amp", "cab", "_", "_", "_"}));

  ChainTestProcessor full;
  seedMono(full, {"a", "b", "c", "d", "e", "f"});
  ASSERT_TRUE(full.removeChainBlock("b"));
  EXPECT_EQ(layout(full), (L{"a", "c", "d", "e", "f", "_"}));
}

TEST(ChainSlotTest, LoadsBeforeAndAfterABlock) {
  ChainTestProcessor proc;
  seedMono(proc, {"amp", "cab"});

  const auto before = proc.loadTone(toneJson(7), "before:amp");
  ASSERT_FALSE(before.empty());
  EXPECT_EQ(layout(proc), (L{before, "amp", "cab", "_", "_"}));

  const auto after = proc.loadTone(toneJson(8), "after:amp");
  ASSERT_FALSE(after.empty());
  EXPECT_EQ(layout(proc), (L{before, "amp", after, "cab", "_"}));

  // Past the minimum the lane grows, still ending in one empty slot.
  const auto last = proc.loadTone(toneJson(9), "after:cab");
  ASSERT_FALSE(last.empty());
  EXPECT_EQ(layout(proc), (L{before, "amp", after, "cab", last, "_"}));

  // Each is one undo step.
  ASSERT_TRUE(proc.undoChain());
  EXPECT_EQ(layout(proc), (L{before, "amp", after, "cab", "_"}));
}

TEST(ChainSlotTest, BeforeTheFirstOfAFullLane) {
  ChainTestProcessor proc;
  seedMono(proc, {"a", "b", "c", "d", "e"});
  const auto id = proc.loadTone(toneJson(7), "before:a");
  ASSERT_FALSE(id.empty());
  EXPECT_EQ(layout(proc), (L{id, "a", "b", "c", "d", "e", "_"}));
}

TEST(ChainSlotTest, AStaleBesideTargetFallsBackToTheFirstEmptySlot) {
  ChainTestProcessor proc;
  seedMono(proc, {"amp"});
  const auto id = proc.loadTone(toneJson(7), "before:gone");
  ASSERT_FALSE(id.empty());
  EXPECT_EQ(layout(proc), (L{"amp", id, "_", "_", "_"}));
}

}  // namespace
