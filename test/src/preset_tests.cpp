// PresetManager file-layer tests, run against a throwaway temp directory.
//
// The store moved to a magic-prefixed binary ValueTree format (same T3KB
// framing as plugin state); these pin the format, the same-name-overwrite
// save path, the user/factory split, and the custom ordering.
#include "PresetManager.h"

#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

namespace {

// Fresh temp preset root per test, deleted on destruction.
struct TempPresetDir {
  TempPresetDir()
      : dir(juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("t3k-preset-tests-" + juce::Uuid().toString())) {
    dir.createDirectory();
  }
  ~TempPresetDir() { dir.deleteRecursively(); }
  juce::File dir;
};

juce::ValueTree makePreset(const juce::String& marker) {
  juce::ValueTree preset(PresetManager::kPresetTag);
  preset.setProperty("marker", marker, nullptr);
  return preset;
}

TEST(PresetManagerTest, SaveLoadRoundTripAndSameNameOverwrites) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);

  const auto info = mgr.save("Lead", makePreset("v1"));
  ASSERT_TRUE(info.id.isNotEmpty());
  EXPECT_FALSE(info.factory);

  juce::ValueTree loaded = mgr.load(info.id);
  ASSERT_TRUE(loaded.isValid());
  EXPECT_EQ(loaded.getProperty("marker").toString(), juce::String("v1"));
  EXPECT_EQ(loaded.getProperty("name").toString(), juce::String("Lead"));

  // Saving the same name again is the update path: same id, new payload,
  // still exactly one preset in the list.
  const auto updated = mgr.save("Lead", makePreset("v2"));
  EXPECT_EQ(updated.id, info.id);
  EXPECT_EQ(mgr.load(info.id).getProperty("marker").toString(), juce::String("v2"));
  EXPECT_EQ(mgr.list().size(), 1u);
}

TEST(PresetManagerTest, RenameAndRemoveApplyToUserPresetsOnly) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);

  const auto info = mgr.save("Old Name", makePreset("x"));
  ASSERT_TRUE(mgr.rename(info.id, "New Name"));
  EXPECT_EQ(mgr.load(info.id).getProperty("name").toString(), juce::String("New Name"));
  EXPECT_FALSE(mgr.rename(info.id, "   "));  // blank names refused

  // A factory preset (file dropped into Factory/) refuses rename and remove.
  tmp.dir.getChildFile("Factory").createDirectory();
  PresetManager seeded(tmp.dir);
  const auto factoryFile =
      tmp.dir.getChildFile("Factory").getChildFile(juce::String("clean") +
                                                   PresetManager::kFileExtension);
  {
    juce::FileOutputStream out(factoryFile);
    ASSERT_TRUE(out.openedOk());
    out.write("T3KB", 4);
    makePreset("f").writeToStream(out);
  }
  EXPECT_FALSE(seeded.rename("factory:clean", "Hacked"));
  EXPECT_FALSE(seeded.remove("factory:clean"));

  EXPECT_TRUE(mgr.remove(info.id));
  EXPECT_FALSE(mgr.load(info.id).isValid());
}

TEST(PresetManagerTest, ListSkipsCorruptAndForeignFiles) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);
  mgr.save("Good", makePreset("ok"));

  // Legacy XML and truncated files must be ignored, not crash or list.
  tmp.dir.getChildFile(juce::String("legacy") + PresetManager::kFileExtension)
      .replaceWithText("<T3KPreset name=\"Old XML\"/>");
  tmp.dir.getChildFile(juce::String("trunc") + PresetManager::kFileExtension)
      .replaceWithText("T3");

  const auto presets = mgr.list();
  ASSERT_EQ(presets.size(), 1u);
  EXPECT_EQ(presets[0].name, juce::String("Good"));
  EXPECT_FALSE(mgr.load("user:legacy").isValid());
}

TEST(PresetManagerTest, MovePersistsOrderWithinTheUserSection) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);
  const auto a = mgr.save("Alpha", makePreset("a"));
  const auto b = mgr.save("Beta", makePreset("b"));
  const auto c = mgr.save("Gamma", makePreset("c"));

  // Name order by default; moving Gamma up one lands it between the others,
  // and the order survives a fresh manager (order.json).
  ASSERT_TRUE(mgr.move(c.id, -1));
  PresetManager fresh(tmp.dir);
  const auto presets = fresh.list();
  ASSERT_EQ(presets.size(), 3u);
  EXPECT_EQ(presets[0].id, a.id);
  EXPECT_EQ(presets[1].id, c.id);
  EXPECT_EQ(presets[2].id, b.id);

  // Edges are refused: Alpha is already first.
  EXPECT_FALSE(fresh.move(a.id, -1));
}

}  // namespace
