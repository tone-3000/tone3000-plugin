#include "PresetManager.h"

#include <gtest/gtest.h>

namespace {
struct TempDir {
  juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("t3k-backup-test-" + juce::Uuid().toString());
  TempDir() { dir.createDirectory(); }
  ~TempDir() { dir.deleteRecursively(); }
};

juce::ValueTree preset(int marker) {
  juce::ValueTree tree("T3KPreset");
  tree.setProperty("schemaVersion", 1, nullptr);
  juce::ValueTree chain("ChainSnapshot");
  juce::ValueTree block("Block");
  const char data[] = {0, 1, 2, 3, 127, -1};
  block.setProperty("modelData", juce::MemoryBlock(data, sizeof(data)), nullptr);
  block.setProperty("irData", juce::MemoryBlock(data, sizeof(data)), nullptr);
  block.setProperty("marker", marker, nullptr);
  chain.appendChild(block, nullptr);
  tree.appendChild(chain, nullptr);
  tree.appendChild(juce::ValueTree("Params"), nullptr);
  return tree;
}

void writeZip(const juce::File& target, const juce::String& manifest,
              const std::vector<std::pair<juce::String, juce::File>>& files) {
  juce::ZipFile::Builder zip;
  auto json = std::make_unique<juce::MemoryInputStream>(manifest.toRawUTF8(), manifest.getNumBytesAsUTF8(), true);
  zip.addEntry(std::move(json), 6, "backup.json", juce::Time::getCurrentTime());
  for (const auto& item : files) zip.addFile(item.second, 6, item.first);
  juce::FileOutputStream out(target);
  ASSERT_TRUE(out.openedOk());
  ASSERT_TRUE(zip.writeToStream(out, nullptr));
}

TEST(PresetBackupTest, RoundTripPreservesDataAndOrderExcludesFactory) {
  TempDir tmp;
  PresetManager source(tmp.dir.getChildFile("source"));
  const auto a = source.save("Amp", preset(1));
  const auto b = source.save("Boost", preset(2));
  ASSERT_TRUE(source.move(b.id, -1));
  auto factory = preset(3);
  factory.setProperty("id", "factory-test", nullptr);
  const auto factoryDir = source.userPresetsDir().getChildFile("Factory");
  factoryDir.createDirectory();
  ASSERT_TRUE(t3k::presetfile::write(factoryDir.getChildFile("Factory.t3kpreset"), factory));
  const auto archive = tmp.dir.getChildFile("backup.zip");
  ASSERT_TRUE(source.exportBackup(archive).wasOk());
  PresetManager dest(tmp.dir.getChildFile("dest"));
  ASSERT_TRUE(dest.importBackup(archive).wasOk());
  const auto list = dest.list();
  ASSERT_EQ(list.size(), 2u);
  EXPECT_EQ(list[0].name, "Boost");
  EXPECT_EQ(list[1].name, "Amp");
  EXPECT_NE(list[0].id, b.id);
  EXPECT_NE(list[1].id, a.id);
  for (size_t i = 0; i < list.size(); ++i) {
    const auto old = source.load(i == 0 ? b.id : a.id).getChildWithName("ChainSnapshot");
    const auto restored = dest.load(list[i].id).getChildWithName("ChainSnapshot");
    EXPECT_TRUE(old.isEquivalentTo(restored));
  }
}

TEST(PresetBackupTest, ImportKeepsExistingAndRepeatedImportsAreCopies) {
  TempDir tmp;
  PresetManager source(tmp.dir.getChildFile("source"));
  source.save("Lead", preset(1));
  const auto archive = tmp.dir.getChildFile("backup.zip");
  ASSERT_TRUE(source.exportBackup(archive).wasOk());
  PresetManager dest(tmp.dir.getChildFile("dest"));
  const auto original = dest.save("Lead", preset(99));
  ASSERT_TRUE(dest.importBackup(archive).wasOk());
  ASSERT_TRUE(dest.importBackup(archive).wasOk());
  const auto list = dest.list();
  ASSERT_EQ(list.size(), 3u);
  EXPECT_EQ(list[0].id, original.id);
  EXPECT_EQ(list[1].name, "Lead (2)");
  EXPECT_EQ(list[2].name, "Lead (3)");
  EXPECT_EQ(static_cast<int>(dest.load(original.id).getChildWithName("ChainSnapshot").getChild(0).getProperty("marker")), 99);
}

TEST(PresetBackupTest, InvalidLaterPresetDoesNotImportEarlierOne) {
  TempDir tmp;
  const auto valid = tmp.dir.getChildFile("valid.t3kpreset");
  ASSERT_TRUE(t3k::presetfile::write(valid, preset(1)));
  const auto bad = tmp.dir.getChildFile("bad.t3kpreset");
  bad.replaceWithText("not a preset");
  const auto archive = tmp.dir.getChildFile("bad.zip");
  writeZip(archive, R"({"format":"TONE3000 user preset backup","version":1,"presets":["a.t3kpreset","b.t3kpreset"]})",
           {{"a.t3kpreset", valid}, {"b.t3kpreset", bad}});
  PresetManager dest(tmp.dir.getChildFile("dest"));
  const auto original = dest.save("Existing", preset(9));
  const auto order = dest.userPresetsDir().getChildFile("order.json");
  order.replaceWithText("[\"" + original.id + "\"]");
  const auto before = order.loadFileAsString();
  EXPECT_TRUE(dest.importBackup(archive).failed());
  ASSERT_EQ(dest.list().size(), 1u);
  EXPECT_EQ(dest.list()[0].id, original.id);
  EXPECT_EQ(order.loadFileAsString(), before);
}

TEST(PresetBackupTest, RejectsTraversalDuplicateEntriesAndUnsupportedVersion) {
  TempDir tmp;
  const auto file = tmp.dir.getChildFile("valid.t3kpreset");
  ASSERT_TRUE(t3k::presetfile::write(file, preset(1)));
  const auto manifest = R"({"format":"TONE3000 user preset backup","version":1,"presets":["a.t3kpreset"]})";
  PresetManager dest(tmp.dir.getChildFile("dest"));
  const auto traversal = tmp.dir.getChildFile("traversal.zip");
  writeZip(traversal, manifest, {{"../a.t3kpreset", file}});
  EXPECT_TRUE(dest.importBackup(traversal).failed());
  EXPECT_FALSE(tmp.dir.getChildFile("a.t3kpreset").exists());
  const auto duplicate = tmp.dir.getChildFile("duplicate.zip");
  writeZip(duplicate, manifest, {{"a.t3kpreset", file}, {"a.t3kpreset", file}});
  EXPECT_TRUE(dest.importBackup(duplicate).failed());
  const auto version = tmp.dir.getChildFile("version.zip");
  writeZip(version, R"({"format":"TONE3000 user preset backup","version":2,"presets":["a.t3kpreset"]})", {{"a.t3kpreset", file}});
  EXPECT_TRUE(dest.importBackup(version).failed());
  EXPECT_TRUE(dest.list().empty());
}

TEST(PresetBackupTest, EmptyStoreAndCorruptUserFileDoNotProduceBackup) {
  TempDir tmp;
  PresetManager store(tmp.dir.getChildFile("store"));
  const auto archive = tmp.dir.getChildFile("backup.zip");
  EXPECT_TRUE(store.exportBackup(archive).failed());
  store.save("Good", preset(1));
  store.userPresetsDir().getChildFile("Bad.t3kpreset").replaceWithText("bad");
  EXPECT_TRUE(store.exportBackup(archive).failed());
  EXPECT_FALSE(archive.exists());
}

TEST(PresetBackupTest, LegacyFileKeepsNameAndDataInBackup) {
  TempDir tmp;
  PresetManager store(tmp.dir.getChildFile("store"));
  store.userPresetsDir().createDirectory();
  const auto file = store.userPresetsDir().getChildFile("legacy-id.t3kpreset");
  {
    juce::FileOutputStream out(file);
    out.write("T3KB", 4);
    auto tree = preset(7);
    tree.setProperty("name", "Legacy", nullptr);
    tree.writeToStream(out);
  }
  const auto archive = tmp.dir.getChildFile("backup.zip");
  ASSERT_TRUE(store.exportBackup(archive).wasOk());
  PresetManager dest(tmp.dir.getChildFile("dest"));
  ASSERT_TRUE(dest.importBackup(archive).wasOk());
  ASSERT_EQ(dest.list().size(), 1u);
  EXPECT_EQ(dest.list()[0].name, "Legacy");
  EXPECT_TRUE(dest.load(dest.list()[0].id).getChildWithName("ChainSnapshot").isEquivalentTo(preset(7).getChildWithName("ChainSnapshot")));
}
TEST(PresetBackupTest, ReplaceRestoresUserOrderKeepsFactoryOrderAndCreatesRecoveryZip) {
  TempDir tmp;
  PresetManager source(tmp.dir.getChildFile("source"));
  const auto amp = source.save("Amp", preset(1));
  const auto boost = source.save("Boost", preset(2));
  ASSERT_TRUE(source.move(boost.id, -1));
  const auto zip = tmp.dir.getChildFile("backup.zip");
  ASSERT_TRUE(source.exportBackup(zip).wasOk());

  PresetManager dest(tmp.dir.getChildFile("destination"));
  const auto extra = dest.save("Extra", preset(99));
  const auto factoryDir = dest.userPresetsDir().getChildFile("Factory");
  ASSERT_TRUE(factoryDir.createDirectory().wasOk());
  for (const auto& name : {"A", "B"}) {
    auto tree = preset(50);
    tree.setProperty("id", name, nullptr);
    tree.setProperty("name", name, nullptr);
    ASSERT_TRUE(t3k::presetfile::write(factoryDir.getChildFile(juce::String(name) + ".t3kpreset"), tree));
  }
  ASSERT_TRUE(dest.move("factory:B", -1));
  ASSERT_TRUE(dest.importBackup(zip, t3k::PresetImportMode::replaceAll).wasOk());
  const auto list = dest.list();
  ASSERT_EQ(list.size(), 4u);
  EXPECT_EQ(list[0].name, "Boost");
  EXPECT_EQ(list[1].name, "Amp");
  EXPECT_EQ(list[2].id, "factory:B");
  EXPECT_EQ(list[3].id, "factory:A");
  EXPECT_TRUE(source.load(amp.id).getChildWithName("ChainSnapshot").isEquivalentTo(
      dest.load(list[1].id).getChildWithName("ChainSnapshot")));

  const auto recoveryFiles = tmp.dir.getChildFile("PresetBackups")
      .findChildFiles(juce::File::findFiles, false, "*.zip");
  ASSERT_EQ(recoveryFiles.size(), 1);
  PresetManager recovered(tmp.dir.getChildFile("recovered"));
  ASSERT_TRUE(recovered.importBackup(recoveryFiles[0]).wasOk());
  ASSERT_EQ(recovered.list().size(), 1u);
  EXPECT_EQ(recovered.list()[0].name, "Extra");
  EXPECT_TRUE(recovered.load(recovered.list()[0].id).getChildWithName("ChainSnapshot").isEquivalentTo(
      preset(99).getChildWithName("ChainSnapshot")));
  EXPECT_FALSE(dest.load(extra.id).isValid());
}

TEST(PresetBackupTest, ReplacingTwiceDoesNotAddCopiesOrNameSuffixes) {
  TempDir tmp;
  PresetManager source(tmp.dir.getChildFile("source"));
  source.save("Lead", preset(1));
  const auto zip = tmp.dir.getChildFile("backup.zip");
  ASSERT_TRUE(source.exportBackup(zip).wasOk());
  PresetManager dest(tmp.dir.getChildFile("destination"));
  dest.save("Lead", preset(99));
  for (int i = 0; i < 2; ++i) {
    ASSERT_TRUE(dest.importBackup(zip, t3k::PresetImportMode::replaceAll).wasOk());
    ASSERT_EQ(dest.list().size(), 1u);
    EXPECT_EQ(dest.list()[0].name, "Lead");
    EXPECT_TRUE(dest.load(dest.list()[0].id).getChildWithName("ChainSnapshot").isEquivalentTo(
        preset(1).getChildWithName("ChainSnapshot")));
  }
  EXPECT_EQ(tmp.dir.getChildFile("PresetBackups").findChildFiles(
      juce::File::findFiles, false, "*.zip").size(), 2);
}

TEST(PresetBackupTest, InvalidReplacementDoesNotChangeExistingFilesOrOrder) {
  TempDir tmp;
  const auto valid = tmp.dir.getChildFile("valid.t3kpreset");
  ASSERT_TRUE(t3k::presetfile::write(valid, preset(1)));
  const auto invalid = tmp.dir.getChildFile("invalid.t3kpreset");
  ASSERT_TRUE(invalid.replaceWithText("invalid"));
  const auto zip = tmp.dir.getChildFile("invalid.zip");
  writeZip(zip, R"({"format":"TONE3000 user preset backup","version":1,"presets":["a.t3kpreset","b.t3kpreset"]})",
      {{"a.t3kpreset", valid}, {"b.t3kpreset", invalid}});
  PresetManager dest(tmp.dir.getChildFile("destination"));
  const auto original = dest.save("Existing", preset(99));
  const auto order = dest.userPresetsDir().getChildFile("order.json");
  ASSERT_TRUE(order.replaceWithText("[\"" + original.id + "\"]"));
  const auto before = order.loadFileAsString();
  EXPECT_TRUE(dest.importBackup(zip, t3k::PresetImportMode::replaceAll).failed());
  ASSERT_EQ(dest.list().size(), 1u);
  EXPECT_EQ(dest.list()[0].id, original.id);
  EXPECT_EQ(order.loadFileAsString(), before);
  EXPECT_FALSE(tmp.dir.getChildFile("PresetBackups").exists());
}

TEST(PresetBackupTest, ReplacementStopsWhenExistingPresetsCannotBeBackedUp) {
  TempDir tmp;
  PresetManager source(tmp.dir.getChildFile("source"));
  source.save("New", preset(1));
  const auto zip = tmp.dir.getChildFile("backup.zip");
  ASSERT_TRUE(source.exportBackup(zip).wasOk());
  PresetManager dest(tmp.dir.getChildFile("destination"));
  const auto original = dest.save("Existing", preset(99));
  const auto corrupt = dest.userPresetsDir().getChildFile("Corrupt.t3kpreset");
  ASSERT_TRUE(corrupt.replaceWithText("keep this file"));
  EXPECT_TRUE(dest.importBackup(zip, t3k::PresetImportMode::replaceAll).failed());
  EXPECT_EQ(corrupt.loadFileAsString(), "keep this file");
  ASSERT_EQ(dest.list().size(), 1u);
  EXPECT_EQ(dest.list()[0].id, original.id);
}

TEST(PresetBackupTest, ReplacementRejectsAnUnwritableOrderPathWithoutChangingPresets) {
  TempDir tmp;
  PresetManager source(tmp.dir.getChildFile("source"));
  source.save("New", preset(1));
  const auto zip = tmp.dir.getChildFile("backup.zip");
  ASSERT_TRUE(source.exportBackup(zip).wasOk());
  PresetManager dest(tmp.dir.getChildFile("destination"));
  const auto original = dest.save("Existing", preset(99));
  const auto order = dest.userPresetsDir().getChildFile("order.json");
  order.deleteFile();
  ASSERT_TRUE(order.createDirectory().wasOk());
  EXPECT_TRUE(dest.importBackup(zip, t3k::PresetImportMode::replaceAll).failed());
  EXPECT_TRUE(order.isDirectory());
  ASSERT_EQ(dest.list().size(), 1u);
  EXPECT_EQ(dest.list()[0].id, original.id);
}

} // namespace
