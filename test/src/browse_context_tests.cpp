// Browse context tests
//
// A tone block's browse context (BrowseContext.h) is the result list its
// tone was picked from; the UI's previous / next steps through it. These pin
// the contracts:
//
//   - sanitize keeps a well-formed context and drops or clamps everything
//     else, so a hostile project, preset or rig file can't hand the UI
//     out-of-range cursors, non-JSON values or megabytes of query,
//   - the context rides the block settings: getChainState reports it, a
//     save / reopen round trip keeps it, undo restores the previous one,
//     and a duplicate carries it,
//   - state from before contexts existed restores with none,
//   - a swap without a context clears the old one (a tone from anywhere but
//     the browser has no list to step through).
#include "BrowseContext.h"
#include "Processor.h"
#include "chain_test_helpers.h"

#include <gtest/gtest.h>

namespace {

juce::var toneRow(int id, const juce::String& title) {
  juce::DynamicObject::Ptr o = new juce::DynamicObject();
  o->setProperty("id", id);
  o->setProperty("title", title);
  o->setProperty("gear", "cab");
  o->setProperty("format", "ir");
  return juce::var(o.get());
}

juce::var context(int index, int count = 3, int page = 1, int totalPages = 2) {
  juce::Array<juce::var> tones;
  for (int i = 0; i < count; ++i)
    tones.add(toneRow(100 + i, "Tone " + juce::String(i)));
  juce::DynamicObject::Ptr query = new juce::DynamicObject();
  query->setProperty("text", "plexi");
  query->setProperty("gear", "cab");
  juce::DynamicObject::Ptr o = new juce::DynamicObject();
  o->setProperty("query", juce::var(query.get()));
  o->setProperty("page", page);
  o->setProperty("pageSize", 12);
  o->setProperty("totalPages", totalPages);
  o->setProperty("index", index);
  o->setProperty("tones", tones);
  return juce::var(o.get());
}

juce::String irTone(int toneId, int modelId) {
  return "{\"id\":" + juce::String(toneId) + ",\"title\":\"IR " + juce::String(toneId) +
         "\",\"format\":\"ir\",\"gear\":\"cab\",\"models\":[{\"id\":" + juce::String(modelId) +
         ",\"name\":\"cab\",\"model_url\":\"https://test.invalid/cab.wav\"}]}";
}

juce::var firstToneBlock(const juce::var& state) {
  if (const auto* lane = state["chain"].getArray())
    for (const auto& item : *lane)
      if (item["kind"].toString() == "tone")
        return item;
  return {};
}

}  // namespace

TEST(BrowseContextTest, SanitizeKeepsAWellFormedContext) {
  const auto clean = browsecontext::sanitize(context(1));
  ASSERT_TRUE(clean.isObject());
  EXPECT_EQ(static_cast<int>(clean["index"]), 1);
  EXPECT_EQ(static_cast<int>(clean["page"]), 1);
  EXPECT_EQ(static_cast<int>(clean["totalPages"]), 2);
  EXPECT_EQ(clean["tones"].size(), 3);
  EXPECT_EQ(clean["query"]["text"].toString(), "plexi");
  // Round trip through the persisted string form.
  const auto back = browsecontext::fromString(browsecontext::toString(clean));
  EXPECT_EQ(browsecontext::toString(back), browsecontext::toString(clean));
}

TEST(BrowseContextTest, SanitizeRejectsGarbage) {
  EXPECT_TRUE(browsecontext::sanitize({}).isVoid());
  EXPECT_TRUE(browsecontext::sanitize("tones").isVoid());
  EXPECT_TRUE(browsecontext::fromString("{not json").isVoid());
  EXPECT_TRUE(browsecontext::fromString("[1,2,3]").isVoid());
  // No usable tones at all.
  EXPECT_TRUE(browsecontext::fromString("{\"tones\":[{\"id\":-4},{\"title\":\"x\"},7]}").isVoid());
}

TEST(BrowseContextTest, SanitizeClampsAndTrims) {
  auto raw = context(999, 3, 50, 2);
  auto* o = raw.getDynamicObject();
  o->setProperty("pageSize", -3);
  // Mixed junk rows: only the positive ids survive, titles are capped.
  auto* tones = raw["tones"].getArray();
  tones->add(juce::var("not an object"));
  tones->add(toneRow(0, "zero id"));
  tones->add(toneRow(7, juce::String::repeatedString("x", 5000)));

  const auto clean = browsecontext::sanitize(raw);
  ASSERT_TRUE(clean.isObject());
  EXPECT_EQ(clean["tones"].size(), 4);
  EXPECT_EQ(static_cast<int>(clean["index"]), 3);  // clamped to the last row
  EXPECT_EQ(static_cast<int>(clean["page"]), 2);   // clamped to totalPages
  EXPECT_EQ(static_cast<int>(clean["pageSize"]), 1);
  EXPECT_EQ(clean["tones"][3]["title"].toString().length(), browsecontext::kMaxTextLength);
}

TEST(BrowseContextTest, SanitizeCapsTonesAndQuery) {
  auto raw = context(0, browsecontext::kMaxTones + 50);
  raw.getDynamicObject()->setProperty(
      "query", juce::JSON::parse("{\"text\":\"" +
                                 juce::String::repeatedString("q", browsecontext::kMaxQueryBytes) +
                                 "\"}"));
  const auto clean = browsecontext::sanitize(raw);
  ASSERT_TRUE(clean.isObject());
  EXPECT_EQ(clean["tones"].size(), browsecontext::kMaxTones);
  EXPECT_FALSE(clean.hasProperty("query"));
}

TEST(BrowseContextTest, TravelsWithTheBlock) {
  ChainTestProcessor proc;
  const auto blockId = proc.loadTone(irTone(100, 1), {}, context(0));
  ASSERT_FALSE(blockId.empty());
  auto block = firstToneBlock(proc.getChainState(-1));
  EXPECT_EQ(static_cast<int>(block["browse"]["index"]), 0);

  // A step: same block, next tone, moved cursor, in one undoable edit.
  ASSERT_TRUE(proc.swapTone(blockId, irTone(101, 2), context(1)));
  block = firstToneBlock(proc.getChainState(-1));
  EXPECT_EQ(static_cast<int>(block["tone"]["id"]), 101);
  EXPECT_EQ(static_cast<int>(block["browse"]["index"]), 1);

  // Save and reopen.
  juce::MemoryBlock saved;
  proc.getStateInformation(saved);
  ChainTestProcessor reopened;
  reopened.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  block = firstToneBlock(reopened.getChainState(-1));
  EXPECT_EQ(static_cast<int>(block["browse"]["index"]), 1);
  EXPECT_EQ(block["browse"]["query"]["text"].toString(), "plexi");

  // Undo puts the previous tone and cursor back.
  ASSERT_TRUE(proc.undoChain());
  block = firstToneBlock(proc.getChainState(-1));
  EXPECT_EQ(static_cast<int>(block["tone"]["id"]), 100);
  EXPECT_EQ(static_cast<int>(block["browse"]["index"]), 0);

  // A duplicate carries the cursor.
  const auto copyId = proc.duplicateChainBlock(blockId, "left", 1);
  ASSERT_FALSE(copyId.empty());
  int withContext = 0;
  const juce::var state = proc.getChainState(-1);
  for (const auto& item : *state["chain"].getArray())
    if (item["kind"].toString() == "tone" && item["browse"].isObject())
      ++withContext;
  EXPECT_EQ(withContext, 2);
}

TEST(BrowseContextTest, SwapWithoutContextClearsIt) {
  ChainTestProcessor proc;
  const auto blockId = proc.loadTone(irTone(100, 1), {}, context(0));
  ASSERT_TRUE(proc.swapTone(blockId, irTone(200, 3)));
  EXPECT_FALSE(firstToneBlock(proc.getChainState(-1)).hasProperty("browse"));
}

TEST(BrowseContextTest, LegacyStateRestoresWithoutContext) {
  ChainTestProcessor proc;
  juce::ValueTree state("ChainSnapshot");
  juce::ValueTree left("ChainBlocks");
  left.appendChild(makeIrBlockTree("blk", 1, 100), nullptr);
  state.appendChild(left, nullptr);
  proc.restoreFromTree(state);
  const auto block = firstToneBlock(proc.getChainState(-1));
  ASSERT_TRUE(block.isObject());
  EXPECT_FALSE(block.hasProperty("browse"));
}

TEST(BrowseContextTest, HostileStateIsSanitizedOnRestore) {
  ChainTestProcessor proc;
  juce::ValueTree state("ChainSnapshot");
  juce::ValueTree left("ChainBlocks");
  auto blockTree = makeIrBlockTree("blk", 1, 100);
  blockTree.setProperty("browse", "{\"index\":1e300,\"page\":-9,\"tones\":[{\"id\":5}]}", nullptr);
  left.appendChild(blockTree, nullptr);
  state.appendChild(left, nullptr);
  proc.restoreFromTree(state);
  const auto block = firstToneBlock(proc.getChainState(-1));
  ASSERT_TRUE(block["browse"].isObject());
  EXPECT_EQ(static_cast<int>(block["browse"]["index"]), 0);
  EXPECT_EQ(static_cast<int>(block["browse"]["page"]), 1);
}
