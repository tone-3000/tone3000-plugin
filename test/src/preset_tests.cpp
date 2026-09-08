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

TEST(PresetManagerTest, SystemFactoryPresetsListedAndLocalOverrideWins) {
  TempPresetDir tmp;
  TempPresetDir system;  // stands in for the installer-shipped Factory dir

  auto writeFactoryFile = [](const juce::File& file, const juce::String& name) {
    juce::ValueTree preset(PresetManager::kPresetTag);
    preset.setProperty("name", name, nullptr);
    juce::FileOutputStream out(file);
    ASSERT_TRUE(out.openedOk());
    out.write("T3KB", 4);
    preset.writeToStream(out);
  };
  writeFactoryFile(system.dir.getChildFile(juce::String("clean") + PresetManager::kFileExtension),
                   "Shipped Clean");
  writeFactoryFile(system.dir.getChildFile(juce::String("lead") + PresetManager::kFileExtension),
                   "Shipped Lead");
  tmp.dir.getChildFile("Factory").createDirectory();
  writeFactoryFile(tmp.dir.getChildFile("Factory").getChildFile(
                       juce::String("clean") + PresetManager::kFileExtension),
                   "Local Clean");

  // Same stem in both dirs collapses to one entry, with the local file
  // winning; the untouched shipped preset still lists and loads.
  PresetManager mgr(tmp.dir, system.dir);
  const auto presets = mgr.list();
  ASSERT_EQ(presets.size(), 2u);
  EXPECT_EQ(presets[0].name, juce::String("Local Clean"));
  EXPECT_TRUE(presets[0].factory);
  EXPECT_EQ(presets[1].name, juce::String("Shipped Lead"));
  EXPECT_EQ(mgr.load("factory:clean").getProperty("name").toString(),
            juce::String("Local Clean"));
  EXPECT_EQ(mgr.load("factory:lead").getProperty("name").toString(),
            juce::String("Shipped Lead"));

  // Shipped presets are as read-only as local factory ones.
  EXPECT_FALSE(mgr.rename("factory:lead", "Hacked"));
  EXPECT_FALSE(mgr.remove("factory:lead"));
}

TEST(PresetManagerTest, UserSectionListsBeforeFactory) {
  // The list order is the MIDI program-change order, and user presets own
  // the low numbers. The factory preset is named to sort first so only the
  // section rule can put it last.
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);
  tmp.dir.getChildFile("Factory").createDirectory();
  {
    juce::ValueTree preset(PresetManager::kPresetTag);
    preset.setProperty("name", "AAA Factory", nullptr);
    juce::FileOutputStream out(tmp.dir.getChildFile("Factory").getChildFile(
        juce::String("aaa") + PresetManager::kFileExtension));
    ASSERT_TRUE(out.openedOk());
    out.write("T3KB", 4);
    preset.writeToStream(out);
  }
  const auto alpha = mgr.save("Alpha", makePreset("a"));
  const auto zulu = mgr.save("Zulu", makePreset("z"));

  auto presets = mgr.list();
  ASSERT_EQ(presets.size(), 3u);
  EXPECT_FALSE(presets[0].factory);
  EXPECT_FALSE(presets[1].factory);
  EXPECT_TRUE(presets[2].factory);

  // A custom order (order.json) keeps the sections separated too.
  ASSERT_TRUE(mgr.move(zulu.id, -1));
  presets = mgr.list();
  ASSERT_EQ(presets.size(), 3u);
  EXPECT_EQ(presets[0].id, zulu.id);
  EXPECT_EQ(presets[1].id, alpha.id);
  EXPECT_TRUE(presets[2].factory);
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

TEST(PresetManagerTest, MoveShiftsByDeltaWithinTheSection) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);
  const auto a = mgr.save("Alpha", makePreset("a"));
  const auto b = mgr.save("Beta", makePreset("b"));
  const auto c = mgr.save("Gamma", makePreset("c"));
  const auto d = mgr.save("Delta", makePreset("d"));

  // Name order Alpha, Beta, Delta, Gamma. Sliding Delta back two puts it first.
  ASSERT_TRUE(mgr.move(d.id, -2));
  auto presets = mgr.list();
  ASSERT_EQ(presets.size(), 4u);
  EXPECT_EQ(presets[0].id, d.id);
  EXPECT_EQ(presets[1].id, a.id);
  EXPECT_EQ(presets[2].id, b.id);
  EXPECT_EQ(presets[3].id, c.id);

  // A delta that would leave the section is a no-op at the edge.
  EXPECT_FALSE(mgr.move(d.id, -4));
}

TEST(PresetManagerTest, CategoryLifecycleAndSorting) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);

  EXPECT_TRUE(mgr.addCategory("Rock"));
  EXPECT_TRUE(mgr.addCategory("ambient"));
  EXPECT_TRUE(mgr.addCategory("Blues"));

  // Duplicate (case-insensitive) is refused
  EXPECT_FALSE(mgr.addCategory("rock"));
  EXPECT_FALSE(mgr.addCategory("ROCK"));

  // Empty or whitespace only is refused
  EXPECT_FALSE(mgr.addCategory("   "));

  // > 50 characters is refused
  const juce::String tooLong(std::string(51, 'a'));
  EXPECT_FALSE(mgr.addCategory(tooLong));

  // 50 characters is allowed
  const juce::String exactly50(std::string(50, 'b'));
  EXPECT_TRUE(mgr.addCategory(exactly50));

  // Sorted case-insensitively: ambient, bbbbb..., Blues, Rock
  const auto categories = mgr.listCategories();
  ASSERT_EQ(categories.size(), 4);
  EXPECT_EQ(categories[0], juce::String("ambient"));
  EXPECT_EQ(categories[1], exactly50);
  EXPECT_EQ(categories[2], juce::String("Blues"));
  EXPECT_EQ(categories[3], juce::String("Rock"));

  // Survives fresh PresetManager instance (categories.json)
  PresetManager fresh(tmp.dir);
  EXPECT_EQ(fresh.listCategories(), categories);
}

TEST(PresetManagerTest, CategoryMultiLingualNames) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);

  EXPECT_TRUE(mgr.addCategory(juce::CharPointer_UTF8("リード・ギター")));
  EXPECT_TRUE(mgr.addCategory(juce::CharPointer_UTF8("Café Rock 🎸")));
  EXPECT_TRUE(mgr.addCategory(juce::CharPointer_UTF8("Schöne Töne")));

  const auto categories = mgr.listCategories();
  ASSERT_EQ(categories.size(), 3);
  EXPECT_TRUE(categories.contains(juce::CharPointer_UTF8("リード・ギター")));
  EXPECT_TRUE(categories.contains(juce::CharPointer_UTF8("Café Rock 🎸")));
  EXPECT_TRUE(categories.contains(juce::CharPointer_UTF8("Schöne Töne")));
}

TEST(PresetManagerTest, PresetCategoryAssignmentAndMove) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);

  mgr.addCategory("Rock");
  const auto p1 = mgr.save("Lead 1", makePreset("p1"));
  const auto p2 = mgr.save("Rhythm 1", makePreset("p2"));

  EXPECT_TRUE(mgr.setPresetCategory(p1.id, "Rock"));
  EXPECT_TRUE(mgr.setPresetCategory(p2.id, "Rock"));

  auto list = mgr.list();
  ASSERT_EQ(list.size(), 2u);
  EXPECT_EQ(list[0].category, juce::String("Rock"));
  EXPECT_EQ(list[1].category, juce::String("Rock"));

  // Move back to root (empty category)
  EXPECT_TRUE(mgr.setPresetCategory(p1.id, ""));
  list = mgr.list();
  EXPECT_EQ(list[0].category, juce::String(""));
}

TEST(PresetManagerTest, DeleteCategoryMovesPresetsToRoot) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);

  mgr.addCategory("Clean");
  const auto p1 = mgr.save("Warm Clean", makePreset("p1"));
  const auto p2 = mgr.save("Bright Clean", makePreset("p2"));

  mgr.setPresetCategory(p1.id, "Clean");
  mgr.setPresetCategory(p2.id, "Clean");

  // Deleting the category removes the category from the list
  EXPECT_TRUE(mgr.deleteCategory("Clean"));
  EXPECT_EQ(mgr.listCategories().size(), 0);

  // But the presets are NOT deleted; their category is reset to root ("")
  const auto list = mgr.list();
  ASSERT_EQ(list.size(), 2u);
  EXPECT_EQ(list[0].category, juce::String(""));
  EXPECT_EQ(list[1].category, juce::String(""));
  EXPECT_TRUE(mgr.load(p1.id).isValid());
  EXPECT_TRUE(mgr.load(p2.id).isValid());
}

TEST(PresetManagerTest, FavoritesLifecycle) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);

  const auto p1 = mgr.save("Fav 1", makePreset("f1"));
  const auto p2 = mgr.save("Normal 2", makePreset("f2"));

  EXPECT_TRUE(mgr.setPresetFavorite(p1.id, true));

  auto list = mgr.list();
  ASSERT_EQ(list.size(), 2u);
  EXPECT_TRUE(list[0].favorite);
  EXPECT_FALSE(list[1].favorite);

  // Survives fresh PresetManager instance
  PresetManager fresh(tmp.dir);
  list = fresh.list();
  EXPECT_TRUE(list[0].favorite);
  EXPECT_FALSE(list[1].favorite);

  // Unstarring works
  EXPECT_TRUE(fresh.setPresetFavorite(p1.id, false));
  list = fresh.list();
  EXPECT_FALSE(list[0].favorite);

  // Batch starring and unstarring
  juce::StringArray both;
  both.add(p1.id);
  both.add(p2.id);
  EXPECT_TRUE(fresh.setPresetsFavorite(both, true));
  list = fresh.list();
  EXPECT_TRUE(list[0].favorite);
  EXPECT_TRUE(list[1].favorite);

  EXPECT_TRUE(fresh.setPresetsFavorite(both, false));
  list = fresh.list();
  EXPECT_FALSE(list[0].favorite);
  EXPECT_FALSE(list[1].favorite);
}

TEST(PresetManagerTest, DuplicatePresetAndBatch) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);

  mgr.addCategory("Metal");
  const auto orig = mgr.save("Chug", makePreset("metal"));
  mgr.setPresetCategory(orig.id, "Metal");

  const auto dup = mgr.duplicatePreset(orig.id);
  ASSERT_TRUE(dup.id.isNotEmpty());
  EXPECT_NE(dup.id, orig.id);
  EXPECT_EQ(dup.name, juce::String("Copy-Chug"));
  EXPECT_EQ(dup.category, juce::String("Metal"));

  const auto list = mgr.list();
  ASSERT_EQ(list.size(), 2u);

  // Duplicate a copy -> "Copy-Copy-Chug"
  const auto dup2 = mgr.duplicatePreset(dup.id);
  EXPECT_EQ(dup2.name, juce::String("Copy-Copy-Chug"));

  // Batch delete
  juce::StringArray toDelete;
  toDelete.add(dup.id);
  toDelete.add(dup2.id);
  EXPECT_TRUE(mgr.removePresets(toDelete));
  EXPECT_EQ(mgr.list().size(), 1u);
}

}  // namespace
