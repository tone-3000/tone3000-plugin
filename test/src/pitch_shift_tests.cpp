// Pitch shift tests
//
// The mechanical guarantees of the input-stage pitch shifter (PitchShift.h)
// and of the processor wiring around it:
//
//   PitchShiftTest  the latency figure the processor reports from the
//                   parameters matches the engine at every window and rate;
//                   at a 1.0 ratio the output is a pure delay at the floor
//                   and unity gain; the shift lands on the expected frequency
//                   at ±1 and ±2 octaves and at a fractional shift; a splice
//                   between uncorrelated taps holds the level; the tonality
//                   limit passes the highs unshifted; a pick attack re-syncs
//                   the tap to the floor; stereo channels share one tap; a
//                   mono buffer against the stereo engine is safe; rate /
//                   block-size / window changes and ±24 extremes stay
//                   finite; a smooth ±24 sweep (STEP off) keeps the tone
//                   continuous; a window change keeps the shift; power,
//                   window and tonality changes blend (no step, no hole) and
//                   a powered-off engine stops running once its fade-out
//                   lands.
//   ProcessorTest   powered off the plugin is bit-exact and zero-latency;
//                   powering on reports boundary + window latency from the
//                   message thread; STEP rounds the shift for the engine;
//                   the parameters round-trip through state and presets; a
//                   state or preset saved before the pitch shifter existed
//                   lands it on the defaults (off); state, MIDI maps and
//                   presets from the beta builds that called it Transpose
//                   load under the current ids (LegacyParamIds.h).
//
// Splice quality (sidebands, warble, onset timing on real DIs) is the
// bench's job, see plugin/docs/pitch-shift.md.
#include "LegacyParamIds.h"
#include "PitchShift.h"
#include "PresetFile.h"
#include "Processor.h"
#include "test_helpers.h"

#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <map>
#include <vector>

namespace {

constexpr int kBlock = 512;

// Streams mono `in` through a shifter in kBlock blocks (the tail as a short
// one), optionally switching to `p2` at sample `switchAt`.
std::vector<float> runPitchShift(const std::vector<float>& in, const PitchShift::Params& p,
                                double fs = kFs, const PitchShift::Params* p2 = nullptr,
                                int switchAt = -1) {
  PitchShift t;
  t.prepare(fs, kBlock);
  t.setEnabled(true);
  t.setParams(p);
  std::vector<float> out;
  out.reserve(in.size());
  for (size_t off = 0; off < in.size(); off += kBlock) {
    if (p2 != nullptr && static_cast<int>(off) == switchAt) t.setParams(*p2);
    const int n = static_cast<int>(std::min<size_t>(kBlock, in.size() - off));
    juce::AudioBuffer<float> buf(1, n);
    buf.copyFrom(0, 0, in.data() + off, n);
    t.process(buf);
    out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + n);
  }
  return out;
}

void expectFinite(const juce::AudioBuffer<float>& buf) {
  for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    for (int i = 0; i < buf.getNumSamples(); ++i) {
      ASSERT_TRUE(std::isfinite(buf.getReadPointer(ch)[i])) << "ch " << ch << " sample " << i;
      ASSERT_LT(std::abs(buf.getReadPointer(ch)[i]), 10.0f);
    }
}

// Largest sample-to-sample step in [from, to).
float maxStep(const std::vector<float>& x, int from, int to) {
  float m = 0.0f;
  for (int i = from + 1; i < to; ++i)
    m = std::max(m, std::abs(x[static_cast<size_t>(i)] - x[static_cast<size_t>(i - 1)]));
  return m;
}

// Energy at `expected` must dominate energy left at `original` in the
// settled tail.
void expectShiftedTo(const std::vector<float>& out, double original, double expected) {
  constexpr int kSettle = 48000, kWindow = 65536;
  ASSERT_GE(out.size(), static_cast<size_t>(kSettle + kWindow));
  const double atExpected = db(goertzelPower(out.data() + kSettle, kWindow, expected));
  const double atOriginal = db(goertzelPower(out.data() + kSettle, kWindow, original));
  EXPECT_GT(atExpected, atOriginal + 20.0)
      << "expected the energy at " << expected << " Hz (" << atExpected << " dB), not at the original "
      << original << " Hz (" << atOriginal << " dB)";
}

}  // namespace

TEST(PitchShiftTest, LatencyFigureMatchesTheEngineAtAnyRate) {
  // The processor reports latency from the parameters via the static
  // figure, before the audio thread has switched windows; the engine the
  // audio thread runs must agree with it, at every window and host rate.
  for (const double fs : {44100.0, 48000.0, 96000.0}) {
    PitchShift t;
    t.prepare(fs, kBlock);
    t.setEnabled(true);
    for (int w = 0; w < static_cast<int>(PitchShift::kWindowMs.size()); ++w) {
      const auto window = PitchShift::windowFromIndex(w);
      PitchShift::Params p;
      p.window = window;
      t.setParams(p);
      EXPECT_EQ(t.latencySamples(), PitchShift::latencySamples(window, fs)) << fs << " Hz, window " << w;
      // The tap's mean delay: halfway between the floor and the buffer.
      const int floor = static_cast<int>(fs * PitchShift::kMinDelayMs / 1000);
      const int buffer = static_cast<int>(fs * PitchShift::windowMs(window) / 1000);
      EXPECT_EQ(t.latencySamples(), (floor + buffer) / 2);
    }
  }
  EXPECT_EQ(PitchShift::latencySamples(PitchShift::Window::ms30, kFs), 768);  // (2 + 30) / 2 ms
  EXPECT_DOUBLE_EQ(PitchShift::latencyMs(PitchShift::Window::ms30), 16.0);
}

TEST(PitchShiftTest, UnityRatioIsAPureDelayAtTheFloor) {
  // Powered on at 0 st the tap does not drift, so the output is the input
  // delayed by the floor (where attacks are re-synced to) at unity gain.
  // Sweeping the knob through 0 therefore never jumps the timing.
  PitchShift::Params p;
  const int floor = PitchShift::minDelaySamples(kFs);
  const auto noise = makeNoise(2 * 48000, 3, 0.4f);
  const auto out = runPitchShift(noise, p);
  EXPECT_EQ(bestCorrelationLag(out, noise, 48000, 8192, floor + 256), floor);
  for (int i = 48000; i < 48000 + 8192; ++i)
    ASSERT_NEAR(out[static_cast<size_t>(i)], noise[static_cast<size_t>(i - floor)], 1e-5f) << i;
  const auto tone = makeSine(3 * 48000, 440.0, 0.5f);
  const auto toneOut = runPitchShift(tone, p);
  const double gain = db(goertzelPower(toneOut.data() + 96000, 16384, 440.0)) -
                      db(goertzelPower(tone.data() + 96000, 16384, 440.0));
  EXPECT_NEAR(gain, 0.0, 0.05);
}

TEST(PitchShiftTest, OctavesUpAndDownLandOnTheFrequency) {
  // Every buffer: two octaves up on the short ones is where the upshift
  // geometry has to shrink its fades to keep a landing range (a splice
  // with nowhere to choose from is not period-matched and drags the pitch).
  const auto in = makeSine(3 * 48000, 440.0, 0.5f);
  for (const int semis : {12, -12, 24, -24})
    for (int w = 0; w < static_cast<int>(PitchShift::kWindowMs.size()); ++w) {
      PitchShift::Params p;
      p.semitones = static_cast<float>(semis);
      p.window = PitchShift::windowFromIndex(w);
      expectShiftedTo(runPitchShift(in, p), 440.0, 440.0 * std::pow(2.0, semis / 12.0));
    }
}

TEST(PitchShiftTest, FractionalShiftsAreContinuous) {
  // The parameter is continuous (STEP is the processor's business): -1.5 st
  // is a ratio of 2^(-1.5/12), not -1 or -2.
  const auto in = makeSine(3 * 48000, 440.0, 0.5f);
  PitchShift::Params p;
  p.semitones = -1.5f;
  expectShiftedTo(runPitchShift(in, p), 440.0, 440.0 * std::pow(2.0, -1.5 / 12.0));
}

TEST(PitchShiftTest, ShiftedToneKeepsUnityGainAtEveryWindow) {
  // Splices land on whole periods of a steady tone, so the crossfades add
  // nothing and take nothing away, at any buffer size.
  const auto in = makeSine(3 * 48000, 220.0, 0.5f);
  for (int w = 0; w < static_cast<int>(PitchShift::kWindowMs.size()); ++w) {
    PitchShift::Params p;
    p.semitones = -2;
    p.window = PitchShift::windowFromIndex(w);
    const auto out = runPitchShift(in, p);
    const double expected = 220.0 * std::pow(2.0, -2.0 / 12.0);
    const double gain = db(goertzelPower(out.data() + 48000, 65536, expected)) -
                        db(goertzelPower(in.data() + 48000, 65536, 220.0));
    EXPECT_NEAR(gain, 0.0, 1.0) << "window " << w;
  }
}

TEST(PitchShiftTest, SpliceBetweenUncorrelatedTapsHoldsTheLevel) {
  // On noise no lag matches, so every drift splice crossfades two
  // uncorrelated signals over the longest fade (120 ms). A plain
  // complementary fade would dip 3 dB in the middle of each one; the gains
  // are normalised by the taps' correlation so the level holds. 20 ms RMS
  // windows of the shifted noise stay within 1 dB of their median.
  const auto in = makeNoise(4 * 48000, 21, 0.4f);
  PitchShift::Params p;
  p.semitones = -2;
  const auto out = runPitchShift(in, p);
  constexpr int kWin = 960;
  std::vector<double> rms;
  for (int end = 48000 + kWin; end <= static_cast<int>(out.size()); end += kWin) {
    double acc = 0.0;
    for (int i = end - kWin; i < end; ++i) acc += static_cast<double>(out[static_cast<size_t>(i)]) * out[static_cast<size_t>(i)];
    rms.push_back(db(acc / kWin));
  }
  auto sorted = rms;
  std::nth_element(sorted.begin(), sorted.begin() + static_cast<long>(sorted.size() / 2), sorted.end());
  const double median = sorted[sorted.size() / 2];
  for (size_t k = 0; k < rms.size(); ++k) EXPECT_NEAR(rms[k], median, 1.0) << "window " << k;
}

TEST(PitchShiftTest, TonalityLimitPassesTheHighsUnshifted) {
  // Above the limit the input bypasses the shifter: an octave up moves a
  // 6 kHz partial to 12 kHz with the limit off, and leaves it at 6 kHz with
  // a 2 kHz limit.
  const auto in = makeSine(3 * 48000, 6000.0, 0.5f);
  PitchShift::Params p;
  p.semitones = 12;
  expectShiftedTo(runPitchShift(in, p), 6000.0, 12000.0);
  p.tonalityHz = 2000.0f;
  expectShiftedTo(runPitchShift(in, p), 12000.0, 6000.0);
  // ... while a partial below the limit still shifts.
  const auto low = makeSine(3 * 48000, 440.0, 0.5f);
  expectShiftedTo(runPitchShift(low, p), 440.0, 880.0);
}

TEST(PitchShiftTest, PickAttackReSyncsTheTapToTheFloor) {
  // On a sustained note the tap drifts across the buffer; a pick attack
  // must not wait for it. The burst has to appear in the output within the
  // floor plus the re-sync span, wherever the tap was.
  const int floor = PitchShift::minDelaySamples(kFs);
  const int span = static_cast<int>(kFs * 0.004);
  const int fade = static_cast<int>(kFs * 0.002);
  // 1 ms RMS of `x` ending at `end`.
  const auto rms = [](const std::vector<float>& x, int end) {
    double acc = 0.0;
    for (int i = end - 48; i < end; ++i) acc += static_cast<double>(x[static_cast<size_t>(i)]) * x[static_cast<size_t>(i)];
    return std::sqrt(acc / 48.0);
  };
  for (const int holdMs : {700, 1150, 1600, 2050}) {
    const int hold = holdMs * 48;
    // A quiet low note (little energy above the detector's 600 Hz), then a
    // loud broadband burst: a 14 dB step.
    auto in = makeSine(hold + 24000, 110.0, 0.1f);
    const auto burst = makeNoise(24000, 11, 0.9f);
    for (int i = 0; i < 24000; ++i) in[static_cast<size_t>(hold + i)] = burst[static_cast<size_t>(i)];
    PitchShift::Params p;
    p.semitones = -2;
    const auto out = runPitchShift(in, p);
    // Arrival: the first 1 ms window past the step's midpoint level (the
    // tone's RMS is 0.07, the burst's 0.52).
    int arrival = -1;
    for (int end = hold + 48; end < hold + 24000 && arrival < 0; ++end)
      if (rms(out, end) > 0.3) arrival = end;
    ASSERT_GT(arrival, 0) << holdMs;
    const int lag = arrival - hold;
    EXPECT_GE(lag, floor) << holdMs;
    // Floor + re-sync span + the fade + the 1 ms window + 1 ms of detector.
    EXPECT_LE(lag, floor + span + fade + 96) << holdMs;
  }
}

TEST(PitchShiftTest, StereoChannelsShareOneTap) {
  // The lag search and the detector run on the channel mean and both
  // channels read the same tap, so a right channel that is half the left
  // stays exactly half through every splice: the image never smears.
  PitchShift t;
  t.prepare(kFs, kBlock);
  t.setEnabled(true);
  PitchShift::Params p;
  p.semitones = -3;
  t.setParams(p);
  juce::AudioBuffer<float> buf(2, kBlock);
  constexpr int kLength = 192 * kBlock;
  const auto noise = makeNoise(kLength, 5, 0.4f);
  for (int off = 0; off < kLength; off += kBlock) {
    buf.copyFrom(0, 0, noise.data() + off, kBlock);
    buf.copyFrom(1, 0, noise.data() + off, kBlock);
    buf.applyGain(1, 0, kBlock, 0.5f);
    t.process(buf);
    for (int i = 0; i < kBlock; ++i)
      ASSERT_NEAR(buf.getReadPointer(1)[i], 0.5f * buf.getReadPointer(0)[i], 1e-6f) << off + i;
  }
}

TEST(PitchShiftTest, MonoBufferAgainstTheStereoEngineIsSafe) {
  // A genuinely mono host buffer (see
  // ProcessorTest.StereoChainsFoldToMonoWithoutAStereoOutput) feeds the
  // engine from its one channel; nothing may read or write past it.
  PitchShift t;
  t.prepare(kFs, kBlock);
  t.setEnabled(true);
  PitchShift::Params p;
  p.semitones = 7;
  t.setParams(p);
  juce::AudioBuffer<float> mono(1, kBlock);
  const auto noise = makeNoise(kBlock, 99, 0.3f);
  for (int i = 0; i < 50; ++i) {
    mono.copyFrom(0, 0, noise.data(), kBlock);
    t.process(mono);
  }
  expectFinite(mono);
}

TEST(PitchShiftTest, SurvivesRateBlockAndWindowChanges) {
  PitchShift t;
  PitchShift::Params p;
  p.semitones = 5;
  auto run = [&](int block, unsigned seed) {
    juce::AudioBuffer<float> buf(2, block);
    const auto noise = makeNoise(block, seed, 0.3f);
    buf.copyFrom(0, 0, noise.data(), block);
    buf.copyFrom(1, 0, noise.data(), block);
    t.process(buf);
    expectFinite(buf);
  };
  t.prepare(44100.0, 256);
  t.setEnabled(true);
  t.setParams(p);
  run(256, 1);
  // A device change is a fresh prepare(), like a real prepareToPlay.
  t.prepare(96000.0, 1024);
  run(1024, 2);
  // A block bigger than the prepared size (an offline bounce).
  run(4096, 3);
  // Every window, switched live, at both shift directions.
  for (int w = 0; w < static_cast<int>(PitchShift::kWindowMs.size()); ++w) {
    p.window = PitchShift::windowFromIndex(w);
    for (const int semis : {24, -24}) {
      p.semitones = static_cast<float>(semis);
      t.setParams(p);
      for (int i = 0; i < 8; ++i) run(1024, 10 + static_cast<unsigned>(w) + static_cast<unsigned>(i));
    }
  }
}

TEST(PitchShiftTest, ExtremeShiftsStayBounded) {
  for (const int semis : {-24, 24}) {
    PitchShift::Params p;
    p.semitones = static_cast<float>(semis);
    const auto out = runPitchShift(makeNoise(static_cast<int>(kFs), 555u + static_cast<unsigned>(semis), 0.5f), p);
    for (const float s : out) {
      ASSERT_TRUE(std::isfinite(s)) << semis;
      ASSERT_LT(std::abs(s), 10.0f) << semis;
    }
  }
}

TEST(PitchShiftTest, SmoothSweepNeverBreaksTheTone) {
  // STEP off, the knob sweeps: the shift changes a little every block, the
  // whole ±24 both ways in 4 s. A pending lag search must survive the small
  // changes (a tap arriving at the buffer end without a plan would run off
  // the ring), so the tone stays continuous: no sample step past a 220 Hz
  // tone's own (~0.014 at 0.5), no hole, everything finite.
  PitchShift t;
  t.prepare(kFs, kBlock);
  t.setEnabled(true);
  constexpr int kBlocks = 4 * 48000 / kBlock;
  const auto in = makeSine(kBlocks * kBlock, 220.0, 0.5f);
  std::vector<float> out;
  juce::AudioBuffer<float> buf(1, kBlock);
  for (int b = 0; b < kBlocks; ++b) {
    // A triangle: 0 -> +24 -> -24 -> 0.
    const double phase = static_cast<double>(b) / kBlocks;
    const double semis = phase < 0.25 ? 96.0 * phase : phase < 0.75 ? 24.0 - 96.0 * (phase - 0.25) : -24.0 + 96.0 * (phase - 0.75);
    PitchShift::Params p;
    p.semitones = static_cast<float>(semis);
    t.setParams(p);
    buf.copyFrom(0, 0, in.data() + b * kBlock, kBlock);
    t.process(buf);
    out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
  }
  for (const float s : out) ASSERT_TRUE(std::isfinite(s));
  // The shifted tone at +24 st is 880 Hz, whose own step is ~0.058; allow
  // for the fades between uncorrelated taps on top.
  EXPECT_LT(maxStep(out, 48000, static_cast<int>(out.size())), 0.12f);
  for (int end = 48000; end <= static_cast<int>(out.size()); end += 480) {
    double acc = 0.0;
    for (int i = end - 480; i < end; ++i) acc += static_cast<double>(out[static_cast<size_t>(i)]) * out[static_cast<size_t>(i)];
    EXPECT_GT(std::sqrt(acc / 480), 0.15) << "hole at " << end;  // the tone's RMS is 0.35
  }
}

TEST(PitchShiftTest, WindowChangeKeepsTheShift) {
  // Switching buffers mid-stream: the ratio must carry over and the tap
  // must find its way into the new range.
  const auto in = makeSine(4 * 48000, 440.0, 0.5f);
  PitchShift::Params a, b;
  a.semitones = b.semitones = 12;
  a.window = PitchShift::Window::ms20;
  b.window = PitchShift::Window::ms60;
  const auto out = runPitchShift(in, a, kFs, &b, 94 * kBlock);  // on a block edge, or never applied
  expectShiftedTo(out, 440.0, 880.0);
}

TEST(PitchShiftTest, WindowChangeIsSeamless) {
  // Shrinking the buffer from 60 to 20 ms while the tap sits deep in it:
  // the rings keep their audio, so there is no hole, and the tap splices
  // back into range through a crossfade, so there is no step. A 220 Hz
  // tone's own largest step is ~0.014 per sample at 0.5 amplitude.
  const auto in = makeSine(3 * 48000, 220.0, 0.5f);
  PitchShift::Params a, b;
  a.semitones = b.semitones = -2;
  a.window = PitchShift::Window::ms60;
  b.window = PitchShift::Window::ms20;
  const int switchAt = 96 * kBlock;
  const auto out = runPitchShift(in, a, kFs, &b, switchAt);
  EXPECT_LT(maxStep(out, switchAt - 4800, switchAt + 4800), 0.03f);
  for (int end = switchAt + 240; end <= switchAt + 4800; end += 240) {
    double acc = 0.0;
    for (int i = end - 240; i < end; ++i) acc += static_cast<double>(out[static_cast<size_t>(i)]) * out[static_cast<size_t>(i)];
    EXPECT_GT(std::sqrt(acc / 240), 0.25) << "hole at " << end;  // the tone's RMS is 0.35
  }
}

TEST(PitchShiftTest, PowerBlendsInsteadOfStepping) {
  // Power on and off mid-tone: the dry and the shifted signal crossfade
  // over 25 ms; neither edge may step, and after the fade-out lands the
  // engine reports itself stopped so the processor can bypass it. An
  // off/on tap inside the fade-out (one block apart) must turn the blend
  // around, not restart the engine at a nonzero mix.
  PitchShift t;
  t.prepare(kFs, kBlock);
  PitchShift::Params p;
  p.semitones = -5;
  constexpr int kLength = 288 * kBlock;  // ~3 s
  const auto in = makeSine(kLength, 220.0, 0.5f);
  juce::AudioBuffer<float> buf(1, kBlock);
  std::vector<float> out;
  int stoppedAt = -1;
  for (int off = 0; off < kLength; off += kBlock) {
    if (off == 48 * kBlock) t.setEnabled(true);
    if (off == 120 * kBlock) t.setEnabled(false);
    if (off == 121 * kBlock) t.setEnabled(true);
    if (off == 192 * kBlock) t.setEnabled(false);
    buf.copyFrom(0, 0, in.data() + off, kBlock);
    if (t.isRunning()) {
      t.setParams(p);
      t.process(buf);
    } else if (off > 192 * kBlock && stoppedAt < 0) {
      stoppedAt = off;
    }
    out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
  }
  EXPECT_LT(maxStep(out, 0, kLength), 0.03f);
  ASSERT_GT(stoppedAt, 0);
  EXPECT_LT(stoppedAt - 192 * kBlock, 48000 * 0.05) << "the fade-out should land within ~25 ms";
  // Off before the power-on and after the fade-out: the input untouched.
  for (int i = 0; i < 48 * kBlock; ++i) ASSERT_EQ(out[static_cast<size_t>(i)], in[static_cast<size_t>(i)]);
  for (int i = stoppedAt; i < kLength; ++i) ASSERT_EQ(out[static_cast<size_t>(i)], in[static_cast<size_t>(i)]);
  // And it did shift in between.
  const double shifted = 220.0 * std::pow(2.0, -5.0 / 12.0);
  EXPECT_GT(db(goertzelPower(out.data() + 60 * kBlock, 32768, shifted)),
            db(goertzelPower(out.data() + 60 * kBlock, 32768, 220.0)) + 20.0);
}

TEST(PitchShiftTest, TonalityBlendsInsteadOfStepping) {
  const auto in = makeSine(3 * 48000, 220.0, 0.5f);
  PitchShift::Params a, b;
  a.semitones = b.semitones = -2;
  b.tonalityHz = 3000.0f;
  const int switchAt = 96 * kBlock;
  const auto out = runPitchShift(in, a, kFs, &b, switchAt);
  EXPECT_LT(maxStep(out, switchAt - 4800, switchAt + 4800), 0.03f);
}

TEST(PitchShiftTest, UpshiftOnTheSmallestBufferStaysOnPitch) {
  // +12 on 20 ms is the tightest case: the tap covers the buffer in ~12 ms
  // and the guard, fade and search lead eat most of it. Splices must still
  // find whole-period jumps.
  const auto in = makeSine(3 * 48000, 440.0, 0.5f);
  PitchShift::Params p;
  p.semitones = 12;
  p.window = PitchShift::Window::ms20;
  const auto out = runPitchShift(in, p);
  // Peak of a 1.36 s spectrum within 3 cents of 880.
  double best = 0.0, bestF = 0.0;
  for (double f = 870.0; f <= 890.0; f += 0.25) {
    const double pw = goertzelPower(out.data() + 48000, 65536, f);
    if (pw > best) best = pw, bestF = f;
  }
  EXPECT_NEAR(1200.0 * std::log2(bestF / 880.0), 0.0, 3.0) << bestF << " Hz";
}

// Processor-level contracts.

namespace {

float denormalised(TONE3000Processor& proc, const char* id) {
  auto* p = proc.parameters.getParameter(id);
  return p->convertFrom0to1(p->getValue());
}

void setDenormalised(TONE3000Processor& proc, const char* id, float value) {
  auto* p = proc.parameters.getParameter(id);
  p->setValueNotifyingHost(p->convertTo0to1(value));
}

// Latency changes are reported from the message thread (see
// TONE3000Processor::updateLatency); pump it.
void pumpMessages() { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); }

std::vector<float> processThrough(TONE3000Processor& proc, const std::vector<float>& in) {
  std::vector<float> out(in.size(), 0.0f);
  juce::AudioBuffer<float> buffer(2, kBlock);
  juce::MidiBuffer midi;
  for (size_t off = 0; off < in.size(); off += kBlock) {
    buffer.copyFrom(0, 0, in.data() + off, kBlock);
    buffer.copyFrom(1, 0, in.data() + off, kBlock);
    proc.processBlock(buffer, midi);
    std::copy(buffer.getReadPointer(0), buffer.getReadPointer(0) + kBlock, out.begin() + static_cast<long>(off));
  }
  return out;
}

}  // namespace

TEST(ProcessorTest, PitchDefaultsAreOffAndMatchTheDsp) {
  TONE3000Processor proc;
  const PitchShift::Params p;
  EXPECT_FLOAT_EQ(proc.parameters.getRawParameterValue("pitchEnabled")->load(), 0.0f);
  EXPECT_NEAR(denormalised(proc, "pitchSemitones"), p.semitones, 1e-4f);
  EXPECT_FLOAT_EQ(proc.parameters.getRawParameterValue("pitchStep")->load(), 1.0f);  // whole semitones
  EXPECT_FLOAT_EQ(denormalised(proc, "pitchTonality"), PitchShift::kTonalityOffHz);  // off
  EXPECT_FLOAT_EQ(denormalised(proc, "pitchWindow"), static_cast<float>(p.window));
  // The knob's ends and centre: ±24, 0 at noon; the range is continuous.
  auto* semis = proc.parameters.getParameter("pitchSemitones");
  EXPECT_FLOAT_EQ(semis->convertFrom0to1(0.0f), -24.0f);
  EXPECT_NEAR(semis->convertFrom0to1(0.5f), 0.0f, 1e-4f);  // a float range: noon is 0 to the ulp
  EXPECT_FLOAT_EQ(semis->convertFrom0to1(1.0f), 24.0f);
  EXPECT_NEAR(semis->convertFrom0to1(semis->convertTo0to1(-1.5f)), -1.5f, 1e-4f);
  // The tonality log map round-trips its ends.
  auto* tonality = proc.parameters.getParameter("pitchTonality");
  EXPECT_NEAR(tonality->convertFrom0to1(0.0f), PitchShift::kTonalityMinHz, 0.5f);
  EXPECT_NEAR(tonality->convertFrom0to1(1.0f), PitchShift::kTonalityOffHz, 0.5f);
}

TEST(ProcessorTest, PitchOffIsBitExactAndZeroLatencyEvenWithAShiftDialled) {
  // Off is the default, and a dialled-in shift with the power off must not
  // leak: the group's knob is a setting, the power is the effect.
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);
  setDenormalised(proc, "pitchSemitones", -4.0f);
  pumpMessages();
  EXPECT_EQ(proc.getLatencySamples(), 0);
  const auto in = makeNoise(64 * kBlock, 7, 0.4f);
  const auto out = processThrough(proc, in);
  // The DC blocker is the only thing in the path (see
  // EmptyChainAt48kIsTransparentWithZeroLatency), so the tail correlates at
  // lag 0 with no smearing.
  EXPECT_EQ(bestCorrelationLag(out, in, 16384, 4096, 64), 0);
}

TEST(ProcessorTest, PitchPowerReportsWindowLatencyFromTheMessageThread) {
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);
  ASSERT_EQ(proc.getLatencySamples(), 0);

  proc.parameters.getParameter("pitchEnabled")->setValueNotifyingHost(1.0f);
  pumpMessages();
  EXPECT_EQ(proc.getLatencySamples(), PitchShift::latencySamples(PitchShift::kDefaultWindow, kFs));

  setDenormalised(proc, "pitchWindow", static_cast<float>(PitchShift::Window::ms60));
  pumpMessages();
  EXPECT_EQ(proc.getLatencySamples(), PitchShift::latencySamples(PitchShift::Window::ms60, kFs));

  // The knob itself never moves the latency: the engine runs at 0 st too.
  setDenormalised(proc, "pitchSemitones", 0.0f);
  setDenormalised(proc, "pitchSemitones", -12.0f);
  pumpMessages();
  EXPECT_EQ(proc.getLatencySamples(), PitchShift::latencySamples(PitchShift::Window::ms60, kFs));

  proc.parameters.getParameter("pitchEnabled")->setValueNotifyingHost(0.0f);
  pumpMessages();
  EXPECT_EQ(proc.getLatencySamples(), 0);
}

TEST(ProcessorTest, PitchShiftsThePluginOutput) {
  // End to end at the default window: a powered -12 lands the octave below
  // in the output, delayed by the reported latency.
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);
  proc.parameters.getParameter("pitchEnabled")->setValueNotifyingHost(1.0f);
  setDenormalised(proc, "pitchSemitones", -12.0f);
  pumpMessages();
  const auto in = makeSine(4 * 48000, 440.0, 0.5f);
  const auto out = processThrough(proc, in);
  expectShiftedTo(out, 440.0, 220.0);
}

TEST(ProcessorTest, PitchSurvivesStateRoundTrip) {
  juce::MemoryBlock state;
  {
    TONE3000Processor a;
    a.parameters.getParameter("pitchEnabled")->setValueNotifyingHost(1.0f);
    setDenormalised(a, "pitchSemitones", -3.25f);
    a.parameters.getParameter("pitchStep")->setValueNotifyingHost(0.0f);
    setDenormalised(a, "pitchTonality", 4000.0f);
    setDenormalised(a, "pitchWindow", 2.0f);
    a.getStateInformation(state);
  }
  TONE3000Processor b;
  b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  EXPECT_FLOAT_EQ(b.parameters.getRawParameterValue("pitchEnabled")->load(), 1.0f);
  EXPECT_NEAR(denormalised(b, "pitchSemitones"), -3.25f, 1e-4f);
  EXPECT_FLOAT_EQ(b.parameters.getRawParameterValue("pitchStep")->load(), 0.0f);
  EXPECT_NEAR(denormalised(b, "pitchTonality"), 4000.0f, 1.0f);
  EXPECT_FLOAT_EQ(denormalised(b, "pitchWindow"), 2.0f);
}

TEST(ProcessorTest, StateFromBeforePitchShiftLandsOnItsDefaults) {
  // A session saved before the pitch shifter existed carries none of its
  // entries. Restoring it must land the group off at its defaults, never
  // leave a live shift running (a silent latency and pitch change on
  // project load).
  juce::MemoryBlock saved;
  {
    TONE3000Processor old;
    old.parameters.getParameter("gateThreshold")->setValueNotifyingHost(0.6f);
    old.getStateInformation(saved);
  }
  juce::ValueTree tree = juce::ValueTree::readFromData(
      static_cast<const char*>(saved.getData()) + 4, saved.getSize() - 4);
  ASSERT_TRUE(tree.isValid());
  juce::ValueTree params = tree.getChildWithName("PARAMETERS");
  ASSERT_TRUE(params.isValid());
  for (const auto* id : {"pitchEnabled", "pitchSemitones", "pitchStep", "pitchTonality",
                         "pitchWindow"}) {
    const auto child = params.getChildWithProperty("id", id);
    ASSERT_TRUE(child.isValid()) << id;
    params.removeChild(child, nullptr);
  }
  juce::MemoryBlock reframed;
  {
    juce::MemoryOutputStream out(reframed, false);
    out.write("T3KB", 4);
    tree.writeToStream(out);
  }

  TONE3000Processor proc;
  proc.parameters.getParameter("pitchEnabled")->setValueNotifyingHost(1.0f);
  setDenormalised(proc, "pitchSemitones", -5.0f);
  proc.setStateInformation(reframed.getData(), static_cast<int>(reframed.getSize()));
  EXPECT_NEAR(proc.parameters.getRawParameterValue("gateThreshold")->load(), -40.0f, 0.01f)
      << "the old state's own parameters must still restore";
  EXPECT_FLOAT_EQ(proc.parameters.getRawParameterValue("pitchEnabled")->load(), 0.0f);
  EXPECT_NEAR(denormalised(proc, "pitchSemitones"), 0.0f, 1e-4f);
  EXPECT_FLOAT_EQ(proc.parameters.getRawParameterValue("pitchStep")->load(), 1.0f);
}

TEST(ProcessorTest, PitchStepRoundsTheShiftForTheEngine) {
  // The knob left at -1.6 st: with STEP on the engine plays -2 (whole
  // semitones, whatever the host automates), with STEP off it plays -1.6.
  for (const bool step : {true, false}) {
    TONE3000Processor proc;
    proc.setPlayConfigDetails(2, 2, kFs, kBlock);
    proc.prepareToPlay(kFs, kBlock);
    proc.parameters.getParameter("pitchEnabled")->setValueNotifyingHost(1.0f);
    proc.parameters.getParameter("pitchStep")->setValueNotifyingHost(step ? 1.0f : 0.0f);
    setDenormalised(proc, "pitchSemitones", -1.6f);
    pumpMessages();
    const auto out = processThrough(proc, makeSine(4 * 48000, 440.0, 0.5f));
    const double played = 440.0 * std::pow(2.0, (step ? -2.0 : -1.6) / 12.0);
    const double other = 440.0 * std::pow(2.0, (step ? -1.6 : -2.0) / 12.0);
    // 0.4 st apart, ~10 Hz here: a 1.4 s window resolves them.
    expectShiftedTo(out, other, played);
  }
}

TEST(ProcessorTest, PresetsCarryPitchShift) {
  const juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("t3k-pitch-shift-tests-" + juce::Uuid().toString());
  tmp.createDirectory();
  {
    TONE3000Processor proc;
    proc.setPresetStoreForTesting(tmp);
    // A stock preset loads with the group off (a preset from a build before
    // the pitch shifter has no entries at all and takes the same default
    // path, see loadPreset's missing-id fallback).
    const juce::var stock = proc.savePreset("Stock");
    ASSERT_TRUE(stock.isObject());

    proc.parameters.getParameter("pitchEnabled")->setValueNotifyingHost(1.0f);
    setDenormalised(proc, "pitchSemitones", -2.0f);
    proc.parameters.getParameter("pitchStep")->setValueNotifyingHost(0.0f);
    const juce::var dropD = proc.savePreset("Drop D");
    ASSERT_TRUE(dropD.isObject());

    ASSERT_TRUE(proc.loadPreset(stock["id"].toString()));
    EXPECT_FLOAT_EQ(proc.parameters.getRawParameterValue("pitchEnabled")->load(), 0.0f);
    EXPECT_NEAR(denormalised(proc, "pitchSemitones"), 0.0f, 1e-4f);

    ASSERT_TRUE(proc.loadPreset(dropD["id"].toString()));
    EXPECT_FLOAT_EQ(proc.parameters.getRawParameterValue("pitchEnabled")->load(), 1.0f);
    EXPECT_NEAR(denormalised(proc, "pitchSemitones"), -2.0f, 1e-4f);
    EXPECT_FLOAT_EQ(proc.parameters.getRawParameterValue("pitchStep")->load(), 0.0f);
  }
  tmp.deleteRecursively();
}

// Legacy ids: the beta builds stored the pitch shifter as transpose*.

namespace {

// The ids the beta builds wrote, by current id.
const std::map<juce::String, juce::String>& legacyIds() {
  static const std::map<juce::String, juce::String> ids = {
      {"pitchEnabled", "transposeEnabled"},   {"pitchSemitones", "transposeSemitones"},
      {"pitchStep", "transposeStep"},         {"pitchTonality", "transposeTonality"},
      {"pitchWindow", "transposeWindow"},
  };
  return ids;
}

// Rewrites `property` of every child from the current id to the beta one,
// the inverse of the migration under test.
void writeLegacyIds(juce::ValueTree tree, const juce::Identifier& property) {
  for (auto child : tree)
    if (const auto it = legacyIds().find(child.getProperty(property).toString()); it != legacyIds().end())
      child.setProperty(property, it->second, nullptr);
}

juce::MemoryBlock framed(const juce::ValueTree& tree) {
  juce::MemoryBlock block;
  juce::MemoryOutputStream out(block, false);
  out.write("T3KB", 4);
  tree.writeToStream(out);
  return block;
}

}  // namespace

TEST(LegacyParamIdsTest, RenamesTheBetaIdsAndKeepsEverythingElse) {
  for (const auto& [current, legacy] : legacyIds()) EXPECT_EQ(t3k::legacy_ids::currentParamId(legacy), current);
  EXPECT_EQ(t3k::legacy_ids::currentParamId("gateThreshold"), "gateThreshold");
  EXPECT_EQ(t3k::legacy_ids::currentParamId(""), "");

  juce::ValueTree tree("PARAMETERS");
  const auto add = [&](const char* id, float value) {
    juce::ValueTree p("PARAM");
    p.setProperty("id", id, nullptr);
    p.setProperty("value", value, nullptr);
    tree.appendChild(p, nullptr);
  };
  add("gateThreshold", -40.0f);
  add("transposeSemitones", -2.0f);
  add("transposeStep", 0.0f);
  EXPECT_EQ(t3k::legacy_ids::migrateParamIds(tree, "id"), 2);
  ASSERT_EQ(tree.getNumChildren(), 3);
  EXPECT_EQ(tree.getChild(0).getProperty("id").toString(), "gateThreshold");
  EXPECT_EQ(tree.getChild(1).getProperty("id").toString(), "pitchSemitones");
  EXPECT_FLOAT_EQ(static_cast<float>(tree.getChild(1).getProperty("value")), -2.0f);
  EXPECT_EQ(tree.getChild(2).getProperty("id").toString(), "pitchStep");
  // Already current: nothing to do, nothing touched.
  EXPECT_EQ(t3k::legacy_ids::migrateParamIds(tree, "id"), 0);
  EXPECT_EQ(t3k::legacy_ids::migrateParamIds(juce::ValueTree(), "id"), 0);
}

TEST(LegacyParamIdsTest, ALegacyEntryReplacesAStaleCurrentOne) {
  // A state the new build saved, then a beta build loaded and saved again,
  // holds both: the beta build kept the id it didn't know and wrote its own
  // beside it. The beta entry is the one the user last edited, whichever
  // side of the stale one it landed on.
  for (const bool legacyFirst : {true, false}) {
    juce::ValueTree tree("PARAMETERS");
    const auto add = [&](const char* id, float value) {
      juce::ValueTree p("PARAM");
      p.setProperty("id", id, nullptr);
      p.setProperty("value", value, nullptr);
      tree.appendChild(p, nullptr);
    };
    if (legacyFirst) add("transposeEnabled", 1.0f);
    add("pitchEnabled", 0.0f);
    add("gateThreshold", -40.0f);
    if (!legacyFirst) add("transposeEnabled", 1.0f);
    EXPECT_EQ(t3k::legacy_ids::migrateParamIds(tree, "id"), 1);
    ASSERT_EQ(tree.getNumChildren(), 2) << legacyFirst;
    const auto enabled = tree.getChildWithProperty("id", "pitchEnabled");
    ASSERT_TRUE(enabled.isValid());
    EXPECT_FLOAT_EQ(static_cast<float>(enabled.getProperty("value")), 1.0f) << legacyFirst;
    EXPECT_TRUE(tree.getChildWithProperty("id", "gateThreshold").isValid());
  }
}

TEST(ProcessorTest, StateFromABetaBuildRestoresThePitchShiftAndItsMidiMap) {
  // A beta session: the parameters and a MIDI mapping (an expression pedal
  // on the semitones) stored under the transpose* ids. The restore must land
  // every value and keep the mapping, which an unknown target id would drop.
  juce::MemoryBlock saved;
  {
    TONE3000Processor beta;
    beta.parameters.getParameter("pitchEnabled")->setValueNotifyingHost(1.0f);
    setDenormalised(beta, "pitchSemitones", -3.25f);
    beta.parameters.getParameter("pitchStep")->setValueNotifyingHost(0.0f);
    setDenormalised(beta, "pitchTonality", 4000.0f);
    setDenormalised(beta, "pitchWindow", 2.0f);
    ASSERT_TRUE(beta.midiMapper.setCcMapping("pitchSemitones", 11));
    ASSERT_TRUE(beta.midiMapper.setCcMapping("gateEnabled", 20));
    beta.getStateInformation(saved);
  }
  juce::ValueTree tree = juce::ValueTree::readFromData(
      static_cast<const char*>(saved.getData()) + 4, saved.getSize() - 4);
  ASSERT_TRUE(tree.isValid());
  writeLegacyIds(tree.getChildWithName("PARAMETERS"), "id");
  writeLegacyIds(tree.getChildWithName("MidiMappings"), "targetId");
  ASSERT_TRUE(tree.getChildWithName("PARAMETERS").getChildWithProperty("id", "transposeSemitones").isValid());
  const auto reframed = framed(tree);

  TONE3000Processor proc;
  proc.setStateInformation(reframed.getData(), static_cast<int>(reframed.getSize()));
  EXPECT_FLOAT_EQ(proc.parameters.getRawParameterValue("pitchEnabled")->load(), 1.0f);
  EXPECT_NEAR(denormalised(proc, "pitchSemitones"), -3.25f, 1e-4f);
  EXPECT_FLOAT_EQ(proc.parameters.getRawParameterValue("pitchStep")->load(), 0.0f);
  EXPECT_NEAR(denormalised(proc, "pitchTonality"), 4000.0f, 1.0f);
  EXPECT_FLOAT_EQ(denormalised(proc, "pitchWindow"), 2.0f);

  const juce::var midi = proc.midiMapper.getState();
  const auto* mappings = midi["mappings"].getArray();
  ASSERT_NE(mappings, nullptr);
  ASSERT_EQ(mappings->size(), 2);
  bool semitonesMapped = false;
  for (const auto& m : *mappings)
    if (m["targetId"].toString() == "pitchSemitones" && static_cast<int>(m["number"]) == 11) semitonesMapped = true;
  EXPECT_TRUE(semitonesMapped);

  // The next save writes the current ids, so the migration runs once.
  juce::MemoryBlock resaved;
  proc.getStateInformation(resaved);
  const juce::ValueTree after = juce::ValueTree::readFromData(
      static_cast<const char*>(resaved.getData()) + 4, resaved.getSize() - 4);
  for (const auto& [current, legacy] : legacyIds()) {
    EXPECT_TRUE(after.getChildWithName("PARAMETERS").getChildWithProperty("id", current).isValid()) << current;
    EXPECT_FALSE(after.getChildWithName("PARAMETERS").getChildWithProperty("id", legacy).isValid()) << legacy;
  }
  EXPECT_FALSE(after.getChildWithName("MidiMappings").getChildWithProperty("targetId", "transposeSemitones").isValid());
}

TEST(ProcessorTest, PresetFromABetaBuildLoadsThePitchShift) {
  const juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("t3k-pitch-shift-tests-" + juce::Uuid().toString());
  tmp.createDirectory();
  {
    TONE3000Processor proc;
    proc.setPresetStoreForTesting(tmp);
    proc.parameters.getParameter("pitchEnabled")->setValueNotifyingHost(1.0f);
    setDenormalised(proc, "pitchSemitones", -2.0f);
    proc.parameters.getParameter("pitchStep")->setValueNotifyingHost(0.0f);
    const juce::var dropD = proc.savePreset("Drop D");
    ASSERT_TRUE(dropD.isObject());

    // Rewrite the file the way a beta build would have saved it.
    const auto files = tmp.findChildFiles(juce::File::findFiles, false, "*.t3kpreset");
    ASSERT_EQ(files.size(), 1);
    juce::ValueTree preset = t3k::presetfile::read(files[0]);
    ASSERT_TRUE(preset.isValid());
    writeLegacyIds(preset.getChildWithName("Params"), "id");
    ASSERT_TRUE(t3k::presetfile::write(files[0], preset));

    ASSERT_TRUE(proc.resetToDefault());
    EXPECT_FLOAT_EQ(proc.parameters.getRawParameterValue("pitchEnabled")->load(), 0.0f);

    ASSERT_TRUE(proc.loadPreset(dropD["id"].toString()));
    EXPECT_FLOAT_EQ(proc.parameters.getRawParameterValue("pitchEnabled")->load(), 1.0f);
    EXPECT_NEAR(denormalised(proc, "pitchSemitones"), -2.0f, 1e-4f);
    EXPECT_FLOAT_EQ(proc.parameters.getRawParameterValue("pitchStep")->load(), 0.0f);

    // Saving over it writes the current ids.
    ASSERT_TRUE(proc.savePreset("Drop D").isObject());
    const juce::ValueTree resaved = t3k::presetfile::read(files[0]);
    EXPECT_TRUE(resaved.getChildWithName("Params").getChildWithProperty("id", "pitchSemitones").isValid());
    EXPECT_FALSE(resaved.getChildWithName("Params").getChildWithProperty("id", "transposeSemitones").isValid());
  }
  tmp.deleteRecursively();
}
