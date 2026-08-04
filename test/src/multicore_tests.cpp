// ── Multi-core stereo tests ──
//
// Stereo mode processes the two chain lanes concurrently (see LaneWorker.h):
// the Right/branch lane runs on a realtime worker thread while the audio
// thread processes the other. Parallelism is pure scheduling — no arithmetic
// or ordering changes inside a lane — so its one testable contract is strong:
//
//   - the parallel schedule's output is BIT-IDENTICAL to the serial one,
//     across topologies (independent lanes, branched), host rates and
//     oversampling factors,
//   - the worker survives the host lifecycle (re-prepare, release, restart)
//     with audio flowing throughout.
//
// The rigs deliberately give the lanes different chains and sub-unity mix
// values: a cross-lane scratch race (the historical hazard — the dry-mix
// buffer used to be shared) corrupts exactly the dry portion of the blend,
// which identical lanes or mix = 1.0 would hide.
//
// Chains are seeded through setStateInformation with model bytes embedded
// (ModelCache), so loads are cache-first and never touch the network.
#include "Processor.h"
#include "chain_test_helpers.h"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace {

constexpr int kBlock = 512;

// Skips the first second like the rest of the suite (wet fades, smoothers,
// convolver engagement) — but expects *zero* difference after it.
float settledDiff(const std::vector<float>& a, const std::vector<float>& b) {
  return settledMaxChannelDiff(a, b, 48000);
}

// Left lane: amp + cab (mix 0.7 on the cab). Right lane: cab only (mix 0.4).
// Different chains and different mixes per lane, so lane cross-talk of any
// kind (scratch, engines, gains) shows up as a serial/parallel mismatch.
juce::ValueTree makeStereoRigState() {
  juce::ValueTree state("TONE3000State");
  state.setProperty("stereoEnabled", true, nullptr);

  juce::ValueTree left("ChainBlocks");
  left.appendChild(makeNamBlockTree("blk-amp", 1, 100), nullptr);
  auto cabL = makeIrBlockTree("blk-cab-l", 2, 200);
  cabL.setProperty("mix", 0.7f, nullptr);
  left.appendChild(cabL, nullptr);
  state.appendChild(left, nullptr);

  juce::ValueTree right("RightChainBlocks");
  auto cabR = makeIrBlockTree("blk-cab-r", 3, 300);
  cabR.setProperty("mix", 0.4f, nullptr);
  right.appendChild(cabR, nullptr);
  state.appendChild(right, nullptr);

  return state;
}

struct RigConfig {
  bool multiCore;
  bool branched;
  double hostRate;
  bool oversample;
};

std::pair<std::vector<float>, std::vector<float>> runRig(const RigConfig& cfg,
                                                         const std::vector<float>& in) {
  ChainTestProcessor proc;
  // Never persisted: the test must not touch the user's machine-wide
  // preference, and each run pins its own schedule regardless of it.
  proc.setMultiCoreEnabled(cfg.multiCore, /*persist=*/false);

  proc.setPlayConfigDetails(2, 2, cfg.hostRate, kBlock);
  if (cfg.oversample) {
    proc.parameters.getParameter("osEnabled")->setValueNotifyingHost(1.0f);
    proc.parameters.getParameter("osFactor")->setValueNotifyingHost(1.0f);  // index 2 = 8x
  }
  proc.prepareToPlay(cfg.hostRate, kBlock);

  proc.restoreFromTree(makeStereoRigState());
  EXPECT_TRUE(waitForChainLoaded(proc)) << "blocks never finished loading from cache";

  if (cfg.branched) {
    letAudioGoIdle();
    EXPECT_TRUE(proc.setChainBranch("left", "blk-amp"));
  }

  return processStereo(proc, in);
}

// Independent stereo lanes: forked (Right lane on the worker) vs. serial
// must null exactly, at 48k, under 8x oversampling, and across the 44.1k
// resampling boundary.
TEST(MultiCoreTest, IndependentLanesParallelMatchesSerialBitExact) {
  struct Config {
    double hostRate;
    bool oversample;
  };
  const auto in = makeNoise(240 * kBlock, 24601, 0.1f);

  for (const Config c : {Config{48000.0, false}, Config{48000.0, true}, Config{44100.0, false}}) {
    SCOPED_TRACE(juce::String(c.hostRate, 0) + " Hz, oversampling " +
                 (c.oversample ? "8x" : "off"));

    const auto [sl, sr] = runRig({false, false, c.hostRate, c.oversample}, in);
    const auto [pl, pr] = runRig({true, false, c.hostRate, c.oversample}, in);

    EXPECT_EQ(settledDiff(sl, pl), 0.0f) << "left lane diverged under the parallel schedule";
    EXPECT_EQ(settledDiff(sr, pr), 0.0f) << "right lane diverged under the parallel schedule";
  }
}

// Branched routing: the trunk prefix runs serially up to the tap, then the
// trunk remainder and the branch lane fork. Same bit-exactness contract.
TEST(MultiCoreTest, BranchedParallelMatchesSerialBitExact) {
  const auto in = makeNoise(240 * kBlock, 31415, 0.1f);

  for (const bool oversample : {false, true}) {
    SCOPED_TRACE(juce::String("oversampling ") + (oversample ? "8x" : "off"));

    const auto [sl, sr] = runRig({false, true, 48000.0, oversample}, in);
    const auto [pl, pr] = runRig({true, true, 48000.0, oversample}, in);

    EXPECT_EQ(settledDiff(sl, pl), 0.0f) << "trunk lane diverged under the parallel schedule";
    EXPECT_EQ(settledDiff(sr, pr), 0.0f) << "branch lane diverged under the parallel schedule";
  }
}

// Informational speedup measurement (no assertion — timings are machine- and
// load-dependent): one heavy NAM lane per side, chain-stage wall time under
// the serial vs. parallel schedule. Expect the parallel run to approach the
// cost of one lane.
TEST(MultiCoreTest, ReportsParallelSpeedup) {
  auto measure = [](bool multiCore) {
    ChainTestProcessor proc;
    proc.setMultiCoreEnabled(multiCore, /*persist=*/false);
    proc.setPlayConfigDetails(2, 2, kFs, kBlock);
    proc.prepareToPlay(kFs, kBlock);

    juce::ValueTree state("TONE3000State");
    state.setProperty("stereoEnabled", true, nullptr);
    juce::ValueTree left("ChainBlocks");
    left.appendChild(makeNamBlockTree("blk-amp-l", 1, 100), nullptr);
    state.appendChild(left, nullptr);
    juce::ValueTree right("RightChainBlocks");
    right.appendChild(makeNamBlockTree("blk-amp-r", 2, 200), nullptr);
    state.appendChild(right, nullptr);
    proc.restoreFromTree(state);
    EXPECT_TRUE(waitForChainLoaded(proc)) << "blocks never finished loading from cache";

    const auto in = makeNoise(60 * kBlock, 4321, 0.1f);
    processStereo(proc, in);  // warm-up: fades settle, caches warm

    const auto t0 = juce::Time::getHighResolutionTicks();
    processStereo(proc, in);
    const auto t1 = juce::Time::getHighResolutionTicks();
    return juce::Time::highResolutionTicksToSeconds(t1 - t0);
  };

  const double serial = measure(false);
  const double parallel = measure(true);
  std::printf("  two NAM lanes, %.2f s audio: serial %.1f ms, parallel %.1f ms (%.2fx)\n",
              60.0 * kBlock / kFs, serial * 1000.0, parallel * 1000.0, serial / parallel);
}

// Host lifecycle: the worker is restarted by every prepareToPlay and stopped
// by releaseResources. Audio must flow correctly through stop/start cycles —
// including a processBlock after releaseResources (the dispatch gate falls
// back to serial when the worker is down, it must not deadlock or crash).
TEST(MultiCoreTest, WorkerSurvivesHostLifecycle) {
  ChainTestProcessor proc;
  proc.setMultiCoreEnabled(true, /*persist=*/false);
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);

  proc.restoreFromTree(makeStereoRigState());
  ASSERT_TRUE(waitForChainLoaded(proc)) << "blocks never finished loading from cache";

  const auto in = makeNoise(120 * kBlock, 999, 0.1f);
  auto rms = [](const std::vector<float>& v) {
    double sum = 0.0;
    for (float s : v)
      sum += static_cast<double>(s) * s;
    return std::sqrt(sum / static_cast<double>(v.size()));
  };

  for (int cycle = 0; cycle < 3; ++cycle) {
    SCOPED_TRACE("cycle " + juce::String(cycle));
    const auto [l, r] = processStereo(proc, in);
    EXPECT_GT(rms(l), 1e-4) << "left lane went silent";
    EXPECT_GT(rms(r), 1e-4) << "right lane went silent";

    proc.releaseResources();

    // A host should not process after releaseResources, but a defensive
    // block through the downed worker must degrade to serial, not hang.
    juce::AudioBuffer<float> stray(2, kBlock);
    stray.clear();
    juce::MidiBuffer midi;
    proc.processBlock(stray, midi);

    proc.prepareToPlay(kFs, kBlock);
  }
}

}  // namespace
