// Local Library tests (Library.h, ProcessorLibrary.cpp), run against a
// throwaway temp directory holding both the presets folder and the Library
// root.
//
// These pin the tree the UI is built from (your library first, its typed
// Captures and Presets halves, the presets folder mounted as "Presets",
// linked folders, natural order), the edit rules (your library editable,
// imported ones read-only but copyable and removable, each half taking only
// its own kind),
// the .t3ktone / .t3klibrary formats (dedupe, backup restore, a re-import
// replacing a followed library, path-traversal refusal), and the processor
// glue: folder-scoped prev/next, saving into the active preset's folder, and
// the active preset following its file through renames, moves and removes.
#include "Library.h"
#include "LibraryState.h"
#include "PresetFile.h"
#include "Processor.h"
#include "chain_test_helpers.h"
#include "test_helpers.h"

#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

#include <tuple>

namespace {

struct TempLibrary {
  TempLibrary()
      : base(juce::File::getSpecialLocation(juce::File::tempDirectory)
                 .getChildFile("t3k-library-tests-" + juce::Uuid().toString())),
        presetsDir(base.getChildFile("Presets")),
        root(base.getChildFile("Library")),
        presets(presetsDir),
        library(presets) {
    base.createDirectory();
    library.setLocation(root, "tonehound");
    library.setUseTrash(false);
  }
  ~TempLibrary() { base.deleteRecursively(); }

  juce::File base, presetsDir, root;
  PresetManager presets;
  LocalLibrary library;
};

juce::ValueTree makePreset(const juce::String& marker = {}) {
  juce::ValueTree preset(PresetManager::kPresetTag);
  preset.setProperty("marker", marker, nullptr);
  return preset;
}

juce::var findChild(const juce::var& node, const juce::String& name) {
  if (const auto* kids = node["children"].getArray())
    for (const auto& kid : *kids)
      if (kid["name"].toString() == name)
        return kid;
  return {};
}

juce::StringArray childNames(const juce::var& node) {
  juce::StringArray names;
  if (const auto* kids = node["children"].getArray())
    for (const auto& kid : *kids)
      names.add(kid["name"].toString());
  return names;
}

juce::var library(const juce::var& scan, int index) { return scan["libraries"][index]; }

juce::var toneRef(int toneId, int modelId, const juce::String& title, const juce::String& model) {
  juce::DynamicObject::Ptr tone = new juce::DynamicObject();
  tone->setProperty("id", toneId);
  tone->setProperty("title", title);
  tone->setProperty("gear", "amp");
  juce::DynamicObject::Ptr m = new juce::DynamicObject();
  m->setProperty("id", modelId);
  m->setProperty("name", model);
  juce::DynamicObject::Ptr ref = new juce::DynamicObject();
  ref->setProperty("tone", tone.get());
  ref->setProperty("model", m.get());
  return ref.get();
}

TEST(LibraryTest, YourLibraryComesFirstWithThePresetsFolderMounted) {
  TempLibrary t;
  t.presets.save("Lead", makePreset());
  t.root.getChildFile("awesomeuser").getChildFile("Presets").getChildFile("Metal").createDirectory();
  t.root.getChildFile(".import-leftover").createDirectory();  // never listed

  const juce::var scan = t.library.scan();
  ASSERT_EQ(scan["libraries"].size(), 2);

  // Yours exists in the tree before its folder does, with both halves.
  const juce::var mine = library(scan, 0);
  EXPECT_EQ(mine["name"].toString(), juce::String("tonehound"));
  EXPECT_TRUE(static_cast<bool>(mine["mine"]));
  EXPECT_FALSE(static_cast<bool>(mine["writable"]));  // things go into a half
  // (Local is listed even empty; the drawer hides empty sections.)
  EXPECT_EQ(childNames(mine), juce::StringArray({"Captures", "Local", "Presets"}));
  const juce::var captures = findChild(mine, "Captures");
  EXPECT_EQ(captures["type"].toString(), juce::String("captures"));
  EXPECT_TRUE(static_cast<bool>(captures["writable"]));
  EXPECT_FALSE(static_cast<bool>(captures["editable"]));
  EXPECT_FALSE(t.library.capturesDir().exists());  // made on first use
  const juce::var mount = findChild(mine, "Presets");
  ASSERT_TRUE(mount.isObject());
  EXPECT_TRUE(static_cast<bool>(mount["mount"]));
  EXPECT_FALSE(static_cast<bool>(mount["editable"]));  // the mount itself stays put
  EXPECT_TRUE(static_cast<bool>(mount["writable"]));   // but takes new items
  EXPECT_EQ(mount["type"].toString(), juce::String("presets"));
  const juce::var lead = findChild(mount, "Lead");
  ASSERT_TRUE(lead.isObject());
  // Same id the preset browser uses, so both agree on what is active.
  EXPECT_EQ(lead["id"].toString(), t.presets.list().front().id);

  const juce::var theirs = library(scan, 1);
  EXPECT_EQ(theirs["name"].toString(), juce::String("awesomeuser"));
  EXPECT_FALSE(static_cast<bool>(theirs["mine"]));
  EXPECT_TRUE(static_cast<bool>(theirs["removable"]));  // "unfollow"
  const juce::var theirMetal = findChild(findChild(theirs, "Presets"), "Metal");
  EXPECT_FALSE(static_cast<bool>(theirMetal["editable"]));
  EXPECT_EQ(theirMetal["type"].toString(), juce::String("presets"));  // their halves are typed too
}

TEST(LibraryTest, FactoryAndOrderFileStayOutOfTheMount) {
  TempLibrary t;
  t.presets.save("Mine", makePreset());
  t.presetsDir.getChildFile("Factory").createDirectory();
  t.presetsDir.getChildFile("Sub").createDirectory();
  t.presetsDir.getChildFile("order.json").replaceWithText("[]");

  const juce::var mount = findChild(library(t.library.scan(), 0), "Presets");
  EXPECT_EQ(childNames(mount), juce::StringArray({"Sub", "Mine"}));
  EXPECT_TRUE(t.library.canEdit(t.presetsDir.getChildFile("Sub")));
  EXPECT_FALSE(t.library.canEdit(t.presetsDir.getChildFile("Factory")));
}

TEST(LibraryTest, FolderPresetsListInNaturalOrderWithFileIds) {
  TempLibrary t;
  const juce::File setlist = t.library.createFolder(t.presetsDir, "Setlist");
  ASSERT_TRUE(setlist.isDirectory());
  for (const char* name : {"10 Encore", "2 Second", "1 Opener"})
    ASSERT_TRUE(t.presets.saveInFolder(setlist, name, makePreset()).id.isNotEmpty());

  const auto entries = t.presets.listFolder(setlist);
  ASSERT_EQ(entries.size(), 3u);
  EXPECT_EQ(entries[0].info.name, juce::String("1 Opener"));
  EXPECT_EQ(entries[1].info.name, juce::String("2 Second"));
  EXPECT_EQ(entries[2].info.name, juce::String("10 Encore"));
  EXPECT_EQ(entries[0].info.id, PresetManager::fileId(setlist.getChildFile("1 Opener.t3kpreset")));
  // Library presets never join the program-change list.
  EXPECT_TRUE(t.presets.list().empty());
  // A file id loads like any other.
  EXPECT_TRUE(t.presets.load(entries[1].info.id).isValid());
}

TEST(LibraryTest, SaveInFolderOverwritesTheSameName) {
  TempLibrary t;
  const juce::File folder = t.library.createFolder(t.presetsDir, "Metal");
  const auto first = t.presets.saveInFolder(folder, "BE100 1", makePreset("v1"));
  const auto second = t.presets.saveInFolder(folder, "BE100 1", makePreset("v2"));
  EXPECT_EQ(first.id, second.id);
  EXPECT_EQ(t.presets.listFolder(folder).size(), 1u);
  EXPECT_EQ(t.presets.load(second.id).getProperty("marker").toString(), juce::String("v2"));
}

TEST(LibraryTest, RenameMoveCopyAndRemove) {
  TempLibrary t;
  const juce::File own = t.library.ownDir();
  const juce::File metal = t.library.createFolder(t.presetsDir, "Metal");
  const juce::File setlist = t.library.createFolder(t.presetsDir, "Setlist");
  const auto saved = t.presets.saveInFolder(metal, "BE100", makePreset());
  const juce::File preset = metal.getChildFile("BE100.t3kpreset");
  ASSERT_TRUE(preset.existsAsFile());

  // A preset rename changes the name inside along with the file.
  const juce::File renamed = t.library.rename(preset, "BE100 Crunch");
  ASSERT_TRUE(renamed.existsAsFile());
  EXPECT_EQ(renamed.getFileName(), juce::String("BE100 Crunch.t3kpreset"));
  EXPECT_EQ(t.presets.listFolder(metal).front().info.name, juce::String("BE100 Crunch"));

  // Copying into the same folder: a new name inside and a fresh id.
  const juce::File copied = t.library.copy(renamed, metal);
  ASSERT_TRUE(copied.existsAsFile());
  EXPECT_EQ(copied.getFileName(), juce::String("BE100 Crunch 2.t3kpreset"));
  const auto listed = t.presets.listFolder(metal);
  ASSERT_EQ(listed.size(), 2u);
  EXPECT_EQ(listed[1].info.name, juce::String("BE100 Crunch 2"));
  EXPECT_NE(t.presets.load(listed[0].info.id).getProperty("id"),
            t.presets.load(listed[1].info.id).getProperty("id"));

  // Moves refuse a folder into itself, and land beside what is there.
  EXPECT_EQ(t.library.move(metal, metal.getChildFile("x")), juce::File());
  const juce::File movedFolder = t.library.move(metal, setlist);
  EXPECT_EQ(movedFolder, setlist.getChildFile("Metal"));
  EXPECT_TRUE(movedFolder.getChildFile("BE100 Crunch.t3kpreset").existsAsFile());

  EXPECT_TRUE(t.library.remove(movedFolder));
  EXPECT_FALSE(movedFolder.exists());
  // Your library itself and its halves can't be removed.
  EXPECT_FALSE(t.library.remove(own));
  EXPECT_FALSE(t.library.remove(t.presetsDir));
  t.library.capturesDir().createDirectory();
  EXPECT_FALSE(t.library.remove(t.library.capturesDir()));
  juce::ignoreUnused(saved);
}

TEST(LibraryTest, ImportedLibrariesAreReadOnlyButCopyable) {
  TempLibrary t;
  const juce::File theirs = t.root.getChildFile("awesomeuser");
  const juce::File folder = theirs.getChildFile("Presets").getChildFile("Metal");
  folder.createDirectory();
  t.presets.saveInFolder(folder, "Their Lead", makePreset());  // as if unpacked
  const juce::File preset = folder.getChildFile("Their Lead.t3kpreset");

  EXPECT_EQ(t.library.rename(preset, "Mine now"), juce::File());
  EXPECT_EQ(t.library.move(preset, t.presetsDir), juce::File());
  EXPECT_EQ(t.library.createFolder(folder, "New"), juce::File());
  EXPECT_FALSE(t.library.remove(preset));

  EXPECT_EQ(t.library.copy(folder, t.library.capturesDir()), juce::File());  // presets stay presets
  const juce::File copied = t.library.copy(folder, t.presetsDir);
  EXPECT_TRUE(copied.getChildFile("Their Lead.t3kpreset").existsAsFile());
  EXPECT_TRUE(t.library.canEdit(copied));
  EXPECT_TRUE(t.library.remove(theirs));  // unfollow
}

TEST(LibraryTest, RenamingYourLibraryRenamesItsFolder) {
  TempLibrary t;
  EXPECT_EQ(t.library.createFolder(t.library.ownDir(), "Loose"), juce::File());  // only into a half
  t.library.createFolder(t.library.capturesDir(), "Clean");
  const juce::File renamed = t.library.rename(t.library.ownDir(), "riffmaster");
  EXPECT_EQ(renamed, t.root.getChildFile("riffmaster"));
  EXPECT_TRUE(renamed.getChildFile("Captures").getChildFile("Clean").isDirectory());
  // Another library's name is taken.
  t.root.getChildFile("awesomeuser").createDirectory();
  t.library.setLocation(t.root, "riffmaster");
  EXPECT_EQ(t.library.rename(t.library.ownDir(), "awesomeuser"), juce::File());
}

TEST(LibraryTest, LinkedFoldersShowInYourLibraryWithoutCopying) {
  TempLibrary t;
  const juce::File captures = t.base.getChildFile("NAM").getChildFile("NAM Captures");
  captures.getChildFile("Plexi").createDirectory();
  captures.getChildFile("Plexi").getChildFile("gain 5.nam").replaceWithText("{}");
  captures.getChildFile("cab.wav").replaceWithText("x");
  ASSERT_TRUE(t.library.linkProblem(captures).isEmpty());
  t.library.setLocation(t.root, "tonehound", {captures});

  const juce::var mine = library(t.library.scan(), 0);
  // In your Local, after its own folders (Captures is for your own captures).
  EXPECT_EQ(childNames(mine), juce::StringArray({"Captures", "Local", "Presets"}));
  t.library.createFolder(t.library.capturesDir(), "Clean");
  t.library.createFolder(t.library.localDir(), "Kept");
  const juce::var ownLocal = findChild(library(t.library.scan(), 0), "Local");
  EXPECT_TRUE(static_cast<bool>(ownLocal["local"]));
  EXPECT_EQ(ownLocal["type"].toString(), juce::String("captures"));
  EXPECT_EQ(childNames(ownLocal), juce::StringArray({"Kept", "NAM Captures"}));
  EXPECT_EQ(childNames(findChild(library(t.library.scan(), 0), "Captures")), juce::StringArray({"Clean"}));
  const juce::var linked = findChild(ownLocal, "NAM Captures");
  EXPECT_EQ(linked["type"].toString(), juce::String("captures"));
  EXPECT_TRUE(static_cast<bool>(linked["linked"]));
  EXPECT_FALSE(static_cast<bool>(linked["missing"]));
  EXPECT_FALSE(static_cast<bool>(linked["removable"]));  // unlinking is the UI's, not a delete
  EXPECT_TRUE(static_cast<bool>(linked["writable"]));
  EXPECT_EQ(childNames(linked), juce::StringArray({"Plexi", "cab"}));
  EXPECT_EQ(childNames(findChild(linked, "Plexi")), juce::StringArray({"gain 5"}));

  // Its contents are yours: rename, new folders, moves in and out.
  const juce::File plexi = captures.getChildFile("Plexi");
  EXPECT_TRUE(t.library.canEdit(plexi));
  const juce::File crunch = t.library.rename(plexi.getChildFile("gain 5.nam"), "Crunch");
  EXPECT_EQ(crunch, plexi.getChildFile("Crunch.nam"));
  EXPECT_FALSE(t.library.capturesDir().getChildFile("Clean").getChildFile("Crunch.nam").exists());
  EXPECT_TRUE(t.library.copy(crunch, t.library.capturesDir().getChildFile("Clean")).existsAsFile());
  EXPECT_FALSE(captures.getChildFile("Crunch.nam").exists());  // nothing moved out by the scan
  EXPECT_EQ(t.library.createFolder(captures, "New"), captures.getChildFile("New"));

  // A whole-library export leaves the link out.
  const juce::File archive = t.base.getChildFile("backup.t3klibrary");
  ASSERT_TRUE(t.library.exportArchive(t.library.ownDir(), archive));
  juce::ZipFile zip(archive);
  for (int i = 0; i < zip.getNumEntries(); ++i)
    EXPECT_FALSE(zip.getEntry(i)->filename.contains("Plexi")) << zip.getEntry(i)->filename;
}

TEST(LibraryTest, ImportFolderCopiesCapturesInKeepingSubfolders) {
  TempLibrary t;
  // An old NAM collection: captures in subfolders, plus things that aren't
  // library content (notes, a trainer's leftovers, an empty folder).
  const juce::File old = t.base.getChildFile("Old NAM stuff");
  old.getChildFile("Plexi").createDirectory();
  old.getChildFile("Plexi").getChildFile("gain 5.nam").replaceWithText("{}");
  old.getChildFile("cab.wav").replaceWithText("x");
  old.getChildFile("notes.txt").replaceWithText("hi");
  old.getChildFile("training").getChildFile("model.json").create();
  old.getChildFile("Empty").createDirectory();
  // A folder the Library hides (named for A1): not copied in to be hidden.
  old.getChildFile("Plexi (A1)").createDirectory();
  old.getChildFile("Plexi (A1)").getChildFile("gain 5.nam").replaceWithText("{}");

  const juce::File copied = t.library.importFolder(old, t.library.capturesDir());
  EXPECT_EQ(copied, t.library.capturesDir().getChildFile("Old NAM stuff"));
  EXPECT_TRUE(copied.getChildFile("Plexi").getChildFile("gain 5.nam").existsAsFile());
  EXPECT_TRUE(copied.getChildFile("cab.wav").existsAsFile());
  EXPECT_FALSE(copied.getChildFile("notes.txt").exists());
  EXPECT_FALSE(copied.getChildFile("training").exists());
  EXPECT_FALSE(copied.getChildFile("Empty").exists());
  EXPECT_FALSE(copied.getChildFile("Plexi (A1)").exists());
  EXPECT_TRUE(old.getChildFile("notes.txt").existsAsFile());  // the source is untouched
  EXPECT_FALSE(t.library.linkedDirs().contains(old));

  // Again: beside the first copy, never merged into it.
  EXPECT_EQ(t.library.importFolder(old, t.library.capturesDir()),
            t.library.capturesDir().getChildFile("Old NAM stuff 2"));
  // Refused: into presets, into itself, and a folder with nothing to copy.
  EXPECT_EQ(t.library.importFolder(old, t.presetsDir), juce::File());
  EXPECT_EQ(t.library.importFolder(t.library.capturesDir(), copied), juce::File());
  EXPECT_EQ(t.library.importFolder(old.getChildFile("training"), t.library.capturesDir()), juce::File());
}

TEST(LibraryTest, ACancelledScanStopsWithoutListing) {
  TempLibrary t;
  const juce::File big = t.base.getChildFile("Big");
  for (int i = 0; i < 5; ++i)
    big.getChildFile("Amp " + juce::String(i)).getChildFile("Sub").createDirectory();
  t.library.setLocation(t.root, "tonehound", {big});
  t.library.cancelScans();  // the plugin going away: whatever runs now stops
  const juce::var linked = findChild(findChild(library(t.library.scan(), 0), "Local"), "Big");
  ASSERT_TRUE(linked.isObject());
  EXPECT_EQ(childNames(linked).size(), 0);
}

TEST(LibraryTest, LinksThatWouldOverlapAreRefused) {
  TempLibrary t;
  const juce::File outside = t.base.getChildFile("Outside");
  outside.getChildFile("Inner").createDirectory();
  t.library.ownDir().getChildFile("Mine").createDirectory();

  EXPECT_TRUE(t.library.linkProblem(t.base.getChildFile("nope")).isNotEmpty());  // not a folder
  EXPECT_TRUE(t.library.linkProblem(t.library.ownDir().getChildFile("Mine")).isNotEmpty());
  EXPECT_TRUE(t.library.linkProblem(t.root).isNotEmpty());
  EXPECT_TRUE(t.library.linkProblem(t.base).isNotEmpty());  // holds the Library and Presets
  t.presetsDir.createDirectory();
  EXPECT_TRUE(t.library.linkProblem(t.presetsDir).isNotEmpty());

  t.library.setLocation(t.root, "tonehound", {outside});
  EXPECT_TRUE(t.library.linkProblem(outside).isNotEmpty());  // already linked
  EXPECT_TRUE(t.library.linkProblem(outside.getChildFile("Inner")).isNotEmpty());
  // setLocation itself drops overlapping links.
  t.library.setLocation(t.root, "tonehound", {outside, outside.getChildFile("Inner"), t.root});
  EXPECT_EQ(t.library.linkedDirs().size(), 1);
}

// Local is a folder of yours to organize in: a folder moves there from
// Captures (a keep folder of others' captures, say), and it is fixed itself.
// A folder's captures carry its gear, best effort: the first .nam's
// metadata, the first IR's length (one read of each per folder).
TEST(LibraryTest, CapturesCarryTheirFoldersGear) {
  TempLibrary t;
  const juce::File pedal = t.library.createFolder(t.library.capturesDir(), "Fuzz");
  pedal.getChildFile("a gain 3.nam").replaceWithText(R"({"version": "0.5.4", "metadata": {"gear_type": "pedal"}})");
  pedal.getChildFile("b gain 7.nam").replaceWithText(R"({"version": "0.5.4"})");  // not read: the folder's is
  const juce::File cabs = t.library.createFolder(t.library.capturesDir(), "Cabs");
  ASSERT_TRUE(testFile("cab-ir-test.wav").copyFileTo(cabs.getChildFile("4x12.wav")));
  const juce::File plain = t.library.createFolder(t.library.capturesDir(), "Unknown");
  plain.getChildFile("x.nam").replaceWithText(R"({"version": "0.5.4"})");

  const juce::var captures = findChild(library(t.library.scan(), 0), "Captures");
  EXPECT_EQ(findChild(findChild(captures, "Fuzz"), "a gain 3")["gear"].toString(), juce::String("pedal"));
  EXPECT_EQ(findChild(findChild(captures, "Fuzz"), "b gain 7")["gear"].toString(), juce::String("pedal"));
  EXPECT_EQ(findChild(findChild(captures, "Cabs"), "4x12")["gear"].toString(), juce::String("cab"));
  EXPECT_TRUE(findChild(findChild(captures, "Unknown"), "x")["gear"].toString().isEmpty());

  // A tag in the file's own name beats its folder's metadata.
  pedal.getChildFile("[AMP] Head.nam").replaceWithText(R"({"version": "0.5.4"})");
  const juce::var again = findChild(library(t.library.scan(/*useCache=*/false), 0), "Captures");
  EXPECT_EQ(findChild(findChild(again, "Fuzz"), "[AMP] Head")["gear"].toString(), juce::String("amp"));

  // "DI" in the name: a direct capture, an amp alone, whatever the metadata says.
  const juce::File stack = t.library.createFolder(t.library.capturesDir(), "Stack");
  stack.getChildFile("Mesa Lead - DI.nam").replaceWithText(R"({"version": "0.5.4", "metadata": {"gear_type": "amp_cab"}})");
  const juce::var withDi = findChild(library(t.library.scan(/*useCache=*/false), 0), "Captures");
  EXPECT_EQ(findChild(findChild(withDi, "Stack"), "Mesa Lead - DI")["gear"].toString(), juce::String("amp"));
}

TEST(LibraryTest, FoldersMoveIntoLocal) {
  TempLibrary t;
  const juce::File kept = t.library.createFolder(t.library.capturesDir(), "Pedal boost");
  ASSERT_TRUE(kept.isDirectory());
  EXPECT_TRUE(t.library.canWriteInto(t.library.localDir()));
  const juce::File moved = t.library.move(kept, t.library.localDir());
  EXPECT_EQ(moved, t.library.localDir().getChildFile("Pedal boost"));
  EXPECT_TRUE(moved.isDirectory());
  EXPECT_FALSE(t.library.canEdit(t.library.localDir()));  // the fixed folder stays
  EXPECT_EQ(childNames(findChild(library(t.library.scan(), 0), "Local")), juce::StringArray({"Pedal boost"}));
}

// A scan cut short (the instance going) says so, so it is never kept as
// the saved listing (getLibrary).
TEST(LibraryTest, ACancelledScanIsMarked) {
  TempLibrary t;
  t.library.cancelScans();
  EXPECT_TRUE(static_cast<bool>(t.library.scan()["cancelled"]));
}

// The caller's own stop (an editor closing) cuts a scan short the same way,
// without cancelling the instance's other scans.
TEST(LibraryTest, AScanStopsOnItsCallersStop) {
  TempLibrary t;
  std::atomic<bool> stop{true};
  EXPECT_TRUE(static_cast<bool>(t.library.scan(true, &stop)["cancelled"]));
  EXPECT_FALSE(static_cast<bool>(t.library.scan()["cancelled"]));
}

// setLocation may be handed its own link list (a rename of your library
// passes linkedDirs() back): the links survive.
TEST(LibraryTest, SetLocationKeepsItsOwnLinks) {
  TempLibrary t;
  const juce::File linked = t.base.getChildFile("NAM Captures");
  ASSERT_TRUE(linked.createDirectory().wasOk());
  t.library.setLocation(t.root, "tonehound", {linked});
  ASSERT_EQ(t.library.linkedDirs().size(), 1);
  t.library.setLocation(t.root, "renamed", t.library.linkedDirs());
  EXPECT_EQ(t.library.linkedDirs(), juce::Array<juce::File>({linked}));
}

// A folder named for A1 captures would vanish from the listing the moment
// it was made: it isn't made (or renamed to).
TEST(LibraryTest, FoldersNamedForA1AreRefused) {
  TempLibrary t;
  EXPECT_EQ(t.library.createFolder(t.library.capturesDir(), "Bass (A1)"), juce::File());
  const juce::File clean = t.library.createFolder(t.library.capturesDir(), "Clean");
  ASSERT_TRUE(clean.isDirectory());
  EXPECT_EQ(t.library.rename(clean, "Clean xSTD"), juce::File());
  EXPECT_NE(t.library.rename(clean, "Clean A2"), juce::File());
}

TEST(LibraryTest, AMissingLinkStaysListedToUnlink) {
  TempLibrary t;
  const juce::File gone = t.base.getChildFile("Unplugged drive");
  t.library.setLocation(t.root, "tonehound", {gone});
  const juce::var linked = findChild(findChild(library(t.library.scan(), 0), "Local"), "Unplugged drive");
  ASSERT_TRUE(linked.isObject());
  EXPECT_TRUE(static_cast<bool>(linked["missing"]));
  EXPECT_FALSE(static_cast<bool>(linked["writable"]));
  EXPECT_EQ(t.library.createFolder(gone, "x"), juce::File());
}

TEST(LibraryTest, EachHalfListsAndTakesOnlyItsKind) {
  TempLibrary t;
  const juce::File clean = t.library.createFolder(t.library.capturesDir(), "Clean");
  const juce::File setlist = t.library.createFolder(t.presetsDir, "Setlist");
  ASSERT_TRUE(clean.isDirectory() && setlist.isDirectory());
  // Files of the other kind dropped there by hand are left out of the tree.
  clean.getChildFile("amp.nam").replaceWithText("{}");
  t.presets.saveInFolder(clean, "Stray rig", makePreset());
  t.presets.saveInFolder(setlist, "Song", makePreset());
  setlist.getChildFile("stray.nam").replaceWithText("{}");

  const juce::var mine = library(t.library.scan(), 0);
  EXPECT_EQ(childNames(findChild(findChild(mine, "Captures"), "Clean")), juce::StringArray({"amp"}));
  EXPECT_EQ(childNames(findChild(findChild(mine, "Presets"), "Setlist")), juce::StringArray({"Song"}));

  // Moves, copies and adds refuse the other kind; folders keep their kind.
  EXPECT_EQ(t.library.move(setlist.getChildFile("Song.t3kpreset"), clean), juce::File());
  EXPECT_EQ(t.library.copy(clean.getChildFile("amp.nam"), setlist), juce::File());
  EXPECT_EQ(t.library.move(clean, t.presetsDir), juce::File());
  EXPECT_EQ(t.library.addToneRef(setlist, toneRef(1, 2, "Amp", "Clean")), juce::File());
  EXPECT_TRUE(t.library.addToneRef(clean, toneRef(1, 2, "Amp", "Clean")).existsAsFile());
  EXPECT_FALSE(t.library.takesPresets(clean));
  EXPECT_TRUE(t.library.takesPresets(setlist));
  // A folder from outside the Library has no kind of its own: it may go
  // either way (and lists only what fits there).
  const juce::File outside = t.base.getChildFile("Download");
  outside.getChildFile("x.nam").create();
  EXPECT_TRUE(t.library.copy(outside, clean).isDirectory());
}

TEST(LibraryTest, ToneRefsAndCapturesDedupe) {
  TempLibrary t;
  const juce::File folder = t.library.createFolder(t.library.capturesDir(), "Favorites");
  const juce::File ref = t.library.addToneRef(folder, toneRef(42, 7, "Plexi", "Crunch"));
  ASSERT_TRUE(ref.existsAsFile());
  EXPECT_EQ(ref.getFileName(), juce::String("Plexi - Crunch.t3ktone"));
  EXPECT_EQ(t.library.addToneRef(folder, toneRef(42, 7, "Plexi", "Crunch")), ref);
  EXPECT_NE(t.library.addToneRef(folder, toneRef(42, 8, "Plexi", "Clean")), ref);

  const juce::var parsed = LocalLibrary::readToneRef(ref);
  EXPECT_EQ(static_cast<int>(parsed["tone"]["id"]), 42);
  EXPECT_EQ(static_cast<int>(parsed["model"]["id"]), 7);

  const juce::File source = t.base.getChildFile("amp.nam");
  source.replaceWithText("{}");
  const juce::File capture = t.library.addCapture(folder, source, "My Amp");
  EXPECT_EQ(capture.getFileName(), juce::String("My Amp.nam"));
  EXPECT_EQ(t.library.addCapture(folder, source, "Again"), capture);

  const juce::var node = findChild(findChild(library(t.library.scan(), 0), "Captures"), "Favorites");
  // References and captures sort together, by name.
  EXPECT_EQ(childNames(node), juce::StringArray({"My Amp", "Plexi - Clean", "Plexi - Crunch"}));
  EXPECT_EQ(findChild(node, "My Amp")["format"].toString(), juce::String("nam"));
}

TEST(LibraryTest, BackupRestoresIntoYourLibrary) {
  TempLibrary t;
  t.presets.save("Lead", makePreset());
  const juce::File setlist = t.library.createFolder(t.presetsDir, "Setlist");
  t.presets.saveInFolder(setlist, "Song", makePreset());
  const juce::File clean = t.library.createFolder(t.library.capturesDir(), "Clean");
  clean.getChildFile("amp.nam").replaceWithText("{}");
  const juce::File archive = t.base.getChildFile("backup.t3klibrary");
  ASSERT_TRUE(t.library.exportArchive(t.library.ownDir(), archive));

  // Lose everything, then restore.
  t.library.ownDir().deleteRecursively();
  t.presetsDir.deleteRecursively();
  EXPECT_EQ(t.library.importArchive(archive), t.library.ownDir());
  EXPECT_TRUE(setlist.getChildFile("Song.t3kpreset").existsAsFile());
  EXPECT_TRUE(clean.getChildFile("amp.nam").existsAsFile());
  ASSERT_EQ(t.presets.list().size(), 1u);
  EXPECT_EQ(t.presets.list().front().name, juce::String("Lead"));

  // Restoring over what is there keeps it and adds nothing twice.
  EXPECT_EQ(t.library.importArchive(archive), t.library.ownDir());
  EXPECT_EQ(t.presets.list().size(), 1u);
  EXPECT_EQ(t.presets.listFolder(setlist).size(), 1u);
}

// A fresh install, signed out ("My Library", nothing in it), restoring a
// backup: it comes home as yours, not as someone else's read-only library.
// Once yours has something in it, a library under another name is theirs.
TEST(LibraryTest, AFreshInstallRestoresItsBackupAsItsOwn) {
  TempLibrary before;
  const juce::File clean = before.library.createFolder(before.library.capturesDir(), "Clean");
  clean.getChildFile("amp.nam").replaceWithText("{}");
  const juce::File archive = before.base.getChildFile("backup.t3klibrary");
  ASSERT_TRUE(before.library.exportArchive(before.library.ownDir(), archive));

  TempLibrary fresh;
  fresh.library.setLocation(fresh.root, LocalLibrary::kDefaultOwner);
  EXPECT_EQ(fresh.library.importArchive(archive), fresh.library.ownDir());
  EXPECT_TRUE(fresh.library.capturesDir().getChildFile("Clean").getChildFile("amp.nam").existsAsFile());
  EXPECT_EQ(fresh.library.libraryId(false), before.library.libraryId(false)) << "and takes its id";

  TempLibrary used;
  used.library.setLocation(used.root, LocalLibrary::kDefaultOwner);
  used.library.createFolder(used.library.capturesDir(), "Mine").getChildFile("x.nam").replaceWithText("{}");
  EXPECT_EQ(used.library.importArchive(archive), used.root.getChildFile("tonehound"));
}

TEST(LibraryTest, ReimportingSomeoneElsesLibraryReplacesIt) {
  TempLibrary friendSide;
  friendSide.library.setLocation(friendSide.root, "awesomeuser");
  const juce::File metal = friendSide.library.createFolder(friendSide.presetsDir, "Metal");
  friendSide.presets.saveInFolder(metal, "Old", makePreset());
  const juce::File v1 = friendSide.base.getChildFile("v1.t3klibrary");
  ASSERT_TRUE(friendSide.library.exportArchive(friendSide.library.ownDir(), v1));
  friendSide.library.rename(metal.getChildFile("Old.t3kpreset"), "New");
  const juce::File v2 = friendSide.base.getChildFile("v2.t3klibrary");
  ASSERT_TRUE(friendSide.library.exportArchive(friendSide.library.ownDir(), v2));

  TempLibrary t;
  const juce::File theirs = t.library.importArchive(v1);
  EXPECT_EQ(theirs, t.root.getChildFile("awesomeuser"));
  // Their Presets came along as their library's Presets half.
  EXPECT_TRUE(theirs.getChildFile("Presets").getChildFile("Metal").getChildFile("Old.t3kpreset").existsAsFile());
  EXPECT_TRUE(t.presets.list().empty());

  EXPECT_EQ(t.library.importArchive(v2), theirs);
  EXPECT_FALSE(theirs.getChildFile("Presets").getChildFile("Metal").getChildFile("Old.t3kpreset").exists());
  EXPECT_TRUE(theirs.getChildFile("Presets").getChildFile("Metal").getChildFile("New.t3kpreset").existsAsFile());
}

// Every signed-out library is "My Library": a friend's export of theirs is
// told from your backup by the library id, lands beside yours, and never
// merges into it (or into your presets).
TEST(LibraryTest, AFriendsMyLibraryNeverMergesIntoYours) {
  TempLibrary friendSide;
  friendSide.library.setLocation(friendSide.root, "");
  friendSide.presets.saveInFolder(friendSide.presetsDir, "Their Lead", makePreset());
  const juce::File archive = friendSide.base.getChildFile("theirs.t3klibrary");
  ASSERT_TRUE(friendSide.library.exportArchive(friendSide.library.ownDir(), archive));

  TempLibrary t;
  t.library.setLocation(t.root, "");
  ASSERT_TRUE(t.library.libraryId(true).isNotEmpty());  // a library in use has its id
  const juce::File landed = t.library.importArchive(archive);
  ASSERT_NE(landed, juce::File());
  EXPECT_NE(landed, t.library.ownDir());
  EXPECT_EQ(landed.getFileName(), juce::String("My Library (imported)"));
  EXPECT_TRUE(t.presets.list().empty()) << "their presets stay in their library";
}

TEST(LibraryTest, ImportedFolderLandsBesideWhatIsThere) {
  TempLibrary t;
  const juce::File metal = t.library.createFolder(t.presetsDir, "Metal");
  t.presets.saveInFolder(metal, "Lead", makePreset());
  const juce::File archive = t.base.getChildFile("metal.t3klibrary");
  ASSERT_TRUE(t.library.exportArchive(metal, archive));

  const juce::File landed = t.library.importArchive(archive);
  EXPECT_EQ(landed, t.presetsDir.getChildFile("Metal 2"));  // back into the presets half
  EXPECT_TRUE(landed.getChildFile("Lead.t3kpreset").existsAsFile());

  // Someone else's folder lands in the matching half of their library.
  const juce::File clean = t.library.createFolder(t.library.capturesDir(), "Clean");
  clean.getChildFile("amp.nam").replaceWithText("{}");
  const juce::File cleanArchive = t.base.getChildFile("clean.t3klibrary");
  ASSERT_TRUE(t.library.exportArchive(clean, cleanArchive));
  t.library.setLocation(t.root, "someone else");
  const juce::File theirs = t.library.importArchive(cleanArchive);
  EXPECT_EQ(theirs, t.root.getChildFile("tonehound").getChildFile("Captures").getChildFile("Clean 2"));
}

// The library state (LibraryState.h) travels with an export: kept links and
// folder pictures land keyed where their files did, the pictures with them.
TEST(LibraryTest, ABackupBringsItsLibraryStateHome) {
  namespace ls = t3k::library_state;
  TempLibrary t;
  const juce::File amps = t.library.createFolder(t.library.capturesDir(), "Amps");
  const juce::File kept = t.library.createFolder(t.library.capturesDir(), "Kept");
  amps.getChildFile("Plexi.nam").replaceWithText(R"({"a":1})");
  kept.getChildFile("Plexi.nam").replaceWithText(R"({"a":1})");
  kept.getChildFile("Site.nam").replaceWithText(R"({"b":2})");
  const juce::File own = t.library.ownDir();
  ASSERT_TRUE(ls::picturesOf(own).createDirectory());
  ls::picturesOf(own).getChildFile("p.png").replaceWithText("png");
  ASSERT_TRUE(ls::write(own, juce::JSON::parse(R"({
    "kept": { "Captures/Kept/Plexi.nam": { "source": "Captures/Amps/Plexi.nam" },
              "Captures/Kept/Site.nam": { "tone": { "id": 5 }, "model": { "id": 7 } } },
    "pictures": { "captures/amps": "p.png" },
    "folders": { "captures": [ "Kept", "Amps" ] },
    "keep": "tonehound/Captures/Kept", "links": [ "D:/NAM" ] })")));
  const juce::File archive = t.base.getChildFile("backup.t3klibrary");
  ASSERT_TRUE(t.library.exportArchive(own, archive));

  // Lose it all, the state and pictures too, then restore.
  own.deleteRecursively();
  EXPECT_EQ(t.library.importArchive(archive), own);
  const auto back = ls::read(own);
  EXPECT_EQ(back["kept"]["Captures/Kept/Plexi.nam"]["source"].toString(), juce::String("Captures/Amps/Plexi.nam"));
  EXPECT_EQ(static_cast<int>(back["kept"]["Captures/Kept/Site.nam"]["model"]["id"]), 7);
  EXPECT_EQ(back["pictures"]["captures/amps"].toString(), juce::String("p.png"));
  EXPECT_TRUE(ls::picturesOf(own).getChildFile("p.png").existsAsFile());
  EXPECT_EQ(back["folders"]["captures"][0].toString(), juce::String("Kept")) << "the folder order";
  EXPECT_EQ(back["keep"].toString(), juce::String("tonehound/Captures/Kept"));
  EXPECT_EQ(back["links"][0].toString(), juce::String("D:/NAM"));
}

TEST(LibraryTest, AFoldersStateLandsWithItUnderItsNewName) {
  namespace ls = t3k::library_state;
  TempLibrary t;
  const juce::File amps = t.library.createFolder(t.library.capturesDir(), "Amps");
  amps.getChildFile("Plexi.nam").replaceWithText(R"({"a":1})");
  amps.getChildFile("Copy.nam").replaceWithText(R"({"a":1})");
  const juce::File own = t.library.ownDir();
  ASSERT_TRUE(ls::picturesOf(own).createDirectory());
  ls::picturesOf(own).getChildFile("p.png").replaceWithText("png");
  ASSERT_TRUE(ls::write(own, juce::JSON::parse(R"({
    "kept": { "Captures/Amps/Copy.nam": { "source": "Captures/Amps/Plexi.nam" },
              "Captures/Other.nam": { "tone": { "id": 5 }, "model": { "id": 7 } } },
    "pictures": { "captures/amps": "p.png" } })")));
  const juce::File archive = t.base.getChildFile("amps.t3klibrary");
  ASSERT_TRUE(t.library.exportArchive(amps, archive));

  const juce::File landed = t.library.importArchive(archive);
  ASSERT_EQ(landed, t.library.capturesDir().getChildFile("Amps 2"));
  const auto state = ls::read(own);
  EXPECT_EQ(state["kept"]["Captures/Amps 2/Copy.nam"]["source"].toString(), juce::String("Captures/Amps 2/Plexi.nam"));
  // Its picture as a file of its own: deleting one folder's never takes the other's.
  const auto copied = state["pictures"]["captures/amps 2"].toString();
  EXPECT_TRUE(copied.isNotEmpty() && copied != "p.png") << copied;
  EXPECT_TRUE(ls::picturesOf(own).getChildFile(copied).existsAsFile());
  EXPECT_EQ(state["pictures"]["captures/amps"].toString(), juce::String("p.png"));
  // What was there stays, and nothing from outside the folder came along.
  EXPECT_EQ(state["kept"]["Captures/Amps/Copy.nam"]["source"].toString(), juce::String("Captures/Amps/Plexi.nam"));
  EXPECT_EQ(state["kept"].getDynamicObject()->getProperties().size(), 3);
}

// Export for Sharing: no capture file goes out. TONE3000 ones go as one link
// per tone per folder (linked folders too, under Local), the rest stay home.
// A preset's local block keeps its bytes only when they are a capture
// downloaded from TONE3000 (its kept link's hash), without its paths; a
// folder match alone is a link, never a reason to ship bytes.
TEST(LibraryTest, ASharedLibraryHoldsLinksNotCaptures) {
  namespace ls = t3k::library_state;
  TempLibrary t;
  const juce::File captures = t.library.capturesDir();
  const juce::File paid = t.library.createFolder(captures, "Paid");
  const juce::File engl = t.library.createFolder(captures, "Engl");
  paid.getChildFile("Plexi.nam").replaceWithText(R"({"paid": 1})");
  engl.getChildFile("Lead 1.nam").replaceWithText(R"({"downloaded": 1})");
  engl.getChildFile("Lead 2.nam").replaceWithText(R"({"matched": 2})");
  const juce::File linked = t.base.getChildFile("Collection");
  linked.getChildFile("Savage").createDirectory();
  linked.getChildFile("Savage").getChildFile("D10.nam").replaceWithText("{}");
  linked.getChildFile("Mine.nam").replaceWithText("{}");
  t.library.setLocation(t.root, "tonehound", {linked});

  // A rig: a block per capture, its bytes cached as a preset holds them.
  const auto block = [](const juce::File& source) {
    juce::ValueTree b("ChainBlock");
    b.setProperty("type", "nam", nullptr);
    b.setProperty("toneJson", R"({"local":true,"models":[{"id":1,"source_path":")" +
                                  source.getFullPathName().replace("\\", "\\\\") + R"("}]})",
                  nullptr);
    juce::MemoryBlock bytes;
    source.loadFileAsData(bytes);
    juce::ValueTree cached("CachedModel");
    cached.setProperty("modelId", 1, nullptr);
    cached.setProperty("data", juce::var(bytes), nullptr);
    juce::ValueTree cache("ModelCache");
    cache.appendChild(cached, nullptr);
    b.appendChild(cache, nullptr);
    return b;
  };
  juce::ValueTree rig(PresetManager::kPresetTag);
  rig.setProperty("name", "Rig", nullptr);
  juce::ValueTree chain("Chain");
  chain.appendChild(block(paid.getChildFile("Plexi.nam")), nullptr);
  chain.appendChild(block(engl.getChildFile("Lead 1.nam")), nullptr);
  chain.appendChild(block(engl.getChildFile("Lead 2.nam")), nullptr);
  rig.appendChild(chain, nullptr);
  ASSERT_TRUE(t.presetsDir.createDirectory());
  ASSERT_TRUE(t3k::presetfile::write(t.presetsDir.getChildFile("Rig.t3kpreset"), rig));

  // Lead 1 was downloaded from TONE3000 (its link has the hash); Lead 2 and
  // Savage only sit in folders matched to tones.
  auto* refs = new juce::DynamicObject();
  auto downloaded = toneRef(9, 5, "ENGL Fireball", "Lead 1");
  juce::MemoryBlock lead1;
  engl.getChildFile("Lead 1.nam").loadFileAsData(lead1);
  downloaded.getDynamicObject()->setProperty("hash", ls::contentHash(lead1));
  refs->setProperty(juce::Identifier(engl.getChildFile("Lead 1.nam").getFullPathName()), downloaded);
  refs->setProperty(juce::Identifier(engl.getChildFile("Lead 2.nam").getFullPathName()), toneRef(9, 0, "ENGL Fireball", {}));
  refs->setProperty(juce::Identifier(linked.getChildFile("Savage").getChildFile("D10.nam").getFullPathName()),
                    toneRef(11, 0, "Savage Drive", {}));
  const juce::var siteRefs(refs);  // held: a var made from the pointer twice would free it
  const juce::File archive = t.base.getChildFile("shared.t3klibrary");
  const juce::var summary = t.library.shareArchive(t.library.ownDir(), archive, siteRefs);
  ASSERT_TRUE(summary.isObject());
  EXPECT_EQ(static_cast<int>(summary["links"]), 2) << "one per tone per folder";
  EXPECT_EQ(static_cast<int>(summary["leftOut"]), 2) << "the paid capture and the linked folder's own";
  EXPECT_EQ(static_cast<int>(summary["emptied"]), 2) << "the paid block and the matched-only one";

  juce::ZipFile zip(archive);
  juce::StringArray names;
  for (int i = 0; i < zip.getNumEntries(); ++i) names.add(zip.getEntry(i)->filename);
  for (const auto& name : names) EXPECT_FALSE(name.endsWithIgnoreCase(".nam")) << name;
  EXPECT_TRUE(names.contains("content/Captures/Engl/ENGL Fireball.t3ktone")) << names.joinIntoString(", ");
  EXPECT_TRUE(names.contains("content/Local/Collection/Savage/Savage Drive.t3ktone")) << names.joinIntoString(", ");
  juce::var manifest;
  if (std::unique_ptr<juce::InputStream> in{zip.createStreamForEntry(zip.getIndexOfFileName(LocalLibrary::kManifestName))})
    manifest = juce::JSON::parse(in->readEntireStreamAsString());
  EXPECT_TRUE(static_cast<bool>(manifest["shared"]));
  EXPECT_FALSE(manifest.hasProperty("library_id")) << "not a backup";

  // The rig: the paid and the matched-only blocks emptied, the downloaded
  // one playing, without the path it was loaded from.
  const juce::File unpacked = t.base.getChildFile("content/Presets/Rig.t3kpreset");
  ASSERT_TRUE(zip.uncompressEntry(zip.getIndexOfFileName("content/Presets/Rig.t3kpreset"), t.base).wasOk());
  const auto shared = t3k::presetfile::read(unpacked).getChildWithName("Chain");
  ASSERT_EQ(shared.getNumChildren(), 3);
  EXPECT_EQ(shared.getChild(0).getProperty("type").toString(), juce::String("insert"));
  EXPECT_EQ(shared.getChild(0).getNumChildren(), 0) << "its model bytes stayed home";
  EXPECT_EQ(shared.getChild(1).getProperty("type").toString(), juce::String("nam"));
  EXPECT_FALSE(shared.getChild(1).getProperty("toneJson").toString().contains("source_path")) << "no paths of yours";
  EXPECT_EQ(shared.getChild(2).getProperty("type").toString(), juce::String("insert")) << "a folder match ships no bytes";

  // Someone with your name imports it beside their own, never into it.
  TempLibrary other;
  const juce::File landed = other.library.importArchive(archive);
  EXPECT_NE(landed, other.library.ownDir());
  EXPECT_EQ(landed.getFileName(), juce::String("tonehound (imported)"));

  // Nothing that may go: no archive at all.
  const juce::File none = t.base.getChildFile("none.t3klibrary");
  const juce::var nothing = t.library.shareArchive(paid, none, siteRefs);
  EXPECT_TRUE(static_cast<bool>(nothing["empty"]));
  EXPECT_FALSE(none.exists());
}

TEST(LibraryTest, ImportRefusesEntriesOutsideTheLibrary) {
  TempLibrary t;
  const juce::File archive = t.base.getChildFile("evil.t3klibrary");
  {
    juce::ZipFile::Builder zip;
    const juce::String manifest = R"({"format":"t3klibrary","version":1,"kind":"folder","owner":"x"})";
    zip.addEntry(new juce::MemoryInputStream(manifest.toRawUTF8(), manifest.getNumBytesAsUTF8(), true), 0,
                 LocalLibrary::kManifestName, juce::Time::getCurrentTime());
    zip.addEntry(new juce::MemoryInputStream("x", 1, true), 0, "content/../../evil.nam",
                 juce::Time::getCurrentTime());
    juce::FileOutputStream out(archive);
    ASSERT_TRUE(zip.writeToStream(out, nullptr));
  }
  EXPECT_EQ(t.library.importArchive(archive), juce::File());
  EXPECT_FALSE(t.base.getChildFile("evil.nam").exists());
  EXPECT_FALSE(t.root.getChildFile("evil.nam").exists());
  // The staging folder is gone too.
  EXPECT_EQ(t.root.findChildFiles(juce::File::findDirectories, false).size(), 0);
}

// Processor glue

juce::String activeId(TONE3000Processor& proc) {
  return proc.getChainState(-1)["preset"]["id"].toString();
}
juce::String activeName(TONE3000Processor& proc) {
  return proc.getChainState(-1)["preset"]["name"].toString();
}

struct LibraryProcessor {
  LibraryProcessor() {
    proc.setPresetStoreForTesting(t.presetsDir);
    proc.setLibraryLocation(t.root, "tonehound");
  }
  TempLibrary t;
  TONE3000Processor proc;
};

TEST(LibraryProcessorTest, PrevNextStepsThroughTheActivePresetsFolder) {
  LibraryProcessor lp;
  auto& proc = lp.proc;
  proc.savePreset("Global A");
  proc.savePreset("Global B");
  const juce::File setlist = proc.libraryCreateFolder(lp.t.presetsDir, "Setlist");
  for (const char* song : {"01. Best song", "02. Next best song", "03. Closer"})
    ASSERT_TRUE(proc.savePresetToFolder(setlist, song).isObject());

  ASSERT_TRUE(proc.loadPreset(PresetManager::fileId(setlist.getChildFile("01. Best song.t3kpreset"))));
  ASSERT_TRUE(proc.stepPreset(1));
  EXPECT_EQ(activeName(proc), juce::String("02. Next best song"));
  ASSERT_TRUE(proc.stepPreset(2));  // wraps inside the folder
  EXPECT_EQ(activeName(proc), juce::String("01. Best song"));
  ASSERT_TRUE(proc.stepPreset(-1));
  EXPECT_EQ(activeName(proc), juce::String("03. Closer"));

  // A list preset still steps the list (and program changes never see the
  // setlist).
  ASSERT_TRUE(proc.loadPreset(lp.t.presets.list().front().id));
  EXPECT_EQ(activeName(proc), juce::String("Global A"));
  ASSERT_TRUE(proc.stepPreset(1));
  EXPECT_EQ(activeName(proc), juce::String("Global B"));
}

// A big linked collection on a slow drive takes seconds to walk cold: the
// listing is saved, the next session shows it at once, and the walk then
// replaces it. Unchanged listings aren't rewritten.
TEST(LibraryProcessorTest, TheNextSessionShowsTheSavedListingFirst) {
  LibraryProcessor lp;
  auto& proc = lp.proc;
  const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("t3k-test-library-listing.cache");  // the test build's
  file.deleteFile();
  TONE3000Processor::forgetLibraryScanForTesting();
  EXPECT_FALSE(proc.getSavedLibrary().isObject()) << "nothing saved yet";

  proc.libraryCreateFolder(lp.t.library.capturesDir(), "Clean");
  const juce::var scanned = proc.getLibrary();
  EXPECT_FALSE(scanned.hasProperty("rescanned")) << "bookkeeping stays inside";
  ASSERT_TRUE(file.existsAsFile());
  // This session's own listing now: the one it scanned, shown at once.
  EXPECT_EQ(juce::JSON::toString(proc.getSavedLibrary(), true), juce::JSON::toString(scanned, true));

  const auto written = file.getLastModificationTime();
  juce::Thread::sleep(50);
  proc.getLibrary();
  EXPECT_EQ(file.getLastModificationTime(), written) << "nothing changed: not rewritten";

  TONE3000Processor::forgetLibraryScanForTesting();  // the next session
  const juce::var saved = proc.getSavedLibrary();
  ASSERT_TRUE(saved.isObject());
  EXPECT_EQ(juce::JSON::toString(saved, true), juce::JSON::toString(scanned, true));

  proc.setLibraryLocation(lp.t.root, "someone else");  // another location's listing isn't this one's
  EXPECT_FALSE(proc.getSavedLibrary().isObject());
  file.deleteFile();
}

// The drawer's view rides the instance's state: a reopened project has it.
TEST(LibraryProcessorTest, TheDrawerViewIsSavedWithTheProject) {
  TONE3000Processor saved;
  saved.setLibraryView(R"({"open":["/L/me/Captures"],"scroll":140,"shown":true})");
  juce::MemoryBlock state;
  saved.getStateInformation(state);

  TONE3000Processor reopened;
  reopened.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  EXPECT_EQ(reopened.getLibraryView(), saved.getLibraryView());
}

TEST(LibraryProcessorTest, RigsSaveOnlyIntoPresetFolders) {
  LibraryProcessor lp;
  auto& proc = lp.proc;
  const juce::File linked = lp.t.base.getChildFile("NAM Captures");
  linked.createDirectory();
  proc.setLibraryLocation(lp.t.root, "tonehound", {linked});
  const juce::File clean = proc.libraryCreateFolder(lp.t.library.capturesDir(), "Clean");
  EXPECT_FALSE(proc.savePresetToFolder(clean, "Rig").isObject());
  EXPECT_FALSE(proc.savePresetToFolder(linked, "Rig").isObject());
  EXPECT_FALSE(proc.savePresetToFolder(lp.t.library.capturesDir(), "Rig").isObject());
  EXPECT_TRUE(proc.savePresetToFolder(lp.t.presetsDir, "Rig").isObject());
}

// Keeping a TONE3000 tone's capture: the bytes the block plays, as a
// capture file (a legal name, the same one again handed back).
TEST(LibraryProcessorTest, KeepModelWritesThePlayingModelAsACapture) {
  LibraryProcessor lp;
  auto& proc = lp.proc;
  const juce::File source = lp.t.base.getChildFile("Loose").getChildFile("Gain 1.nam");
  ASSERT_TRUE(source.getParentDirectory().createDirectory().wasOk());
  ASSERT_TRUE(testFile("a2-amp-test.nam").copyFileTo(source));
  const juce::var res = proc.loadLocalTonePath(source.getParentDirectory());
  ASSERT_TRUE(res["error"].isVoid());
  ASSERT_TRUE(waitForChainLoaded(proc));
  const auto blockId = res["blockId"].toString().toStdString();

  const juce::File keepers = proc.libraryCreateFolder(lp.t.library.capturesDir(), "Keepers");
  const juce::File kept = proc.libraryKeepModel(blockId, keepers, "Vox AC30/6 - Brl 3");
  ASSERT_TRUE(kept.existsAsFile());
  EXPECT_EQ(kept.getParentDirectory(), keepers);
  EXPECT_EQ(kept.getFileName(), juce::String("Vox AC30-6 - Brl 3.nam"));
  EXPECT_TRUE(kept.hasIdenticalContentTo(source));
  EXPECT_EQ(proc.libraryKeepModel(blockId, keepers, "Vox AC30/6 - Brl 3"), kept) << "the same one, not a second";
  EXPECT_EQ(proc.libraryKeepModel("no-such-block", keepers, "x"), juce::File());
}

// Saving while the active preset sits in a folder that takes no presets
// (an imported library is read-only; here a captures folder) saves to the
// user presets folder, as before the Library, instead of failing.
TEST(LibraryProcessorTest, SaveFallsBackWhenTheActivePresetsFolderTakesNone) {
  LibraryProcessor lp;
  auto& proc = lp.proc;
  ASSERT_TRUE(proc.savePreset("Base").isObject());
  const juce::File base = lp.t.presetsDir.getChildFile("Base.t3kpreset");
  ASSERT_TRUE(base.existsAsFile());
  const juce::File elsewhere = lp.t.library.capturesDir().getChildFile("Song.t3kpreset");
  ASSERT_TRUE(elsewhere.getParentDirectory().createDirectory().wasOk());
  ASSERT_TRUE(base.copyFileTo(elsewhere));
  ASSERT_FALSE(lp.t.library.takesPresets(elsewhere.getParentDirectory()));
  ASSERT_TRUE(proc.loadPreset(PresetManager::fileId(elsewhere)));
  EXPECT_TRUE(proc.savePreset("Tweaked").isObject());
  EXPECT_TRUE(lp.t.presetsDir.getChildFile("Tweaked.t3kpreset").existsAsFile());
}

TEST(LibraryProcessorTest, SavingALibraryPresetSavesIntoItsFolder) {
  LibraryProcessor lp;
  auto& proc = lp.proc;
  const juce::File setlist = proc.libraryCreateFolder(lp.t.presetsDir, "Setlist");
  proc.savePresetToFolder(setlist, "Song");
  const juce::String id = activeId(proc);
  EXPECT_TRUE(PresetManager::isFileId(id));

  // The save popover's path: same name updates in place, a new name lands
  // beside it, and the user folder gets nothing.
  EXPECT_EQ(proc.savePreset("Song")["id"].toString(), id);
  proc.savePreset("Song (live)");
  EXPECT_EQ(lp.t.presets.listFolder(setlist).size(), 2u);
  EXPECT_TRUE(lp.t.presets.list().empty());  // the list is the Presets folder's top only
  // Writing into someone else's library is refused.
  const juce::File theirs = lp.t.root.getChildFile("awesomeuser");
  theirs.createDirectory();
  EXPECT_FALSE(proc.savePresetToFolder(theirs, "Nope").isObject());
}

TEST(LibraryProcessorTest, ActivePresetFollowsRenamesMovesAndRemoves) {
  LibraryProcessor lp;
  auto& proc = lp.proc;
  const juce::File metal = proc.libraryCreateFolder(lp.t.presetsDir, "Metal");
  const juce::File archive = proc.libraryCreateFolder(lp.t.presetsDir, "Archive");
  proc.savePresetToFolder(metal, "Lead");

  const juce::File renamed = proc.libraryRename(metal.getChildFile("Lead.t3kpreset"), "Solo");
  EXPECT_EQ(activeId(proc), PresetManager::fileId(renamed));
  EXPECT_EQ(activeName(proc), juce::String("Solo"));

  // Moving the folder above it re-points the path.
  const juce::File movedFolder = proc.libraryMove(metal, archive);
  EXPECT_EQ(activeId(proc), PresetManager::fileId(movedFolder.getChildFile("Solo.t3kpreset")));

  // Into the user folder it becomes a list preset with a "user:" id.
  proc.libraryMove(movedFolder.getChildFile("Solo.t3kpreset"), lp.t.presetsDir);
  ASSERT_EQ(lp.t.presets.list().size(), 1u);
  EXPECT_EQ(activeId(proc), lp.t.presets.list().front().id);

  // Gone: no active preset, but the chain stays as it is.
  ASSERT_TRUE(proc.libraryRemove(lp.t.presetsDir.getChildFile("Solo.t3kpreset")));
  EXPECT_TRUE(activeId(proc).isEmpty());
}

// A block plays its file in place: a Library rename or move of the folder
// takes the block along (it names the file where it went), and a load of the
// old path (an undo bringing a removed block back) finds the file there too.
TEST(LibraryProcessorTest, BlocksFollowTheirFilesThroughRenamesAndMoves) {
  LibraryProcessor lp;
  auto& proc = lp.proc;
  const juce::File amps = proc.libraryCreateFolder(lp.t.library.capturesDir(), "Amps");
  const juce::File capture = amps.getChildFile("Gain 1.nam");
  ASSERT_TRUE(testFile("a2-amp-test.nam").copyFileTo(capture));
  const juce::var res = proc.loadLocalTonePath(capture);
  ASSERT_TRUE(res["error"].isVoid());
  ASSERT_TRUE(waitForChainLoaded(proc));
  const auto blockId = res["blockId"].toString().toStdString();
  const auto playing = [&proc]() -> juce::var {
    const juce::var state = proc.getChainState(-1);  // held: the lane points into it
    if (const auto* lane = state["chain"].getArray())
      for (const auto& item : *lane)
        if (item["kind"].toString() == "tone") return item["tone"]["models"][0];
    return {};
  };
  const auto oldUrl = juce::URL(capture).toString(false);
  // The id a model gets from its bytes is the Library's, which finds a
  // moved file by it (LibraryStore::findMovedFiles).
  {
    juce::MemoryBlock bytes;
    ASSERT_TRUE(capture.loadFileAsData(bytes));
    EXPECT_EQ(static_cast<int>(playing()["id"]), t3k::library_state::localModelId(bytes.getData(), bytes.getSize()));
    // The value itself, as every build has made it (sessions and presets
    // store these ids): the same file, the same id, whatever changes here.
    EXPECT_EQ(static_cast<int>(playing()["id"]), 2130798715);
  }
  // Wearing a folder picture from the same folder (as from a library's
  // .t3kpictures): it moves along.
  const juce::File picture = amps.getChildFile("cover.png");
  ASSERT_TRUE(picture.replaceWithText("png"));
  auto* art = new juce::DynamicObject();
  art->setProperty("image", juce::URL(picture).toString(false));
  ASSERT_TRUE(proc.setLocalToneArt(blockId, juce::var(art)));
  const auto image = [&proc]() -> juce::String {
    const juce::var state = proc.getChainState(-1);
    if (const auto* lane = state["chain"].getArray())
      for (const auto& item : *lane)
        if (item["kind"].toString() == "tone") return item["tone"]["images"][0].toString();
    return {};
  };

  const juce::File renamed = proc.libraryRename(amps, "Amps 2");
  EXPECT_EQ(juce::URL(image()).getLocalFile(), renamed.getChildFile("cover.png"));
  ASSERT_TRUE(renamed.isDirectory());
  const juce::File there = renamed.getChildFile("Gain 1.nam");
  EXPECT_EQ(playing()["source_path"].toString(), there.getFullPathName());
  EXPECT_EQ(juce::URL(playing()["model_url"].toString()).getLocalFile(), there);
  EXPECT_EQ(TONE3000Processor::resolveLocalModelFile(lp.t.base, oldUrl), there);

  // Moved again (into another folder): followed through both.
  const juce::File archive = proc.libraryCreateFolder(lp.t.library.capturesDir(), "Archive");
  const juce::File moved = proc.libraryMove(renamed, archive);
  const juce::File now = moved.getChildFile("Gain 1.nam");
  ASSERT_TRUE(now.existsAsFile());
  EXPECT_EQ(playing()["source_path"].toString(), now.getFullPathName());
  EXPECT_EQ(TONE3000Processor::resolveLocalModelFile(lp.t.base, oldUrl), now);

  // A removed block brought back by undo after another rename still loads.
  ASSERT_TRUE(proc.removeChainBlock(blockId));
  const juce::File last = proc.libraryRename(moved, "Amps 3");
  ASSERT_TRUE(last.isDirectory());
  ASSERT_TRUE(proc.undoChain());
  EXPECT_TRUE(waitForChainLoaded(proc));
  EXPECT_EQ(TONE3000Processor::resolveLocalModelFile(lp.t.base, oldUrl), last.getChildFile("Gain 1.nam"));

  // A file that never moved, and one whose folder only shares a name prefix,
  // are left alone.
  const juce::File other = lp.t.base.getChildFile("Amps 22").getChildFile("x.nam");
  EXPECT_EQ(TONE3000Processor::resolveLocalModelFile(lp.t.base, juce::URL(other).toString(false)), other);
}

}  // namespace
