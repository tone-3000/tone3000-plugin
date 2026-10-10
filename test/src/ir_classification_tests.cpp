// IR cab-like / reverb-like classification (ChainBlock::irIsLong), the
// switch behind the -18 dB cab pad and the 100% / 50% default mix.
//
// github issue #89: kernel length alone misfiles real IRs on both sides of
// the 1 s cutoff (a 389 ms chamber; a cab whose fade dips under the trim
// floor just inside it), so where the tone's catalog gear is unambiguous it
// decides: "cab" is cab-like, "space" is reverb-like. Every other gear, and
// untagged files, keep the length verdict.

#include "chain_test_helpers.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {

// A catalog-shaped IR tone whose model is served from `wav` on disk (file://
// URLs read straight from disk, see fetchModelFromUrl), so the load goes
// through the Select flow (loadTone) like a tone picked in the browser.
juce::String catalogIrTone(const juce::String& gear, const juce::File& wav) {
  juce::DynamicObject::Ptr model = new juce::DynamicObject();
  model->setProperty("id", 100);
  model->setProperty("name", "ir");
  model->setProperty("model_url", juce::URL(wav).toString(false));
  juce::DynamicObject::Ptr tone = new juce::DynamicObject();
  tone->setProperty("id", 1);
  tone->setProperty("title", "Test IR");
  tone->setProperty("format", "ir");
  if (gear.isNotEmpty())
    tone->setProperty("gear", gear);
  tone->setProperty("models", juce::Array<juce::var>{juce::var(model.get())});
  return juce::JSON::toString(juce::var(tone.get()));
}

juce::var firstToneBlock(TONE3000Processor& proc) {
  const juce::var state = proc.getChainState(-1);
  if (const auto* lane = state["chain"].getArray())
    for (const auto& item : *lane)
      if (item["kind"].toString() == "tone")
        return item;
  return {};
}

struct Verdict {
  bool irLong;
  float mix;
};

Verdict selectFlowVerdict(const juce::String& gear, const char* fixture) {
  // A scratch copy of the fixture: the loader stamps what it reads (stash
  // liveness, see fetchModelFromUrl), and the fixtures should stay untouched.
  const juce::File copy = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("t3k-ir-class-" + juce::String(fixture));
  EXPECT_TRUE(testFile(fixture).copyFileTo(copy));
  TONE3000Processor proc;
  EXPECT_FALSE(proc.loadTone(catalogIrTone(gear, copy)).empty());
  EXPECT_TRUE(waitForChainLoaded(proc));
  const juce::var block = firstToneBlock(proc);
  copy.deleteFile();
  return {static_cast<bool>(block["irLong"]), static_cast<float>(block["params"]["mix"])};
}

double rmsDb(const std::vector<float>& x, size_t skip) {
  double sum = 0.0;
  for (size_t i = skip; i < x.size(); ++i)
    sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
  return 10.0 * std::log10(std::max(sum / static_cast<double>(x.size() - skip), 1e-30));
}

}  // namespace

TEST(IrClassificationTest, GearDecidesWhereUnambiguousLengthOtherwise) {
  // The site's own tags beat the length on both sides of the cutoff.
  {
    const Verdict v = selectFlowVerdict("space", "cab-ir-test.wav");  // short file, reverb tag
    EXPECT_TRUE(v.irLong);
    EXPECT_FLOAT_EQ(v.mix, 0.5f);
  }
  {
    const Verdict v = selectFlowVerdict("cab", "reverb-ir-mono-test.wav");  // long file, cab tag
    EXPECT_FALSE(v.irLong);
    EXPECT_FLOAT_EQ(v.mix, 1.0f);
  }

  // Ambiguous gear: the length verdict, exactly as before.
  {
    const Verdict v = selectFlowVerdict("pedal", "cab-ir-test.wav");
    EXPECT_FALSE(v.irLong);
    EXPECT_FLOAT_EQ(v.mix, 1.0f);
  }
  {
    const Verdict v = selectFlowVerdict("pedal", "reverb-ir-mono-test.wav");
    EXPECT_TRUE(v.irLong);
    EXPECT_FLOAT_EQ(v.mix, 0.5f);
  }

  // No gear at all (older stored tones): the length verdict too.
  {
    const Verdict v = selectFlowVerdict({}, "reverb-ir-mono-test.wav");
    EXPECT_TRUE(v.irLong);
    EXPECT_FLOAT_EQ(v.mix, 0.5f);
  }
}

// The audible half, through the real chain on a restored rig: the same cab
// kernel in both lanes at the same saved mix, untagged on the left (cab-like
// by length: -18 dB pad) and tagged "space" on the right (reverb-like: no
// pad). The lanes must differ by exactly the pad, and the restore must keep
// the saved mix on both (default mix is a Select-flow one-shot, never a
// restore-time re-guess).
TEST(IrClassificationTest, GearTagChangesOnlyThePadOnRestore) {
  constexpr int kBlock = 512;
  ChainTestProcessor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);

  juce::ValueTree state("ChainSnapshot");
  state.setProperty("stereoEnabled", true, nullptr);
  juce::ValueTree left("ChainBlocks");
  left.appendChild(makeIrBlockTree("blk-untagged", 1, 100, "cab-ir-test.wav"), nullptr);
  state.appendChild(left, nullptr);
  juce::ValueTree right("RightChainBlocks");
  right.appendChild(makeIrBlockTree("blk-space", 2, 200, "cab-ir-test.wav", "space"), nullptr);
  state.appendChild(right, nullptr);
  proc.restoreFromTree(state);
  ASSERT_TRUE(waitForChainLoaded(proc));

  const juce::var chain = proc.getChainState(-1);
  juce::var untagged, space;
  for (const auto& item : *chain["chain"].getArray())
    if (item["kind"].toString() == "tone") untagged = item;
  for (const auto& item : *chain["chainRight"].getArray())
    if (item["kind"].toString() == "tone") space = item;
  EXPECT_FALSE(static_cast<bool>(untagged["irLong"]));
  EXPECT_TRUE(static_cast<bool>(space["irLong"]));
  EXPECT_FLOAT_EQ(static_cast<float>(untagged["params"]["mix"]), 1.0f);
  EXPECT_FLOAT_EQ(static_cast<float>(space["params"]["mix"]), 1.0f);

  const auto in = makeNoise(240 * kBlock, 4321, 0.1f);
  const auto [l, r] = processStereo(proc, in, kBlock);
  const double padDb = rmsDb(r, 48000) - rmsDb(l, 48000);
  EXPECT_NEAR(padDb, 18.0, 0.5) << "the only difference between the lanes is the cab pad";
}
