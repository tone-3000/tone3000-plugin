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

namespace {

juce::String base64Of(const juce::File& file) {
  juce::MemoryBlock bytes;
  EXPECT_TRUE(file.loadFileAsData(bytes));
  return juce::Base64::toBase64(bytes.getData(), bytes.getSize());
}

// One { name, data } entry of the files array the UI ships.
juce::var fileEntry(const juce::String& name, const juce::String& base64) {
  juce::DynamicObject::Ptr entry = new juce::DynamicObject();
  entry->setProperty("name", name);
  entry->setProperty("data", base64);
  return juce::var(entry.get());
}

juce::var testFileEntry(const char* name) { return fileEntry(name, base64Of(testFile(name))); }

juce::var filesOf(const juce::Array<juce::var>& entries) { return juce::var(entries); }

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
