// Shipped factory preset content tests.
//
// The installers ship resources/factory-presets/*.t3kpreset verbatim, so the
// files themselves are release artifacts. These pin what shipping requires:
//
//   - every file parses in the real T3KB preset format,
//   - every tone block's active model bytes are embedded, so a fresh install
//     loads the preset offline (no network, no auth),
//   - no block embeds bytes its tone no longer references: auditioned
//     leftovers baked in at authoring time bloated every user project that
//     loaded the preset until the persistence prune landed (issue #127; see
//     state_cache_tests.cpp for the runtime invariant),
//   - the chain actually restores and loads through the real processor.
#include "PresetManager.h"
#include "Processor.h"
#include "chain_test_helpers.h"

#include <gtest/gtest.h>

#include <cstring>
#include <set>

namespace {

juce::File factoryPresetsDir() {
  // T3K_TEST_FILES_DIR is <repo>/test/files; the shipped presets live at
  // <repo>/resources/factory-presets.
  return juce::File(T3K_TEST_FILES_DIR)
      .getParentDirectory()
      .getParentDirectory()
      .getChildFile("resources")
      .getChildFile("factory-presets");
}

// The model ids a block's stored tone can still name: its active model plus
// the toneJson models array (the same rule as ChainBlock::referencesModel,
// applied to the file's shape before any restore).
std::set<int> referencedModelIds(const juce::ValueTree& block) {
  std::set<int> ids;
  ids.insert(static_cast<int>(block.getProperty("activeModelId", 0)));
  const juce::var tone = juce::JSON::parse(block.getProperty("toneJson").toString());
  if (const auto* models = tone["models"].getArray())
    for (const auto& model : *models)
      ids.insert(static_cast<int>(model["id"]));
  return ids;
}

TEST(FactoryPresetTest, ShippedPresetsAreSlimAndLoadOffline) {
  const juce::File dir = factoryPresetsDir();
  if (!dir.isDirectory())
    GTEST_SKIP() << "no resources/factory-presets directory in this checkout";

  const auto files = dir.findChildFiles(juce::File::findFiles, false,
                                        "*" + juce::String(PresetManager::kFileExtension));
  ASSERT_FALSE(files.isEmpty()) << "factory preset folder exists but holds no presets";

  for (const auto& file : files) {
    SCOPED_TRACE(file.getFileName().toStdString());

    // Real preset framing: T3KB magic + binary ValueTree (see PresetManager).
    juce::FileInputStream in(file);
    ASSERT_TRUE(in.openedOk());
    char magic[4]{};
    ASSERT_EQ(in.read(magic, 4), 4);
    ASSERT_EQ(std::memcmp(magic, "T3KB", 4), 0);
    const juce::ValueTree preset = juce::ValueTree::readFromStream(in);
    ASSERT_TRUE(preset.hasType(PresetManager::kPresetTag));

    const juce::ValueTree snapshot = preset.getChildWithName("ChainSnapshot");
    ASSERT_TRUE(snapshot.isValid());

    // File-level invariants, per tone block in both lanes.
    int toneBlocks = 0;
    for (const auto* laneName : {"ChainBlocks", "RightChainBlocks"}) {
      const juce::ValueTree lane = snapshot.getChildWithName(laneName);
      for (int i = 0; i < lane.getNumChildren(); ++i) {
        const juce::ValueTree block = lane.getChild(i);
        const juce::ValueTree cache = block.getChildWithName("ModelCache");
        if (!cache.isValid())
          continue;  // insert slot
        ++toneBlocks;

        const std::set<int> referenced = referencedModelIds(block);
        std::set<int> cached;
        for (int j = 0; j < cache.getNumChildren(); ++j) {
          const juce::ValueTree model = cache.getChild(j);
          const int modelId = model.getProperty("modelId");
          cached.insert(modelId);
          EXPECT_NE(referenced.count(modelId), 0u)
              << "embeds bytes for model " << modelId << ", which block "
              << block.getProperty("id").toString() << "'s tone no longer references";
          EXPECT_NE(model.getProperty("data").getBinaryData(), nullptr);
        }
        EXPECT_NE(cached.count(static_cast<int>(block.getProperty("activeModelId", 0))), 0u)
            << "active model bytes not embedded; a fresh install can't load this offline";
      }
    }
    ASSERT_GT(toneBlocks, 0) << "preset has no tone blocks";

    // And the chain restores + loads through the real processor, cache-first
    // (the assertions above guarantee no load ever needs the network).
    ChainTestProcessor proc;
    proc.restoreFromTree(snapshot);
    EXPECT_TRUE(waitForChainLoaded(proc)) << "preset chain did not finish loading offline";
  }
}

}  // namespace
