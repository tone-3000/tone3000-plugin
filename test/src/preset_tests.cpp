// PresetManager file-layer tests, run against a throwaway temp directory.
//
// The store is one magic-prefixed binary ValueTree per file (PresetFile.h:
// v2 "T3KH" with a small id/name header, legacy v1 "T3KB" still readable);
// these pin the framing, the same-name-overwrite save path, the user/factory
// split, readable filenames with in-file identity, and the custom ordering.
#include "PresetManager.h"

#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <cstring>

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

// Writes a raw preset file the way a build before readable filenames did:
// arbitrary stem, "name" inside, no "id" property.
void writeRawPreset(const juce::File& file, const juce::String& name,
                    const juce::String& id = {}) {
  juce::ValueTree preset(PresetManager::kPresetTag);
  preset.setProperty("name", name, nullptr);
  if (id.isNotEmpty())
    preset.setProperty("id", id, nullptr);
  file.getParentDirectory().createDirectory();
  file.deleteFile();  // FileOutputStream appends to an existing file
  juce::FileOutputStream out(file);
  ASSERT_TRUE(out.openedOk());
  out.write("T3KB", 4);
  preset.writeToStream(out);
}

juce::String withExt(const juce::String& stem) { return stem + PresetManager::kFileExtension; }

// The .t3kpreset filenames in `dir`, sorted; compare against files({...}).
juce::StringArray presetFileNames(const juce::File& dir) {
  juce::StringArray names;
  for (const auto& f :
       dir.findChildFiles(juce::File::findFiles, false, "*" + juce::String(PresetManager::kFileExtension)))
    names.add(f.getFileName());
  names.sort(false);
  return names;
}

juce::StringArray files(std::initializer_list<const char*> stems) {
  juce::StringArray names;
  for (const char* stem : stems)
    names.add(withExt(stem));
  names.sort(false);
  return names;
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

// Readable filenames

TEST(PresetManagerTest, SanitizeFileStemIsValidEverywhere) {
  using PM = PresetManager;
  // Plain names pass through, spaces and unicode included.
  EXPECT_EQ(PM::sanitizeFileStem("Marshall Bluesbreaker"), juce::String("Marshall Bluesbreaker"));
  EXPECT_EQ(PM::sanitizeFileStem(juce::CharPointer_UTF8("Für Élise 東京")),
            juce::String(juce::CharPointer_UTF8("Für Élise 東京")));
  // The union of every platform's forbidden characters becomes "-", one per
  // run; dashes the user typed stay.
  EXPECT_EQ(PM::sanitizeFileStem("A/B\\C:D*E?F\"G<H>I|J"), juce::String("A-B-C-D-E-F-G-H-I-J"));
  EXPECT_EQ(PM::sanitizeFileStem("Clean // Lead"), juce::String("Clean - Lead"));
  EXPECT_EQ(PM::sanitizeFileStem("A -- B"), juce::String("A -- B"));
  EXPECT_EQ(PM::sanitizeFileStem(juce::String("Tab\tNew\nLine")), juce::String("Tab-New-Line"));
  // Leading/trailing dots and spaces, leading "~", all gone.
  EXPECT_EQ(PM::sanitizeFileStem("  .hidden. "), juce::String("hidden"));
  EXPECT_EQ(PM::sanitizeFileStem("~lock"), juce::String("lock"));
  EXPECT_EQ(PM::sanitizeFileStem("trailing..."), juce::String("trailing"));
  // Windows device names are prefixed, case-insensitively.
  EXPECT_EQ(PM::sanitizeFileStem("con"), juce::String("_con"));
  EXPECT_EQ(PM::sanitizeFileStem("LPT1"), juce::String("_LPT1"));
  EXPECT_EQ(PM::sanitizeFileStem("con.backup"), juce::String("_con.backup"));  // rule stops at the first dot
  EXPECT_EQ(PM::sanitizeFileStem("Console"), juce::String("Console"));
  // Nothing left: the floor.
  EXPECT_EQ(PM::sanitizeFileStem(""), juce::String("Preset"));
  EXPECT_EQ(PM::sanitizeFileStem("///"), juce::String("Preset"));  // only replacement dashes
  EXPECT_EQ(PM::sanitizeFileStem(" . . "), juce::String("Preset"));
  // Byte cap on a code-point boundary: 3-byte CJK glyphs never get split.
  const juce::String cjk = juce::String::repeatedString(juce::CharPointer_UTF8("東"), 60);
  const juce::String capped = PM::sanitizeFileStem(cjk);
  EXPECT_LE(capped.getNumBytesAsUTF8(), PM::kMaxStemBytes);
  EXPECT_EQ(capped.length(), PM::kMaxStemBytes / 3);
  EXPECT_LE(PM::sanitizeFileStem(juce::String::repeatedString("x", 500)).length(), PM::kMaxStemBytes);
}

TEST(PresetManagerTest, SaveUsesTheDisplayNameAsFilenameAndAUuidAsId) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);

  const auto info = mgr.save("Marshall Bluesbreaker", makePreset("m"));
  ASSERT_TRUE(info.id.startsWith("user:"));
  const juce::String rawId = info.id.fromFirstOccurrenceOf("user:", false, false);
  EXPECT_EQ(rawId.length(), 32);  // juce::Uuid hex, no dashes
  EXPECT_TRUE(rawId.containsOnly("0123456789abcdef"));

  EXPECT_EQ(presetFileNames(tmp.dir), files({"Marshall Bluesbreaker"}));
  // The id lives inside the file, and load() resolves through it.
  juce::ValueTree loaded = mgr.load(info.id);
  ASSERT_TRUE(loaded.isValid());
  EXPECT_EQ(loaded.getProperty("id").toString(), rawId);
  EXPECT_EQ(loaded.getProperty("name").toString(), juce::String("Marshall Bluesbreaker"));

  // Forbidden characters in the name only affect the filename.
  const auto slashy = mgr.save("Clean / Lead", makePreset("s"));
  EXPECT_TRUE(tmp.dir.getChildFile(withExt("Clean - Lead")).existsAsFile());
  EXPECT_EQ(mgr.load(slashy.id).getProperty("name").toString(), juce::String("Clean / Lead"));
  ASSERT_EQ(mgr.list().size(), 2u);
}

TEST(PresetManagerTest, FilenameCollisionsGetANumericSuffix) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);

  // Different names, same sanitized stem: distinct presets, distinct files.
  const auto a = mgr.save("A/B", makePreset("1"));
  const auto b = mgr.save("A:B", makePreset("2"));
  const auto c = mgr.save("A|B", makePreset("3"));
  EXPECT_NE(a.id, b.id);
  EXPECT_NE(b.id, c.id);
  EXPECT_EQ(presetFileNames(tmp.dir), files({"A-B", "A-B 2", "A-B 3"}));
  EXPECT_EQ(mgr.load(a.id).getProperty("marker").toString(), juce::String("1"));
  EXPECT_EQ(mgr.load(b.id).getProperty("marker").toString(), juce::String("2"));
  EXPECT_EQ(mgr.load(c.id).getProperty("marker").toString(), juce::String("3"));

  // Same-name save still overwrites in place: no " 2" file appears.
  mgr.save("A/B", makePreset("1b"));
  EXPECT_EQ(presetFileNames(tmp.dir).size(), 3);
  EXPECT_EQ(mgr.load(a.id).getProperty("marker").toString(), juce::String("1b"));
  EXPECT_EQ(mgr.list().size(), 3u);
}

TEST(PresetManagerTest, RenameRenamesTheFileAndKeepsTheId) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);

  const auto info = mgr.save("Old Name", makePreset("x"));
  ASSERT_TRUE(mgr.rename(info.id, "New Name"));
  EXPECT_EQ(presetFileNames(tmp.dir), files({"New Name"}));
  EXPECT_EQ(mgr.load(info.id).getProperty("name").toString(), juce::String("New Name"));
  const auto presets = mgr.list();
  ASSERT_EQ(presets.size(), 1u);
  EXPECT_EQ(presets[0].id, info.id);

  // Renaming onto a taken filename disambiguates instead of clobbering.
  const auto other = mgr.save("Taken", makePreset("t"));
  ASSERT_TRUE(mgr.rename(info.id, "Taken"));
  EXPECT_EQ(presetFileNames(tmp.dir), files({"Taken", "Taken 2"}));
  EXPECT_EQ(mgr.load(other.id).getProperty("marker").toString(), juce::String("t"));
  EXPECT_EQ(mgr.load(info.id).getProperty("marker").toString(), juce::String("x"));

  // A case-only rename keeps one file and takes the new spelling.
  ASSERT_TRUE(mgr.rename(other.id, "TAKEN"));
  EXPECT_EQ(mgr.list().size(), 2u);
  EXPECT_EQ(mgr.load(other.id).getProperty("name").toString(), juce::String("TAKEN"));
  EXPECT_TRUE(presetFileNames(tmp.dir).contains(withExt("TAKEN")));
  EXPECT_EQ(presetFileNames(tmp.dir).size(), 2);
}

TEST(PresetManagerTest, LegacyUuidFilesKeepTheirIdAndMigrateOnWrite) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);

  // A file from before readable names: <uuid>.t3kpreset, no "id" inside.
  const juce::String uuid = juce::Uuid().toString();
  writeRawPreset(tmp.dir.getChildFile(withExt(uuid)), "Vintage Crunch");

  // Listed under the id every existing project/order.json already holds.
  auto presets = mgr.list();
  ASSERT_EQ(presets.size(), 1u);
  EXPECT_EQ(presets[0].id, "user:" + uuid);
  EXPECT_EQ(presets[0].name, juce::String("Vintage Crunch"));
  EXPECT_TRUE(mgr.load("user:" + uuid).isValid());

  // Nothing moves until the preset is written: listing is read-only.
  EXPECT_EQ(presetFileNames(tmp.dir), files({uuid.toRawUTF8()}));

  // Saving over it (same name) is the lazy migration: same id, readable
  // file, id pinned inside, old file gone.
  const auto saved = mgr.save("Vintage Crunch", makePreset("v2"));
  EXPECT_EQ(saved.id, "user:" + uuid);
  EXPECT_EQ(presetFileNames(tmp.dir), files({"Vintage Crunch"}));
  juce::ValueTree loaded = mgr.load("user:" + uuid);
  ASSERT_TRUE(loaded.isValid());
  EXPECT_EQ(loaded.getProperty("id").toString(), uuid);
  EXPECT_EQ(loaded.getProperty("marker").toString(), juce::String("v2"));

  // Rename is the other migration path.
  const juce::String uuid2 = juce::Uuid().toString();
  writeRawPreset(tmp.dir.getChildFile(withExt(uuid2)), "Old Lead");
  ASSERT_TRUE(mgr.rename("user:" + uuid2, "New Lead"));
  EXPECT_EQ(presetFileNames(tmp.dir), files({"New Lead", "Vintage Crunch"}));
  EXPECT_EQ(mgr.load("user:" + uuid2).getProperty("id").toString(), uuid2);
  EXPECT_EQ(mgr.load("user:" + uuid2).getProperty("name").toString(), juce::String("New Lead"));

  // Remove still works through the migrated id.
  EXPECT_TRUE(mgr.remove("user:" + uuid2));
  EXPECT_FALSE(mgr.load("user:" + uuid2).isValid());
  EXPECT_FALSE(mgr.remove("user:does-not-exist"));
}

TEST(PresetManagerTest, OrderSurvivesRenameBecauseIdsDo) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);
  const auto a = mgr.save("Alpha", makePreset("a"));
  const auto b = mgr.save("Beta", makePreset("b"));
  const auto c = mgr.save("Gamma", makePreset("c"));
  ASSERT_TRUE(mgr.move(c.id, -2));  // Gamma first

  ASSERT_TRUE(mgr.rename(c.id, "Zulu"));  // would sort last by name
  const auto presets = mgr.list();
  ASSERT_EQ(presets.size(), 3u);
  EXPECT_EQ(presets[0].id, c.id);
  EXPECT_EQ(presets[0].name, juce::String("Zulu"));
  EXPECT_EQ(presets[1].id, a.id);
  EXPECT_EQ(presets[2].id, b.id);
}

TEST(PresetManagerTest, HandRenamedAndCopiedFilesStillWork) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);

  // A legacy file the user renamed in Finder: stem is the id, any text.
  writeRawPreset(tmp.dir.getChildFile(withExt("My Tone")), "My Tone");
  auto presets = mgr.list();
  ASSERT_EQ(presets.size(), 1u);
  EXPECT_EQ(presets[0].id, juce::String("user:My Tone"));
  EXPECT_TRUE(mgr.load("user:My Tone").isValid());

  // A file duplicated in Finder carries the same internal id: the copy
  // lists under its stem so both remain reachable, and writing the copy
  // gives it that id for good.
  const auto saved = mgr.save("Original", makePreset("o"));
  const juce::File original = tmp.dir.getChildFile(withExt("Original"));
  ASSERT_TRUE(original.copyFileTo(tmp.dir.getChildFile(withExt("Original copy"))));
  presets = mgr.list();
  ASSERT_EQ(presets.size(), 3u);
  std::set<juce::String> ids;
  for (const auto& p : presets)
    ids.insert(p.id);
  EXPECT_EQ(ids.size(), 3u);
  EXPECT_TRUE(ids.count(saved.id) == 1);
  EXPECT_TRUE(ids.count("user:Original copy") == 1);
  EXPECT_EQ(mgr.load(saved.id).getProperty("marker").toString(), juce::String("o"));
  EXPECT_EQ(mgr.load("user:Original copy").getProperty("marker").toString(), juce::String("o"));

  ASSERT_TRUE(mgr.rename("user:Original copy", "Second"));
  EXPECT_EQ(mgr.load("user:Original copy").getProperty("id").toString(),
            juce::String("Original copy"));
  EXPECT_EQ(mgr.load("user:Original copy").getProperty("name").toString(), juce::String("Second"));
  EXPECT_TRUE(tmp.dir.getChildFile(withExt("Second")).existsAsFile());
}

TEST(PresetManagerTest, FactoryOverrideMatchesByInternalId) {
  TempPresetDir tmp;
  TempPresetDir system;

  // Shipped and local files with different stems but the same internal id
  // collapse to one entry, the local one winning; legacy stem matching
  // still works alongside (see SystemFactoryPresetsListedAndLocalOverrideWins).
  writeRawPreset(system.dir.getChildFile(withExt("Shipped Clean")), "Shipped Clean", "clean-id");
  writeRawPreset(tmp.dir.getChildFile("Factory").getChildFile(withExt("My Clean")), "My Clean",
                 "clean-id");
  writeRawPreset(system.dir.getChildFile(withExt("Shipped Lead")), "Shipped Lead", "lead-id");

  PresetManager mgr(tmp.dir, system.dir);
  const auto presets = mgr.list();
  ASSERT_EQ(presets.size(), 2u);
  EXPECT_EQ(presets[0].id, juce::String("factory:clean-id"));
  EXPECT_EQ(presets[0].name, juce::String("My Clean"));
  EXPECT_EQ(presets[1].id, juce::String("factory:lead-id"));
  EXPECT_EQ(mgr.load("factory:clean-id").getProperty("name").toString(), juce::String("My Clean"));
  EXPECT_FALSE(mgr.rename("factory:clean-id", "Nope"));
  EXPECT_FALSE(mgr.remove("factory:clean-id"));
}

TEST(PresetManagerTest, ListCacheTracksExternalChanges) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);
  const auto info = mgr.save("Cached", makePreset("1"));
  ASSERT_EQ(mgr.list().size(), 1u);
  EXPECT_EQ(mgr.list()[0].name, juce::String("Cached"));

  // Another instance/process rewrites the file: the new name shows up. The
  // mtime is forced forward so a same-millisecond rewrite can't hide it.
  const juce::File file = tmp.dir.getChildFile(withExt("Cached"));
  const auto rawId = info.id.fromFirstOccurrenceOf("user:", false, false);
  writeRawPreset(file, "Renamed Elsewhere", rawId);
  file.setLastModificationTime(juce::Time::getCurrentTime() + juce::RelativeTime::seconds(5));
  auto presets = mgr.list();
  ASSERT_EQ(presets.size(), 1u);
  EXPECT_EQ(presets[0].id, info.id);
  EXPECT_EQ(presets[0].name, juce::String("Renamed Elsewhere"));

  // A file added behind our back appears; one deleted disappears.
  writeRawPreset(tmp.dir.getChildFile(withExt("Dropped In")), "Dropped In");
  EXPECT_EQ(mgr.list().size(), 2u);
  ASSERT_TRUE(file.deleteFile());
  presets = mgr.list();
  ASSERT_EQ(presets.size(), 1u);
  EXPECT_EQ(presets[0].name, juce::String("Dropped In"));
  EXPECT_FALSE(mgr.load(info.id).isValid());
}

// File framing (PresetFile.h)

TEST(PresetFileTest, WritesV2FramingAndReadsBothVersions) {
  TempPresetDir tmp;
  const juce::File v2 = tmp.dir.getChildFile(withExt("New"));
  juce::ValueTree preset = makePreset("body");
  preset.setProperty("id", "abc", nullptr);
  preset.setProperty("name", "New", nullptr);
  ASSERT_TRUE(t3k::presetfile::write(v2, preset));

  // On-disk: T3KH, header length, header, body. The header is small no
  // matter how big the body is; that's the whole point.
  juce::FileInputStream in(v2);
  char magic[4]{};
  ASSERT_EQ(in.read(magic, 4), 4);
  EXPECT_EQ(std::memcmp(magic, "T3KH", 4), 0);
  const int headerBytes = in.readInt();
  EXPECT_GT(headerBytes, 0);
  EXPECT_LT(headerBytes, 256);

  auto header = t3k::presetfile::readHeader(v2);
  EXPECT_TRUE(header.valid);
  EXPECT_FALSE(header.legacy);
  EXPECT_EQ(header.id, juce::String("abc"));
  EXPECT_EQ(header.name, juce::String("New"));
  EXPECT_EQ(t3k::presetfile::read(v2).getProperty("marker").toString(), juce::String("body"));

  // A v1 file still reads, and is flagged so callers know it cost a full parse.
  const juce::File v1 = tmp.dir.getChildFile(withExt("Old"));
  writeRawPreset(v1, "Old", "old-id");
  header = t3k::presetfile::readHeader(v1);
  EXPECT_TRUE(header.valid);
  EXPECT_TRUE(header.legacy);
  EXPECT_EQ(header.id, juce::String("old-id"));
  EXPECT_EQ(header.name, juce::String("Old"));
  EXPECT_TRUE(t3k::presetfile::read(v1).hasType(PresetManager::kPresetTag));

  // Garbage and a wrong tag are rejected by both readers.
  const juce::File junk = tmp.dir.getChildFile(withExt("Junk"));
  junk.replaceWithText("T3KHnot a preset at all");
  EXPECT_FALSE(t3k::presetfile::readHeader(junk).valid);
  EXPECT_FALSE(t3k::presetfile::read(junk).isValid());
  EXPECT_FALSE(t3k::presetfile::readHeader(tmp.dir.getChildFile(withExt("Missing"))).valid);
}

TEST(PresetFileTest, WriteReplacesRatherThanAppends) {
  // FileOutputStream appends by default; the writer must not, or a
  // re-saved preset would grow a second copy behind the first.
  TempPresetDir tmp;
  const juce::File file = tmp.dir.getChildFile(withExt("Twice"));
  ASSERT_TRUE(t3k::presetfile::write(file, makePreset("aaaa")));
  const auto sizeAfterFirst = file.getSize();
  ASSERT_TRUE(t3k::presetfile::write(file, makePreset("bbbb")));
  EXPECT_EQ(file.getSize(), sizeAfterFirst);
  EXPECT_EQ(t3k::presetfile::read(file).getProperty("marker").toString(), juce::String("bbbb"));
  // No scratch file left behind, and none that the preset scan could match.
  EXPECT_EQ(tmp.dir.getNumberOfChildFiles(juce::File::findFiles), 1);
}

TEST(PresetManagerTest, ListReadsOnlyTheHeaderOfV2Files) {
  // A v2 file whose header is fine but whose body is unreadable lists with
  // the header's id/name (proving list() never touched the body) and only
  // fails when actually loaded.
  TempPresetDir tmp;
  const juce::File file = tmp.dir.getChildFile(withExt("Headless"));
  {
    juce::ValueTree header(t3k::presetfile::kHeaderTag);
    header.setProperty("id", "hdr-id", nullptr);
    header.setProperty("name", "Header Only", nullptr);
    juce::MemoryOutputStream headerBytes;
    header.writeToStream(headerBytes);
    juce::FileOutputStream out(file);
    ASSERT_TRUE(out.openedOk());
    out.write("T3KH", 4);
    out.writeInt(static_cast<int>(headerBytes.getDataSize()));
    out.write(headerBytes.getData(), headerBytes.getDataSize());
    out.writeString("this is not a ValueTree");
  }
  PresetManager mgr(tmp.dir);
  const auto presets = mgr.list();
  ASSERT_EQ(presets.size(), 1u);
  EXPECT_EQ(presets[0].id, juce::String("user:hdr-id"));
  EXPECT_EQ(presets[0].name, juce::String("Header Only"));
  EXPECT_FALSE(mgr.load("user:hdr-id").isValid());
}

TEST(PresetManagerTest, SaveWritesV2AndLegacyFilesUpgradeOnWrite) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);
  const auto saved = mgr.save("Fresh", makePreset("1"));
  auto header = t3k::presetfile::readHeader(tmp.dir.getChildFile(withExt("Fresh")));
  ASSERT_TRUE(header.valid);
  EXPECT_FALSE(header.legacy);
  EXPECT_EQ("user:" + header.id, saved.id);

  // A legacy uuid file: listed via a full parse the first time, rewritten
  // as v2 under its readable name once renamed, id preserved.
  const juce::String uuid = juce::Uuid().toString();
  writeRawPreset(tmp.dir.getChildFile(withExt(uuid)), "Old Timer");
  ASSERT_EQ(mgr.list().size(), 2u);
  ASSERT_TRUE(mgr.rename("user:" + uuid, "Old Timer"));
  const juce::File migrated = tmp.dir.getChildFile(withExt("Old Timer"));
  header = t3k::presetfile::readHeader(migrated);
  ASSERT_TRUE(header.valid);
  EXPECT_FALSE(header.legacy);
  EXPECT_EQ(header.id, uuid);
  EXPECT_FALSE(tmp.dir.getChildFile(withExt(uuid)).exists());
  EXPECT_TRUE(mgr.load("user:" + uuid).isValid());
}

// Categories, favourites and bulk actions. State lives in presets-meta.json
// beside the preset files, keyed by preset id.

TEST(PresetManagerTest, CategoryLifecycleValidationAndSorting) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);

  EXPECT_TRUE(mgr.addCategory("Rock"));
  EXPECT_TRUE(mgr.addCategory("ambient"));
  EXPECT_TRUE(mgr.addCategory("Blues"));
  EXPECT_FALSE(mgr.addCategory("ROCK"));  // case-insensitive duplicate
  EXPECT_FALSE(mgr.addCategory("   "));   // blank
  EXPECT_FALSE(mgr.addCategory(juce::String(std::string(PresetManager::kMaxCategoryNameLength + 1, 'a'))));
  const juce::String longest(std::string(PresetManager::kMaxCategoryNameLength, 'b'));
  EXPECT_TRUE(mgr.addCategory(longest));

  const auto categories = mgr.listCategories();
  ASSERT_EQ(categories.size(), 4);
  EXPECT_EQ(categories[0], juce::String("ambient"));
  EXPECT_EQ(categories[1], longest);
  EXPECT_EQ(categories[2], juce::String("Blues"));
  EXPECT_EQ(categories[3], juce::String("Rock"));

  PresetManager fresh(tmp.dir);  // survives a new instance
  EXPECT_EQ(fresh.listCategories(), categories);
}

TEST(PresetManagerTest, CategoryNamesKeepUnicode) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);
  const juce::String jp = juce::CharPointer_UTF8("リード・ギター");
  const juce::String emoji = juce::CharPointer_UTF8("Café Rock 🎸");
  EXPECT_TRUE(mgr.addCategory(jp));
  EXPECT_TRUE(mgr.addCategory(emoji));

  PresetManager fresh(tmp.dir);
  EXPECT_TRUE(fresh.listCategories().contains(jp));
  EXPECT_TRUE(fresh.listCategories().contains(emoji));
}

TEST(PresetManagerTest, FilingRequiresAnExistingCategoryAndUserPreset) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);
  mgr.addCategory("Rock");
  const auto p = mgr.save("Lead", makePreset("a"));

  EXPECT_FALSE(mgr.setPresetCategory(p.id, "Nope"));  // never invents a category
  EXPECT_EQ(mgr.list()[0].category, juce::String());
  EXPECT_FALSE(mgr.setPresetCategory("user:missing", "Rock"));

  EXPECT_TRUE(mgr.setPresetCategory(p.id, "rock"));  // resolves to the stored spelling
  EXPECT_EQ(mgr.list()[0].category, juce::String("Rock"));
  EXPECT_TRUE(mgr.setPresetCategory(p.id, ""));
  EXPECT_EQ(mgr.list()[0].category, juce::String());
}

TEST(PresetManagerTest, BulkMoveIsAllOrNothing) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);
  mgr.addCategory("Clean");
  const auto a = mgr.save("A", makePreset("a"));
  const auto b = mgr.save("B", makePreset("b"));

  EXPECT_FALSE(mgr.movePresetsToCategory(juce::StringArray{a.id, "user:missing"}, "Clean"));
  for (const auto& info : mgr.list())
    EXPECT_EQ(info.category, juce::String());

  EXPECT_TRUE(mgr.movePresetsToCategory(juce::StringArray{a.id, b.id}, "Clean"));
  for (const auto& info : mgr.list())
    EXPECT_EQ(info.category, juce::String("Clean"));
}

TEST(PresetManagerTest, DeleteCategoryMovesPresetsToRootAndKeepsStars) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);
  mgr.addCategory("Clean");
  const auto a = mgr.save("Warm", makePreset("a"));
  const auto b = mgr.save("Bright", makePreset("b"));
  mgr.movePresetsToCategory(juce::StringArray{a.id, b.id}, "Clean");
  mgr.setPresetFavorite(a.id, true);

  EXPECT_TRUE(mgr.deleteCategory("clean"));
  EXPECT_EQ(mgr.listCategories().size(), 0);
  EXPECT_FALSE(mgr.deleteCategory("Clean"));  // already gone

  const auto list = mgr.list();
  ASSERT_EQ(list.size(), 2u);
  for (const auto& info : list) {
    EXPECT_EQ(info.category, juce::String());
    EXPECT_TRUE(mgr.load(info.id).isValid());
  }
  EXPECT_TRUE(mgr.list()[1].favorite || mgr.list()[0].favorite);
}

TEST(PresetManagerTest, CategoryAndStarFollowARename) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);
  mgr.addCategory("Rock");
  const auto p = mgr.save("Old", makePreset("a"));
  mgr.setPresetCategory(p.id, "Rock");
  mgr.setPresetFavorite(p.id, true);

  ASSERT_TRUE(mgr.rename(p.id, "New"));
  const auto list = mgr.list();
  ASSERT_EQ(list.size(), 1u);
  EXPECT_EQ(list[0].name, juce::String("New"));
  EXPECT_EQ(list[0].category, juce::String("Rock"));
  EXPECT_TRUE(list[0].favorite);
}

TEST(PresetManagerTest, FavoritesPersistBulkAndRejectUnknownIds) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);
  const auto a = mgr.save("A", makePreset("a"));
  const auto b = mgr.save("B", makePreset("b"));

  EXPECT_TRUE(mgr.setPresetFavorite(a.id, true));
  EXPECT_FALSE(mgr.setPresetFavorite("user:missing", true));
  EXPECT_FALSE(mgr.setPresetsFavorite(juce::StringArray{b.id, "user:missing"}, true));

  PresetManager fresh(tmp.dir);
  auto list = fresh.list();
  EXPECT_TRUE(list[0].favorite);
  EXPECT_FALSE(list[1].favorite);  // the refused bulk call changed nothing

  EXPECT_TRUE(fresh.setPresetsFavorite(juce::StringArray{a.id, b.id}, true));
  EXPECT_TRUE(fresh.setPresetsFavorite(juce::StringArray{a.id, b.id}, false));
  for (const auto& info : fresh.list())
    EXPECT_FALSE(info.favorite);
}

TEST(PresetManagerTest, FactoryPresetsCanBeStarredButNotFiled) {
  TempPresetDir tmp;
  TempPresetDir factory;
  writeRawPreset(factory.dir.getChildFile(withExt("Shipped")), "Shipped");
  PresetManager mgr(tmp.dir, factory.dir);
  mgr.addCategory("Rock");

  PresetManager::Info shipped;
  for (const auto& info : mgr.list())
    if (info.factory)
      shipped = info;
  ASSERT_TRUE(shipped.id.isNotEmpty());

  EXPECT_TRUE(mgr.setPresetFavorite(shipped.id, true));
  EXPECT_FALSE(mgr.setPresetCategory(shipped.id, "Rock"));
  for (const auto& info : mgr.list())
    if (info.factory) {
      EXPECT_TRUE(info.favorite);
      EXPECT_EQ(info.category, juce::String());
    }
}

TEST(PresetManagerTest, DuplicateKeepsCategoryDropsStarAndNeverOverwritesACopy) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);
  mgr.addCategory("Metal");
  const auto orig = mgr.save("Chug", makePreset("metal"));
  mgr.setPresetCategory(orig.id, "Metal");
  mgr.setPresetFavorite(orig.id, true);

  const auto copy1 = mgr.duplicatePreset(orig.id);
  ASSERT_TRUE(copy1.id.isNotEmpty());
  EXPECT_NE(copy1.id, orig.id);
  EXPECT_EQ(copy1.name, juce::String("Copy-Chug"));
  EXPECT_EQ(copy1.category, juce::String("Metal"));

  const auto copy2 = mgr.duplicatePreset(orig.id);  // same source again
  EXPECT_EQ(copy2.name, juce::String("Copy-Chug 2"));
  EXPECT_NE(copy2.id, copy1.id);

  int stars = 0;
  for (const auto& info : mgr.list())
    stars += info.favorite ? 1 : 0;
  EXPECT_EQ(stars, 1);
  EXPECT_EQ(mgr.list().size(), 3u);
  EXPECT_EQ(mgr.load(copy1.id).getProperty("marker").toString(), juce::String("metal"));
  EXPECT_TRUE(mgr.duplicatePreset("user:missing").id.isEmpty());
}

TEST(PresetManagerTest, RemovePresetsReportsWhatWentAndClearsItsState) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);
  mgr.addCategory("Rock");
  const auto a = mgr.save("A", makePreset("a"));
  const auto b = mgr.save("B", makePreset("b"));
  mgr.setPresetCategory(a.id, "Rock");
  mgr.setPresetFavorite(a.id, true);

  const auto removed = mgr.removePresets(juce::StringArray{a.id, "user:missing"});
  EXPECT_EQ(removed, juce::StringArray{a.id});
  ASSERT_EQ(mgr.list().size(), 1u);
  EXPECT_EQ(mgr.list()[0].id, b.id);

  // A new preset can't inherit the deleted one's star/category.
  const auto again = mgr.save("A", makePreset("a2"));
  for (const auto& info : mgr.list())
    if (info.id == again.id) {
      EXPECT_FALSE(info.favorite);
      EXPECT_EQ(info.category, juce::String());
    }
}

TEST(PresetManagerTest, AnUnreadableMetaFileIsNeverOverwrittenByAWriter) {
  TempPresetDir tmp;
  PresetManager mgr(tmp.dir);
  ASSERT_TRUE(mgr.addCategory("Rock"));
  const auto p = mgr.save("A", makePreset("a"));
  ASSERT_TRUE(mgr.setPresetFavorite(p.id, true));

  const juce::File meta = tmp.dir.getChildFile("presets-meta.json");
  ASSERT_TRUE(meta.existsAsFile());
  const juce::String garbage = "{ not json";
  ASSERT_TRUE(meta.replaceWithText(garbage));

  EXPECT_FALSE(mgr.addCategory("Blues"));
  EXPECT_FALSE(mgr.setPresetFavorite(p.id, false));
  EXPECT_EQ(meta.loadFileAsString(), garbage);  // left for the user to repair, not wiped
}

}  // namespace
