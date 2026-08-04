// Chain branch tests
//
// Stereo chain branching (see setChainBranch in ProcessorChain.cpp): the
// branch lane taps its input from the trunk lane's signal after one of the
// trunk's tone blocks. These pin the feature's contracts:
//
//   - branching requires stereo mode and a real tone block in the trunk,
//   - the branch lane audibly receives the trunk's processed signal (both
//     channels identical when the branch lane is empty), and reverting
//     restores independent chains,
//   - the branch clears itself when the tapped block is removed or moved to
//     the other lane, follows swapChains, dies with stereo mode, and undo
//     brings it back,
//   - it survives a full plugin state save/restore round trip,
//   - setting a branch forces the input mode off "stereo" (a branched chain
//     has a single mono source).
//
// Chains are seeded through setStateInformation with the IR bytes embedded
// (ModelCache), so loads are cache-first and never touch the network.
// (The rig is shared with duplicate_tests.cpp via chain_test_helpers.h:
// ChainTestProcessor, the block-tree builders, and stereo drive/compare.)
#include "Processor.h"
#include "chain_test_helpers.h"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace {

constexpr int kBlock = 512;

TEST(ChainBranchTest, RequiresStereoAndValidToneBlock) {
  ChainTestProcessor proc;

  // Mono mode: no second chain to branch.
  EXPECT_FALSE(proc.setChainBranch("left", "whatever"));

  proc.setStereoMode(true);
  // Unknown block id.
  EXPECT_FALSE(proc.setChainBranch("left", "not-a-block"));

  // Insert slots are pass-through placeholders, not tap points.
  const juce::var state = proc.getChainState(-1);
  const auto* chain = state["chain"].getArray();
  ASSERT_NE(chain, nullptr);
  ASSERT_GT(chain->size(), 0);
  const juce::String insertId = (*chain)[0]["blockId"].toString();
  EXPECT_FALSE(proc.setChainBranch("left", insertId.toStdString()));

  EXPECT_FALSE(proc.getChainState(-1)["branch"].isObject());
  // Nothing to clear either.
  EXPECT_FALSE(proc.clearChainBranch());
}

TEST(ChainBranchTest, RoutesTrunkSignalIntoBranchLane) {
  ChainTestProcessor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);

  // Left lane: one cab IR. Right lane: empty (pass-through).
  seedStereoChains(proc, {"blk-a"}, {});
  ASSERT_TRUE(waitForChainLoaded(proc)) << "IR block never finished loading from cache";

  // Independent chains: left is convolved (and padded -18 dB), right is the
  // dry input; the channels must differ substantially.
  const auto in = makeNoise(240 * kBlock, 1234, 0.25f);
  {
    const auto [l, r] = processStereo(proc, in);
    EXPECT_GT(settledMaxChannelDiff(l, r), 0.01f) << "independent chains should differ";
  }

  // Branch: the right lane now taps the left lane's signal after the IR.
  letAudioGoIdle();
  ASSERT_TRUE(proc.setChainBranch("left", "blk-a"));
  {
    const juce::var state = proc.getChainState(-1);
    ASSERT_TRUE(state["branch"].isObject());
    EXPECT_EQ(state["branch"]["side"].toString(), "left");
    EXPECT_EQ(state["branch"]["afterBlockId"].toString(), "blk-a");
    // A branched chain has one mono source; the stereo fold is forced off.
    EXPECT_EQ(state["inputMode"].toString(), "left");
  }

  // The branch lane is empty, so both channels carry the identical trunk
  // output once the per-channel filter states have converged.
  {
    const auto [l, r] = processStereo(proc, makeNoise(240 * kBlock, 5678, 0.25f));
    EXPECT_LT(settledMaxChannelDiff(l, r), 1e-4f) << "branched lanes should carry one signal";
  }

  // Revert: independent chains again.
  letAudioGoIdle();
  ASSERT_TRUE(proc.clearChainBranch());
  EXPECT_FALSE(proc.getChainState(-1)["branch"].isObject());
  {
    const auto [l, r] = processStereo(proc, makeNoise(240 * kBlock, 9012, 0.25f));
    EXPECT_GT(settledMaxChannelDiff(l, r), 0.01f) << "clearing the branch must restore both chains";
  }

  // While branched, the stereo input fold is rejected (L/R still work).
  letAudioGoIdle();
  ASSERT_TRUE(proc.setChainBranch("left", "blk-a"));
  proc.setInputMode(TONE3000Processor::InputMode::Stereo);
  EXPECT_EQ(proc.getInputMode(), TONE3000Processor::InputMode::Left);
  proc.setInputMode(TONE3000Processor::InputMode::Right);
  EXPECT_EQ(proc.getInputMode(), TONE3000Processor::InputMode::Right);
}

// The user-reported dual-cab scenario: trunk = amp head + cab IR, branch
// lane = the *same* cab IR, tapped between amp and cab. Both channels run
// identical post-tap processing, so the output must be dual mono, across
// host rates and oversampling factors (each lane owns its own IR island
// and boundary path; any per-lane state divergence shows up here as an
// inter-channel level or time offset).
TEST(ChainBranchTest, BranchedIdenticalCabsStayDualMono) {
  struct Config {
    double hostRate;
    bool oversample;
  };
  for (const Config cfg :
       {Config{48000.0, false}, Config{48000.0, true}, Config{44100.0, false},
        Config{44100.0, true}}) {
    SCOPED_TRACE(juce::String(cfg.hostRate, 0) + " Hz, oversampling " +
                 (cfg.oversample ? "8x" : "off"));

    ChainTestProcessor proc;
    proc.setPlayConfigDetails(2, 2, cfg.hostRate, kBlock);
    if (cfg.oversample) {
      proc.parameters.getParameter("osEnabled")->setValueNotifyingHost(1.0f);
      proc.parameters.getParameter("osFactor")->setValueNotifyingHost(1.0f);  // index 2 = 8x
    }
    proc.prepareToPlay(cfg.hostRate, kBlock);

    juce::ValueTree state("ChainSnapshot");
    state.setProperty("stereoEnabled", true, nullptr);
    juce::ValueTree left("ChainBlocks");
    left.appendChild(makeNamBlockTree("blk-amp", 1, 100), nullptr);
    left.appendChild(makeIrBlockTree("blk-cab-l", 2, 200), nullptr);
    state.appendChild(left, nullptr);
    juce::ValueTree right("RightChainBlocks");
    right.appendChild(makeIrBlockTree("blk-cab-r", 3, 300), nullptr);
    state.appendChild(right, nullptr);
    proc.restoreFromTree(state);
    ASSERT_TRUE(waitForChainLoaded(proc)) << "blocks never finished loading from cache";

    letAudioGoIdle();
    ASSERT_TRUE(proc.setChainBranch("left", "blk-amp"));

    const auto [l, r] = processStereo(proc, makeNoise(240 * kBlock, 4242, 0.1f));
    const float diff = settledMaxChannelDiff(l, r);
    if (diff >= 1e-4f) {
      // Characterize the mismatch: pure gain (RMS ratio) vs time offset
      // (cross-correlation lag of R against L).
      double rmsL = 0.0, rmsR = 0.0;
      for (size_t i = 48000; i < l.size(); ++i) {
        rmsL += static_cast<double>(l[i]) * l[i];
        rmsR += static_cast<double>(r[i]) * r[i];
      }
      const int lag = bestCorrelationLag(r, l, 64000, 8192, 4096);
      ADD_FAILURE() << "channels diverged: maxDiff=" << diff
                    << " rmsR/rmsL=" << std::sqrt(rmsR / std::max(rmsL, 1e-30))
                    << " bestLag(R vs L)=" << lag;
    }
  }
}

// One full amp + cab rig, in the two configurations the user A/Bs by
// toggling chain modes: mono chain mode ([amp, ir] processing both
// channels), or stereo mode with the same [amp, ir] on the left, a second
// identical ir on the right, branched between amp and cab.
std::pair<std::vector<float>, std::vector<float>> runAmpIrSetup(bool branched,
                                                                const char* irFile,
                                                                const std::vector<float>& in) {
  ChainTestProcessor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);

  juce::ValueTree state("ChainSnapshot");
  state.setProperty("stereoEnabled", branched, nullptr);
  juce::ValueTree left("ChainBlocks");
  left.appendChild(makeNamBlockTree("blk-amp", 1, 100), nullptr);
  left.appendChild(makeIrBlockTree("blk-cab-l", 2, 200, irFile), nullptr);
  state.appendChild(left, nullptr);
  juce::ValueTree right("RightChainBlocks");
  if (branched)
    right.appendChild(makeIrBlockTree("blk-cab-r", 3, 300, irFile), nullptr);
  state.appendChild(right, nullptr);
  proc.restoreFromTree(state);
  EXPECT_TRUE(waitForChainLoaded(proc)) << "blocks never finished loading from cache";

  if (branched) {
    letAudioGoIdle();
    EXPECT_TRUE(proc.setChainBranch("left", "blk-amp"));
  }
  return processStereo(proc, in);
}

// The user's actual A/B with a *mono* cab IR: mono chain mode and the
// branched dual-cab rig are the same processing graph, so their outputs
// must match channel-for-channel.
TEST(ChainBranchTest, BranchedDualCabMatchesMonoChainMode) {
  const auto in = makeNoise(240 * kBlock, 777, 0.1f);
  const auto [ml, mr] = runAmpIrSetup(false, "cab-ir-test.wav", in);
  const auto [bl, br] = runAmpIrSetup(true, "cab-ir-test.wav", in);
  EXPECT_LT(settledMaxChannelDiff(ml, bl), 1e-4f) << "left channel drifted from mono mode";
  EXPECT_LT(settledMaxChannelDiff(mr, br), 1e-4f) << "right channel drifted from mono mode";
}

// A *stereo* IR file is the one rig where the mono↔branched A/B hears a real
// difference, by design: mono chain mode runs it in true stereo (L and R
// convolve different kernels: a wide, decorrelated image), while stereo
// mode's lanes are mono, so each cab instance uses only the IR's first
// channel. The branched output is still perfect dual mono; it just isn't
// the mono-mode sound (the IR's second-channel content is gone).
TEST(ChainBranchTest, StereoIrDiffersBetweenMonoAndBranchedByDesign) {
  // The reverb IR's kernel is over a second long, so startup transients
  // (block wet fades, the chain-edit fade around setChainBranch) ring far
  // past the usual settle window; run longer and skip past a full tail.
  const auto in = makeNoise(480 * kBlock, 888, 0.1f);
  const size_t skip = 168000;
  const auto [ml, mr] = runAmpIrSetup(false, "reverb-ir-stereo-test.wav", in);
  const auto [bl, br] = runAmpIrSetup(true, "reverb-ir-stereo-test.wav", in);

  // Mono mode: true stereo; the channels genuinely differ.
  EXPECT_GT(settledMaxChannelDiff(ml, mr, skip), 1e-3f) << "true-stereo IR should decorrelate";
  // Branched: dual mono, identical channels…
  EXPECT_LT(settledMaxChannelDiff(bl, br, skip), 1e-4f) << "branched lanes should stay dual mono";
  // …whose left is the mono-mode left up to JUCE engine internals (the
  // mono and true-stereo convolvers differ ≈ -44 dB on the same kernel,
  // measured and inaudible), while the right audibly loses the IR's second
  // channel.
  EXPECT_LT(settledMaxChannelDiff(ml, bl, skip), 5e-3f);
  EXPECT_GT(settledMaxChannelDiff(mr, br, skip), 1e-3f);
}

// Moving the tap is one setChainBranch call, no clearing first. Each move
// is its own undo step; re-pointing to the current spot is a no-op.
TEST(ChainBranchTest, RepointsBranchInOneMove) {
  ChainTestProcessor proc;
  seedStereoChains(proc, {"blk-a", "blk-b"}, {"blk-c"});

  ASSERT_TRUE(proc.setChainBranch("left", "blk-a"));

  // Same trunk, different spot.
  ASSERT_TRUE(proc.setChainBranch("left", "blk-b"));
  EXPECT_EQ(proc.getChainState(-1)["branch"]["afterBlockId"].toString(), "blk-b");

  // The other lane can take over as trunk in one move too.
  ASSERT_TRUE(proc.setChainBranch("right", "blk-c"));
  {
    const juce::var state = proc.getChainState(-1);
    EXPECT_EQ(state["branch"]["side"].toString(), "right");
    EXPECT_EQ(state["branch"]["afterBlockId"].toString(), "blk-c");
  }

  // Re-pointing to the spot already tapped is a no-op (no history entry)…
  ASSERT_TRUE(proc.setChainBranch("right", "blk-c"));

  // …so undo steps back through the real moves, one at a time.
  ASSERT_TRUE(proc.undoChain());
  EXPECT_EQ(proc.getChainState(-1)["branch"]["afterBlockId"].toString(), "blk-b");
  ASSERT_TRUE(proc.undoChain());
  EXPECT_EQ(proc.getChainState(-1)["branch"]["afterBlockId"].toString(), "blk-a");
}

TEST(ChainBranchTest, ClearsWhenTappedBlockRemovedAndRestoresOnUndo) {
  ChainTestProcessor proc;
  seedStereoChains(proc, {"blk-a"}, {"blk-b"});

  ASSERT_TRUE(proc.setChainBranch("left", "blk-a"));
  ASSERT_TRUE(proc.getChainState(-1)["branch"].isObject());

  // Removing the tapped block reverts to independent chains…
  ASSERT_TRUE(proc.removeChainBlock("blk-a"));
  EXPECT_FALSE(proc.getChainState(-1)["branch"].isObject());

  // …and undo restores both the block and the branch that tapped it.
  ASSERT_TRUE(proc.undoChain());
  const juce::var state = proc.getChainState(-1);
  ASSERT_TRUE(state["branch"].isObject());
  EXPECT_EQ(state["branch"]["afterBlockId"].toString(), "blk-a");
}

TEST(ChainBranchTest, ClearsWhenTappedBlockMovesToOtherLane) {
  ChainTestProcessor proc;
  seedStereoChains(proc, {"blk-a"}, {});

  ASSERT_TRUE(proc.setChainBranch("left", "blk-a"));
  // The tapped block leaving the trunk lane invalidates the tap point.
  ASSERT_TRUE(proc.moveBlockToChain("blk-a", "right", 0));
  EXPECT_FALSE(proc.getChainState(-1)["branch"].isObject());
}

TEST(ChainBranchTest, FollowsSwapChainsAndSurvivesMonoRoundTrip) {
  ChainTestProcessor proc;
  seedStereoChains(proc, {"blk-a"}, {"blk-b"});

  ASSERT_TRUE(proc.setChainBranch("left", "blk-a"));

  // Swapping the chains moves the trunk to the other side; the tap follows.
  ASSERT_TRUE(proc.swapChains());
  {
    const juce::var state = proc.getChainState(-1);
    ASSERT_TRUE(state["branch"].isObject());
    EXPECT_EQ(state["branch"]["side"].toString(), "right");
    EXPECT_EQ(state["branch"]["afterBlockId"].toString(), "blk-a");
  }

  // Stereo off makes the branch dormant: hidden from the state (mono mode
  // has no branch UI) but retained, so it re-engages when stereo comes back.
  proc.setStereoMode(false);
  EXPECT_FALSE(proc.getChainState(-1)["branch"].isObject());

  // A dormant branch doesn't constrain the input fold…
  proc.setInputMode(TONE3000Processor::InputMode::Stereo);
  EXPECT_EQ(proc.getInputMode(), TONE3000Processor::InputMode::Stereo);

  proc.setStereoMode(true);
  {
    const juce::var state = proc.getChainState(-1);
    ASSERT_TRUE(state["branch"].isObject());
    EXPECT_EQ(state["branch"]["side"].toString(), "right");
    EXPECT_EQ(state["branch"]["afterBlockId"].toString(), "blk-a");
    // …but re-engaging it re-enforces the mono-source invariant.
    EXPECT_EQ(state["inputMode"].toString(), "left");
  }
}

TEST(ChainBranchTest, ClearsWhenTappedBlockRemovedWhileDormant) {
  ChainTestProcessor proc;
  seedStereoChains(proc, {"blk-a"}, {"blk-b"});
  ASSERT_TRUE(proc.setChainBranch("left", "blk-a"));

  // Removing the tapped block while the branch lies dormant in mono mode
  // must not leave a stale id behind for the next stereo session.
  proc.setStereoMode(false);
  ASSERT_TRUE(proc.removeChainBlock("blk-a"));
  proc.setStereoMode(true);
  EXPECT_FALSE(proc.getChainState(-1)["branch"].isObject());
}

TEST(ChainBranchTest, SurvivesStateSaveRestore) {
  juce::MemoryBlock state;
  {
    ChainTestProcessor a;
    seedStereoChains(a, {"blk-a"}, {"blk-b"});
    ASSERT_TRUE(a.setChainBranch("left", "blk-a"));
    a.getStateInformation(state);
  }

  ChainTestProcessor b;
  b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  const juce::var restored = b.getChainState(-1);
  EXPECT_TRUE(static_cast<bool>(restored["stereoEnabled"]));
  ASSERT_TRUE(restored["branch"].isObject());
  EXPECT_EQ(restored["branch"]["side"].toString(), "left");
  EXPECT_EQ(restored["branch"]["afterBlockId"].toString(), "blk-a");
  // The forced mono-source input mode rides the session state too.
  EXPECT_EQ(restored["inputMode"].toString(), "left");
}

}  // namespace
