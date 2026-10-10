// Local file loading (loadLocalTone for drops, loadLocalTonePath for the
// tile menus' file pickers): load-time validation (NAM files must be A2),
// and that accepted files ride the normal load pipeline as `local` blocks:
// background load from the content-addressed stash (no network), IR mix
// defaults by kernel length, folder loads as one multi-model tone whose
// full model list survives switches.
//
// Note: both entry points write their stash into the real app-data
// LocalModels folder. The names are content hashes, so repeated runs reuse
// the same few small files (and the week-based GC clears them eventually).

#include "chain_test_helpers.h"

#include <gtest/gtest.h>

#include <cmath>

#if !JUCE_WINDOWS
#include <sys/stat.h>  // chmod, for the unwritable-directory heal test
#include <unistd.h>    // geteuid: root ignores permission bits, so that leg skips
#endif

namespace {

// fileEntry / testFileEntry / filesOf (the { name, data } entries the UI
// ships) live in chain_test_helpers.h.

// First tone block of the (mono) chain, or void when none.
juce::var firstToneBlock(TONE3000Processor& proc) {
  const juce::var state = proc.getChainState(-1);
  if (const auto* lane = state["chain"].getArray())
    for (const auto& item : *lane)
      if (item["kind"].toString() == "tone")
        return item;
  return {};
}

}  // namespace

TEST(LocalLoadTest, A2NamFileLoadsAsLocalBlock) {
  TONE3000Processor proc;
  const juce::var res =
      proc.loadLocalTone("a2-amp-test", filesOf({testFileEntry("a2-amp-test.nam")}));
  EXPECT_TRUE(res["error"].isVoid()) << res["error"].toString().toStdString();
  ASSERT_TRUE(res["blockId"].toString().isNotEmpty());
  ASSERT_TRUE(waitForChainLoaded(proc));

  const juce::var block = firstToneBlock(proc);
  EXPECT_EQ(block["blockId"].toString(), res["blockId"].toString());
  EXPECT_TRUE(static_cast<bool>(block["tone"]["local"]));
  EXPECT_EQ(block["tone"]["format"].toString(), juce::String("nam"));
  EXPECT_EQ(block["tone"]["title"].toString(), juce::String("a2-amp-test"));
  // Model names drop the extension too.
  ASSERT_EQ(block["tone"]["models"].size(), 1);
  EXPECT_EQ(block["tone"]["models"][0]["name"].toString(), juce::String("a2-amp-test"));
}

TEST(LocalLoadTest, FolderLoadsAsOneTonePerFileModelsSurviveSwitch) {
  TONE3000Processor proc;
  const juce::var res = proc.loadLocalTone(
      "My Pack", filesOf({testFileEntry("a2-amp-test.nam"), testFileEntry("a2-amp-cab-test.nam")}));
  EXPECT_TRUE(res["error"].isVoid()) << res["error"].toString().toStdString();
  ASSERT_TRUE(waitForChainLoaded(proc));

  juce::var block = firstToneBlock(proc);
  EXPECT_EQ(block["tone"]["title"].toString(), juce::String("My Pack"));
  ASSERT_EQ(block["tone"]["models"].size(), 2);
  // Local summary models carry their stash URL: it's what the picker's
  // switch call sends back down.
  const juce::var second = block["tone"]["models"][1];
  ASSERT_TRUE(second["model_url"].toString().startsWith("file://"));

  ASSERT_TRUE(proc.switchModel(block["blockId"].toString().toStdString(),
                               static_cast<int>(second["id"]), second));
  ASSERT_TRUE(waitForChainLoaded(proc));

  // The switch keeps the whole local model list (catalog tones would prune
  // to the picked model here).
  block = firstToneBlock(proc);
  EXPECT_EQ(static_cast<int>(block["activeModelId"]), static_cast<int>(second["id"]));
  EXPECT_EQ(block["tone"]["models"].size(), 2);
}

TEST(LocalLoadTest, DropOnExistingToneBlockSwapsInPlace) {
  TONE3000Processor proc;
  const juce::var first =
      proc.loadLocalTone("a2-amp-test", filesOf({testFileEntry("a2-amp-test.nam")}));
  ASSERT_TRUE(first["blockId"].toString().isNotEmpty());
  ASSERT_TRUE(waitForChainLoaded(proc));
  const juce::String blockId = first["blockId"].toString();

  const juce::var swapped = proc.loadLocalTone(
      "cab-ir-test", filesOf({testFileEntry("cab-ir-test.wav")}), blockId.toStdString());
  EXPECT_TRUE(swapped["error"].isVoid()) << swapped["error"].toString().toStdString();
  EXPECT_EQ(swapped["blockId"].toString(), blockId);
  ASSERT_TRUE(waitForChainLoaded(proc));

  const juce::var block = firstToneBlock(proc);
  EXPECT_EQ(block["blockId"].toString(), blockId);
  EXPECT_EQ(block["tone"]["title"].toString(), juce::String("cab-ir-test"));
  EXPECT_EQ(block["tone"]["format"].toString(), juce::String("ir"));

  // Still a single tone block (didn't insert a second).
  const juce::var state = proc.getChainState(-1);
  int tones = 0;
  if (const auto* lane = state["chain"].getArray())
    for (const auto& item : *lane)
      if (item["kind"].toString() == "tone")
        ++tones;
  EXPECT_EQ(tones, 1);
}

TEST(LocalLoadTest, IrMixDefaultsFollowKernelLength) {
  // Short (cab) IR: fully wet by default.
  {
    TONE3000Processor proc;
    proc.loadLocalTone("cab-ir-test", filesOf({testFileEntry("cab-ir-test.wav")}));
    ASSERT_TRUE(waitForChainLoaded(proc));
    const juce::var block = firstToneBlock(proc);
    EXPECT_FALSE(static_cast<bool>(block["irLong"]));
    EXPECT_FLOAT_EQ(static_cast<float>(block["params"]["mix"]), 1.0f);
  }
  // Long (reverb) IR: half wet by default, same as a Select-flow load.
  {
    TONE3000Processor proc;
    proc.loadLocalTone("reverb-ir-mono-test", filesOf({testFileEntry("reverb-ir-mono-test.wav")}));
    ASSERT_TRUE(waitForChainLoaded(proc));
    const juce::var block = firstToneBlock(proc);
    EXPECT_TRUE(static_cast<bool>(block["irLong"]));
    EXPECT_FLOAT_EQ(static_cast<float>(block["params"]["mix"]), 0.5f);
  }
}

// The tile's glyph: a local tone carries the catalog `gear` id when native
// could infer one from the file, and no `gear` at all otherwise (the UI then
// keeps its generic file icon). Folders take the first file's answer.
TEST(LocalLoadTest, GearIsInferredFromNamMetadataAndIrLength) {
  const auto gearOf = [](const juce::String& title, const juce::Array<juce::var>& files) {
    TONE3000Processor proc;
    const juce::var res = proc.loadLocalTone(title, filesOf(files));
    EXPECT_TRUE(res["error"].isVoid()) << res["error"].toString().toStdString();
    EXPECT_TRUE(waitForChainLoaded(proc));
    return firstToneBlock(proc)["tone"]["gear"];
  };

  // metadata.gear_type spellings map onto the catalog ids.
  EXPECT_EQ(gearOf("amp", {testFileEntry("a2-am-test-2.nam")}).toString(), juce::String("amp"));
  EXPECT_EQ(gearOf("amp cab", {testFileEntry("a2-amp-cab-test.nam")}).toString(),
            juce::String("amp-cab"));
  // No metadata: no gear (not an amp by default).
  EXPECT_TRUE(gearOf("plain", {testFileEntry("a2-amp-test.nam")}).isVoid());

  // Unknown free text stays unknown: the real A2 file with a made-up type.
  juce::var odd = juce::JSON::parse(testFile("a2-amp-test.nam"));
  juce::DynamicObject::Ptr metadata = new juce::DynamicObject();
  metadata->setProperty("gear_type", "synth");
  odd.getDynamicObject()->setProperty("metadata", juce::var(metadata.get()));
  const juce::String oddJson = juce::JSON::toString(odd);
  EXPECT_TRUE(gearOf("odd", {fileEntry("odd.nam", juce::Base64::toBase64(
                                                       oddJson.toRawUTF8(),
                                                       oddJson.getNumBytesAsUTF8()))})
                  .isVoid());

  // IRs: cab-length kernels are cabs, reverbs stay generic.
  EXPECT_EQ(gearOf("cab", {testFileEntry("cab-ir-test.wav")}).toString(), juce::String("cab"));
  EXPECT_TRUE(gearOf("verb", {testFileEntry("reverb-ir-mono-test.wav")}).isVoid());

  // Folder: the first file decides.
  EXPECT_EQ(gearOf("pack", {testFileEntry("a2-amp-cab-test.nam"), testFileEntry("a2-am-test-2.nam")})
                .toString(),
            juce::String("amp-cab"));
}

TEST(LocalLoadTest, RejectsBadFilesAndSkipsThemInFolders) {
  TONE3000Processor proc;

  // Valid JSON, wrong architecture: rejected at drop time (never reaches
  // the background loader, whose failure UI would suggest retrying).
  const juce::String lstm = R"({"version":"0.5.4","architecture":"LSTM","config":{}})";
  const juce::String lstm64 = juce::Base64::toBase64(lstm.toRawUTF8(), lstm.getNumBytesAsUTF8());
  juce::var res = proc.loadLocalTone("model", filesOf({fileEntry("model.nam", lstm64)}));
  EXPECT_EQ(res["error"].toString(), juce::String("Only A2 NAM files are supported"));

  res = proc.loadLocalTone("bad", filesOf({fileEntry("bad.wav", juce::Base64::toBase64("x", 1))}));
  EXPECT_EQ(res["error"].toString(), juce::String("Not a valid WAV file"));

  res = proc.loadLocalTone("notes",
                           filesOf({fileEntry("notes.txt", juce::Base64::toBase64("hi", 2))}));
  EXPECT_EQ(res["error"].toString(), juce::String("Only .nam and .wav files are supported"));

  // A rejected drop must not leave a block behind.
  EXPECT_TRUE(firstToneBlock(proc).isVoid());

  // A folder with one bad file still loads the good ones.
  res = proc.loadLocalTone(
      "Mixed Pack", filesOf({fileEntry("model.nam", lstm64), testFileEntry("a2-amp-test.nam")}));
  EXPECT_TRUE(res["error"].isVoid()) << res["error"].toString().toStdString();
  ASSERT_TRUE(waitForChainLoaded(proc));
  EXPECT_EQ(firstToneBlock(proc)["tone"]["models"].size(), 1);
}

// loadLocalTonePath: the tile menus' Load File / Load Folder pickers. Same
// pipeline as loadLocalTone but fed from disk paths (native FileChooser
// results); the folder rules (majority extension, natural order, title from
// the folder name) live natively here instead of in the web UI.

TEST(LocalLoadTest, PathLoadsSingleFileAndSwapsInPlace) {
  TONE3000Processor proc;
  const juce::var res = proc.loadLocalTonePath(testFile("a2-amp-test.nam"));
  EXPECT_TRUE(res["error"].isVoid()) << res["error"].toString().toStdString();
  ASSERT_TRUE(res["blockId"].toString().isNotEmpty());
  ASSERT_TRUE(waitForChainLoaded(proc));

  juce::var block = firstToneBlock(proc);
  EXPECT_TRUE(static_cast<bool>(block["tone"]["local"]));
  EXPECT_EQ(block["tone"]["format"].toString(), juce::String("nam"));
  EXPECT_EQ(block["tone"]["title"].toString(), juce::String("a2-amp-test"));

  // Targeting an existing tone block replaces in place, like a drop on a tile.
  const juce::String blockId = res["blockId"].toString();
  const juce::var swapped =
      proc.loadLocalTonePath(testFile("cab-ir-test.wav"), blockId.toStdString());
  EXPECT_EQ(swapped["blockId"].toString(), blockId);
  ASSERT_TRUE(waitForChainLoaded(proc));
  block = firstToneBlock(proc);
  EXPECT_EQ(block["blockId"].toString(), blockId);
  EXPECT_EQ(block["tone"]["format"].toString(), juce::String("ir"));
  EXPECT_EQ(block["tone"]["title"].toString(), juce::String("cab-ir-test"));
}

TEST(LocalLoadTest, PathLoadsFolderMajorityExtensionInNaturalOrder) {
  // Scratch folder: three distinct .nam files whose natural order differs
  // from lexicographic ("amp 10" sorts before "amp 2" there), one of them in
  // a subfolder (recursion), plus a minority .wav and a stray .txt (ignored).
  const juce::File dir =
      juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("t3k-pack-test");
  dir.deleteRecursively();
  ASSERT_TRUE(dir.getChildFile("More").createDirectory());
  ASSERT_TRUE(testFile("a2-amp-test.nam").copyFileTo(dir.getChildFile("amp 2.nam")));
  ASSERT_TRUE(testFile("a2-amp-cab-test.nam").copyFileTo(dir.getChildFile("amp 10.nam")));
  ASSERT_TRUE(
      testFile("a2-am-test-2.nam").copyFileTo(dir.getChildFile("More").getChildFile("amp 1.nam")));
  ASSERT_TRUE(testFile("cab-ir-test.wav").copyFileTo(dir.getChildFile("cab.wav")));
  ASSERT_TRUE(dir.getChildFile("notes.txt").replaceWithText("hi"));

  TONE3000Processor proc;
  const juce::var res = proc.loadLocalTonePath(dir);
  EXPECT_TRUE(res["error"].isVoid()) << res["error"].toString().toStdString();
  ASSERT_TRUE(waitForChainLoaded(proc));

  const juce::var block = firstToneBlock(proc);
  EXPECT_EQ(block["tone"]["title"].toString(), juce::String("t3k-pack-test"));
  EXPECT_EQ(block["tone"]["format"].toString(), juce::String("nam"));
  ASSERT_EQ(block["tone"]["models"].size(), 3)
      << "models: " << juce::JSON::toString(block["tone"]["models"]).toStdString();
  EXPECT_EQ(block["tone"]["models"][0]["name"].toString(), juce::String("amp 1"));
  EXPECT_EQ(block["tone"]["models"][1]["name"].toString(), juce::String("amp 2"));
  EXPECT_EQ(block["tone"]["models"][2]["name"].toString(), juce::String("amp 10"));

  dir.deleteRecursively();
}

TEST(LocalLoadTest, CaptureInFolderLoadsItsSiblingsStartingOnIt) {
  // A capture picked in the Library: its folder's files of the same kind
  // (no subfolders, no other extension) as one block, starting on it.
  const juce::File dir =
      juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("t3k-infolder-test");
  dir.deleteRecursively();
  ASSERT_TRUE(dir.getChildFile("Sub").createDirectory());
  ASSERT_TRUE(testFile("a2-amp-test.nam").copyFileTo(dir.getChildFile("gain 2.nam")));
  ASSERT_TRUE(testFile("a2-amp-cab-test.nam").copyFileTo(dir.getChildFile("gain 10.nam")));
  ASSERT_TRUE(testFile("a2-am-test-2.nam").copyFileTo(dir.getChildFile("Sub").getChildFile("other.nam")));
  ASSERT_TRUE(testFile("cab-ir-test.wav").copyFileTo(dir.getChildFile("cab.wav")));

  TONE3000Processor proc;
  const juce::var res = proc.loadLocalToneInFolder(dir.getChildFile("gain 10.nam"));
  EXPECT_TRUE(res["error"].isVoid()) << res["error"].toString().toStdString();
  ASSERT_TRUE(waitForChainLoaded(proc));

  const juce::var block = firstToneBlock(proc);
  EXPECT_EQ(block["tone"]["title"].toString(), juce::String("t3k-infolder-test"));
  ASSERT_EQ(block["tone"]["models"].size(), 2)
      << "models: " << juce::JSON::toString(block["tone"]["models"]).toStdString();
  // The picker's natural order is kept; the picked capture is the active one.
  EXPECT_EQ(block["tone"]["models"][0]["name"].toString(), juce::String("gain 2"));
  EXPECT_EQ(block["tone"]["models"][1]["name"].toString(), juce::String("gain 10"));
  EXPECT_EQ(static_cast<int>(block["activeModelId"]), static_cast<int>(block["tone"]["models"][1]["id"]));
  // Each model remembers the file it came from (the Library's kept links).
  EXPECT_EQ(block["tone"]["models"][1]["source_path"].toString(),
            dir.getChildFile("gain 10.nam").getFullPathName());
  // The hint is used once, never persisted with the tone.
  EXPECT_FALSE(block["tone"].hasProperty("active_model_id"));

  // Alone in its folder (by kind): a plain one-file block.
  TONE3000Processor single;
  ASSERT_TRUE(single.loadLocalToneInFolder(dir.getChildFile("cab.wav"))["error"].isVoid());
  ASSERT_TRUE(waitForChainLoaded(single));
  EXPECT_EQ(firstToneBlock(single)["tone"]["title"].toString(), juce::String("cab"));
  EXPECT_EQ(firstToneBlock(single)["tone"]["models"].size(), 1);

  dir.deleteRecursively();
}

// Natural order by name, extension aside: "4" before "4.5" before "10".
TEST(LocalLoadTest, FolderModelsSortByNameWithoutTheExtension) {
  const juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("t3k-natural-order");
  dir.deleteRecursively();
  ASSERT_TRUE(dir.createDirectory().wasOk());
  // Different files: identical bytes would share an id and drop all but one.
  ASSERT_TRUE(testFile("a2-amp-test.nam").copyFileTo(dir.getChildFile("Gain 4.5.nam")));
  ASSERT_TRUE(testFile("a2-amp-cab-test.nam").copyFileTo(dir.getChildFile("Gain 10.nam")));
  ASSERT_TRUE(testFile("a2-am-test-2.nam").copyFileTo(dir.getChildFile("Gain 4.nam")));
  TONE3000Processor proc;
  const juce::var res = proc.loadLocalTonePath(dir);
  ASSERT_TRUE(res["error"].isVoid()) << res["error"].toString().toStdString();
  const juce::var models = firstToneBlock(proc)["tone"]["models"];
  juce::StringArray names;
  for (int i = 0; i < models.size(); ++i) names.add(models[i]["name"].toString());
  EXPECT_EQ(names.joinIntoString("|"), juce::String("Gain 4|Gain 4.5|Gain 10"));
  dir.deleteRecursively();
}

TEST(LocalLoadTest, LocalToneArtAddsTheMatchedPhotoAndCreator) {
  TONE3000Processor proc;
  const juce::var res = proc.loadLocalTonePath(testFile("a2-amp-test.nam"));
  ASSERT_TRUE(res["error"].isVoid());
  ASSERT_TRUE(waitForChainLoaded(proc));
  const std::string blockId = res["blockId"].toString().toStdString();

  juce::DynamicObject::Ptr art = new juce::DynamicObject();
  art->setProperty("image", "https://img.example/amp.jpg");
  art->setProperty("username", "83ennui");
  art->setProperty("url", "https://www.tone3000.com/tones/42");
  EXPECT_TRUE(proc.setLocalToneArt(blockId, juce::var(art.get())));
  EXPECT_FALSE(proc.setLocalToneArt(blockId, juce::var(art.get())));  // no change the second time

  const juce::var tone = firstToneBlock(proc)["tone"];
  EXPECT_EQ(tone["images"][0].toString(), juce::String("https://img.example/amp.jpg"));
  EXPECT_EQ(tone["user"]["username"].toString(), juce::String("83ennui"));
  EXPECT_TRUE(static_cast<bool>(tone["local"]));  // still the local files
  EXPECT_EQ(tone["title"].toString(), juce::String("a2-amp-test"));

  EXPECT_FALSE(proc.setLocalToneArt("no-such-block", juce::var(art.get())));

  // A kept copy shows its original's folder: retitled, the old artwork gone.
  juce::DynamicObject::Ptr look = new juce::DynamicObject();
  look->setProperty("block_title", "Fuzz Face");
  look->setProperty("clear", true);
  EXPECT_TRUE(proc.setLocalToneArt(blockId, juce::var(look.get())));
  const juce::var shown = firstToneBlock(proc)["tone"];
  EXPECT_EQ(shown["title"].toString(), juce::String("Fuzz Face"));
  EXPECT_EQ(shown["images"].size(), 0);  // the summary always carries the key
  EXPECT_TRUE(shown["user"]["username"].toString().isEmpty());
  EXPECT_FALSE(proc.setLocalToneArt(blockId, juce::var(look.get())));  // nothing left to change

  // The matched tone's gear wins over the file's metadata.
  juce::DynamicObject::Ptr matched = new juce::DynamicObject();
  matched->setProperty("image", "https://img.example/head.jpg");
  matched->setProperty("gear", "amp");
  EXPECT_TRUE(proc.setLocalToneArt(blockId, juce::var(matched.get())));
  EXPECT_EQ(firstToneBlock(proc)["tone"]["gear"].toString(), juce::String("amp"));

  // Gear alone (a file name's say, nothing else changing) still lands.
  juce::DynamicObject::Ptr gearOnly = new juce::DynamicObject();
  gearOnly->setProperty("gear", "pedal");
  EXPECT_TRUE(proc.setLocalToneArt(blockId, juce::var(gearOnly.get())));
  EXPECT_EQ(firstToneBlock(proc)["tone"]["gear"].toString(), juce::String("pedal"));
}

TEST(LocalLoadTest, PathRejectsBadInputs) {
  TONE3000Processor proc;

  juce::var res = proc.loadLocalTonePath(juce::File("/nonexistent-t3k/missing.nam"));
  EXPECT_EQ(res["error"].toString(), juce::String("Couldn't read the file"));

  const juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("t3k-pack-reject-test");
  dir.deleteRecursively();
  ASSERT_TRUE(dir.createDirectory().wasOk());
  ASSERT_TRUE(dir.getChildFile("notes.txt").replaceWithText("hi"));

  res = proc.loadLocalTonePath(dir.getChildFile("notes.txt"));
  EXPECT_EQ(res["error"].toString(), juce::String("Only .nam and .wav files are supported"));

  // A folder whose only contents are unsupported types has nothing to load.
  res = proc.loadLocalTonePath(dir);
  EXPECT_EQ(res["error"].toString(), juce::String("No .nam or .wav files in the folder"));

  // Rejected loads must not leave a block behind.
  EXPECT_TRUE(firstToneBlock(proc).isVoid());
  dir.deleteRecursively();
}

// A file on a desktop disk plays where it is: its model_url is the file
// itself, nothing is copied into the stash, and the file keeps its dates
// (the stash's liveness stamp is for stash copies only).
TEST(LocalLoadTest, DiskFilesPlayInPlace) {
#if JUCE_IOS
  GTEST_SKIP() << "iOS keeps a copy: its pickers' files are only readable once";
#endif
  const juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("t3k-in-place-" + juce::Uuid().toString());
  ASSERT_TRUE(dir.createDirectory().wasOk());
  const juce::File first = dir.getChildFile("Gain 1.nam"), second = dir.getChildFile("Gain 2.nam");
  ASSERT_TRUE(testFile("a2-amp-test.nam").copyFileTo(first));
  ASSERT_TRUE(testFile("a2-amp-cab-test.nam").copyFileTo(second));
  const juce::Time old = juce::Time::getCurrentTime() - juce::RelativeTime::days(30);
  for (const auto& f : {first, second})
    ASSERT_TRUE(f.setLastModificationTime(old));

  TONE3000Processor proc;
  const juce::var res = proc.loadLocalTonePath(dir);
  ASSERT_TRUE(res["error"].isVoid()) << res["error"].toString().toStdString();
  ASSERT_TRUE(waitForChainLoaded(proc));
  const juce::var block = firstToneBlock(proc);
  ASSERT_EQ(block["tone"]["models"].size(), 2);
  EXPECT_EQ(juce::URL(block["tone"]["models"][0]["model_url"].toString()).getLocalFile(), first);
  const juce::var other = block["tone"]["models"][1];
  EXPECT_EQ(juce::URL(other["model_url"].toString()).getLocalFile(), second);

  // A switch reads the other file from where it is too.
  ASSERT_TRUE(proc.switchModel(block["blockId"].toString().toStdString(), static_cast<int>(other["id"]), other));
  ASSERT_TRUE(waitForChainLoaded(proc));
  EXPECT_EQ(static_cast<int>(firstToneBlock(proc)["activeModelId"]), static_cast<int>(other["id"]));
  for (const auto& f : {first, second})
    EXPECT_LT(std::abs((f.getLastModificationTime() - old).inSeconds()), 2.0) << "the user's file keeps its date";
  dir.deleteRecursively();
}

// A download that came back as an error page isn't a model: the check every
// local load runs (and the Library's downloads before writing) says so.
TEST(LocalLoadTest, ErrorPagesAreNotModels) {
  const juce::String page = "<html><body>401 Unauthorized</body></html>";
  EXPECT_TRUE(TONE3000Processor::localModelProblem("m.nam", page.toRawUTF8(), page.getNumBytesAsUTF8()).isNotEmpty());
  EXPECT_TRUE(TONE3000Processor::localModelProblem("m.wav", page.toRawUTF8(), page.getNumBytesAsUTF8()).isNotEmpty());
  juce::MemoryBlock a2;
  ASSERT_TRUE(testFile("a2-amp-test.nam").loadFileAsData(a2));
  EXPECT_TRUE(TONE3000Processor::localModelProblem("m.nam", a2.getData(), a2.getSize()).isEmpty());
}

// Only a stash copy's name says which bytes it holds: a file played in place
// that has gone stays gone, never re-rooted onto a same-named stash file.
TEST(LocalLoadTest, OnlyStashNamesReRoot) {
  EXPECT_TRUE(TONE3000Processor::isStashFileName("deadbeef-4096.nam"));
  EXPECT_TRUE(TONE3000Processor::isStashFileName("0123abcd-77.wav"));
  for (const char* name : {"DI.nam", "Gain 1.nam", "deadbeef.nam", "deadbeef-4096.txt", "DEADBEEF-1.nam", "-12.nam"})
    EXPECT_FALSE(TONE3000Processor::isStashFileName(name)) << name;

  const juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory);
  const juce::File root = tmp.getChildFile("t3k-stash-" + juce::Uuid().toString());
  ASSERT_TRUE(root.createDirectory());
  ASSERT_TRUE(root.getChildFile("DI.nam").replaceWithText("someone else's DI"));
  const juce::File gone = tmp.getChildFile("t3k-gone-" + juce::Uuid().toString()).getChildFile("DI.nam");
  EXPECT_EQ(TONE3000Processor::resolveLocalModelFile(root, juce::URL(gone).toString(false)), gone);
  root.deleteRecursively();
}

// The stash path a block persists as its model_url is absolute, and the tone
// JSON carrying it lives in presets, DAW/app state and undo snapshots. On iOS
// the app data container's UUID rotates on every reinstall or app update, so
// those paths name a container that is gone. Resolution therefore re-roots the
// (content-hashed) file name under the current stash folder.
TEST(LocalLoadTest, StashUrlResolvesUnderTheCurrentRoot) {
  const juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory);
  const juce::File root = tmp.getChildFile("t3k-stash-" + juce::Uuid().toString());
  const juce::File live = root.getChildFile("deadbeef-4096.nam");
  ASSERT_TRUE(root.createDirectory());
  ASSERT_TRUE(live.replaceWithText("model bytes"));

  // What a preset saved under a previous container holds.
  const juce::String staleUrl =
      juce::URL(tmp.getChildFile("Containers")
                    .getChildFile(juce::Uuid().toString())
                    .getChildFile("LocalModels")
                    .getChildFile("deadbeef-4096.nam"))
          .toString(false);
  EXPECT_EQ(TONE3000Processor::resolveLocalModelFile(root, staleUrl), live);

  // A path that still resolves comes back untouched: every desktop case.
  EXPECT_EQ(TONE3000Processor::resolveLocalModelFile(root, juce::URL(live).toString(false)), live);

  // Nothing to re-root for a catalog URL.
  EXPECT_EQ(TONE3000Processor::resolveLocalModelFile(root, "https://test.invalid/model.nam"),
            juce::File());

  // Bytes that are gone everywhere still read as missing (the caller reports
  // the failure), not as some other file.
  EXPECT_FALSE(
      TONE3000Processor::resolveLocalModelFile(root, staleUrl.replace("deadbeef", "0badc0de"))
          .existsAsFile());

  root.deleteRecursively();
}

// The iOS document picker hands back URLs, whose last path component is
// percent-escaped. Names must come back exactly as the desktop path derives
// them from juce::File, or a file called "Deluxe Reverb 2.nam" loads (and
// stashes, and titles its tile) as "Deluxe%20Reverb%202".
TEST(LocalLoadTest, UrlFileNameIsPercentDecoded) {
  const juce::File dir =
      juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("t3k-name");
  const juce::File spaced = dir.getChildFile("Deluxe Reverb 2.nam");
  EXPECT_EQ(TONE3000Processor::localFileNameFromUrl(juce::URL(spaced)), "Deluxe Reverb 2.nam");

  const juce::File accented = dir.getChildFile("Amplificador Válvulas.wav");
  EXPECT_EQ(TONE3000Processor::localFileNameFromUrl(juce::URL(accented)),
            juce::String::fromUTF8("Amplificador V\xc3\xa1lvulas.wav"));

  // A non-file URL has no local file to ask, so the escapes come off the path.
  EXPECT_EQ(TONE3000Processor::localFileNameFromUrl(
                juce::URL("https://test.invalid/x/Deluxe%20Reverb%202.nam")),
            "Deluxe Reverb 2.nam");
}

// The iOS picker path. Compiled on every platform (the DSP suite does not
// build for iOS), fed file:// URLs here, security-scoped ones on the iPad; the
// bytes come through juce::URL::createInputStream either way. Multi-select is
// the folder route on iOS, so it must sort, filter and aggregate errors the
// way loadLocalTonePath does for a folder.
TEST(LocalLoadTest, UrlsLoadMultiSelectInNaturalOrderSkippingBadFiles) {
  const juce::File dir =
      juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("t3k-url-test");
  dir.deleteRecursively();
  ASSERT_TRUE(dir.createDirectory().wasOk());
  ASSERT_TRUE(testFile("a2-amp-test.nam").copyFileTo(dir.getChildFile("amp 2.nam")));
  ASSERT_TRUE(testFile("a2-amp-cab-test.nam").copyFileTo(dir.getChildFile("amp 10.nam")));
  ASSERT_TRUE(testFile("a2-am-test-2.nam").copyFileTo(dir.getChildFile("amp 1.nam")));
  ASSERT_TRUE(dir.getChildFile("notes.txt").replaceWithText("hi"));

  // Picker order is arbitrary; hand them over scrambled, with a wrong type and
  // a file that does not exist mixed in.
  TONE3000Processor proc;
  const juce::var res = proc.loadLocalToneUrls({
      juce::URL(dir.getChildFile("amp 10.nam")),
      juce::URL(dir.getChildFile("notes.txt")),
      juce::URL(dir.getChildFile("amp 2.nam")),
      juce::URL(dir.getChildFile("missing.nam")),
      juce::URL(dir.getChildFile("amp 1.nam")),
  });
  EXPECT_TRUE(res["error"].isVoid()) << res["error"].toString().toStdString();
  ASSERT_TRUE(waitForChainLoaded(proc));

  const juce::var block = firstToneBlock(proc);
  EXPECT_EQ(block["tone"]["title"].toString(), juce::String("5 files"));
  EXPECT_EQ(block["tone"]["format"].toString(), juce::String("nam"));
  ASSERT_EQ(block["tone"]["models"].size(), 3)
      << "models: " << juce::JSON::toString(block["tone"]["models"]).toStdString();
  EXPECT_EQ(block["tone"]["models"][0]["name"].toString(), juce::String("amp 1"));
  EXPECT_EQ(block["tone"]["models"][1]["name"].toString(), juce::String("amp 2"));
  EXPECT_EQ(block["tone"]["models"][2]["name"].toString(), juce::String("amp 10"));

  dir.deleteRecursively();
}

TEST(LocalLoadTest, UrlsSingleFileTitlesFromNameAndRejectBadInputs) {
  TONE3000Processor proc;

  // One file: the title is its name without the extension, as on desktop.
  juce::var res = proc.loadLocalToneUrls({juce::URL(testFile("a2-amp-test.nam"))});
  EXPECT_TRUE(res["error"].isVoid()) << res["error"].toString().toStdString();
  ASSERT_TRUE(waitForChainLoaded(proc));
  EXPECT_EQ(firstToneBlock(proc)["tone"]["title"].toString(), juce::String("a2-amp-test"));

  TONE3000Processor rejecting;
  res = rejecting.loadLocalToneUrls({});
  EXPECT_EQ(res["error"].toString(), juce::String("Nothing to load"));

  res = rejecting.loadLocalToneUrls({juce::URL(juce::File("/nonexistent-t3k/missing.nam"))});
  EXPECT_EQ(res["error"].toString(), juce::String("Couldn't read the file"));

  const juce::File dir =
      juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("t3k-url-reject");
  dir.deleteRecursively();
  ASSERT_TRUE(dir.createDirectory().wasOk());
  ASSERT_TRUE(dir.getChildFile("notes.txt").replaceWithText("hi"));
  res = rejecting.loadLocalToneUrls({juce::URL(dir.getChildFile("notes.txt"))});
  EXPECT_EQ(res["error"].toString(), juce::String("Only .nam and .wav files are supported"));

  // The folder cap (300 files) applies to a multi-select too. Nothing is read
  // before the cap fires, so the URLs need not exist.
  juce::Array<juce::URL> many;
  for (int i = 0; i < 301; ++i)
    many.add(juce::URL(dir.getChildFile("amp " + juce::String(i) + ".nam")));
  res = rejecting.loadLocalToneUrls(many);
  EXPECT_EQ(res["error"].toString(), juce::String("Too many files (max 300)"));

  EXPECT_TRUE(firstToneBlock(rejecting).isVoid());
  dir.deleteRecursively();
}

// ensureWritableDir heals the app-data wound behind github issue #76: a
// folder left root-owned (sudo'd install script, restored backup) fails
// every stash and settings write while reads keep working. The plugin can't
// chown it back; it renames the broken node aside (the parent belongs to
// the user) and starts fresh, deleting nothing.
TEST(LocalLoadTest, EnsureWritableDirCreatesHealsAndPreservesEvidence) {
  const juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("t3k-ensure-" + juce::Uuid().toString());
  ASSERT_TRUE(tmp.createDirectory().wasOk());

  // Missing: created outright.
  const juce::File fresh = tmp.getChildFile("data");
  EXPECT_TRUE(TONE3000Processor::ensureWritableDir(fresh));
  EXPECT_TRUE(fresh.isDirectory());

  // Healthy: untouched, contents kept, nothing moved aside.
  ASSERT_TRUE(fresh.getChildFile("keep.bin").replaceWithText("bytes"));
  EXPECT_TRUE(TONE3000Processor::ensureWritableDir(fresh));
  EXPECT_TRUE(fresh.getChildFile("keep.bin").existsAsFile());
  EXPECT_EQ(tmp.findChildFiles(juce::File::findFilesAndDirectories, false).size(), 1);

  // A file squatting on the path: moved aside with its bytes, a directory
  // takes its place.
  const juce::File squat = tmp.getChildFile("squat");
  ASSERT_TRUE(squat.replaceWithText("old"));
  EXPECT_TRUE(TONE3000Processor::ensureWritableDir(squat));
  EXPECT_TRUE(squat.isDirectory());
  bool asideKeptBytes = false;
  for (const auto& sibling : tmp.findChildFiles(juce::File::findFiles, false))
    if (sibling.getFileName().startsWith("squat") && sibling.loadFileAsString() == "old")
      asideKeptBytes = true;
  EXPECT_TRUE(asideKeptBytes);

#if !JUCE_WINDOWS
  // The restored-backup shape: a directory the user still owns, write bits
  // stripped. Healed in place by putting the mode back, so its contents
  // never move. The root-owned variant takes the rename-aside path instead;
  // a test can't stage that unprivileged, and root sails past permission
  // bits anyway, so this leg is unprivileged-only.
  if (geteuid() != 0) {
    const juce::File locked = tmp.getChildFile("locked");
    ASSERT_TRUE(locked.createDirectory().wasOk());
    ASSERT_TRUE(locked.getChildFile("old.t3kpreset").replaceWithText("preset"));
    ASSERT_EQ(::chmod(locked.getFullPathName().toRawUTF8(), 0555), 0);
    ASSERT_FALSE(locked.hasWriteAccess());

    EXPECT_TRUE(TONE3000Processor::ensureWritableDir(locked));
    EXPECT_TRUE(locked.isDirectory());
    EXPECT_TRUE(locked.hasWriteAccess());
    EXPECT_TRUE(locked.getChildFile("old.t3kpreset").existsAsFile());
    // Only the owner got the write bit back: the folder holds the user's
    // sign-in tokens, so 0555 must heal to 0755, never 0777.
    struct stat healed {};
    ASSERT_EQ(::stat(locked.getFullPathName().toRawUTF8(), &healed), 0);
    EXPECT_EQ(healed.st_mode & 0777, 0755u);
    // Nothing moved aside: the fix happened in place.
    for (const auto& sibling : tmp.findChildFiles(juce::File::findDirectories, false))
      EXPECT_TRUE(sibling == locked || !sibling.getFileName().startsWith("locked"));
  }
#endif

  EXPECT_TRUE(tmp.deleteRecursively());
}
