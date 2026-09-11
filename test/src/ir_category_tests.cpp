// Pins the fix for TONE3000 issue #89: IR loudness/classification used to be
// driven entirely by measured kernel length (kShortIrMaxSeconds), which a
// silence-padded file corrupts - two versions of the same cabinet IR (one
// padded past the cutoff) landed on different engines *and* different
// audible defaults (-18 dB pad / default mix), a jarring level jump between
// otherwise-identical content. The fix replaces that with an explicit,
// persisted per-block IrCategory (Cab/IrPlayer): the sole source of the pad
// and default mix, resolved from catalog `gear` metadata (site loads) or a
// one-shot duration guess (local file loads only) and never touched again.
// Engine selection (uniform vs non-uniform convolution) is a fully separate,
// duration-based CPU decision - always uniform for Cab (unconditionally
// hard-capped to 500 ms at load), adaptive from detected content for
// IrPlayer.

#include "chain_test_helpers.h"

#include <gtest/gtest.h>

namespace {

constexpr int kBlock = 512;

// A single-IR-block mono chain, category persisted exactly as a real save
// would (see ProcessorState.cpp); nullptr leaves it unset so restore falls
// back to the content-detection duration guess.
void seedMonoIrChain(ChainTestProcessor& proc, const juce::String& blockId, const char* fileName,
                     const char* irCategory) {
  proc.setPlayConfigDetails(2, 2, 48000.0, kBlock);
  proc.prepareToPlay(48000.0, kBlock);

  juce::ValueTree state("ChainSnapshot");
  state.setProperty("stereoEnabled", false, nullptr);
  juce::ValueTree lane("ChainBlocks");
  lane.appendChild(makeIrBlockTree(blockId, 1, 100, fileName, irCategory), nullptr);
  state.appendChild(lane, nullptr);
  state.appendChild(juce::ValueTree("RightChainBlocks"), nullptr);
  proc.restoreFromTree(state);
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

std::vector<float> makeImpulse(int totalBlocks) {
  std::vector<float> impulse(static_cast<size_t>(totalBlocks * kBlock), 0.0f);
  impulse[static_cast<size_t>(kBlock)] = 1.0f;  // not sample 0: skip the noise gate's attack
  return impulse;
}

// Local-file drop payload helpers, mirroring local_load_tests.cpp (no
// network: files ride as base64 the way the UI ships a drop).
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

juce::var testFileEntry(const char* name) { return fileEntry(name, base64Of(testFile(name))); }

}  // namespace

// The actual #89 regression: identical real content, one copy padded with
// trailing silence past the old 1 s cutoff - today's raw-length logic would
// classify these differently (and sound different); with category explicit
// and persisted, both converge on the same category, the same pad/mix, and
// (since a known Cab is hard-capped to its first 500 ms regardless of file
// length) bit-identical processed audio.
TEST(IrCategoryTest, Issue89SameContentDifferentPaddingProducesIdenticalOutput) {
  ChainTestProcessor procShort, procPadded;
  seedMonoIrChain(procShort, "blk", "cab-ir-test.wav", "cab");
  seedMonoIrChain(procPadded, "blk", "cab-ir-test-padded.wav", "cab");
  ASSERT_TRUE(waitForChainLoaded(procShort));
  ASSERT_TRUE(waitForChainLoaded(procPadded));

  const juce::var blockShort = firstToneBlock(procShort);
  const juce::var blockPadded = firstToneBlock(procPadded);
  ASSERT_FALSE(blockShort.isVoid());
  ASSERT_FALSE(blockPadded.isVoid());

  EXPECT_EQ(blockShort["irCategory"].toString(), juce::String("cab"));
  EXPECT_EQ(blockPadded["irCategory"].toString(), juce::String("cab"));
  EXPECT_FLOAT_EQ(static_cast<float>(blockShort["params"]["mix"]), 1.0f);
  EXPECT_FLOAT_EQ(static_cast<float>(blockPadded["params"]["mix"]), 1.0f);
  // A known Cab is always the uniform engine, unconditionally - the padded
  // file's extra length never even reaches a length check.
  EXPECT_FALSE(static_cast<bool>(blockShort["irLong"]));
  EXPECT_FALSE(static_cast<bool>(blockPadded["irLong"]));

  const auto impulse = makeImpulse(200);
  const auto [outShortL, outShortR] = processStereo(procShort, impulse);
  const auto [outPaddedL, outPaddedR] = processStereo(procPadded, impulse);

  ASSERT_EQ(outShortL.size(), outPaddedL.size());
  float maxDiff = 0.0f;
  for (size_t i = 0; i < outShortL.size(); ++i)
    maxDiff = std::max(maxDiff, std::abs(outShortL[i] - outPaddedL[i]));
  EXPECT_LT(maxDiff, 1e-5f) << "same content, different trailing silence: output must match";
}

// gear -> category mapping (ProcessorChain.cpp's irCategoryFromGear): known
// synchronously in loadTone, before the (network) download even starts, so
// this needs no network and no wait - the category is already resolved by
// the time loadTone returns.
TEST(IrCategoryTest, GearMapsToCategory) {
  auto toneJson = [](const char* gear) {
    return juce::String("{\"id\":1,\"title\":\"Test\",\"format\":\"ir\",\"gear\":\"") + gear +
           "\",\"models\":[{\"id\":100,\"name\":\"m\","
           "\"model_url\":\"https://test.invalid/m.wav\"}]}";
  };

  struct Case {
    const char* gear;
    const char* expectedCategory;
  };
  const Case cases[] = {
      {"cab", "cab"},   {"space", "irPlayer"}, {"outboard", "irPlayer"},
      {"experimental", "irPlayer"}, {"ir", "irPlayer"},
  };

  for (const auto& c : cases) {
    TONE3000Processor proc;
    const std::string blockId = proc.loadTone(toneJson(c.gear), "");
    ASSERT_FALSE(blockId.empty()) << c.gear;
    const juce::var block = firstToneBlock(proc);
    ASSERT_FALSE(block.isVoid()) << c.gear;
    EXPECT_EQ(block["irCategory"].toString(), juce::String(c.expectedCategory)) << c.gear;
  }
}

// Missing `gear` (a real catalog data gap, not a local file) maps to
// IrPlayer exactly like every other non-cab tag - never guessed from
// duration, which only applies to local file loads (see below).
TEST(IrCategoryTest, MissingGearMapsToIrPlayer) {
  const juce::String toneJson =
      "{\"id\":1,\"title\":\"Test\",\"format\":\"ir\","
      "\"models\":[{\"id\":100,\"name\":\"m\",\"model_url\":\"https://test.invalid/m.wav\"}]}";
  TONE3000Processor proc;
  const std::string blockId = proc.loadTone(toneJson, "");
  ASSERT_FALSE(blockId.empty());
  const juce::var block = firstToneBlock(proc);
  ASSERT_FALSE(block.isVoid());
  EXPECT_EQ(block["irCategory"].toString(), juce::String("irPlayer"));
}

// Local file loads carry no `gear` tag at all: category is seeded once from
// the load-time content scan instead (an initial guess only, never
// authoritative - the richer local-file category-picking UX is a separate,
// out-of-scope follow-up).
TEST(IrCategoryTest, LocalFileLoadGuessesCategoryFromDetectedContent) {
  TONE3000Processor procCab;
  const juce::var cabRes =
      procCab.loadLocalTone("cab", juce::var(juce::Array<juce::var>{testFileEntry("cab-ir-test.wav")}));
  ASSERT_TRUE(cabRes["error"].isVoid()) << cabRes["error"].toString().toStdString();
  ASSERT_TRUE(waitForChainLoaded(procCab));
  const juce::var cabBlock = firstToneBlock(procCab);
  ASSERT_FALSE(cabBlock.isVoid());
  EXPECT_TRUE(static_cast<bool>(cabBlock["tone"]["local"]));
  EXPECT_EQ(cabBlock["irCategory"].toString(), juce::String("cab"));
  // A fresh block (never loaded before): applyDefaultMixOnLoad sets the mix
  // from the just-resolved category.
  EXPECT_FLOAT_EQ(static_cast<float>(cabBlock["params"]["mix"]), 1.0f);

  TONE3000Processor procReverb;
  const juce::var reverbRes = procReverb.loadLocalTone(
      "reverb", juce::var(juce::Array<juce::var>{testFileEntry("reverb-ir-mono-test.wav")}));
  ASSERT_TRUE(reverbRes["error"].isVoid()) << reverbRes["error"].toString().toStdString();
  ASSERT_TRUE(waitForChainLoaded(procReverb));
  const juce::var reverbBlock = firstToneBlock(procReverb);
  ASSERT_FALSE(reverbBlock.isVoid());
  EXPECT_EQ(reverbBlock["irCategory"].toString(), juce::String("irPlayer"));
  EXPECT_FLOAT_EQ(static_cast<float>(reverbBlock["params"]["mix"]), 0.5f);
}

// setBlockIrCategory resets pad/mix to the new category's fixed default (V1:
// no memory of a prior per-category setting).
TEST(IrCategoryTest, SetBlockIrCategoryResetsMixToFixedDefault) {
  ChainTestProcessor proc;
  seedMonoIrChain(proc, "blk", "cab-ir-test.wav", "cab");
  ASSERT_TRUE(waitForChainLoaded(proc));

  {
    const juce::var block = firstToneBlock(proc);
    ASSERT_FALSE(block.isVoid());
    EXPECT_EQ(block["irCategory"].toString(), juce::String("cab"));
    EXPECT_FLOAT_EQ(static_cast<float>(block["params"]["mix"]), 1.0f);
  }

  ASSERT_TRUE(proc.setBlockIrCategory("blk", "irPlayer"));
  {
    const juce::var block = firstToneBlock(proc);
    EXPECT_EQ(block["irCategory"].toString(), juce::String("irPlayer"));
    EXPECT_FLOAT_EQ(static_cast<float>(block["params"]["mix"]), 0.5f);
  }

  // A user mix tweak is discarded by the next category switch (V1: fixed
  // defaults only, no per-category memory).
  ASSERT_TRUE(proc.setBlockParam("blk", "mix", 0.1));
  ASSERT_TRUE(proc.setBlockIrCategory("blk", "cab"));
  {
    const juce::var block = firstToneBlock(proc);
    EXPECT_EQ(block["irCategory"].toString(), juce::String("cab"));
    EXPECT_FLOAT_EQ(static_cast<float>(block["params"]["mix"]), 1.0f);
  }

  // Setting the same category again is a no-op that keeps the user's mix.
  ASSERT_TRUE(proc.setBlockParam("blk", "mix", 0.3));
  ASSERT_TRUE(proc.setBlockIrCategory("blk", "cab"));
  {
    const juce::var block = firstToneBlock(proc);
    EXPECT_FLOAT_EQ(static_cast<float>(block["params"]["mix"]), 0.3f);
  }
}

// Engine selection stays duration-based (now via detected content) and
// fully decoupled from category: two IrPlayer blocks, one genuinely short,
// one genuinely long, still land on different engines even though their
// category - and therefore pad/mix - is pinned identical.
TEST(IrCategoryTest, EngineSelectionStaysContentBasedForIrPlayer) {
  ChainTestProcessor procShort, procLong;
  seedMonoIrChain(procShort, "blk", "cab-ir-test.wav", "irPlayer");
  seedMonoIrChain(procLong, "blk", "reverb-ir-mono-test.wav", "irPlayer");
  ASSERT_TRUE(waitForChainLoaded(procShort));
  ASSERT_TRUE(waitForChainLoaded(procLong));

  const juce::var blockShort = firstToneBlock(procShort);
  const juce::var blockLong = firstToneBlock(procLong);
  ASSERT_FALSE(blockShort.isVoid());
  ASSERT_FALSE(blockLong.isVoid());

  EXPECT_EQ(blockShort["irCategory"].toString(), juce::String("irPlayer"));
  EXPECT_EQ(blockLong["irCategory"].toString(), juce::String("irPlayer"));
  EXPECT_FALSE(static_cast<bool>(blockShort["irLong"]))
      << "short content must still land on the uniform engine";
  EXPECT_TRUE(static_cast<bool>(blockLong["irLong"]))
      << "long content must still land on the non-uniform engine";
}

// State round-trip: an explicit category persists exactly as saved. State
// saved before this field existed (irCategory absent) is not silently
// treated as IrPlayer - it falls back to the same content-detection guess a
// fresh local-file load uses, reproducing the pre-fix pad behavior for a
// genuinely short cab file (so an old session's sound doesn't shift on next
// load) without ever re-deriving the *mix* the user actually saved.
TEST(IrCategoryTest, StateRoundTripPreservesOrBackfillsCategory) {
  ChainTestProcessor procExplicit;
  seedMonoIrChain(procExplicit, "blk", "cab-ir-test.wav", "irPlayer");
  ASSERT_TRUE(waitForChainLoaded(procExplicit));
  ASSERT_TRUE(procExplicit.setBlockParam("blk", "mix", 0.77));

  const juce::MemoryBlock saved = [&] {
    juce::MemoryBlock data;
    procExplicit.getStateInformation(data);
    return data;
  }();

  ChainTestProcessor restored;
  restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  ASSERT_TRUE(waitForChainLoaded(restored));
  const juce::var restoredBlock = firstToneBlock(restored);
  ASSERT_FALSE(restoredBlock.isVoid());
  EXPECT_EQ(restoredBlock["irCategory"].toString(), juce::String("irPlayer"));
  EXPECT_FLOAT_EQ(static_cast<float>(restoredBlock["params"]["mix"]), 0.77f);

  // Pre-migration state: no persisted category at all.
  ChainTestProcessor procLegacy;
  seedMonoIrChain(procLegacy, "blk", "cab-ir-test.wav", nullptr);
  ASSERT_TRUE(waitForChainLoaded(procLegacy));
  const juce::var legacyBlock = firstToneBlock(procLegacy);
  ASSERT_FALSE(legacyBlock.isVoid());
  EXPECT_EQ(legacyBlock["irCategory"].toString(), juce::String("cab"))
      << "genuinely short content backfills to Cab, matching the pre-fix pad behavior";
  // makeIrBlockTree always persists mix=1.0; a legacy restore never
  // re-derives it from the backfilled category.
  EXPECT_FLOAT_EQ(static_cast<float>(legacyBlock["params"]["mix"]), 1.0f);
}
