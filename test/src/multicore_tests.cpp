// Multi-core processing tests
//
// Multi-core mode spreads the chain stage across the RtWorkerPool realtime
// workers (see RtWorkerPool.h) in two places: stereo mode forks the two
// lanes (the Right/branch lane on a worker, the other on the audio thread),
// and a NAM engine forks its voice × phase instances (oversampling phases,
// dual mono voices, or both), nested inside a lane fork when both apply.
// Parallelism is pure scheduling (no arithmetic or ordering changes
// anywhere), so its one testable contract is strong:
//
//   - the parallel schedule's output is BIT-IDENTICAL to the serial one,
//     across topologies (independent lanes, branched, mono, dual mono), host
//     rates and oversampling factors, and across mid-stream toggles of the
//     setting,
//   - the pool survives the host lifecycle (re-prepare, release, restart)
//     with audio flowing throughout.
//
// The stereo rigs deliberately give the lanes different chains and sub-unity
// mix values: a cross-lane scratch race (the historical hazard: the dry-mix
// buffer used to be shared) corrupts exactly the dry portion of the blend,
// which identical lanes or mix = 1.0 would hide. The mono rigs isolate the
// phase fork: no lanes fork in mono, so any serial/parallel divergence there
// is the NAM phase path alone.
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
// convolver engagement), but expects *zero* difference after it.
float settledDiff(const std::vector<float>& a, const std::vector<float>& b) {
  return settledMaxChannelDiff(a, b, 48000);
}

// Left lane: amp + cab (mix 0.7 on the cab). Right lane: cab only (mix 0.4).
// Different chains and different mixes per lane, so lane cross-talk of any
// kind (scratch, engines, gains) shows up as a serial/parallel mismatch.
juce::ValueTree makeStereoRigState() {
  juce::ValueTree state("ChainSnapshot");
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

// Mono + oversampling isolates the NAM phase fork (mono mode never forks
// lanes): the engine's phase instances run on pool workers when multi-core
// is on and sequentially when it's off, and the two schedules must null
// exactly at every factor.
TEST(MultiCoreTest, MonoPhaseParallelMatchesSerialBitExact) {
  auto runMonoRig = [](bool multiCore, float osFactorNormalized, const std::vector<float>& in) {
    ChainTestProcessor proc;
    proc.setMultiCoreEnabled(multiCore, /*persist=*/false);
    proc.setPlayConfigDetails(2, 2, kFs, kBlock);
    proc.parameters.getParameter("osEnabled")->setValueNotifyingHost(1.0f);
    proc.parameters.getParameter("osFactor")->setValueNotifyingHost(osFactorNormalized);
    proc.prepareToPlay(kFs, kBlock);

    juce::ValueTree state("ChainSnapshot");
    juce::ValueTree lane("ChainBlocks");
    lane.appendChild(makeNamBlockTree("blk-amp", 1, 100), nullptr);
    auto cab = makeIrBlockTree("blk-cab", 2, 200);
    cab.setProperty("mix", 0.7f, nullptr);
    lane.appendChild(cab, nullptr);
    state.appendChild(lane, nullptr);
    proc.restoreFromTree(state);
    EXPECT_TRUE(waitForChainLoaded(proc)) << "blocks never finished loading from cache";

    return processStereo(proc, in);
  };

  struct Factor {
    float normalized;
    const char* label;
  };
  const auto in = makeNoise(240 * kBlock, 27182, 0.1f);

  for (const Factor f : {Factor{0.0f, "2x"}, Factor{0.5f, "4x"}, Factor{1.0f, "8x"}}) {
    SCOPED_TRACE(juce::String("oversampling ") + f.label);

    const auto [sl, sr] = runMonoRig(false, f.normalized, in);
    const auto [pl, pr] = runMonoRig(true, f.normalized, in);

    EXPECT_EQ(settledDiff(sl, pl), 0.0f) << "phase fork diverged from the serial phase loop";
    EXPECT_EQ(settledDiff(sr, pr), 0.0f) << "fan-out channel diverged";
  }
}

// Dual mono (Processor.h, InputMode::DualMono): a mono chain fed two takes
// runs a two-voice NAM engine, and with multi-core on the engine forks all
// its voice × phase instances in one flat fork (2 jobs plain, 2 × factor
// oversampled: 16 at 8x, the pool's job cap). Same contract, with a
// per-voice assertion: distinct takes, each voice must null exactly against
// the serial schedule, and the two must stay different (a fan-out would
// make them equal).
TEST(MultiCoreTest, DualMonoParallelMatchesSerialBitExact) {
  auto runDualMonoRig = [](bool multiCore, float osFactorNormalized,
                           const std::vector<float>& inL, const std::vector<float>& inR) {
    ChainTestProcessor proc;
    proc.setMultiCoreEnabled(multiCore, /*persist=*/false);
    proc.setPlayConfigDetails(2, 2, kFs, kBlock);
    if (osFactorNormalized >= 0.0f) {
      proc.parameters.getParameter("osEnabled")->setValueNotifyingHost(1.0f);
      proc.parameters.getParameter("osFactor")->setValueNotifyingHost(osFactorNormalized);
    }
    proc.prepareToPlay(kFs, kBlock);

    juce::ValueTree state("ChainSnapshot");
    juce::ValueTree lane("ChainBlocks");
    lane.appendChild(makeNamBlockTree("blk-amp", 1, 100), nullptr);
    auto cab = makeIrBlockTree("blk-cab", 2, 200);
    cab.setProperty("mix", 0.7f, nullptr);
    lane.appendChild(cab, nullptr);
    state.appendChild(lane, nullptr);
    proc.restoreFromTree(state, "dual");
    EXPECT_TRUE(waitForChainLoaded(proc)) << "blocks never finished loading from cache";
    EXPECT_EQ(proc.namEngineVoiceCount("blk-amp"), 2);

    return processStereoLR(proc, inL, inR);
  };

  struct Factor {
    float normalized;  // < 0 = oversampling off
    const char* label;
  };
  const auto inL = makeNoise(240 * kBlock, 1001, 0.1f);
  const auto inR = makeNoise(240 * kBlock, 2002, 0.1f);

  for (const Factor f :
       {Factor{-1.0f, "off"}, Factor{0.0f, "2x"}, Factor{0.5f, "4x"}, Factor{1.0f, "8x"}}) {
    SCOPED_TRACE(juce::String("oversampling ") + f.label);

    const auto [sl, sr] = runDualMonoRig(false, f.normalized, inL, inR);
    const auto [pl, pr] = runDualMonoRig(true, f.normalized, inL, inR);

    EXPECT_GT(settledDiff(sl, sr), 1e-3f) << "the two voices should carry different takes";
    EXPECT_EQ(settledDiff(sl, pl), 0.0f) << "left voice diverged under the parallel schedule";
    EXPECT_EQ(settledDiff(sr, pr), 0.0f) << "right voice diverged under the parallel schedule";
  }
}

// The toggle is pure scheduling and applies per callback, so flipping it
// repeatedly mid-stream (as a user would from Settings) must leave the
// output bit-identical to a run that never toggled: the hardest version of
// the contract, on the full stereo rig at 8x (lane forks with nested phase
// forks appearing and disappearing between blocks).
TEST(MultiCoreTest, MidStreamTogglesStayBitExact) {
  const auto in = makeNoise(240 * kBlock, 16180, 0.1f);

  const auto [sl, sr] = runRig({false, false, 48000.0, true}, in);

  ChainTestProcessor proc;
  proc.setMultiCoreEnabled(true, /*persist=*/false);
  proc.setPlayConfigDetails(2, 2, 48000.0, kBlock);
  proc.parameters.getParameter("osEnabled")->setValueNotifyingHost(1.0f);
  proc.parameters.getParameter("osFactor")->setValueNotifyingHost(1.0f);  // 8x
  proc.prepareToPlay(48000.0, kBlock);
  proc.restoreFromTree(makeStereoRigState());
  ASSERT_TRUE(waitForChainLoaded(proc)) << "blocks never finished loading from cache";

  // Feed the same signal in 30-block chunks, flipping the setting between
  // chunks (processor state carries across processStereo calls).
  std::vector<float> tl, tr;
  bool multiCore = true;
  constexpr size_t kChunk = 30 * kBlock;
  for (size_t off = 0; off < in.size(); off += kChunk) {
    const std::vector<float> chunk(in.begin() + static_cast<long>(off),
                                   in.begin() + static_cast<long>(off + kChunk));
    const auto [cl, cr] = processStereo(proc, chunk);
    tl.insert(tl.end(), cl.begin(), cl.end());
    tr.insert(tr.end(), cr.begin(), cr.end());
    multiCore = !multiCore;
    proc.setMultiCoreEnabled(multiCore, /*persist=*/false);
  }

  EXPECT_EQ(settledDiff(sl, tl), 0.0f) << "left output diverged across mid-stream toggles";
  EXPECT_EQ(settledDiff(sr, tr), 0.0f) << "right output diverged across mid-stream toggles";
}

// Informational speedup measurement (no assertion; timings are machine- and
// load-dependent): one heavy NAM lane per side, chain-stage wall time under
// the serial vs. parallel schedule. Expect the parallel run to approach the
// cost of one lane.
TEST(MultiCoreTest, ReportsParallelSpeedup) {
  auto measure = [](bool multiCore) {
    ChainTestProcessor proc;
    proc.setMultiCoreEnabled(multiCore, /*persist=*/false);
    proc.setPlayConfigDetails(2, 2, kFs, kBlock);
    proc.prepareToPlay(kFs, kBlock);

    juce::ValueTree state("ChainSnapshot");
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

// Informational, the headline case for the phase fork: a mono NAM at 8x
// runs 8 phase instances per block, the worst single-thread load in the
// plugin when serial and near-ideal parallelism when forked (8 independent
// equal-size jobs). Expect the largest speedup of the suite here.
TEST(MultiCoreTest, ReportsOversampledSpeedup) {
  auto measure = [](bool multiCore) {
    ChainTestProcessor proc;
    proc.setMultiCoreEnabled(multiCore, /*persist=*/false);
    proc.setPlayConfigDetails(2, 2, kFs, kBlock);
    proc.parameters.getParameter("osEnabled")->setValueNotifyingHost(1.0f);
    proc.parameters.getParameter("osFactor")->setValueNotifyingHost(1.0f);  // 8x
    proc.prepareToPlay(kFs, kBlock);

    juce::ValueTree state("ChainSnapshot");
    juce::ValueTree lane("ChainBlocks");
    lane.appendChild(makeNamBlockTree("blk-amp", 1, 100), nullptr);
    state.appendChild(lane, nullptr);
    proc.restoreFromTree(state);
    EXPECT_TRUE(waitForChainLoaded(proc)) << "blocks never finished loading from cache";

    const auto in = makeNoise(60 * kBlock, 1618, 0.1f);
    processStereo(proc, in);  // warm-up: fades settle, caches warm

    const auto t0 = juce::Time::getHighResolutionTicks();
    processStereo(proc, in);
    const auto t1 = juce::Time::getHighResolutionTicks();
    return juce::Time::highResolutionTicksToSeconds(t1 - t0);
  };

  const double serial = measure(false);
  const double parallel = measure(true);
  std::printf("  mono NAM at 8x, %.2f s audio: serial %.1f ms, parallel %.1f ms (%.2fx)\n",
              60.0 * kBlock / kFs, serial * 1000.0, parallel * 1000.0, serial / parallel);
}

// Host lifecycle: the pool is restarted by every prepareToPlay and stopped
// by releaseResources. Audio must flow correctly through stop/start cycles,
// including a processBlock after releaseResources (the fork gates fall
// back to serial when the pool is down, they must not deadlock or crash).
// Oversampling stays on so both fork sites (lanes and nested NAM phases)
// ride through every cycle.
TEST(MultiCoreTest, WorkerSurvivesHostLifecycle) {
  ChainTestProcessor proc;
  proc.setMultiCoreEnabled(true, /*persist=*/false);
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.parameters.getParameter("osEnabled")->setValueNotifyingHost(1.0f);
  proc.parameters.getParameter("osFactor")->setValueNotifyingHost(1.0f);  // 8x
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
