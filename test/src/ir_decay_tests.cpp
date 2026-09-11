// Pins the IR envelope: setBlockIrDecay shapes a 2-segment Attack/Decay gain
// envelope over the truncated IR content (Init Level at sample 0, ramping
// through Attack to unity/0dB - Attack's peak is pinned, not adjustable -
// then through Decay to Decay Level - see
// TONE3000Processor::prepareIrShapeRebuild), feeding the same off-thread
// rebuild path Length used to (rebuildIrShapeInBackground), never touches the
// dry path, and the shape survives a state round-trip. Decay Length is the
// TOTAL truncated length (the real "End" position, a fraction of the
// block's full detected content); Attack Length is a fraction *of that
// total*, marking where the peak sits within it - this file also covers
// what ir_length_tests.cpp used to (Length no longer exists as its own
// knob, folded into Decay Length).
#include "chain_test_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr int kBlock = 512;

void seedMonoIrChain(ChainTestProcessor& proc, const juce::String& blockId,
                     const char* fileName = "reverb-ir-mono-test.wav") {
  juce::ValueTree state("ChainSnapshot");
  state.setProperty("stereoEnabled", false, nullptr);
  juce::ValueTree lane("ChainBlocks");
  lane.appendChild(makeIrBlockTree(blockId, 1, 100, fileName), nullptr);
  state.appendChild(lane, nullptr);
  state.appendChild(juce::ValueTree("RightChainBlocks"), nullptr);
  proc.restoreFromTree(state);
}

// See ir_length_tests.cpp's (removed) own copy of this helper for the full
// story: a naive sleep-based wait lets requestSwapFadeAndWait's
// !isAudioActive() early return skip the entire mute/unmute mechanism,
// silently testing the wrong code path. Keep the heartbeat alive with small
// silent pumps throughout, priming immediately before the triggering call so
// there's no cold gap before the background job can reach its own
// fade-request.
void pumpAudio(ChainTestProcessor& proc, int totalMs, bool alreadyWarm = false) {
  const std::vector<float> silence(static_cast<size_t>(kBlock), 0.0f);
  constexpr int kPumpIntervalMs = 50;
  if (!alreadyWarm)
    processStereo(proc, silence);
  for (int elapsed = 0; elapsed < totalMs; elapsed += kPumpIntervalMs) {
    juce::Thread::sleep(kPumpIntervalMs);
    processStereo(proc, silence);
  }
}

// Mirrors ChainBlock.h's own no-op defaults: no attack ramp, decay spans the
// full (remaining) content, every level at unity. Attack's peak is pinned at
// unity/0dB (not a param), matching standard AD-envelope semantics.
struct EnvelopeParams {
  double initLevel = 1.0;
  double attackLength = 0.0;
  double attackCurve = 0.5;
  double decayLength = 1.0;
  double decayLevel = 1.0;
  double decayCurve = 0.5;
};

void setEnvelopeAndWaitForRebuild(ChainTestProcessor& proc, const char* blockId,
                                  const EnvelopeParams& p) {
  const std::vector<float> silence(static_cast<size_t>(kBlock), 0.0f);
  processStereo(proc, silence);  // prime
  ASSERT_TRUE(proc.setBlockIrDecay(blockId, p.initLevel, p.attackLength, p.attackCurve,
                                   p.decayLength, p.decayLevel, p.decayCurve));
  pumpAudio(proc, 1200, /*alreadyWarm=*/true);
}

// Placed well into the buffer (not sample 0, mirroring
// PredelayTest.DelaysOnlyTheWetPath): a spike at the very start can get
// suppressed by the input noise gate before it ever reaches the convolver.
constexpr int kImpulseBlock = 15;
constexpr size_t kImpulseOnset = static_cast<size_t>(kImpulseBlock) * kBlock;

std::vector<float> makeDelayedImpulse(int totalBlocks) {
  std::vector<float> impulse(static_cast<size_t>(totalBlocks * kBlock), 0.0f);
  impulse[kImpulseOnset] = 1.0f;
  return impulse;
}

// Continuous noise integrates across the *whole* kernel on every output
// sample (each sample is a convolution sum over all taps), so a change
// confined to the tail barely moves overall RMS - not useful for isolating
// the envelope's effect. A single impulse instead traces the (now-shaped)
// kernel directly: output[onset + i] is just kernel[i] (scaled by any live
// gains).
size_t lastAboveFloor(const std::vector<float>& v, float floor) {
  for (size_t i = v.size(); i-- > kImpulseOnset;)
    if (std::abs(v[i]) > floor) return i;
  return kImpulseOnset;
}

double rmsFrom(const std::vector<float>& v, size_t from) {
  double sumSq = 0.0;
  size_t n = 0;
  for (size_t i = from; i < v.size(); ++i) {
    sumSq += static_cast<double>(v[i]) * v[i];
    ++n;
  }
  return n > 0 ? std::sqrt(sumSq / n) : 0.0;
}
}  // namespace

// A shorter Decay Length (with Attack at its default zero) should measurably
// shrink the convolved tail relative to the default (untruncated) envelope -
// the direct signature that Attack+Decay Length together replace what the
// old standalone Length knob did.
TEST(IrDecayTest, TruncatesViaAttackAndDecayLength) {
  constexpr int kTotalBlocks = 90;  // kImpulseBlock (43ms) + past the reverb IR's own tail
  const auto impulse = makeDelayedImpulse(kTotalBlocks);
  const float floor = 1e-4f;

  auto runWithDecayLength = [&](double decayLength) {
    ChainTestProcessor proc;
    proc.setPlayConfigDetails(2, 2, kFs, kBlock);
    proc.prepareToPlay(kFs, kBlock);
    seedMonoIrChain(proc, "blk-a");
    EXPECT_TRUE(waitForChainLoaded(proc)) << "IR block never finished loading from cache";
    EXPECT_TRUE(proc.setBlockParam("blk-a", "mix", 1.0));  // fully wet
    EnvelopeParams p;
    p.decayLength = decayLength;
    setEnvelopeAndWaitForRebuild(proc, "blk-a", p);
    letAudioGoIdle();
    return processStereo(proc, impulse);
  };

  const auto [fullL, fullR] = runWithDecayLength(1.0);
  const auto [halfL, halfR] = runWithDecayLength(0.5);
  juce::ignoreUnused(fullR, halfR);

  const size_t fullTail = lastAboveFloor(fullL, floor);
  const size_t halfTail = lastAboveFloor(halfL, floor);
  ASSERT_GT(fullTail, kImpulseOnset) << "full-length run produced no measurable tail at all";
  std::printf("[IrDecayTest] tail extent (samples past onset) full=%zu, decayLength=0.5 -> %zu\n",
              fullTail - kImpulseOnset, halfTail - kImpulseOnset);
  // Not a tight proportionality check: the measured tail is the convolution
  // output crossing a floor, not the raw kernel length, so a 50% kernel
  // truncation doesn't map to an exactly-50% shorter measured tail. This
  // just needs to show truncation is clearly happening.
  EXPECT_LT(halfTail - kImpulseOnset, (fullTail - kImpulseOnset) * 0.85)
      << "halving Decay Length doesn't appear to be truncating the convolved tail";
}

// Mirrors PredelayTest.DoesNotAffectDryPath / the old IrLengthTest/IrDecayTest
// dry-path checks: at mix=0, only the envelope differs between the two runs,
// so any leak into dry shows up as a real divergence, not floating-point
// noise.
TEST(IrDecayTest, DoesNotAffectDryPath) {
  auto runWithEnvelope = [](const EnvelopeParams& p) {
    ChainTestProcessor proc;
    proc.setPlayConfigDetails(2, 2, kFs, kBlock);
    proc.prepareToPlay(kFs, kBlock);
    seedMonoIrChain(proc, "blk-a");
    EXPECT_TRUE(waitForChainLoaded(proc)) << "IR block never finished loading from cache";
    EXPECT_TRUE(proc.setBlockParam("blk-a", "mix", 0.0));  // pure dry
    setEnvelopeAndWaitForRebuild(proc, "blk-a", p);
    letAudioGoIdle();

    constexpr int kWarmupBlocks = 15;
    processStereo(proc, makeNoise(kWarmupBlocks * kBlock, 1111, 0.25f));
    return processStereo(proc, makeNoise(20 * kBlock, 4242, 0.25f));
  };

  EnvelopeParams neutral;
  EnvelopeParams shaped;
  shaped.decayLength = 0.5;
  shaped.decayLevel = 0.0;

  const auto [neutralL, neutralR] = runWithEnvelope(neutral);
  const auto [shapedL, shapedR] = runWithEnvelope(shaped);

  ASSERT_EQ(neutralL.size(), shapedL.size());
  float maxAbsDiff = 0.0f;
  for (size_t i = 0; i < neutralL.size(); ++i) {
    maxAbsDiff = std::max(maxAbsDiff, std::abs(neutralL[i] - shapedL[i]));
    maxAbsDiff = std::max(maxAbsDiff, std::abs(neutralR[i] - shapedR[i]));
  }
  const float maxAbsDiffDb = juce::Decibels::gainToDecibels(maxAbsDiff, -300.0f);
  std::printf("[IrDecayTest] max |diff| between neutral and shaped envelope at mix=0: %.9f (%.1f dB)\n",
              static_cast<double>(maxAbsDiff), static_cast<double>(maxAbsDiffDb));
  EXPECT_LT(maxAbsDiff, 1e-4f) << "dry path diverges far more than floating-point rounding noise "
                                  "explains - the envelope is likely leaking into the dry signal";
}

// A Decay Level of 0.0 (true silence, not just "quiet") should collapse the
// late tail far more than the neutral (1.0/unity) default - the basic
// signature that the level knobs are shaping gain over time, not just
// applying a flat overall gain change (which Out Gain already covers), and
// that 0% genuinely means silence rather than a modest attenuation.
TEST(IrDecayTest, LowDecayLevelAttenuatesLateTailEnergy) {
  constexpr int kTotalBlocks = 90;
  const auto impulse = makeDelayedImpulse(kTotalBlocks);
  const float floor = 1e-4f;

  auto runWithDecayLevel = [&](double decayLevel) {
    ChainTestProcessor proc;
    proc.setPlayConfigDetails(2, 2, kFs, kBlock);
    proc.prepareToPlay(kFs, kBlock);
    seedMonoIrChain(proc, "blk-a");
    EXPECT_TRUE(waitForChainLoaded(proc)) << "IR block never finished loading from cache";
    EXPECT_TRUE(proc.setBlockParam("blk-a", "mix", 1.0));  // fully wet
    EnvelopeParams p;
    p.decayLevel = decayLevel;
    setEnvelopeAndWaitForRebuild(proc, "blk-a", p);
    letAudioGoIdle();
    return processStereo(proc, impulse);
  };

  const auto [neutralL, neutralR] = runWithDecayLevel(1.0);
  const auto [zeroL, zeroR] = runWithDecayLevel(0.0);  // true silence at the end
  juce::ignoreUnused(neutralR, zeroR);

  // Late window: the last 30% of the neutral run's own measured tail extent,
  // applied identically to both runs - exactly where Decay Level's influence
  // dominates the envelope.
  const size_t neutralTail = lastAboveFloor(neutralL, floor);
  ASSERT_GT(neutralTail, kImpulseOnset) << "neutral run produced no measurable tail at all";
  const size_t lateStart =
      kImpulseOnset + static_cast<size_t>((neutralTail - kImpulseOnset) * 0.7);

  const double rmsNeutralLate = rmsFrom(neutralL, lateStart);
  const double rmsZeroLate = rmsFrom(zeroL, lateStart);
  ASSERT_GT(rmsNeutralLate, 0.0) << "neutral run's late window is silent - fixture is broken";

  std::printf("[IrDecayTest] late-window rms neutral=%.9f, Decay Level=0%%=%.9f\n", rmsNeutralLate,
              rmsZeroLate);
  EXPECT_LT(rmsZeroLate, rmsNeutralLate * 0.1)
      << "Decay Level=0% doesn't appear to be collapsing the tail's gain anywhere near silence";
}

// Two different Decay Curve values, same (non-flat) Decay Level, must
// produce genuinely different output - proves decayCurveNormalized actually
// warps the Decay segment's shape, not a no-op. Uses the curve knob's two
// extremes (0.0 front-loaded: steep initial drop; 1.0 back-loaded: holds,
// then drops at the very end) for the widest divergence. Attack stays at its
// default zero length, so the whole truncated content is the Decay segment.
TEST(IrDecayTest, DecayCurveShapesTheMiddleOfTheDecaySegment) {
  auto runWithCurve = [](double decayCurve) {
    ChainTestProcessor proc;
    proc.setPlayConfigDetails(2, 2, kFs, kBlock);
    proc.prepareToPlay(kFs, kBlock);
    seedMonoIrChain(proc, "blk-a");
    EXPECT_TRUE(waitForChainLoaded(proc)) << "IR block never finished loading from cache";
    EXPECT_TRUE(proc.setBlockParam("blk-a", "mix", 1.0));
    EnvelopeParams p;
    p.decayLevel = 0.0;  // a real envelope to shape
    p.decayCurve = decayCurve;
    setEnvelopeAndWaitForRebuild(proc, "blk-a", p);
    letAudioGoIdle();

    constexpr int kWarmupBlocks = 15;
    processStereo(proc, makeNoise(kWarmupBlocks * kBlock, 1111, 0.25f));
    return processStereo(proc, makeNoise(20 * kBlock, 4242, 0.25f));
  };

  const auto [frontLoadedL, frontLoadedR] = runWithCurve(0.0);
  const auto [backLoadedL, backLoadedR] = runWithCurve(1.0);

  ASSERT_EQ(frontLoadedL.size(), backLoadedL.size());
  float maxAbsDiff = 0.0f;
  for (size_t i = 0; i < frontLoadedL.size(); ++i) {
    maxAbsDiff = std::max(maxAbsDiff, std::abs(frontLoadedL[i] - backLoadedL[i]));
    maxAbsDiff = std::max(maxAbsDiff, std::abs(frontLoadedR[i] - backLoadedR[i]));
  }
  std::printf(
      "[IrDecayTest] max |diff| between Decay Curve=0.0 (front-loaded) and =1.0 (back-loaded): %.9f\n",
      static_cast<double>(maxAbsDiff));
  EXPECT_GT(maxAbsDiff, 1e-3f)
      << "Decay Curve's two extremes produced effectively identical output - "
         "decayCurveNormalized doesn't appear to change the Decay segment's shape";
}

// Same proof as the Decay Curve test above, but for the (new) independent
// Attack segment: Decay Length sets a real (half-content) total, and Attack
// Length=1.0 puts the peak at the very end of that total - so the whole
// truncated content is the Attack segment (decay span = 0), isolating Attack
// Curve's own effect. Fades in from Init Level=0 (silence) up to unity/0dB
// (Attack's pinned peak).
TEST(IrDecayTest, AttackCurveShapesTheMiddleOfTheAttackSegment) {
  auto runWithCurve = [](double attackCurve) {
    ChainTestProcessor proc;
    proc.setPlayConfigDetails(2, 2, kFs, kBlock);
    proc.prepareToPlay(kFs, kBlock);
    seedMonoIrChain(proc, "blk-a");
    EXPECT_TRUE(waitForChainLoaded(proc)) << "IR block never finished loading from cache";
    EXPECT_TRUE(proc.setBlockParam("blk-a", "mix", 1.0));
    EnvelopeParams p;
    p.initLevel = 0.0;  // a real envelope to shape (fade-in to Attack's unity peak)
    p.attackLength = 1.0;  // peak at the very end of the total - decay span = 0
    p.attackCurve = attackCurve;
    p.decayLength = 0.5;  // a real (half-content) total for Attack to span
    setEnvelopeAndWaitForRebuild(proc, "blk-a", p);
    letAudioGoIdle();

    constexpr int kWarmupBlocks = 15;
    processStereo(proc, makeNoise(kWarmupBlocks * kBlock, 1111, 0.25f));
    return processStereo(proc, makeNoise(20 * kBlock, 4242, 0.25f));
  };

  const auto [frontLoadedL, frontLoadedR] = runWithCurve(0.0);
  const auto [backLoadedL, backLoadedR] = runWithCurve(1.0);

  ASSERT_EQ(frontLoadedL.size(), backLoadedL.size());
  float maxAbsDiff = 0.0f;
  for (size_t i = 0; i < frontLoadedL.size(); ++i) {
    maxAbsDiff = std::max(maxAbsDiff, std::abs(frontLoadedL[i] - backLoadedL[i]));
    maxAbsDiff = std::max(maxAbsDiff, std::abs(frontLoadedR[i] - backLoadedR[i]));
  }
  std::printf(
      "[IrDecayTest] max |diff| between Attack Curve=0.0 and =1.0: %.9f\n",
      static_cast<double>(maxAbsDiff));
  EXPECT_GT(maxAbsDiff, 1e-3f)
      << "Attack Curve's two extremes produced effectively identical output - "
         "attackCurveNormalized doesn't appear to change the Attack segment's shape";
}

// The envelope shape is persisted, but the *audible* shape only exists
// because it's reapplied to the freshly rebuilt (always full-length, flat)
// engine on restore - pins the generalized reapply condition in
// applyPreparedModelToChainBlock (an OR across all six params).
TEST(IrDecayTest, ShapeReappliesAfterStateRestore) {
  constexpr int kTotalBlocks = 90;
  const auto impulse = makeDelayedImpulse(kTotalBlocks);

  ChainTestProcessor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);
  seedMonoIrChain(proc, "blk-a");
  ASSERT_TRUE(waitForChainLoaded(proc));
  ASSERT_TRUE(proc.setBlockParam("blk-a", "mix", 1.0));
  // A nontrivial combo across all six params, exercising Attack, Decay, both
  // curves, and a true-zero level together through the restore path.
  EnvelopeParams p;
  p.initLevel = 0.7;
  p.attackLength = 0.3;
  p.attackCurve = 0.2;
  p.decayLength = 0.5;
  p.decayLevel = 0.0;
  p.decayCurve = 0.9;
  setEnvelopeAndWaitForRebuild(proc, "blk-a", p);
  letAudioGoIdle();

  const auto [beforeL, beforeR] = processStereo(proc, impulse);

  juce::MemoryBlock savedState;
  proc.getStateInformation(savedState);

  ChainTestProcessor restored;
  restored.setPlayConfigDetails(2, 2, kFs, kBlock);
  restored.prepareToPlay(kFs, kBlock);
  restored.setStateInformation(savedState.getData(), static_cast<int>(savedState.getSize()));

  // waitForChainLoaded only polls getChainState, never processBlock, so the
  // reapply job the restore's own load queues (applyPreparedModelToChainBlock)
  // can fire its requestSwapFadeAndWait before or during this wait - pump
  // throughout, not just once loaded.
  bool loaded = false;
  {
    const std::vector<float> silence(static_cast<size_t>(kBlock), 0.0f);
    const auto deadline = juce::Time::getMillisecondCounter() + 5000u;
    while (juce::Time::getMillisecondCounter() < deadline) {
      processStereo(restored, silence);
      const juce::var state = restored.getChainState(-1);
      bool allLoaded = true;
      for (const auto* lane : {state["chain"].getArray(), state["chainRight"].getArray()}) {
        if (lane == nullptr)
          continue;
        for (const auto& item : *lane)
          if (item["kind"].toString() == "tone" && !static_cast<bool>(item["loaded"]))
            allLoaded = false;
      }
      if (allLoaded && !restored.isChainEditFadeHeld()) {
        loaded = true;
        break;
      }
      juce::Thread::sleep(20);
    }
  }
  ASSERT_TRUE(loaded) << "restored IR block never finished reloading";
  ASSERT_TRUE(restored.setBlockParam("blk-a", "mix", 1.0));
  pumpAudio(restored, 1200, /*alreadyWarm=*/true);
  letAudioGoIdle();

  const auto [afterL, afterR] = processStereo(restored, impulse);

  ASSERT_EQ(beforeL.size(), afterL.size());
  float maxAbsDiff = 0.0f;
  for (size_t i = 0; i < beforeL.size(); ++i) {
    maxAbsDiff = std::max(maxAbsDiff, std::abs(beforeL[i] - afterL[i]));
    maxAbsDiff = std::max(maxAbsDiff, std::abs(beforeR[i] - afterR[i]));
  }
  std::printf("[IrDecayTest] max |diff| before save vs after restore: %.9f\n",
              static_cast<double>(maxAbsDiff));
  EXPECT_LT(maxAbsDiff, 1e-4f)
      << "restore didn't reapply the persisted envelope shape to the reloaded engine";
}
