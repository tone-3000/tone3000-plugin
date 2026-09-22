// The tone library: the user's own folder tree of local tones under
// <app data>/TONE3000/Library, its path safety (every path crosses the
// webview bridge as a root-relative string), and the two directions that
// matter — a library entry loading as a normal local block, and a live
// block filing itself back into the library with its model bytes.
//
// Every test points the processor at its own temp root
// (setLibraryRoot), so nothing here touches the real library. The load
// paths still stash into the real app-data LocalModels folder, exactly as
// local_load_tests.cpp does.

#include "chain_test_helpers.h"

#include <gtest/gtest.h>

namespace {

// A processor with a throwaway library root, torn down with the fixture.
class LibraryTest : public ::testing::Test {
protected:
  void SetUp() override {
    root = juce::File::getSpecialLocation(juce::File::tempDirectory)
               .getChildFile("t3k-library-tests")
               .getChildFile(juce::Uuid().toString());
    root.createDirectory();
    proc.setLibraryRoot(root);
  }

  void TearDown() override { root.deleteRecursively(); }

  /** Copy a test asset into the library tree under `relativePath`. */
  juce::File place(const juce::String& relativePath, const char* asset) {
    const juce::File target = root.getChildFile(relativePath);
    target.getParentDirectory().createDirectory();
    EXPECT_TRUE(testFile(asset).copyFileTo(target));
    return target;
  }

  ChainTestProcessor proc;
  juce::File root;
};

juce::String base64Of(const juce::File& file) {
  juce::MemoryBlock bytes;
  EXPECT_TRUE(file.loadFileAsData(bytes));
  return juce::Base64::toBase64(bytes.getData(), bytes.getSize());
}

juce::var fileEntry(const juce::String& name, const juce::String& base64) {
  juce::DynamicObject::Ptr entry = new juce::DynamicObject();
  entry->setProperty("name", name);
  entry->setProperty("data", base64);
  return juce::var(entry.get());
}

// First tone block of the (mono) chain, or void when none.
juce::var firstToneBlock(TONE3000Processor& proc) {
  const juce::var state = proc.getChainState(-1);
  if (const auto* lane = state["chain"].getArray())
    for (const auto& item : *lane)
      if (item["kind"].toString() == "tone")
        return item;
  return {};
}

// Names of a listing's folders / models, in listed order.
juce::StringArray namesOf(const juce::var& listing, const char* key) {
  juce::StringArray names;
  if (const auto* entries = listing[juce::Identifier(key)].getArray())
    for (const auto& entry : *entries)
      names.add(entry["name"].toString());
  return names;
}

}  // namespace

TEST_F(LibraryTest, ListsFoldersAndModelsIgnoringEverythingElse) {
  place("Amps/Twin.nam", "a2-amp-test.nam");
  place("Cabs/1x12.wav", "cab-ir-test.wav");
  root.getChildFile("notes.txt").replaceWithText("not a model");

  const juce::var listing = proc.listLibrary("");
  EXPECT_TRUE(listing["error"].isVoid());
  EXPECT_EQ(listing["path"].toString(), juce::String(""));
  // The root has no parent; a subfolder's is a real (possibly empty) path.
  EXPECT_TRUE(listing["parent"].isVoid());
  EXPECT_EQ(namesOf(listing, "folders"), juce::StringArray({"Amps", "Cabs"}));
  EXPECT_TRUE(namesOf(listing, "models").isEmpty()) << "notes.txt must not be listed";
  EXPECT_EQ(static_cast<int>(listing["folders"][0]["models"]), 1);

  const juce::var amps = proc.listLibrary("Amps");
  EXPECT_EQ(amps["parent"].toString(), juce::String(""));
  EXPECT_EQ(namesOf(amps, "models"), juce::StringArray({"Twin"}));
  // Names drop the extension; the kind is what the plugin branches on.
  EXPECT_EQ(amps["models"][0]["kind"].toString(), juce::String("nam"));
  EXPECT_EQ(amps["models"][0]["path"].toString(), juce::String("Amps/Twin.nam"));
  EXPECT_EQ(proc.listLibrary("Cabs")["models"][0]["kind"].toString(), juce::String("ir"));
}

TEST_F(LibraryTest, ModelsListInNaturalNameOrder) {
  place("Pack/amp 10.nam", "a2-amp-test.nam");
  place("Pack/amp 2.nam", "a2-amp-cab-test.nam");

  EXPECT_EQ(namesOf(proc.listLibrary("Pack"), "models"),
            juce::StringArray({"amp 2", "amp 10"}));
}

TEST_F(LibraryTest, PathsThatEscapeTheLibraryAreRefused) {
  place("Amps/Twin.nam", "a2-amp-test.nam");
  const juce::File outside = root.getParentDirectory().getChildFile("outside.nam");
  ASSERT_TRUE(testFile("a2-amp-test.nam").copyFileTo(outside));

  for (const char* path : {"..", "../outside.nam", "Amps/../../outside.nam"}) {
    EXPECT_FALSE(proc.listLibrary(path)["error"].isVoid()) << path;
    EXPECT_FALSE(proc.loadLibraryTone(path)["error"].isVoid()) << path;
    EXPECT_FALSE(proc.removeLibraryItem(path)) << path;
  }
  EXPECT_FALSE(proc.listLibrary(outside.getFullPathName())["error"].isVoid());
  EXPECT_TRUE(outside.existsAsFile()) << "nothing outside the library may be touched";
  outside.deleteFile();
}

TEST_F(LibraryTest, SanitizeKeepsALongNameUsableOnWindows) {
  // Trailing dots are stripped because Windows silently drops them, which
  // would leave the file on disk under a different name than the one handed
  // back to the UI. Truncating a long name must not put one back.
  const juce::String longName = juce::String::repeatedString("a", 119) + "." +
                                juce::String::repeatedString("b", 40);
  const juce::String clean = ToneLibrary::sanitizeName(longName);

  EXPECT_LE(clean.length(), 120);
  EXPECT_FALSE(clean.endsWithChar('.')) << clean.toStdString();
}

TEST_F(LibraryTest, WritingPastTheUniqueNameCapNeverOverwrites) {
  // uniqueChild gives up after a bounded number of " (n)" attempts. Whatever
  // it does then, it must not hand back a path that already holds one of the
  // user's captures — filing a tone may fail, but it may never destroy one.
  proc.createLibraryFolder("", "Full");
  const juce::File folder = root.getChildFile("Full");
  for (int n = 1; n < 1000; ++n) {
    const juce::String suffix = n == 1 ? juce::String() : " (" + juce::String(n) + ")";
    folder.getChildFile("Twin" + suffix + ".nam").replaceWithText("original " + juce::String(n));
  }
  const juce::File victim = folder.getChildFile("Twin (999).nam");
  ASSERT_TRUE(victim.existsAsFile());

  const juce::var result =
      proc.importFilesToLibrary("Full", juce::var(juce::Array<juce::var>{fileEntry(
                                            "Twin.nam", base64Of(testFile("a2-amp-test.nam")))}));

  EXPECT_EQ(victim.loadFileAsString(), juce::String("original 999"))
      << "an existing capture was overwritten";
  EXPECT_FALSE(result["error"].isVoid()) << "no free name left, so the write must report failure";
}

TEST_F(LibraryTest, CreateRenameAndRemove) {
  const juce::var created = proc.createLibraryFolder("", "Fender");
  ASSERT_TRUE(created["error"].isVoid()) << created["error"].toString().toStdString();
  EXPECT_EQ(created["path"].toString(), juce::String("Fender"));
  EXPECT_TRUE(root.getChildFile("Fender").isDirectory());
  // Names are sanitized on the way in, so a path separator can't escape.
  EXPECT_EQ(proc.createLibraryFolder("", "Marshall/JCM")["path"].toString(),
            juce::String("MarshallJCM"));
  EXPECT_FALSE(proc.createLibraryFolder("", "Fender")["error"].isVoid());

  place("Fender/Twin.nam", "a2-amp-test.nam");
  const juce::var renamed = proc.renameLibraryItem("Fender/Twin.nam", "Twin Reverb");
  ASSERT_TRUE(renamed["error"].isVoid()) << renamed["error"].toString().toStdString();
  // A file keeps its extension: it decides NAM vs IR downstream.
  EXPECT_EQ(renamed["path"].toString(), juce::String("Fender/Twin Reverb.nam"));
  EXPECT_TRUE(root.getChildFile("Fender/Twin Reverb.nam").existsAsFile());

  EXPECT_TRUE(proc.removeLibraryItem("Fender"));
  EXPECT_FALSE(root.getChildFile("Fender").exists());
  EXPECT_FALSE(proc.removeLibraryItem("Fender"));
}

TEST_F(LibraryTest, MovingAFileIntoAFolderRelocatesIt) {
  place("Twin.nam", "a2-amp-test.nam");
  ASSERT_TRUE(proc.createLibraryFolder("", "Amps")["error"].isVoid());

  const juce::var moved = proc.moveLibraryItem("Twin.nam", "Amps");
  ASSERT_TRUE(moved["error"].isVoid()) << moved["error"].toString().toStdString();
  EXPECT_EQ(moved["path"].toString(), juce::String("Amps/Twin.nam"));
  EXPECT_FALSE(root.getChildFile("Twin.nam").exists()) << "a move must not leave a copy behind";
  EXPECT_TRUE(
      root.getChildFile("Amps/Twin.nam").hasIdenticalContentTo(testFile("a2-amp-test.nam")));
}

TEST_F(LibraryTest, MovingAFolderCarriesItsContents) {
  place("Pack/a.nam", "a2-amp-test.nam");
  place("Pack/deep/b.wav", "cab-ir-test.wav");
  ASSERT_TRUE(proc.createLibraryFolder("", "Live")["error"].isVoid());

  const juce::var moved = proc.moveLibraryItem("Pack", "Live");
  ASSERT_TRUE(moved["error"].isVoid()) << moved["error"].toString().toStdString();
  EXPECT_EQ(moved["path"].toString(), juce::String("Live/Pack"));
  EXPECT_FALSE(root.getChildFile("Pack").exists());
  EXPECT_TRUE(root.getChildFile("Live/Pack/a.nam").existsAsFile());
  EXPECT_TRUE(root.getChildFile("Live/Pack/deep/b.wav").existsAsFile());
}

TEST_F(LibraryTest, MovingToAnEmptyDestinationMovesToTheLibraryRoot) {
  // The breadcrumb's "Library" crumb is the root, and the root's path is "".
  place("Amps/Twin.nam", "a2-amp-test.nam");

  const juce::var moved = proc.moveLibraryItem("Amps/Twin.nam", "");
  ASSERT_TRUE(moved["error"].isVoid()) << moved["error"].toString().toStdString();
  EXPECT_EQ(moved["path"].toString(), juce::String("Twin.nam"));
  EXPECT_TRUE(root.getChildFile("Twin.nam").existsAsFile());
  EXPECT_FALSE(root.getChildFile("Amps/Twin.nam").exists());
}

TEST_F(LibraryTest, MovingNeverOverwritesATakenName) {
  ASSERT_FALSE(testFile("a2-amp-test.nam").hasIdenticalContentTo(testFile("a2-amp-cab-test.nam")))
      << "the two fixtures must differ or this test proves nothing";
  place("Amps/Twin.nam", "a2-amp-test.nam");
  place("Twin.nam", "a2-amp-cab-test.nam");

  const juce::var moved = proc.moveLibraryItem("Twin.nam", "Amps");
  ASSERT_TRUE(moved["error"].isVoid()) << moved["error"].toString().toStdString();
  EXPECT_EQ(moved["path"].toString(), juce::String("Amps/Twin (2).nam"));
  // Both captures survive, each with its own bytes.
  EXPECT_TRUE(
      root.getChildFile("Amps/Twin.nam").hasIdenticalContentTo(testFile("a2-amp-test.nam")));
  EXPECT_TRUE(root.getChildFile("Amps/Twin (2).nam")
                  .hasIdenticalContentTo(testFile("a2-amp-cab-test.nam")));
  EXPECT_FALSE(root.getChildFile("Twin.nam").exists());
}

TEST_F(LibraryTest, MovingAFolderUniquesTheWholeNameNotAnExtension) {
  // "Twin v1.2" is a folder: it has no extension, so the suffix goes on the
  // end, not between "Twin v1" and a fake ".2".
  place("Amps/Twin v1.2/a.nam", "a2-amp-test.nam");
  place("Twin v1.2/b.nam", "a2-amp-cab-test.nam");

  const juce::var moved = proc.moveLibraryItem("Twin v1.2", "Amps");
  ASSERT_TRUE(moved["error"].isVoid()) << moved["error"].toString().toStdString();
  EXPECT_EQ(moved["path"].toString(), juce::String("Amps/Twin v1.2 (2)"));
  EXPECT_TRUE(root.getChildFile("Amps/Twin v1.2 (2)/b.nam").existsAsFile());
  EXPECT_TRUE(root.getChildFile("Amps/Twin v1.2/a.nam").existsAsFile())
      << "the folder that was already there must not be merged into or replaced";
}

TEST_F(LibraryTest, MovingIntoTheFolderItIsAlreadyInChangesNothing) {
  // Dropping a row on the folder being viewed is easy to do by accident; it
  // must not rename the file to "Twin (2)" by treating itself as a collision.
  place("Amps/Twin.nam", "a2-amp-test.nam");

  const juce::var moved = proc.moveLibraryItem("Amps/Twin.nam", "Amps");
  EXPECT_TRUE(moved["error"].isVoid()) << moved["error"].toString().toStdString();
  EXPECT_EQ(moved["path"].toString(), juce::String("Amps/Twin.nam"));
  EXPECT_TRUE(root.getChildFile("Amps/Twin.nam").existsAsFile());
  EXPECT_FALSE(root.getChildFile("Amps/Twin (2).nam").exists());
}

TEST_F(LibraryTest, AFolderCannotMoveIntoItselfOrOneOfItsSubfolders) {
  place("Pack/sub/x.nam", "a2-amp-test.nam");

  EXPECT_FALSE(proc.moveLibraryItem("Pack", "Pack")["error"].isVoid());
  EXPECT_FALSE(proc.moveLibraryItem("Pack", "Pack/sub")["error"].isVoid());
  EXPECT_TRUE(root.getChildFile("Pack/sub/x.nam").existsAsFile())
      << "a refused move must lose nothing";
}

TEST_F(LibraryTest, AFolderMovesIntoASiblingWhoseNameMerelyStartsTheSame) {
  // "Pack 2" begins with "Pack" but is not inside it: a prefix test on the
  // path strings would wrongly refuse this.
  place("Pack/a.nam", "a2-amp-test.nam");
  ASSERT_TRUE(proc.createLibraryFolder("", "Pack 2")["error"].isVoid());

  const juce::var moved = proc.moveLibraryItem("Pack", "Pack 2");
  ASSERT_TRUE(moved["error"].isVoid()) << moved["error"].toString().toStdString();
  EXPECT_EQ(moved["path"].toString(), juce::String("Pack 2/Pack"));
  EXPECT_TRUE(root.getChildFile("Pack 2/Pack/a.nam").existsAsFile());
}

TEST_F(LibraryTest, MovePathsThatEscapeTheLibraryAreRefused) {
  place("Amps/Twin.nam", "a2-amp-test.nam");
  const juce::File outsideDir = root.getParentDirectory();
  const juce::File outside = outsideDir.getChildFile("t3k-move-outside.nam");
  ASSERT_TRUE(testFile("a2-amp-test.nam").copyFileTo(outside));

  // Either argument reaching out of the library is refused, and nothing moves.
  EXPECT_FALSE(proc.moveLibraryItem("../t3k-move-outside.nam", "Amps")["error"].isVoid());
  EXPECT_FALSE(proc.moveLibraryItem(outside.getFullPathName(), "Amps")["error"].isVoid());
  EXPECT_FALSE(proc.moveLibraryItem("Amps/Twin.nam", "..")["error"].isVoid());
  EXPECT_FALSE(
      proc.moveLibraryItem("Amps/Twin.nam", outsideDir.getFullPathName())["error"].isVoid());

  EXPECT_TRUE(outside.existsAsFile()) << "nothing outside the library may be touched";
  EXPECT_FALSE(outsideDir.getChildFile("Twin.nam").exists());
  EXPECT_TRUE(root.getChildFile("Amps/Twin.nam").existsAsFile());
  outside.deleteFile();
}

TEST_F(LibraryTest, TheLibraryRootCannotBeMoved) {
  ASSERT_TRUE(proc.createLibraryFolder("", "Amps")["error"].isVoid());

  EXPECT_FALSE(proc.moveLibraryItem("", "Amps")["error"].isVoid());
  EXPECT_TRUE(root.getChildFile("Amps").isDirectory());
}

TEST_F(LibraryTest, MovingToAMissingFolderOrOntoAFileIsRefused) {
  place("Twin.nam", "a2-amp-test.nam");
  place("Amps/Other.nam", "a2-amp-cab-test.nam");

  EXPECT_FALSE(proc.moveLibraryItem("Twin.nam", "NoSuchFolder")["error"].isVoid());
  EXPECT_FALSE(root.getChildFile("NoSuchFolder").exists()) << "a move must not invent folders";
  // A file is not a folder to move into.
  EXPECT_FALSE(proc.moveLibraryItem("Twin.nam", "Amps/Other.nam")["error"].isVoid());
  EXPECT_TRUE(root.getChildFile("Twin.nam").existsAsFile());
  EXPECT_FALSE(proc.moveLibraryItem("NoSuchItem.nam", "Amps")["error"].isVoid());
}

TEST_F(LibraryTest, ImportCopiesFilesAndFoldersIn) {
  const juce::var file = proc.importPathToLibrary("", testFile("a2-amp-test.nam"));
  ASSERT_TRUE(file["error"].isVoid()) << file["error"].toString().toStdString();
  EXPECT_EQ(file["path"].toString(), juce::String("a2-amp-test.nam"));
  EXPECT_TRUE(root.getChildFile("a2-amp-test.nam").existsAsFile());
  // A second import of the same name is uniqued, never an overwrite.
  EXPECT_EQ(proc.importPathToLibrary("", testFile("a2-amp-test.nam"))["path"].toString(),
            juce::String("a2-amp-test (2).nam"));

  const juce::File assets(T3K_TEST_FILES_DIR);
  int loadable = 0;
  for (const auto& asset : assets.findChildFiles(juce::File::findFiles, true))
    if (ToneLibrary::isModelFile(asset))
      ++loadable;

  const juce::var folder = proc.importPathToLibrary("", assets);
  ASSERT_TRUE(folder["error"].isVoid()) << folder["error"].toString().toStdString();
  EXPECT_EQ(static_cast<int>(folder["copied"]), loadable);
  const juce::var imported = proc.listLibrary(folder["path"].toString());
  EXPECT_EQ(namesOf(imported, "models").size(), loadable)
      << "a folder comes in whole, minus what the plugin can't load";
}

TEST_F(LibraryTest, DroppedFilesAreValidatedBeforeTheyLand) {
  const juce::Array<juce::var> files{
      fileEntry("a2-amp-test.nam", base64Of(testFile("a2-amp-test.nam"))),
      fileEntry("broken.nam", juce::Base64::toBase64("not a NAM file")),
      fileEntry("notes.txt", juce::Base64::toBase64("hello"))};

  const juce::var result = proc.importFilesToLibrary("", juce::var(files));
  ASSERT_TRUE(result["error"].isVoid()) << result["error"].toString().toStdString();
  EXPECT_EQ(static_cast<int>(result["copied"]), 1);
  EXPECT_EQ(static_cast<int>(result["skipped"]), 2);
  EXPECT_EQ(namesOf(proc.listLibrary(""), "models"), juce::StringArray({"a2-amp-test"}));

  // Nothing loadable in the drop at all: the first file's error surfaces.
  const juce::var rejected = proc.importFilesToLibrary(
      "", juce::var(juce::Array<juce::var>{
              fileEntry("broken.nam", juce::Base64::toBase64("not a NAM file"))}));
  EXPECT_EQ(rejected["error"].toString(), juce::String("Not a valid NAM file"));
}

TEST_F(LibraryTest, DroppedNamesCarryTheirSubfolderAndCannotEscape) {
  // A dropped folder arrives flat, its shape carried in the entry names, and
  // has to land on disk the same way the picker's copy would.
  const juce::String amp = base64Of(testFile("a2-amp-test.nam"));
  const juce::var result = proc.importFilesToLibrary(
      "", juce::var(juce::Array<juce::var>{fileEntry("Pack/Marshall/JCM800.nam", amp),
                                           fileEntry("Pack/plain.nam", amp),
                                           // Nothing built from a name may
                                           // climb out of the library.
                                           fileEntry("../escaped.nam", amp)}));
  ASSERT_TRUE(result["error"].isVoid()) << result["error"].toString().toStdString();
  EXPECT_EQ(static_cast<int>(result["copied"]), 3);

  EXPECT_TRUE(root.getChildFile("Pack/Marshall/JCM800.nam").existsAsFile());
  EXPECT_TRUE(root.getChildFile("Pack/plain.nam").existsAsFile());
  EXPECT_TRUE(root.getChildFile("escaped.nam").existsAsFile()) << "flattened into the library";
  EXPECT_FALSE(root.getParentDirectory().getChildFile("escaped.nam").exists());
}

TEST_F(LibraryTest, CreateFolderUniquesOnlyWhenAsked) {
  ASSERT_EQ(proc.createLibraryFolder("", "Pack")["path"].toString(), juce::String("Pack"));
  // The New Folder action reports the collision...
  EXPECT_FALSE(proc.createLibraryFolder("", "Pack")["error"].isVoid());
  // ...while an import, which didn't choose the name, sits beside it.
  EXPECT_EQ(proc.createLibraryFolder("", "Pack", true)["path"].toString(),
            juce::String("Pack (2)"));
}

TEST_F(LibraryTest, ListingCountsWhatLoadingTheFolderWouldAdd) {
  // Majority extension decides NAM vs IR, and the count is recursive: this
  // folder loads as two NAM models, not the one IR sitting at its top.
  place("Pack/cab.wav", "cab-ir-test.wav");
  place("Pack/Captures/amp.nam", "a2-amp-test.nam");
  place("Pack/Captures/cab.nam", "a2-amp-cab-test.nam");

  const juce::var listing = proc.listLibrary("Pack");
  EXPECT_EQ(namesOf(listing, "models"), juce::StringArray({"cab"}));
  EXPECT_EQ(static_cast<int>(listing["loadable"]), 2);
  // The browser's per-folder badge counts everything loadable under it.
  EXPECT_EQ(static_cast<int>(proc.listLibrary("")["folders"][0]["models"]), 3);

  // And loading it really does add those two.
  const juce::var res = proc.loadLibraryTone("Pack");
  ASSERT_TRUE(res["error"].isVoid()) << res["error"].toString().toStdString();
  ASSERT_TRUE(waitForChainLoaded(proc));
  EXPECT_EQ(firstToneBlock(proc)["tone"]["models"].size(), 2);
}

TEST_F(LibraryTest, LoadingAnEntryMakesTheSameLocalBlockADropWould) {
  place("Amps/Twin.nam", "a2-amp-test.nam");

  const juce::var res = proc.loadLibraryTone("Amps/Twin.nam");
  ASSERT_TRUE(res["error"].isVoid()) << res["error"].toString().toStdString();
  ASSERT_TRUE(waitForChainLoaded(proc));

  const juce::var block = firstToneBlock(proc);
  EXPECT_EQ(block["blockId"].toString(), res["blockId"].toString());
  EXPECT_TRUE(static_cast<bool>(block["tone"]["local"]));
  EXPECT_EQ(block["tone"]["title"].toString(), juce::String("Twin"));
  EXPECT_EQ(block["tone"]["format"].toString(), juce::String("nam"));
}

TEST_F(LibraryTest, LoadingAFolderMakesOneMultiModelBlock) {
  place("Pack/amp.nam", "a2-amp-test.nam");
  place("Pack/cab.nam", "a2-amp-cab-test.nam");

  const juce::var res = proc.loadLibraryTone("Pack");
  ASSERT_TRUE(res["error"].isVoid()) << res["error"].toString().toStdString();
  ASSERT_TRUE(waitForChainLoaded(proc));

  const juce::var block = firstToneBlock(proc);
  EXPECT_EQ(block["tone"]["title"].toString(), juce::String("Pack"));
  ASSERT_EQ(block["tone"]["models"].size(), 2);
  EXPECT_EQ(block["tone"]["models"][0]["name"].toString(), juce::String("amp"));
}

TEST_F(LibraryTest, LoadingAMissingEntryReportsInsteadOfAddingABlock) {
  const juce::var res = proc.loadLibraryTone("Amps/Gone.nam");
  EXPECT_FALSE(res["error"].isVoid());
  EXPECT_TRUE(firstToneBlock(proc).isVoid());
}

TEST_F(LibraryTest, SavingABlockFilesItsModelBytes) {
  const juce::var loaded = proc.loadLocalTonePath(testFile("a2-amp-test.nam"));
  ASSERT_TRUE(loaded["error"].isVoid()) << loaded["error"].toString().toStdString();
  ASSERT_TRUE(waitForChainLoaded(proc));
  const std::string blockId = loaded["blockId"].toString().toStdString();

  ASSERT_TRUE(proc.createLibraryFolder("", "Amps")["error"].isVoid());
  const juce::var saved = proc.saveBlockToLibrary(blockId, "Amps");
  ASSERT_TRUE(saved["error"].isVoid()) << saved["error"].toString().toStdString();
  // Tone title and model name are the same file here, so they collapse to
  // one name instead of "a2-amp-test - a2-amp-test".
  EXPECT_EQ(saved["path"].toString(), juce::String("Amps/a2-amp-test.nam"));

  const juce::File filed = root.getChildFile("Amps/a2-amp-test.nam");
  ASSERT_TRUE(filed.existsAsFile());
  EXPECT_EQ(filed.getSize(), testFile("a2-amp-test.nam").getSize());

  // Filing the same block again keeps both copies rather than overwriting.
  EXPECT_EQ(proc.saveBlockToLibrary(blockId, "Amps")["path"].toString(),
            juce::String("Amps/a2-amp-test (2).nam"));

  // And what was filed loads straight back in.
  const juce::var reloaded = proc.loadLibraryTone("Amps/a2-amp-test.nam");
  EXPECT_TRUE(reloaded["error"].isVoid()) << reloaded["error"].toString().toStdString();
}

TEST_F(LibraryTest, SavingAnEmptySlotOrUnknownBlockIsRefused) {
  EXPECT_FALSE(proc.saveBlockToLibrary("no-such-block", "")["error"].isVoid());

  // An insert slot is a real block id, but there is no model behind it.
  const juce::var state = proc.getChainState(-1);
  ASSERT_TRUE(state["chain"].getArray() != nullptr);
  const juce::String insertId = state["chain"][0]["blockId"].toString();
  ASSERT_TRUE(insertId.isNotEmpty());
  EXPECT_FALSE(proc.saveBlockToLibrary(insertId.toStdString(), "")["error"].isVoid());
}
