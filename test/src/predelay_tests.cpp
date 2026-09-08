// Pins that BlockPredelay only touches the wet path: at mix = 0 (pure dry),
// engaging predelay must have zero audible effect, since dryScratch is
// snapshotted before predelay ever runs (see Processor.cpp's per-block loop).
#include "chain_test_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr int kBlock = 512;
}  // namespace

// Comparing block output directly against the raw synthetic input (as an
// earlier version of this test did) is invalid: the global input gate
// (Processor.cpp's inputGate.process(), applied before the chain even
// starts) has its own envelope/threshold dynamics and is not a pure
// passthrough for noise. The only valid comparison is two full processor
// runs against *each other*, predelay off vs on, everything else identical
// - isolating predelay's own contribution from the rest of the signal path.
TEST(PredelayTest, DoesNotAffectDryPath) {
  auto runWithPredelay = [](double predelayNormalized) {
    ChainTestProcessor proc;
    proc.setPlayConfigDetails(2, 2, kFs, kBlock);
    proc.prepareToPlay(kFs, kBlock);

    seedStereoChains(proc, {"blk-a"}, {});
    EXPECT_TRUE(waitForChainLoaded(proc)) << "IR block never finished loading from cache";

    // Pure dry: mix = 0. Predelay is the only thing that differs between
    // the two runs this is called for.
    EXPECT_TRUE(proc.setBlockParam("blk-a", "mix", 0.0));
    EXPECT_TRUE(proc.setBlockParam("blk-a", "predelay", predelayNormalized));
    letAudioGoIdle();

    // mixSmoother (50ms) and wetFadeGain (25ms) only advance while
    // processBlock actually runs - letAudioGoIdle() is wall-clock, not
    // audio time. Warm up on real audio so both are fully converged before
    // the comparison window (same margin DelaysOnlyTheWetPath proves
    // sufficient below); discard this output.
    constexpr int kWarmupBlocks = 15;
    processStereo(proc, makeNoise(kWarmupBlocks * kBlock, 1111, 0.25f));

    return processStereo(proc, makeNoise(20 * kBlock, 4242, 0.25f));
  };

  const auto [noDelayL, noDelayR] = runWithPredelay(0.0);
  // Max predelay, so any leak into dry would be maximally audible/obvious.
  const auto [maxDelayL, maxDelayR] = runWithPredelay(1.0);

  ASSERT_EQ(noDelayL.size(), maxDelayL.size());
  float maxAbsDiff = 0.0f;
  for (size_t i = 0; i < noDelayL.size(); ++i) {
    maxAbsDiff = std::max(maxAbsDiff, std::abs(noDelayL[i] - maxDelayL[i]));
    maxAbsDiff = std::max(maxAbsDiff, std::abs(noDelayR[i] - maxDelayR[i]));
  }
  // Not bit-exact: two independently-run instances through a chain with a
  // noise gate, DC blocker and FFT convolver naturally accumulate ordinary
  // floating-point rounding noise (unlike multicore_tests.cpp's bit-identical
  // guarantee, which compares reordered ops on the *same* instance/inputs).
  // A real predelay->dry leak would show up as a ~1-second-scale waveform
  // shift, i.e. an amplitude-scale difference, not a rounding-floor one.
  const float maxAbsDiffDb = juce::Decibels::gainToDecibels(maxAbsDiff, -300.0f);
  std::printf("[PredelayTest] max |diff| between predelay=0 and predelay=max at mix=0: %.9f (%.1f dB)\n",
              static_cast<double>(maxAbsDiff), static_cast<double>(maxAbsDiffDb));
  EXPECT_LT(maxAbsDiff, 1e-4f) << "dry path diverges far more than floating-point rounding noise "
                                  "explains - predelay is likely leaking into the dry signal";
}

TEST(PredelayTest, DelaysOnlyTheWetPath) {
  ChainTestProcessor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);

  seedStereoChains(proc, {"blk-a"}, {});
  ASSERT_TRUE(waitForChainLoaded(proc));

  // Fully wet: output is exactly the convolved+predelayed signal, nothing
  // dry blended in (mix=1 is also the short-cab load default, so no ramp
  // to wait out there). Total buffer must comfortably fit
  // warm-up + predelay + the cab IR's own onset, or the delayed onset
  // never appears inside the window at all.
  ASSERT_TRUE(proc.setBlockParam("blk-a", "mix", 1.0));
  ASSERT_TRUE(proc.setBlockParam("blk-a", "predelay", 0.0));
  letAudioGoIdle();

  constexpr int kTotalBlocks = 90;   // 960ms @ 48k/512
  // Fires at ~160ms: past both the 25ms wetFadeGain ramp and predelay's own
  // ~80ms default live-change ramp (see BlockPredelay), so proc2's impulse
  // below hits the fully-settled 200ms target, not a still-ramping value.
  constexpr int kImpulseBlock = 15;
  std::vector<float> impulse(static_cast<size_t>(kTotalBlocks * kBlock), 0.0f);
  impulse[static_cast<size_t>(kImpulseBlock * kBlock)] = 1.0f;
  const auto [noDelayL, noDelayR] = processStereo(proc, impulse);
  juce::ignoreUnused(noDelayR);

  // First sample where the convolved impulse response rises above the noise
  // floor (the cab IR's own onset, no predelay).
  auto firstAboveFloor = [](const std::vector<float>& v, float floor) {
    for (size_t i = 0; i < v.size(); ++i)
      if (std::abs(v[i]) > floor) return i;
    return v.size();
  };
  const float floor = 1e-5f;
  const size_t onsetNoDelay = firstAboveFloor(noDelayL, floor);
  ASSERT_LT(onsetNoDelay, noDelayL.size()) << "IR produced no measurable output at all";

  // Same impulse, now with 200ms predelay (9600 samples @ 48k): the wet
  // onset should land ~200ms later than the undelayed case. predelay's own
  // live-change ramp (~80ms default) is well clear of the impulse at
  // kImpulseBlock (43ms in) since it starts ramping from the very first
  // processBlock call, before the impulse fires.
  ChainTestProcessor proc2;
  proc2.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc2.prepareToPlay(kFs, kBlock);
  seedStereoChains(proc2, {"blk-a"}, {});
  ASSERT_TRUE(waitForChainLoaded(proc2));
  ASSERT_TRUE(proc2.setBlockParam("blk-a", "mix", 1.0));
  ASSERT_TRUE(proc2.setBlockParam("blk-a", "predelay", 0.2));  // 200ms
  letAudioGoIdle();

  const auto [delayedL, delayedR] = processStereo(proc2, impulse);
  juce::ignoreUnused(delayedR);
  const size_t onsetDelayed = firstAboveFloor(delayedL, floor);
  ASSERT_LT(onsetDelayed, delayedL.size()) << "Predelayed IR produced no measurable output at all";

  const size_t expectedShiftSamples = static_cast<size_t>(0.2 * kFs);
  const size_t actualShift = onsetDelayed - onsetNoDelay;
  EXPECT_NEAR(static_cast<double>(actualShift), static_cast<double>(expectedShiftSamples), 50.0)
      << "onset(no predelay)=" << onsetNoDelay << " onset(200ms predelay)=" << onsetDelayed;
}
