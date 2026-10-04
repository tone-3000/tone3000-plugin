// Pins that POST-position block EQ only touches the wet path: at mix = 0
// (pure dry), engaging a strongly shaped POST band must have zero audible
// effect, since the EQ runs on the wet buffer before the dry/wet blend
// (see Processor.cpp's per-block loop). Regression guard: if POST EQ ever
// moves back onto the already-mixed buffer, even a pure-dry block (mix = 0)
// gets colored by EQ, and DoesNotAffectDryPath below catches it.
#include "chain_test_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr int kBlock = 512;

// A strongly shaped POST band, well clear of "inert" (~0 dB) so the EQ is
// unambiguously active.
juce::var shapedBand() { return eqBandVar("bell", 1500.0, 12.0, 1.2); }

// Two full processor runs, EQ flat (default, isActive() == false, a no-op)
// vs. a strongly shaped POST band, everything else identical - isolating the
// EQ's own contribution from the rest of the signal path. Comparing against
// the raw input would be invalid because of the global input gate's own
// dynamics; only two full runs against *each other* isolate the stage under
// test.
std::pair<std::vector<float>, std::vector<float>> runWithEq(double mix, bool shapeEq) {
  ChainTestProcessor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);

  seedStereoChains(proc, {"blk-a"}, {});
  EXPECT_TRUE(waitForChainLoaded(proc)) << "IR block never finished loading from cache";

  EXPECT_TRUE(proc.setBlockParam("blk-a", "mix", mix));
  if (shapeEq)
    EXPECT_TRUE(proc.setBlockEqBand("blk-a", 2, shapedBand()));
  letAudioGoIdle();

  // mixSmoother (50ms) and wetFadeGain (25ms) only advance while
  // processBlock actually runs - letAudioGoIdle() is wall-clock, not audio
  // time. Warm up on real audio so both are fully converged before the
  // comparison window.
  constexpr int kWarmupBlocks = 15;
  processStereo(proc, makeNoise(kWarmupBlocks * kBlock, 1111, 0.25f));

  return processStereo(proc, makeNoise(20 * kBlock, 4242, 0.25f));
}

float maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
  float maxDiff = 0.0f;
  for (size_t i = 0; i < a.size(); ++i) maxDiff = std::max(maxDiff, std::abs(a[i] - b[i]));
  return maxDiff;
}
}  // namespace

TEST(EqPostRoutingTest, DoesNotAffectDryPath) {
  const auto [flatL, flatR] = runWithEq(/*mix=*/0.0, /*shapeEq=*/false);
  const auto [shapedL, shapedR] = runWithEq(/*mix=*/0.0, /*shapeEq=*/true);

  ASSERT_EQ(flatL.size(), shapedL.size());
  // Not bit-exact: two independently-run instances through a chain with a
  // noise gate, DC blocker and FFT convolver naturally accumulate ordinary
  // floating-point rounding noise. A real EQ->dry leak would show up as a
  // broadband spectral difference, not a rounding-floor one.
  const float diff = std::max(maxAbsDiff(flatL, shapedL), maxAbsDiff(flatR, shapedR));
  const float diffDb = juce::Decibels::gainToDecibels(diff, -300.0f);
  std::printf(
      "[EqPostRoutingTest] max |diff| between EQ flat and EQ shaped at mix=0: %.9f (%.1f dB)\n",
      static_cast<double>(diff), static_cast<double>(diffDb));
  EXPECT_LT(diff, 1e-4f) << "dry path diverges far more than floating-point rounding noise "
                            "explains - POST EQ is likely leaking into the dry signal";
}

// Companion to the test above: proves the shaped band actually does
// something, so a bug that made POST EQ a global no-op couldn't pass
// DoesNotAffectDryPath vacuously.
TEST(EqPostRoutingTest, AffectsWetPath) {
  const auto [flatL, flatR] = runWithEq(/*mix=*/1.0, /*shapeEq=*/false);
  const auto [shapedL, shapedR] = runWithEq(/*mix=*/1.0, /*shapeEq=*/true);

  ASSERT_EQ(flatL.size(), shapedL.size());
  const float diff = std::max(maxAbsDiff(flatL, shapedL), maxAbsDiff(flatR, shapedR));
  EXPECT_GT(diff, 1e-3f) << "shaped POST band had no measurable effect on the fully-wet output";
}
