// Spread / StereoOffset tests
//
// The mechanical guarantees of the post-chain stereo image engines
// (Spread.h / StereoOffset.h; design notes in plugin/docs/spread.md):
//
//   SpreadTest        center detent is exactly dual-mono, ± the same offset
//                     mirrors the channels exactly, the low band stays
//                     dual-mono, and hard sign flips never click (signed
//                     offset smoothing).
//   StereoOffsetTest  the chosen side really is delayed by the dialed ms.
//
// The by-ear items (mono fold-down comb motion, flange zone, "reads as a
// second take") can only be checked by listening.
#include "Spread.h"
#include "StereoOffset.h"
#include "test_helpers.h"

#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <utility>
#include <vector>

namespace {

constexpr int kBlock = 512;

struct StereoResult {
  std::vector<float> l, r;
};

// Streams mono input (seeded onto both channels, as the mono chain mode
// does) through a Spread held at fixed knob values. Input length must be a
// multiple of kBlock.
StereoResult runSpread(const std::vector<float>& in, float offsetNorm, float wobbleNorm) {
  Spread spread;
  spread.prepare(kFs, kBlock);
  juce::AudioBuffer<float> buf(2, kBlock);
  StereoResult out;
  out.l.reserve(in.size());
  out.r.reserve(in.size());
  for (size_t off = 0; off < in.size(); off += kBlock) {
    for (int i = 0; i < kBlock; ++i) {
      buf.setSample(0, i, in[off + static_cast<size_t>(i)]);
      buf.setSample(1, i, in[off + static_cast<size_t>(i)]);
    }
    spread.setTarget(SpreadParams::fromNormalized(offsetNorm, wobbleNorm), true);
    spread.process(buf);
    for (int i = 0; i < kBlock; ++i) {
      out.l.push_back(buf.getSample(0, i));
      out.r.push_back(buf.getSample(1, i));
    }
  }
  return out;
}

double rms(const std::vector<float>& x, size_t from) {
  double sum = 0.0;
  for (size_t i = from; i < x.size(); ++i)
    sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
  return std::sqrt(sum / static_cast<double>(x.size() - from));
}

// Spread

TEST(SpreadTest, CenterDetentIsExactlyDualMono) {
  // At the center detent the lag path blends fully to the dry high band, so
  // L and R must be bit-identical, even with wobble up and during the
  // engage fade (both channels share the mono input).
  const auto in = makeNoise(64 * kBlock, 1234, 0.5f);
  const auto out = runSpread(in, 0.5f, 0.5f);
  for (size_t i = 0; i < in.size(); ++i)
    ASSERT_EQ(out.l[i], out.r[i]) << "channels diverged at sample " << i;
}

TEST(SpreadTest, OppositeSignsMirrorExactly) {
  // +T and -T must be exact L/R mirrors (wobble at zero: its noise source is
  // seeded per instance, so a nonzero depth would break determinism, not the
  // mirror property itself).
  const auto in = makeNoise(64 * kBlock, 99, 0.5f);
  const auto plus = runSpread(in, 0.75f, 0.0f);   // +12 ms, lag on R
  const auto minus = runSpread(in, 0.25f, 0.0f);  // -12 ms, lag on L
  for (size_t i = 0; i < in.size(); ++i) {
    ASSERT_EQ(plus.l[i], minus.r[i]) << "mirror broke at sample " << i;
    ASSERT_EQ(plus.r[i], minus.l[i]) << "mirror broke at sample " << i;
  }
}

TEST(SpreadTest, LowBandStaysDualMono) {
  // Below the 130 Hz crossover the two channels carry the same low band; a
  // 30 Hz tone should null between L and R down to the (LR4-attenuated)
  // high-band residue.
  const int frames = 64 * kBlock;
  const auto in = makeSine(frames, 30.0, 0.5f);
  const auto out = runSpread(in, 0.75f, 0.0f);
  // Skip the engage fade + filter settling (~0.5 s).
  const size_t settled = static_cast<size_t>(kFs / 2);
  std::vector<float> diff(out.l.size());
  for (size_t i = 0; i < diff.size(); ++i)
    diff[i] = out.l[i] - out.r[i];
  const double nullDb = db(std::pow(rms(diff, settled), 2.0)) -
                        db(std::pow(rms(out.l, settled), 2.0));
  EXPECT_LT(nullDb, -35.0) << "low band not dual-mono: L-R null only " << nullDb << " dB";
}

TEST(SpreadTest, WobbleAtZeroIsTimeInvariant) {
  // At 0% depth the engine must be exactly time-invariant: zero modulation
  // of any kind. Proof by periodicity: feed a bit-exactly periodic input
  // (one 128-sample sine cycle tiled, 375 Hz at 48 kHz) and require the
  // settled output to repeat with the same period. Any wobble leaking into
  // the delay time (or any other hidden time variance) breaks this.
  constexpr int kPeriod = 128;
  const auto cycle = makeSine(kPeriod, kFs / kPeriod, 0.5f);
  const int frames = 288 * kBlock;  // ~3 s
  std::vector<float> in(static_cast<size_t>(frames));
  for (int i = 0; i < frames; ++i)
    in[static_cast<size_t>(i)] = cycle[static_cast<size_t>(i % kPeriod)];

  const auto out = runSpread(in, 0.75f, 0.0f);  // +12 ms, wob 0%

  // Skip 2 s (engage fade + filter transients), check the last second.
  const size_t start = 2 * static_cast<size_t>(kFs);
  for (const auto* ch : {&out.l, &out.r})
    for (size_t i = start; i < ch->size(); ++i)
      ASSERT_NEAR((*ch)[i], (*ch)[i - kPeriod], 1e-6f)
          << "output not periodic at sample " << i << "; something is modulating";
}

TEST(SpreadTest, WobbleAddsNoBroadbandFizz) {
  // The wobble must stay a sub-audio wander: any audio-rate residue in the
  // delay-time noise frequency-modulates the lag channel into broadband
  // noise skirts: audible fizz. Feed a pure sine at full wobble depth and
  // require the floor far from the carrier to stay down. (Regression: a
  // single 0.3 Hz one-pole on the noise leaves ~1% of its variance above
  // 20 Hz, which read as fizz once the depth normalization was corrected.)
  const int frames = 288 * kBlock;  // ~3 s
  const auto in = makeSine(frames, 500.0, 0.5f);
  const auto out = runSpread(in, 0.5f + 20.0f / 48.0f, 1.0f);  // +20 ms R, wob 100%

  // Analyze the lag channel after the engage fade + walk settle (~1 s).
  const size_t start = static_cast<size_t>(kFs);
  const size_t n = out.r.size() - start;
  const double carrierDb = db(goertzelPower(out.r.data() + start, n, 500.0));

  // Probe bins far from the carrier and off its harmonics.
  for (const double freq : {2917.0, 4111.0, 6373.0, 9241.0, 13687.0}) {
    const double noiseDb = db(goertzelPower(out.r.data() + start, n, freq));
    EXPECT_LT(noiseDb - carrierDb, -90.0)
        << "fizz at " << freq << " Hz: " << (noiseDb - carrierDb) << " dB rel. carrier";
  }
}

TEST(SpreadTest, HardSignFlipsNeverClick) {
  // Slam the knob between the extremes (+24 <-> -24 ms) every 20 blocks.
  // The signed one-pole must carry the delay through zero as a smooth
  // varispeed bend, never a discontinuity on either channel.
  const int numBlocks = 120;
  const auto in = makeSine(numBlocks * kBlock, 440.0, 0.5f);

  Spread spread;
  spread.prepare(kFs, kBlock);
  juce::AudioBuffer<float> buf(2, kBlock);
  std::vector<float> outL, outR;
  for (int b = 0; b < numBlocks; ++b) {
    for (int i = 0; i < kBlock; ++i) {
      buf.setSample(0, i, in[static_cast<size_t>(b * kBlock + i)]);
      buf.setSample(1, i, in[static_cast<size_t>(b * kBlock + i)]);
    }
    const float offsetNorm = (b / 20) % 2 == 0 ? 1.0f : 0.0f;
    spread.setTarget(SpreadParams::fromNormalized(offsetNorm, 0.5f), true);
    spread.process(buf);
    for (int i = 0; i < kBlock; ++i) {
      outL.push_back(buf.getSample(0, i));
      outR.push_back(buf.getSample(1, i));
    }
  }

  // A 440 Hz sine at 0.5 moves ≤ ~0.029 per sample; the varispeed bend and
  // allpass transients stretch that, but a click is a near-step. Skip the
  // engage fade-in.
  const size_t settled = static_cast<size_t>(kFs / 10);
  for (const auto* ch : {&outL, &outR})
    for (size_t i = settled; i < ch->size(); ++i)
      ASSERT_LT(std::abs((*ch)[i] - (*ch)[i - 1]), 0.1f)
          << "discontinuity at sample " << i;
}

// StereoOffset

TEST(StereoOffsetTest, DelaysTheChosenSideByTheDialedMs) {
  // Full-right knob = the right chain delayed by 24 ms; the left chain must
  // pass through untouched.
  const int frames = 96 * kBlock;
  const auto in = makeNoise(frames, 7, 0.5f);

  StereoOffset offset;
  offset.prepare(kFs, kBlock);
  juce::AudioBuffer<float> buf(2, kBlock);
  std::vector<float> outL, outR;
  for (size_t off = 0; off < in.size(); off += kBlock) {
    for (int i = 0; i < kBlock; ++i) {
      buf.setSample(0, i, in[off + static_cast<size_t>(i)]);
      buf.setSample(1, i, in[off + static_cast<size_t>(i)]);
    }
    offset.setTarget(StereoOffsetParams::fromNormalized(1.0f), true);
    offset.process(buf);
    for (int i = 0; i < kBlock; ++i) {
      outL.push_back(buf.getSample(0, i));
      outR.push_back(buf.getSample(1, i));
    }
  }

  EXPECT_EQ(outL, in);  // untouched side: not one bit may move

  // After the glide-in settles, the right side sits at 24 ms = 1152 samples.
  const int expectedLag = static_cast<int>(std::round(24.0e-3 * kFs));
  const int start = static_cast<int>(kFs);  // well past the 40 ms ramp
  const int lag = bestCorrelationLag(outR, in, start, 4 * kBlock, expectedLag + 64);
  EXPECT_EQ(lag, expectedLag);
}

}  // namespace
