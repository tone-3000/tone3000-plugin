// Transpose tests
//
// The mechanical guarantees of the input-stage pitch shifter (Transpose.h)
// and of the processor wiring around it:
//
//   TransposeTest   latency is exactly the window (the figure the processor
//                   reports from the parameters must match the engine), the
//                   shift lands on the expected frequency including the fine
//                   trim, a mono buffer against the stereo engine is safe,
//                   rate / block-size / window changes and ±12 extremes stay
//                   finite, and a window change is a clean swap.
//   ProcessorTest   powered off the plugin is bit-exact and zero-latency;
//                   powering on reports boundary + window latency from the
//                   message thread; the parameters round-trip through state
//                   and presets; a state or preset saved before Transpose
//                   existed lands it on the defaults (off).
//
// Whether a given window "warbles" on chords is a by-ear item.
#include "Processor.h"
#include "Transpose.h"
#include "test_helpers.h"

#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <vector>

namespace {

constexpr int kBlock = 512;

// Streams `in` (mono, a multiple of kBlock long) through a shifter held at
// `p`, optionally switching to `p2` at sample `switchAt`.
std::vector<float> runTranspose(const std::vector<float>& in, const Transpose::Params& p,
                                double fs = kFs, const Transpose::Params* p2 = nullptr,
                                int switchAt = -1) {
  Transpose t;
  t.prepare(fs, kBlock);
  t.setParams(p);
  juce::AudioBuffer<float> buf(1, kBlock);
  std::vector<float> out;
  out.reserve(in.size());
  for (size_t off = 0; off < in.size(); off += kBlock) {
    if (p2 != nullptr && static_cast<int>(off) == switchAt) t.setParams(*p2);
    buf.copyFrom(0, 0, in.data() + off, kBlock);
    t.process(buf);
    out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
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

// Energy at `expected` must dominate energy left at `original` in the
// settled tail (past the longest window and the engine's settle).
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

TEST(TransposeTest, LatencyIsExactlyTheWindowAtAnyRate) {
  // The processor reports latency from the parameters via the static
  // figure, before the audio thread has switched engines; the engine the
  // audio thread runs must agree with it, at every window and host rate.
  for (const double fs : {44100.0, 48000.0, 96000.0}) {
    Transpose t;
    t.prepare(fs, kBlock);
    for (int w = 0; w < static_cast<int>(Transpose::kWindowMs.size()); ++w) {
      const auto window = Transpose::windowFromIndex(w);
      Transpose::Params p;
      p.window = window;
      t.setParams(p);
      EXPECT_EQ(t.latencySamples(), Transpose::latencySamples(window, fs)) << fs << " Hz, window " << w;
      EXPECT_EQ(t.latencySamples(), static_cast<int>(fs * Transpose::windowMs(window) / 1000));
    }
  }
  EXPECT_EQ(Transpose::latencySamples(Transpose::Window::balanced, kFs), 2880);  // 60 ms
}

TEST(TransposeTest, UnityRatioIsTimeAlignedAtTheReportedLatency) {
  // Powered on at 0 st the engine still runs (so sweeping through 0 never
  // jumps the timing); its output must then line up with the input at the
  // reported latency (a broadband signal: a phase vocoder keeps a pure
  // tone's frequency, not its absolute phase) and sit at unity gain.
  Transpose::Params p;
  const int latency = Transpose::latencySamples(p.window, kFs);
  const auto noise = makeNoise(4 * 48000, 3, 0.4f);
  const auto out = runTranspose(noise, p);
  EXPECT_EQ(bestCorrelationLag(out, noise, 96000, 16384, latency + 256), latency);
  const auto tone = makeSine(4 * 48000, 440.0, 0.5f);
  const auto toneOut = runTranspose(tone, p);
  const double gain = db(goertzelPower(toneOut.data() + 96000, 16384, 440.0)) -
                      db(goertzelPower(tone.data() + 96000, 16384, 440.0));
  EXPECT_NEAR(gain, 0.0, 0.5);
}

TEST(TransposeTest, OctaveUpAndDownLandOnTheFrequency) {
  const auto in = makeSine(3 * 48000, 440.0, 0.5f);
  Transpose::Params up, down;
  up.semitones = 12;
  down.semitones = -12;
  expectShiftedTo(runTranspose(in, up), 440.0, 880.0);
  expectShiftedTo(runTranspose(in, down), 440.0, 220.0);
}

TEST(TransposeTest, FineTrimJoinsTheRatio) {
  // -2 st and +50 cents is a ratio of 2^(-1.5/12).
  const auto in = makeSine(3 * 48000, 440.0, 0.5f);
  Transpose::Params p;
  p.semitones = -2;
  p.cents = 50.0f;
  expectShiftedTo(runTranspose(in, p), 440.0, 440.0 * std::pow(2.0, -1.5 / 12.0));
}

TEST(TransposeTest, TonalityLimitHoldsHighPartialsBack) {
  // Above the limit the shift becomes an offset instead of a ratio: an
  // octave up moves a 6 kHz partial to 12 kHz with the limit off, but only
  // by the limit's own offset (2 kHz * (2 - 1)) with a 2 kHz limit.
  const auto in = makeSine(3 * 48000, 6000.0, 0.5f);
  Transpose::Params p;
  p.semitones = 12;
  expectShiftedTo(runTranspose(in, p), 6000.0, 12000.0);
  p.tonalityHz = 2000.0f;
  // Signalsmith splits the limit between input and output (limit/sqrt(2)).
  const double limit = 2000.0 / std::sqrt(2.0);
  expectShiftedTo(runTranspose(in, p), 6000.0, 6000.0 + limit);
}

TEST(TransposeTest, MonoBufferAgainstTheStereoEngineIsSafe) {
  // The engine is always stereo-configured; a genuinely mono host buffer
  // (see ProcessorTest.StereoChainsFoldToMonoWithoutAStereoOutput) feeds
  // both lanes from its one channel and reads one back (the contributor's
  // original wrapper read past the buffer here).
  Transpose t;
  t.prepare(kFs, kBlock);
  Transpose::Params p;
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

TEST(TransposeTest, SurvivesRateBlockAndWindowChanges) {
  Transpose t;
  Transpose::Params p;
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
  t.setParams(p);
  run(256, 1);
  // A device change is a fresh prepare(), like a real prepareToPlay.
  t.prepare(96000.0, 1024);
  run(1024, 2);
  // A block bigger than the scratch (an offline bounce) runs in slices.
  run(4096, 3);
  // Every window, switched live.
  for (int w = 0; w < static_cast<int>(Transpose::kWindowMs.size()); ++w) {
    p.window = Transpose::windowFromIndex(w);
    t.setParams(p);
    run(1024, 10 + static_cast<unsigned>(w));
  }
}

TEST(TransposeTest, ExtremeShiftsStayBounded) {
  for (const int semis : {-12, 12}) {
    Transpose::Params p;
    p.semitones = semis;
    const auto out = runTranspose(makeNoise(static_cast<int>(kFs), 555u + static_cast<unsigned>(semis), 0.5f), p);
    for (const float s : out) {
      ASSERT_TRUE(std::isfinite(s)) << semis;
      ASSERT_LT(std::abs(s), 10.0f) << semis;
    }
  }
}

TEST(TransposeTest, WindowChangeKeepsTheShift) {
  // Switching engines mid-stream: the new engine must carry the current
  // frequency map (a stale 1.0 map would drop the shift), and the old
  // engine's state must not bleed through.
  const auto in = makeSine(4 * 48000, 440.0, 0.5f);
  Transpose::Params a, b;
  a.semitones = b.semitones = 12;
  a.window = Transpose::Window::fast;
  b.window = Transpose::Window::smooth;
  const auto out = runTranspose(in, a, kFs, &b, 48000);
  expectShiftedTo(out, 440.0, 880.0);
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

TEST(ProcessorTest, TransposeDefaultsAreOffAndMatchTheDsp) {
  TONE3000Processor proc;
  const Transpose::Params p;
  EXPECT_FLOAT_EQ(proc.parameters.getRawParameterValue("transposeEnabled")->load(), 0.0f);
  EXPECT_FLOAT_EQ(denormalised(proc, "transposeSemitones"), static_cast<float>(p.semitones));
  EXPECT_NEAR(denormalised(proc, "transposeFine"), p.cents, 1e-4f);
  EXPECT_FLOAT_EQ(denormalised(proc, "transposeTonality"), Transpose::kTonalityOffHz);  // off
  EXPECT_FLOAT_EQ(denormalised(proc, "transposeWindow"), static_cast<float>(p.window));
  // The knob's ends and centre map to whole semitones.
  auto* semis = proc.parameters.getParameter("transposeSemitones");
  EXPECT_FLOAT_EQ(semis->convertFrom0to1(0.0f), -12.0f);
  EXPECT_FLOAT_EQ(semis->convertFrom0to1(0.5f), 0.0f);
  EXPECT_FLOAT_EQ(semis->convertFrom0to1(1.0f), 12.0f);
  // The tonality log map round-trips its ends.
  auto* tonality = proc.parameters.getParameter("transposeTonality");
  EXPECT_NEAR(tonality->convertFrom0to1(0.0f), Transpose::kTonalityMinHz, 0.5f);
  EXPECT_NEAR(tonality->convertFrom0to1(1.0f), Transpose::kTonalityOffHz, 0.5f);
}

TEST(ProcessorTest, TransposeOffIsBitExactAndZeroLatencyEvenWithAShiftDialled) {
  // Off is the default, and a dialled-in shift with the power off must not
  // leak: the group's knob is a setting, the power is the effect.
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);
  setDenormalised(proc, "transposeSemitones", -4.0f);
  pumpMessages();
  EXPECT_EQ(proc.getLatencySamples(), 0);
  const auto in = makeNoise(64 * kBlock, 7, 0.4f);
  const auto out = processThrough(proc, in);
  // The DC blocker is the only thing in the path (see
  // EmptyChainAt48kIsTransparentWithZeroLatency), so the tail correlates at
  // lag 0 with no smearing.
  EXPECT_EQ(bestCorrelationLag(out, in, 16384, 4096, 64), 0);
}

TEST(ProcessorTest, TransposePowerReportsWindowLatencyFromTheMessageThread) {
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);
  ASSERT_EQ(proc.getLatencySamples(), 0);

  proc.parameters.getParameter("transposeEnabled")->setValueNotifyingHost(1.0f);
  pumpMessages();
  EXPECT_EQ(proc.getLatencySamples(), Transpose::latencySamples(Transpose::Window::balanced, kFs));

  setDenormalised(proc, "transposeWindow", static_cast<float>(Transpose::Window::smooth));
  pumpMessages();
  EXPECT_EQ(proc.getLatencySamples(), Transpose::latencySamples(Transpose::Window::smooth, kFs));

  // The knob itself never moves the latency: the engine runs at 0 st too.
  setDenormalised(proc, "transposeSemitones", 0.0f);
  setDenormalised(proc, "transposeSemitones", -12.0f);
  pumpMessages();
  EXPECT_EQ(proc.getLatencySamples(), Transpose::latencySamples(Transpose::Window::smooth, kFs));

  proc.parameters.getParameter("transposeEnabled")->setValueNotifyingHost(0.0f);
  pumpMessages();
  EXPECT_EQ(proc.getLatencySamples(), 0);
}

TEST(ProcessorTest, TransposeShiftsThePluginOutput) {
  // End to end at the default window: a powered -12 lands the octave below
  // in the output, delayed by the reported latency.
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);
  proc.parameters.getParameter("transposeEnabled")->setValueNotifyingHost(1.0f);
  setDenormalised(proc, "transposeSemitones", -12.0f);
  pumpMessages();
  const auto in = makeSine(4 * 48000, 440.0, 0.5f);
  const auto out = processThrough(proc, in);
  expectShiftedTo(out, 440.0, 220.0);
}

TEST(ProcessorTest, TransposeSurvivesStateRoundTrip) {
  juce::MemoryBlock state;
  {
    TONE3000Processor a;
    a.parameters.getParameter("transposeEnabled")->setValueNotifyingHost(1.0f);
    setDenormalised(a, "transposeSemitones", -3.0f);
    setDenormalised(a, "transposeFine", 25.0f);
    setDenormalised(a, "transposeTonality", 4000.0f);
    setDenormalised(a, "transposeWindow", 2.0f);
    a.getStateInformation(state);
  }
  TONE3000Processor b;
  b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  EXPECT_FLOAT_EQ(b.parameters.getRawParameterValue("transposeEnabled")->load(), 1.0f);
  EXPECT_FLOAT_EQ(denormalised(b, "transposeSemitones"), -3.0f);
  EXPECT_NEAR(denormalised(b, "transposeFine"), 25.0f, 0.01f);
  EXPECT_NEAR(denormalised(b, "transposeTonality"), 4000.0f, 1.0f);
  EXPECT_FLOAT_EQ(denormalised(b, "transposeWindow"), 2.0f);
}

TEST(ProcessorTest, StateFromBeforeTransposeLandsOnItsDefaults) {
  // A session saved before Transpose existed carries none of its entries.
  // Restoring it must land the group off at its defaults, never leave a
  // live shift running (a silent latency and pitch change on project load).
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
  for (const auto* id : {"transposeEnabled", "transposeSemitones", "transposeFine", "transposeTonality",
                         "transposeWindow"}) {
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
  proc.parameters.getParameter("transposeEnabled")->setValueNotifyingHost(1.0f);
  setDenormalised(proc, "transposeSemitones", -5.0f);
  proc.setStateInformation(reframed.getData(), static_cast<int>(reframed.getSize()));
  EXPECT_NEAR(proc.parameters.getRawParameterValue("gateThreshold")->load(), -40.0f, 0.01f)
      << "the old state's own parameters must still restore";
  EXPECT_FLOAT_EQ(proc.parameters.getRawParameterValue("transposeEnabled")->load(), 0.0f);
  EXPECT_FLOAT_EQ(denormalised(proc, "transposeSemitones"), 0.0f);
}

TEST(ProcessorTest, PresetsCarryTranspose) {
  const juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("t3k-transpose-tests-" + juce::Uuid().toString());
  tmp.createDirectory();
  {
    TONE3000Processor proc;
    proc.setPresetStoreForTesting(tmp);
    // A stock preset loads with the group off (a preset from a pre-Transpose
    // build has no entries at all and takes the same default path, see
    // loadPreset's missing-id fallback).
    const juce::var stock = proc.savePreset("Stock");
    ASSERT_TRUE(stock.isObject());

    proc.parameters.getParameter("transposeEnabled")->setValueNotifyingHost(1.0f);
    setDenormalised(proc, "transposeSemitones", -2.0f);
    setDenormalised(proc, "transposeFine", -10.0f);
    const juce::var dropD = proc.savePreset("Drop D");
    ASSERT_TRUE(dropD.isObject());

    ASSERT_TRUE(proc.loadPreset(stock["id"].toString()));
    EXPECT_FLOAT_EQ(proc.parameters.getRawParameterValue("transposeEnabled")->load(), 0.0f);
    EXPECT_FLOAT_EQ(denormalised(proc, "transposeSemitones"), 0.0f);

    ASSERT_TRUE(proc.loadPreset(dropD["id"].toString()));
    EXPECT_FLOAT_EQ(proc.parameters.getRawParameterValue("transposeEnabled")->load(), 1.0f);
    EXPECT_FLOAT_EQ(denormalised(proc, "transposeSemitones"), -2.0f);
    EXPECT_NEAR(denormalised(proc, "transposeFine"), -10.0f, 0.01f);
  }
  tmp.deleteRecursively();
}
