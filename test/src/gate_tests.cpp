// Noise gate tests
//
// The mechanical guarantees of the input-stage gate (NoiseGate.h) and of its
// advanced-panel parameters (gateRelease / gateHold / gateRange), which
// were the first parameters added after the public release and so carry
// every compatibility contract at once:
//
//   NoiseGateTest   release sets the tail's time constant, hold delays the
//                   close by exactly its duration, range sets the closed
//                   floor, and the Params defaults are the retuned values
//                   (see NoiseGate::Params for why they moved).
//   ProcessorTest   the APVTS defaults match those, the deck survives a
//                   state round trip, a state saved before the deck existed
//                   lands on the defaults (even over a live non-default
//                   value), and presets carry the deck.
//
// Whether a given release "feels tight" is a by-ear item.
#include "NoiseGate.h"
#include "Processor.h"
#include "test_helpers.h"

#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <vector>

namespace {

constexpr int kBlock = 256;
constexpr double kToneHz = 1000.0;

// Streams `in` (mono) through a gate held at `p`; the input's length must be
// a multiple of kBlock.
std::vector<float> runGate(const std::vector<float>& in, const NoiseGate::Params& p) {
  NoiseGate gate;
  gate.prepare(kFs);
  gate.setParams(p);
  juce::AudioBuffer<float> buf(1, kBlock);
  std::vector<float> out;
  out.reserve(in.size());
  for (size_t off = 0; off < in.size(); off += kBlock) {
    buf.copyFrom(0, 0, in.data() + off, kBlock);
    gate.process(buf);
    out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
  }
  return out;
}

int ms(double milliseconds) { return static_cast<int>(std::lround(milliseconds * 0.001 * kFs)); }

// A loud burst that opens the gate, then the same tone far below the
// threshold: the closing gate's tail rides a steady carrier, so the gain at
// any instant is the output/input RMS ratio.
std::vector<float> burstThenFloor(int burstSamples, int floorSamples, float floorAmplitude) {
  auto x = makeSine(burstSamples + floorSamples, kToneHz, 0.5f);
  for (int i = burstSamples; i < burstSamples + floorSamples; ++i)
    x[static_cast<size_t>(i)] *= floorAmplitude / 0.5f;
  return x;
}

double gainDbAt(const std::vector<float>& out, const std::vector<float>& in, int at) {
  const int window = ms(2.0);  // two cycles of the 1 kHz carrier
  double o = 0.0, i = 0.0;
  for (int k = at; k < at + window; ++k) {
    o += static_cast<double>(out[static_cast<size_t>(k)]) * out[static_cast<size_t>(k)];
    i += static_cast<double>(in[static_cast<size_t>(k)]) * in[static_cast<size_t>(k)];
  }
  return 10.0 * std::log10(std::max(o, 1e-30) / std::max(i, 1e-30));
}

// First sample at/after `from` where the gain has fallen below `dbBelow`;
// -1 if it never does.
int firstBelow(const std::vector<float>& out, const std::vector<float>& in, int from,
               double dbBelow) {
  const int step = ms(0.5);
  for (int at = from; at + ms(2.0) <= static_cast<int>(out.size()); at += step)
    if (gainDbAt(out, in, at) < dbBelow)
      return at;
  return -1;
}

}  // namespace

// Release and hold moved from the launch build's 100 / 50 ms on purpose
// (NoiseGate::Params); the threshold and the full-mute range did not.
TEST(NoiseGateTest, DefaultParamsAreTheRetunedValues) {
  const NoiseGate::Params p;
  EXPECT_FLOAT_EQ(p.thresholdDb, -80.0f);
  EXPECT_FLOAT_EQ(p.releaseMs, 50.0f);
  EXPECT_FLOAT_EQ(p.holdMs, 20.0f);
  EXPECT_FLOAT_EQ(p.rangeDb, 80.0f);
}

// Time constant of the tail, from the close starting (gain first 3 dB down)
// to -20 dB: a one-pole spans that in ln(10) - 0.36 ≈ 1.94 τ.
double measuredTauMs(const std::vector<float>& out, const std::vector<float>& in, int from) {
  const int closeStart = firstBelow(out, in, from, -3.0);
  const int at20 = firstBelow(out, in, from, -20.0);
  EXPECT_GT(closeStart, 0);
  EXPECT_GT(at20, closeStart);
  return (at20 - closeStart) / (0.001 * kFs) / (std::log(10.0) - 0.36);
}

TEST(NoiseGateTest, ReleaseSetsTheTailTimeConstant) {
  // Hold at zero so the tail is the release alone. The target gain also
  // decays with the detector's fixed 25 ms fall, cubed by the expander curve
  // (≈ 8 ms): releases above that follow the knob, the tightest settings
  // bottom out there.
  const int burst = ms(200), tail = ms(3000);
  const auto in = burstThenFloor(burst, tail, 0.0005f);  // floor at -66 dB
  auto tauFor = [&](float releaseMs) {
    NoiseGate::Params p;
    p.thresholdDb = -40.0f;
    p.releaseMs = releaseMs;
    p.holdMs = 0.0f;
    return measuredTauMs(runGate(in, p), in, burst);
  };
  EXPECT_NEAR(tauFor(100.0f), 100.0, 25.0);
  EXPECT_NEAR(tauFor(500.0f), 500.0, 125.0);
  EXPECT_LT(tauFor(5.0f), 10.0) << "the tightest release must land on the detector floor";
}

TEST(NoiseGateTest, HoldDelaysTheCloseByItsDuration) {
  const int burst = ms(200), tail = ms(1500);
  const auto in = burstThenFloor(burst, tail, 0.0005f);
  auto closeStart = [&](float holdMs) {
    NoiseGate::Params p;
    p.thresholdDb = -40.0f;
    p.releaseMs = 5.0f;
    p.holdMs = holdMs;
    return firstBelow(runGate(in, p), in, burst, -3.0);
  };
  const int noHold = closeStart(0.0f);
  const int hold150 = closeStart(150.0f);
  ASSERT_GT(noHold, 0);
  ASSERT_GT(hold150, 0);
  EXPECT_NEAR(hold150 - noHold, ms(150), ms(3));
}

TEST(NoiseGateTest, RangeSetsTheClosedFloor) {
  // A carrier 50 dB under the threshold never opens the gate; the expander
  // curve would put it at -150 dB, so what comes through is the range floor.
  const auto in = makeSine(ms(500), kToneHz, 0.001f);  // -60 dB
  for (const float rangeDb : {20.0f, 40.0f, 80.0f}) {
    SCOPED_TRACE("range " + std::to_string(rangeDb) + " dB");
    NoiseGate::Params p;
    p.thresholdDb = -10.0f;
    p.rangeDb = rangeDb;
    const auto out = runGate(in, p);
    EXPECT_NEAR(gainDbAt(out, in, ms(400)), -rangeDb, 0.5);
  }
}

TEST(NoiseGateTest, ParamsApplyMidStream) {
  // A running gate must pick up a new release without a re-prepare: the
  // processor pushes the cached parameters every block. The burst ends on a
  // block boundary so the change lands exactly there.
  const int burst = 40 * kBlock, tail = ms(1500);
  const auto in = burstThenFloor(burst, tail, 0.0005f);
  NoiseGate gate;
  gate.prepare(kFs);
  NoiseGate::Params p;
  p.thresholdDb = -40.0f;
  p.releaseMs = 500.0f;
  p.holdMs = 0.0f;
  gate.setParams(p);
  juce::AudioBuffer<float> buf(1, kBlock);
  std::vector<float> out;
  for (size_t off = 0; off < in.size(); off += kBlock) {
    if (static_cast<int>(off) == burst) {
      p.releaseMs = 5.0f;
      gate.setParams(p);
    }
    buf.copyFrom(0, 0, in.data() + off, kBlock);
    gate.process(buf);
    out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
  }
  EXPECT_LT(measuredTauMs(out, in, burst), 10.0) << "the 500 ms release stuck after the change to 5 ms";
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

}  // namespace

TEST(ProcessorTest, GateDeckDefaultsMatchTheDsp) {
  TONE3000Processor proc;
  const NoiseGate::Params p;
  EXPECT_FLOAT_EQ(denormalised(proc, "gateRelease"), p.releaseMs);
  EXPECT_FLOAT_EQ(denormalised(proc, "gateHold"), p.holdMs);
  EXPECT_FLOAT_EQ(denormalised(proc, "gateRange"), p.rangeDb);
  // The release's log map round-trips its ends, and its default sits at the
  // knob's centre (the UI's KnobScale mirrors both).
  auto* release = proc.parameters.getParameter("gateRelease");
  EXPECT_FLOAT_EQ(release->convertFrom0to1(0.0f), 5.0f);
  EXPECT_FLOAT_EQ(release->convertFrom0to1(1.0f), 500.0f);
  EXPECT_NEAR(release->convertTo0to1(p.releaseMs), 0.5f, 1e-6f);
}

TEST(ProcessorTest, GateDeckSurvivesStateRoundTrip) {
  juce::MemoryBlock state;
  {
    TONE3000Processor a;
    setDenormalised(a, "gateRelease", 12.0f);
    setDenormalised(a, "gateHold", 5.0f);
    setDenormalised(a, "gateRange", 40.0f);
    a.getStateInformation(state);
  }
  TONE3000Processor b;
  b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  EXPECT_NEAR(denormalised(b, "gateRelease"), 12.0f, 0.01f);
  EXPECT_NEAR(denormalised(b, "gateHold"), 5.0f, 0.01f);
  EXPECT_NEAR(denormalised(b, "gateRange"), 40.0f, 0.01f);
}

TEST(ProcessorTest, StateFromBeforeTheGateDeckLandsOnItsDefaults) {
  // A session saved by the launch build carries no gateRelease / gateHold /
  // gateRange entries. Restoring it must land the deck on the defaults (the
  // deliberately tighter close every stock rig now gets), not leave whatever
  // this instance held before the restore.
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
  for (const auto* id : {"gateRelease", "gateHold", "gateRange"}) {
    const auto child = params.getChildWithProperty("id", id);
    ASSERT_TRUE(child.isValid());
    params.removeChild(child, nullptr);
  }
  juce::MemoryBlock reframed;
  {
    juce::MemoryOutputStream out(reframed, false);
    out.write("T3KB", 4);
    tree.writeToStream(out);
  }

  TONE3000Processor proc;
  setDenormalised(proc, "gateRelease", 12.0f);
  setDenormalised(proc, "gateHold", 5.0f);
  setDenormalised(proc, "gateRange", 40.0f);
  proc.setStateInformation(reframed.getData(), static_cast<int>(reframed.getSize()));
  EXPECT_NEAR(proc.parameters.getRawParameterValue("gateThreshold")->load(), -40.0f, 0.01f)
      << "the old state's own parameters must still restore";
  const NoiseGate::Params p;
  EXPECT_FLOAT_EQ(denormalised(proc, "gateRelease"), p.releaseMs);
  EXPECT_FLOAT_EQ(denormalised(proc, "gateHold"), p.holdMs);
  EXPECT_FLOAT_EQ(denormalised(proc, "gateRange"), p.rangeDb);
}

TEST(ProcessorTest, PresetsCarryTheGateDeck) {
  const juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("t3k-gate-tests-" + juce::Uuid().toString());
  tmp.createDirectory();
  {
    TONE3000Processor proc;
    proc.setPresetStoreForTesting(tmp);
    setDenormalised(proc, "gateRelease", 12.0f);
    setDenormalised(proc, "gateHold", 5.0f);
    setDenormalised(proc, "gateRange", 40.0f);
    const juce::var saved = proc.savePreset("Tight");
    ASSERT_TRUE(saved.isObject());

    setDenormalised(proc, "gateRelease", 300.0f);
    setDenormalised(proc, "gateHold", 150.0f);
    setDenormalised(proc, "gateRange", 20.0f);
    ASSERT_TRUE(proc.loadPreset(saved["id"].toString()));
    EXPECT_NEAR(denormalised(proc, "gateRelease"), 12.0f, 0.01f);
    EXPECT_NEAR(denormalised(proc, "gateHold"), 5.0f, 0.01f);
    EXPECT_NEAR(denormalised(proc, "gateRange"), 40.0f, 0.01f);
  }
  tmp.deleteRecursively();
}
