// Model-cache persistence tests (issue #127: Logic project bloat).
//
// The invariant under test: DAW state (and presets, same snapshot code)
// embeds model bytes only for models the block's tone still references (the
// active model, plus a local tone's full stored list), and restores seed
// only those back. Auditioned catalog models stay an in-memory
// convenience: switchModel collapses toneJson to the active model, so
// persisting the leftover bytes wrote 50-224 MB states that hosts then
// multiplied across autosaves/backups, with no way to ever use them again.
//
// Also pinned: a state whose activeModelId is missing from its own toneJson
// (older builds could drift the two apart) loads from the embedded bytes
// instead of stranding the block on a retry that can never resolve.
#include "Processor.h"
#include "chain_test_helpers.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <vector>

namespace {

// Append one more cached model to a block tree's ModelCache child, the shape
// older builds wrote for every model the user ever auditioned.
void appendCachedModel(juce::ValueTree& block, int modelId, const char* fileName) {
  juce::MemoryBlock bytes;
  EXPECT_TRUE(testFile(fileName).loadFileAsData(bytes));
  juce::ValueTree cached("CachedModel");
  cached.setProperty("modelId", modelId, nullptr);
  cached.setProperty("data", juce::var(bytes), nullptr);
  block.getChildWithName("ModelCache").appendChild(cached, nullptr);
}

// Decode a T3KB blob (getStateInformation's output) back into its state tree.
juce::ValueTree parseStateBlob(const juce::MemoryBlock& data) {
  if (data.getSize() <= 4 || std::memcmp(data.getData(), "T3KB", 4) != 0)
    return {};
  return juce::ValueTree::readFromData(static_cast<const char*>(data.getData()) + 4,
                                       data.getSize() - 4);
}

// Sorted ids of the models a saved state embeds for one block.
std::vector<int> cachedModelIds(const juce::ValueTree& state, const juce::String& blockId) {
  const juce::ValueTree snapshot = state.getChildWithName("ChainSnapshot");
  std::vector<int> ids;
  for (const auto* laneName : {"ChainBlocks", "RightChainBlocks"}) {
    const juce::ValueTree lane = snapshot.getChildWithName(laneName);
    for (int i = 0; i < lane.getNumChildren(); ++i) {
      const juce::ValueTree block = lane.getChild(i);
      if (block.getProperty("id").toString() != blockId)
        continue;
      const juce::ValueTree cache = block.getChildWithName("ModelCache");
      for (int j = 0; j < cache.getNumChildren(); ++j)
        ids.push_back(cache.getChild(j).getProperty("modelId"));
    }
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

// One block's row in the chain-state payload, by id.
juce::var chainStateBlock(TONE3000Processor& proc, const juce::String& blockId) {
  const juce::var state = proc.getChainState(-1);
  for (const auto* laneKey : {"chain", "chainRight"})
    if (const auto* lane = state[laneKey].getArray())
      for (const auto& item : *lane)
        if (item["blockId"].toString() == blockId)
          return item;
  return {};
}

// Poll the chain state until `done` holds for the block (background loads
// land on the pool's schedule).
bool waitForBlock(TONE3000Processor& proc, const juce::String& blockId,
                  const std::function<bool(const juce::var&)>& done, int timeoutMs = 20000) {
  const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
  while (juce::Time::getMillisecondCounter() < deadline) {
    const juce::var block = chainStateBlock(proc, blockId);
    if (block.isObject() && done(block))
      return true;
    juce::Thread::sleep(20);
  }
  return false;
}

// The model object switchModel receives from the UI (full catalog entry).
juce::var makeModelVar(int id, const juce::String& name, const juce::String& url) {
  juce::DynamicObject::Ptr model = new juce::DynamicObject();
  model->setProperty("id", id);
  model->setProperty("name", name);
  model->setProperty("model_url", url);
  return juce::var(model.get());
}

// A file:// URL whose target (and stash re-root, thanks to the unique leaf)
// doesn't exist: a fetch of it fails deterministically, with no network.
juce::String deadLocalUrl(const char* extension) {
  const juce::File missing =
      juce::File::getSpecialLocation(juce::File::tempDirectory)
          .getChildFile("t3k-missing-" + juce::Uuid().toString() + extension);
  return juce::URL(missing).toString(false);
}

juce::ValueTree monoSnapshot(const juce::ValueTree& block) {
  juce::ValueTree snapshot("ChainSnapshot");
  juce::ValueTree left("ChainBlocks");
  left.appendChild(block, nullptr);
  snapshot.appendChild(left, nullptr);
  return snapshot;
}

TEST(StateCacheTest, SaveEmbedsOnlyModelsTheToneStillReferences) {
  ChainTestProcessor proc;

  // A catalog NAM block as switchModel leaves it (toneJson names only the
  // active model 100), restored from a state that also carries two
  // auditioned leftovers, the shape older builds persisted.
  auto block = makeNamBlockTree("blk-a", 1, 100);
  appendCachedModel(block, 101, "a2-amp-test.nam");
  appendCachedModel(block, 102, "a2-amp-test.nam");
  proc.restoreFromTree(monoSnapshot(block));
  ASSERT_TRUE(waitForChainLoaded(proc));

  juce::MemoryBlock saved;
  proc.getStateInformation(saved);
  const juce::ValueTree state = parseStateBlob(saved);
  ASSERT_TRUE(state.isValid());
  EXPECT_EQ(cachedModelIds(state, "blk-a"), (std::vector<int>{100}));

  // The slimmed state must still reopen offline: the tone's model_url is
  // unfetchable, so a successful load proves the active bytes rode along.
  ChainTestProcessor reopened;
  reopened.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  EXPECT_TRUE(waitForChainLoaded(reopened));
}

TEST(StateCacheTest, LocalTonePersistsItsFullModelListAndSwitchesOffline) {
  ChainTestProcessor proc;

  // A drop-loaded local tone stores every model (that's its offline switch
  // catalog), so all of them are referenced and must survive the save.
  auto block = makeIrBlockTree("blk-local", 7, 300);
  block.setProperty(
      "toneJson",
      "{\"id\":7,\"title\":\"My Drops\",\"format\":\"ir\",\"local\":true,"
      "\"models\":[{\"id\":300,\"name\":\"drop-a\",\"model_url\":\"https://test.invalid/drop-a.wav\"},"
      "{\"id\":301,\"name\":\"drop-b\",\"model_url\":\"https://test.invalid/drop-b.wav\"}]}",
      nullptr);
  appendCachedModel(block, 301, "cab-ir-test.wav");
  proc.restoreFromTree(monoSnapshot(block));
  ASSERT_TRUE(waitForChainLoaded(proc));

  juce::MemoryBlock saved;
  proc.getStateInformation(saved);
  EXPECT_EQ(cachedModelIds(parseStateBlob(saved), "blk-local"), (std::vector<int>{300, 301}));

  // Switching to the sibling model works offline: its bytes were seeded from
  // the state (the URL is unfetchable, so a fetch would fail loudly).
  ASSERT_TRUE(proc.switchModel("blk-local", 301,
                               makeModelVar(301, "drop-b", "https://test.invalid/drop-b.wav")));
  EXPECT_TRUE(waitForBlock(proc, "blk-local", [](const juce::var& b) {
    return static_cast<int>(b["activeModelId"]) == 301 && static_cast<bool>(b["loaded"]) &&
           !static_cast<bool>(b["modelLoading"]) && !static_cast<bool>(b["loadFailed"]);
  }));
}

TEST(StateCacheTest, RestoreDoesNotSeedUnreferencedCacheEntries) {
  ChainTestProcessor proc;

  // A catalog block whose state carries a stale auditioned model (201) the
  // toneJson no longer names.
  auto block = makeIrBlockTree("blk-b", 2, 200);
  appendCachedModel(block, 201, "cab-ir-test.wav");
  proc.restoreFromTree(monoSnapshot(block));
  ASSERT_TRUE(waitForChainLoaded(proc));

  // The stale entry neither re-saves...
  juce::MemoryBlock saved;
  proc.getStateInformation(saved);
  EXPECT_EQ(cachedModelIds(parseStateBlob(saved), "blk-b"), (std::vector<int>{200}));

  // ...nor was it seeded into memory: switching to it with a dead URL must
  // fail (a seeded cache would satisfy the switch without any fetch).
  ASSERT_TRUE(proc.switchModel("blk-b", 201, makeModelVar(201, "stale", deadLocalUrl(".wav"))));
  EXPECT_TRUE(waitForBlock(proc, "blk-b", [](const juce::var& b) {
    return static_cast<bool>(b["loadFailed"]);
  }));
}

TEST(StateCacheTest, ActiveModelMissingFromToneJsonLoadsFromEmbeddedCache) {
  ChainTestProcessor proc;

  // Older builds could drift toneJson and activeModelId apart; the reporter's
  // log shows such blocks bailing "not in stored tone JSON" and stranding on
  // retry with their bytes sitting right in the state. They must load.
  auto block = makeNamBlockTree("blk-c", 3, 100);
  block.setProperty("activeModelId", 999, nullptr);
  appendCachedModel(block, 999, "a2-amp-test.nam");
  proc.restoreFromTree(monoSnapshot(block));
  ASSERT_TRUE(waitForChainLoaded(proc));

  // The active model's bytes survive the next save even though toneJson
  // can't name them (they're the only way this block ever reopens), along
  // with the toneJson-referenced model 100.
  juce::MemoryBlock saved;
  proc.getStateInformation(saved);
  EXPECT_EQ(cachedModelIds(parseStateBlob(saved), "blk-c"), (std::vector<int>{100, 999}));

  ChainTestProcessor reopened;
  reopened.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  EXPECT_TRUE(waitForChainLoaded(reopened));
}

}  // namespace
