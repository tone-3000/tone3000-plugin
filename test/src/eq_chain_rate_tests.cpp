// Pins that a block's EQ and spectrum analyzer are designed for the *chain*
// rate (48 kHz × oversampling factor) no matter how the block entered the
// chain. Regression guard for issue #212: blocks added mid-session (loadTone,
// i.e. every browser add and local file load) kept BlockEq's default 48 kHz,
// so inside an oversampled chain every band landed at freq × factor: a bell
// dialed in at 250 Hz boosted 1 kHz at ×4, 500 Hz at ×2, and snapped back to
// 250 Hz on the next prepareToPlay. Blocks that arrive through state restore
// (presets, undo, DAW state) were always prepared correctly, which is why the
// other chain tests, all of which seed blocks through restore, never saw it;
// both origins are exercised here.
#include "chain_test_helpers.h"

#include <cmath>
#include <string>

namespace {
constexpr int kBlock = 512;

// The bell under test, and a probe two octaves above it: far enough that a
// Q 2 bell is flat there, and exactly where a 250 Hz design lands at ×4.
constexpr double kBellHz = 250.0;
constexpr double kBellGainDb = 15.0;
constexpr double kBellQ = 2.0;
constexpr double kFarHz = 1000.0;

// Integer cycles of both probe tones at kFs, so Goertzel reads them cleanly.
constexpr int kProbeBlocks = 48;

enum class BlockOrigin { Restore, LoadTone };

const char* name(BlockOrigin origin) {
  return origin == BlockOrigin::Restore ? "restore" : "loadTone";
}

// Oversampling as a host sets it: factor 1 = off; 2/4/8 = osFactor choice
// index 0/1/2 (normalized over the three choices).
void setOversampling(TONE3000Processor& proc, int factor) {
  const bool on = factor > 1;
  proc.parameters.getParameter("osEnabled")->setValueNotifyingHost(on ? 1.0f : 0.0f);
  if (on) {
    const int choiceIndex = factor == 2 ? 0 : factor == 4 ? 1 : 2;
    proc.parameters.getParameter("osFactor")->setValueNotifyingHost(choiceIndex / 2.0f);
  }
}

// A prepared processor with one fully-wet cab IR block. The oversampling
// factor is in force *before* the block arrives, so the block has to meet
// the chain rate on its own. Returns the block's id.
std::string rigWithOneBlock(ChainTestProcessor& proc, BlockOrigin origin, int osFactor) {
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  setOversampling(proc, osFactor);
  proc.prepareToPlay(kFs, kBlock);

  std::string blockId;
  if (origin == BlockOrigin::Restore) {
    seedStereoChains(proc, {"blk-a"}, {});
    blockId = "blk-a";
  } else {
    const juce::var res =
        proc.loadLocalTone("cab-ir-test", filesOf({testFileEntry("cab-ir-test.wav")}));
    EXPECT_TRUE(res["error"].isVoid()) << res["error"].toString().toStdString();
    blockId = res["blockId"].toString().toStdString();
  }
  EXPECT_FALSE(blockId.empty());
  EXPECT_TRUE(waitForChainLoaded(proc)) << "block never finished loading from cache";
  EXPECT_TRUE(proc.setBlockParam(blockId, "mix", 1.0));  // the EQ shapes everything we hear
  return blockId;
}

// (`far`/`near` are legacy empty macros in <windows.h>; avoid them as names.)
std::vector<float> twoTone(int frames) {
  auto probe = makeSine(frames, kBellHz, 0.15f);
  const auto upper = makeSine(frames, kFarHz, 0.15f);
  for (size_t i = 0; i < probe.size(); ++i)
    probe[i] += upper[i];
  return probe;
}

struct ProbeLevels {
  double atBellHzDb, atFarHzDb;
};

// Output level at both probe frequencies, through a block whose band 2 is
// either flat (default) or the bell under test.
ProbeLevels measure(BlockOrigin origin, int osFactor, bool withBell) {
  ChainTestProcessor proc;
  const std::string blockId = rigWithOneBlock(proc, origin, osFactor);
  if (withBell)
    EXPECT_TRUE(proc.setBlockEqBand(blockId, 2, eqBandVar("bell", kBellHz, kBellGainDb, kBellQ)));
  letAudioGoIdle();

  // Smoothers and fades only advance on real audio: converge them first.
  processStereo(proc, twoTone(20 * kBlock));
  const auto [outL, outR] = processStereo(proc, twoTone(kProbeBlocks * kBlock));
  juce::ignoreUnused(outR);
  return {db(goertzelPower(outL, kBellHz)), db(goertzelPower(outL, kFarHz))};
}
}  // namespace

// Two full runs per rig, flat vs. bell, everything else identical, so the
// difference is the EQ's own response: +15 dB at the bell's frequency and
// nothing two octaves up. A stale 48 kHz design fails both at once (at ×4
// the two readings swap; at ×2 both sit on the skirt of a 500 Hz bell).
TEST(EqChainRateTest, BellLandsOnItsFrequencyHoweverTheBlockArrived) {
  for (const BlockOrigin origin : {BlockOrigin::Restore, BlockOrigin::LoadTone}) {
    for (const int osFactor : {1, 2, 4}) {
      SCOPED_TRACE(std::string(name(origin)) + " block, oversampling x" +
                   std::to_string(osFactor));
      const ProbeLevels flat = measure(origin, osFactor, /*withBell=*/false);
      const ProbeLevels bell = measure(origin, osFactor, /*withBell=*/true);

      EXPECT_NEAR(bell.atBellHzDb - flat.atBellHzDb, kBellGainDb, 1.0)
          << "the bell missed its own frequency";
      EXPECT_NEAR(bell.atFarHzDb - flat.atFarHzDb, 0.0, 1.0)
          << "the bell moved a frequency two octaves away";
    }
  }
}

// The analyzer backdrop maps FFT bins to Hz with the same rate (a stale
// 48 kHz mapping draws a 1 kHz tone at 250 Hz under ×4), so it has to meet
// the chain rate on the same paths. Checked on the mid-session origin, the
// one that used to miss it.
TEST(EqChainRateTest, AnalyzerPeaksAtTheToneFrequencyForMidSessionBlock) {
  ChainTestProcessor proc;
  const std::string blockId = rigWithOneBlock(proc, BlockOrigin::LoadTone, 4);
  ASSERT_TRUE(proc.setBlockSpectrumEnabled(blockId, true));
  letAudioGoIdle();
  processStereo(proc, makeSine(kProbeBlocks * kBlock, kFarHz, 0.25f));

  // Analyzer ballistics: analyses are throttled to ~30 ms and the display
  // attacks from the floor over a few of them.
  juce::var spectrum;
  for (int poll = 0; poll < 8; ++poll) {
    juce::Thread::sleep(35);
    spectrum = proc.getBlockSpectrum(blockId);
  }
  const auto* bins = spectrum.getArray();
  ASSERT_NE(bins, nullptr);
  ASSERT_EQ(bins->size(), BlockSpectrum::kNumBins);

  int peakBin = 0;
  for (int i = 1; i < bins->size(); ++i)
    if (static_cast<double>((*bins)[i]) > static_cast<double>((*bins)[peakBin]))
      peakBin = i;

  // Centre of that bin on the analyzer's log axis (see BlockSpectrum).
  const double logRatio = std::log(BlockSpectrum::kMaxFreqHz / BlockSpectrum::kMinFreqHz);
  const double peakHz =
      BlockSpectrum::kMinFreqHz * std::exp(logRatio * (peakBin + 0.5) / BlockSpectrum::kNumBins);
  // One analyzer bin is ~0.16 octave wide; allow a neighbour.
  EXPECT_NEAR(std::log2(peakHz / kFarHz), 0.0, 0.25)
      << "analyzer peak drawn at " << peakHz << " Hz for a " << kFarHz << " Hz tone";
}
