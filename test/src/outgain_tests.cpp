// Pins issue #99: Out Gain must apply to the combined dry+wet signal, after
// the mix blend, not to the wet term alone before it (see the tail loop in
// Processor.cpp's processChainOnBuffer). At mix < 100%, a wet-only gain
// leaves the dry share untouched, so pulling the knob down barely moves the
// block's actual output.
#include "chain_test_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

// Mono chain (not seedStereoChains's stereo + empty-right-lane setup): with
// stereo mode on and no blocks in the right lane, that lane is the raw,
// unprocessed input, and Balance/Pan blends it back into the final output -
// masking exactly the block-level gain change this test measures. Mono mode
// runs only the Left chain (no pan stage in the way), so the final output is
// this block's own tail loop, nothing else.
void seedMonoChain(ChainTestProcessor& proc, const juce::String& blockId, bool enabled = true) {
  juce::ValueTree state("ChainSnapshot");
  state.setProperty("stereoEnabled", false, nullptr);
  juce::ValueTree lane("ChainBlocks");
  auto block = makeIrBlockTree(blockId, 1, 100);
  block.setProperty("enabled", enabled, nullptr);
  lane.appendChild(block, nullptr);
  state.appendChild(lane, nullptr);
  state.appendChild(juce::ValueTree("RightChainBlocks"), nullptr);
  proc.restoreFromTree(state);
}

constexpr int kBlock = 512;
// mixSmoother/outputGainSmoother ramp over 50ms, wetFadeGain's load-in fade
// over 25ms (see ChainBlock.h's kWetFadeSeconds); 15 blocks @ 512/48k is
// 160ms, comfortably past both.
constexpr int kWarmupBlocks = 15;
constexpr int kRunBlocks = 20;

// A single IR block, driven with the given mix/outputGain (both normalized
// 0..1) already applied and settled, returns the following kRunBlocks worth
// of noise output. Everything upstream of the block's own mix/gain tail
// (input gate, convolution, IR normalization) is identical run to run since
// none of it reads mix or outputGain - only the noise seed distinguishes the
// discarded warmup from the measured window, so two calls with the same
// `mix`/`outputGain` reproduce the same dry/wet components bit-for-bit.
std::pair<std::vector<float>, std::vector<float>> runWithMixAndGain(double mix,
                                                                     double outputGainNormalized,
                                                                     bool enabled = true) {
  ChainTestProcessor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);

  seedMonoChain(proc, "blk-a", enabled);
  EXPECT_TRUE(waitForChainLoaded(proc)) << "IR block never finished loading from cache";

  EXPECT_TRUE(proc.setBlockParam("blk-a", "mix", mix));
  EXPECT_TRUE(proc.setBlockParam("blk-a", "outputGain", outputGainNormalized));
  letAudioGoIdle();

  processStereo(proc, makeNoise(kWarmupBlocks * kBlock, 1111, 0.25f));  // discard: settle smoothers
  return processStereo(proc, makeNoise(kRunBlocks * kBlock, 4242, 0.25f));
}

// A phase-continuous sine (envelope tests need a smooth carrier; noise's own
// sample-to-sample jumps would bury the click under test). Slices taken with
// matching offsets stay continuous across processStereo calls.
std::vector<float> makeSine(size_t numSamples, double freqHz, float amp,
                            size_t phaseOffsetSamples = 0) {
  std::vector<float> v(numSamples);
  const double w = 2.0 * juce::MathConstants<double>::pi * freqHz / kFs;
  for (size_t i = 0; i < numSamples; ++i)
    v[i] = amp * static_cast<float>(std::sin(w * static_cast<double>(i + phaseOffsetSamples)));
  return v;
}

float maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b, size_t skip) {
  float m = 0.0f;
  for (size_t i = skip; i < a.size(); ++i) m = std::max(m, std::abs(a[i] - b[i]));
  return m;
}

constexpr size_t kSkip = 0;  // no cross-run smoother drift here; see comment above
constexpr double kUnityNorm = 0.5;    // 0 dB relative (gainDb formula's center)
constexpr double kReducedNorm = 0.0;  // -24 dB relative: (0.0 - 0.5) * 48
constexpr double kMix = 0.5;
// Ratio between the two outputGain settings' linear gain. Derived from the
// documented normalized->dB mapping alone (0.5 span == 48 dB), not from the
// short-IR cab pad offset, so it holds regardless of that constant.
const double kGainRatio = std::pow(10.0, ((kReducedNorm - kUnityNorm) * 48.0) / 20.0);
}  // namespace

// At mix=0.5, pulling Out Gain down by 24 dB must measurably attenuate the
// combined dry+wet output by (close to) the full 24 dB - not just the wet
// half, which a wet-only gain would leave at roughly half that drop since
// the untouched dry share still carries half the signal's energy.
TEST(OutGainTest, OutGainAffectsFullSignal) {
  const auto [unityL, unityR] = runWithMixAndGain(kMix, kUnityNorm);
  const auto [reducedL, reducedR] = runWithMixAndGain(kMix, kReducedNorm);

  auto rms = [](const std::vector<float>& l, const std::vector<float>& r) {
    double sumSq = 0.0;
    for (float v : l) sumSq += static_cast<double>(v) * v;
    for (float v : r) sumSq += static_cast<double>(v) * v;
    return std::sqrt(sumSq / (l.size() + r.size()));
  };

  const double rmsUnity = rms(unityL, unityR);
  const double rmsReduced = rms(reducedL, reducedR);
  ASSERT_GT(rmsUnity, 0.0) << "unity-gain run produced silence - fixture is broken";
  ASSERT_GT(rmsReduced, 0.0) << "reduced-gain run produced silence";

  const double measuredDropDb = 20.0 * std::log10(rmsUnity / rmsReduced);
  std::printf("[OutGainTest] measured drop at mix=0.5, outputGain -24dB: %.2f dB\n", measuredDropDb);

  // A wet-only gain would attenuate roughly half the signal's energy (the
  // wet share), landing well under half the requested 24 dB; the fixed,
  // post-mix gain scales the whole combined signal, landing close to it.
  EXPECT_NEAR(measuredDropDb, 24.0, 3.0)
      << "Out Gain isn't attenuating the full combined signal - looks wet-only again (issue #99)";
}

// Same two reference points, reconstructed sample-by-sample instead of via
// RMS: isolate the block's dry component (mix=0) and wet component (mix=1),
// each captured at the same "unity" outputGain so they combine cleanly, then
// verify the actual mix=0.5/reduced-gain run matches the post-mix formula
// and NOT the old wet-only one - i.e. this test would have failed against
// the previous (wet-only) implementation.
TEST(OutGainTest, OutGainMatchesPostMixFormulaNotWetOnlyFormula) {
  const auto [dryL, dryR] = runWithMixAndGain(0.0, kUnityNorm);  // mix=0 -> pure dry * unityGain
  const auto [wetL, wetR] = runWithMixAndGain(1.0, kUnityNorm);  // mix=1 -> pure wet * unityGain
  const auto [actualL, actualR] = runWithMixAndGain(kMix, kReducedNorm);

  ASSERT_EQ(dryL.size(), wetL.size());
  ASSERT_EQ(dryL.size(), actualL.size());

  auto buildExpected = [](const std::vector<float>& dry, const std::vector<float>& wet,
                          bool postMixGain) {
    std::vector<float> out(dry.size());
    for (size_t i = 0; i < dry.size(); ++i) {
      const float d = dry[i], w = wet[i];
      out[i] = postMixGain
                   // fixed: gain applies to the combined signal
                   ? static_cast<float>((d * (1.0 - kMix) + w * kMix) * kGainRatio)
                   // broken: gain applies to the wet term alone, before mixing
                   : static_cast<float>(d * (1.0 - kMix) + (w * kGainRatio) * kMix);
    }
    return out;
  };

  const auto expectedFixedL = buildExpected(dryL, wetL, true);
  const auto expectedFixedR = buildExpected(dryR, wetR, true);
  const auto expectedBrokenL = buildExpected(dryL, wetL, false);
  const auto expectedBrokenR = buildExpected(dryR, wetR, false);

  const float diffFromFixed =
      std::max(maxAbsDiff(actualL, expectedFixedL, kSkip), maxAbsDiff(actualR, expectedFixedR, kSkip));
  const float diffFromBroken = std::max(maxAbsDiff(actualL, expectedBrokenL, kSkip),
                                        maxAbsDiff(actualR, expectedBrokenR, kSkip));
  std::printf("[OutGainTest] actual vs post-mix formula: max|diff|=%.9f; vs wet-only formula: max|diff|=%.9f\n",
              static_cast<double>(diffFromFixed), static_cast<double>(diffFromBroken));

  // Everything upstream of the mix/gain tail is identical across these three
  // runs (same input, same gate/convolution/normalization state each time),
  // so matching the correct formula should be tight - floating-point
  // rounding noise, not a real divergence.
  EXPECT_LT(diffFromFixed, 1e-4f)
      << "current output doesn't match the post-mix Out Gain formula from issue #99";
  // The wet-only formula predicts a substantially different signal (it
  // leaves the dry share at unity while this run's dry share should be
  // attenuated too); a real gap here, not rounding noise, is what proves
  // this test would have caught the original bug.
  EXPECT_GT(diffFromBroken, 5e-3f)
      << "current output matches the old wet-only formula - Out Gain regressed to pre-mix (issue #99)";
}

// The short-IR cab pad (-18 dB, invisible chain gain staging) belongs to the
// wet term, never the blend: a cab block at mix=0 with the knob at unity
// must pass dry through at pass-through level, exactly like a disabled
// block. Guards against the pad riding along when Out Gain moves post-mix -
// the two structural tests above can't see it (the RMS test is a ratio, so
// a constant pad cancels; the formula test's measured components already
// contain it).
TEST(OutGainTest, CabPadStaysOffTheDryPath) {
  const auto [refL, refR] = runWithMixAndGain(0.0, kUnityNorm, /*enabled=*/false);
  const auto [dryL, dryR] = runWithMixAndGain(0.0, kUnityNorm);

  auto rms = [](const std::vector<float>& v) {
    double s = 0.0;
    for (float x : v) s += static_cast<double>(x) * x;
    return std::sqrt(s / static_cast<double>(v.size()));
  };
  ASSERT_GT(rms(refL), 0.0) << "disabled-block reference produced silence - fixture is broken";
  const double db = 20.0 * std::log10(rms(dryL) / rms(refL));
  std::printf("[OutGainTest] enabled cab block @ mix=0 vs disabled block: %+.2f dB\n", db);
  EXPECT_NEAR(db, 0.0, 1.0)
      << "dry-path level shifts when a short-IR block is enabled at mix=0 - the cab pad is "
         "leaking off the wet term";
}

// Power-off must glide, never step: wetFadeGain crossfades the mix toward
// dry AND glides the post-mix Out Gain to unity, so the moment the faded
// block starts being skipped (unity pass-through) is seamless. A post-mix
// gain that ignores the fade parks the glide at dry * outGain and then jumps
// to unity dry when the skip kicks in - with the knob at -24 dB that's a
// 24 dB step. Tracked as a windowed peak envelope on a phase-continuous
// sine; the fixed fade spreads the level change across several windows while
// the broken landing concentrates it in one boundary.
TEST(OutGainTest, BypassFadeWithReducedGainNeverSteps) {
  ChainTestProcessor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);
  seedMonoChain(proc, "blk-a");
  ASSERT_TRUE(waitForChainLoaded(proc)) << "IR block never finished loading from cache";

  EXPECT_TRUE(proc.setBlockParam("blk-a", "mix", 1.0));
  EXPECT_TRUE(proc.setBlockParam("blk-a", "outputGain", kReducedNorm));
  letAudioGoIdle();

  const size_t warmupLen = static_cast<size_t>(kWarmupBlocks * kBlock);
  const size_t runLen = static_cast<size_t>(kRunBlocks * kBlock);
  processStereo(proc, makeSine(warmupLen, 220.0, 0.25f));  // settle at wet * -24 dB

  // Cut power, then keep the (phase-continuous) sine running through the
  // whole fade and past the skip boundary.
  EXPECT_TRUE(proc.setBlockParam("blk-a", "enabled", 0.0));
  const auto [outL, outR] = processStereo(proc, makeSine(runLen, 220.0, 0.25f, warmupLen));

  // Peak envelope in windows longer than the 220 Hz period, then the largest
  // window-to-window level jump.
  constexpr size_t kWin = 256;
  std::vector<float> env;
  for (size_t off = 0; off + kWin <= outL.size(); off += kWin) {
    float peak = 0.0f;
    for (size_t i = off; i < off + kWin; ++i) peak = std::max(peak, std::abs(outL[i]));
    env.push_back(peak);
  }
  double maxStepDb = 0.0;
  for (size_t i = 1; i < env.size(); ++i)
    if (env[i - 1] > 1e-6f && env[i] > 1e-6f)
      maxStepDb =
          std::max(maxStepDb, std::abs(20.0 * std::log10(env[i] / static_cast<double>(env[i - 1]))));
  std::printf("[OutGainTest] max window-to-window envelope step across power-off: %.2f dB\n",
              maxStepDb);

  // The 25 ms fade spreads the -24 dB -> unity landing over ~5 windows
  // (about 5 dB per window); the broken landing is a one-boundary 24 dB
  // jump. 12 dB splits the two with margin on both sides.
  EXPECT_LT(maxStepDb, 12.0) << "power-off with reduced Out Gain steps at the skip boundary "
                                "instead of gliding to unity dry";
}
