// Processor-level tests
//
// The full TONE3000Processor (headless build) driven the way a host drives
// it: prepareToPlay + processBlock. These pin the plugin's host-facing
// contracts that no unit-level DSP test can see:
//
//   - a 48 kHz host with an empty chain is transparent with zero latency,
//   - at other host rates the boundary resampler's *reported* latency (PDC)
//     matches its *measured* group delay,
//   - toggling oversampling never changes reported latency (no PDC churn),
//   - without a stereo output the spread parameter is inert (plain mono out)
//     and getChainState reports the capability to the UI,
//   - without a stereo output stereo chains keep running and are summed to
//     mono, ½(L+R) with solo/polarity live inside the sum,
//   - parameter state survives a save/restore round trip,
//   - garbage, legacy-format, and newer-schema state blobs are ignored,
//   - the tail report covers the DC blocker floor.
//
// Model tests use embedded local fixtures, so nothing touches the network.
// Runs on the message thread (ScopedJuceInitialiser_GUI in main).
// Oversampling parameter changes are applied via a re-prepare, exactly like
// a host would (the live-toggle path is an AsyncUpdater that needs a running
// message pump; prepareToPlay resolves the same parameters synchronously).
#include "Processor.h"
#include "test_helpers.h"
#include "chain_test_helpers.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace {

// Drives the processor like a host: identical audio into both channels in
// fixed-size blocks (in.size() must be a multiple of blockSize). Returns
// channel 0 of the output.
std::vector<float> processThrough(TONE3000Processor& proc, const std::vector<float>& in,
                                  int blockSize) {
  const int total = static_cast<int>(in.size());
  std::vector<float> out(in.size(), 0.0f);
  juce::AudioBuffer<float> buffer(2, blockSize);
  juce::MidiBuffer midi;
  for (int off = 0; off < total; off += blockSize) {
    buffer.copyFrom(0, 0, in.data() + off, blockSize);
    buffer.copyFrom(1, 0, in.data() + off, blockSize);
    proc.processBlock(buffer, midi);
    std::copy(buffer.getReadPointer(0), buffer.getReadPointer(0) + blockSize,
              out.begin() + off);
  }
  return out;
}

// Round-trip gain at `freq` between the settled tails of `in` and `out`.
double settledGainDb(const std::vector<float>& out, const std::vector<float>& in, double freq,
                     double fs, int start = 16384, int window = 16384) {
  const double gOut = goertzelPower(out.data() + start, static_cast<size_t>(window), freq, fs);
  const double gIn = goertzelPower(in.data() + start, static_cast<size_t>(window), freq, fs);
  return db(gOut) - db(gIn);
}

TEST(ProcessorTest, EmptyChainAt48kIsTransparentWithZeroLatency) {
  // The core promise of the chain-domain design: at a 48 kHz host with
  // oversampling off, both resampling layers are dropped: no latency, and
  // the only thing between input and output is the (5 Hz) DC blocker.
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, kFs, 512);
  proc.prepareToPlay(kFs, 512);
  EXPECT_EQ(proc.getLatencySamples(), 0);

  const int total = 93 * 512;
  const auto in = makeSine(total, 997.0, 0.5f);
  const auto out = processThrough(proc, in, 512);

  EXPECT_NEAR(settledGainDb(out, in, 997.0, kFs), 0.0, 0.05);
  EXPECT_EQ(bestCorrelationLag(out, in, 16384, 4096, 32), 0) << "48k path must add no delay";
}

TEST(ProcessorTest, SpreadStaysIdleWithoutAStereoOutput) {
  // A rig that can't reproduce two distinct channels (mono host track, or a
  // standalone one-channel output device that still hands us a stereo
  // buffer but plays only channel 0) must never run the spread double: the
  // output stays the plain mono chain even when a preset arrives with
  // spreadEnabled on. The parameter keeps its value; the UI greys the group
  // out via the stereoOutput capability flag. Emulated with a mono main
  // output bus, which drives the same detection.
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 1, kFs, 512);
  proc.prepareToPlay(kFs, 512);
  EXPECT_FALSE(static_cast<bool>(proc.getChainState(-1)["stereoOutput"]));

  proc.parameters.getParameter("spreadEnabled")->setValueNotifyingHost(1.0f);
  proc.parameters.getParameter("spreadOffset")->setValueNotifyingHost(1.0f);  // +24 ms lag

  const int total = 93 * 512;
  const auto in = makeNoise(total, 4242, 0.25f);
  juce::AudioBuffer<float> buffer(2, 512);
  juce::MidiBuffer midi;
  for (int off = 0; off < total; off += 512) {
    buffer.copyFrom(0, 0, in.data() + off, 512);
    buffer.copyFrom(1, 0, in.data() + off, 512);
    proc.processBlock(buffer, midi);
    for (int i = 0; i < 512; ++i)
      ASSERT_EQ(buffer.getReadPointer(0)[i], buffer.getReadPointer(1)[i])
          << "spread ran on a mono rig at sample " << off + i;
  }

  // A stereo bus reports the capability back.
  TONE3000Processor stereoProc;
  stereoProc.setPlayConfigDetails(2, 2, kFs, 512);
  stereoProc.prepareToPlay(kFs, 512);
  EXPECT_TRUE(static_cast<bool>(stereoProc.getChainState(-1)["stereoOutput"]));
}

TEST(ProcessorTest, StereoChainsFoldToMonoWithoutAStereoOutput) {
  // Stereo chains on a rig that can't reproduce stereo (a mono host track
  // here) keep running and are summed at the output: ½(balL·L + balR·R),
  // the same fold a host applies when it sums a stereo bus to mono, so a
  // rig keeps its level when moved between track types. With two identical
  // (empty) lanes the sum is transparent; polarity and solo keep acting
  // inside it, which also pins that the Right lane really processes
  // (before this, a mono track silently played the Left lane alone).
  TONE3000Processor proc;
  proc.setPlayConfigDetails(1, 1, kFs, 512);
  proc.setStereoMode(true);
  proc.prepareToPlay(kFs, 512);
  EXPECT_FALSE(static_cast<bool>(proc.getChainState(-1)["stereoOutput"]));

  const int total = 93 * 512;
  const auto in = makeSine(total, 997.0, 0.5f);
  juce::MidiBuffer midi;
  const auto run = [&] {
    std::vector<float> out(in.size(), 0.0f);
    juce::AudioBuffer<float> buffer(1, 512);
    for (int off = 0; off < total; off += 512) {
      buffer.copyFrom(0, 0, in.data() + off, 512);
      proc.processBlock(buffer, midi);
      std::copy(buffer.getReadPointer(0), buffer.getReadPointer(0) + 512, out.begin() + off);
    }
    return out;
  };

  // Identical lanes at centered balance: ½(in + in) = in, transparent.
  EXPECT_NEAR(settledGainDb(run(), in, 997.0, kFs), 0.0, 0.05);

  // Flipping one lane's polarity cancels the sum: lane R is really in there.
  proc.parameters.getParameter("chainInvertRight")->setValueNotifyingHost(1.0f);
  {
    const auto out = run();
    float peak = 0.0f;
    for (int i = 16384; i < total; ++i)
      peak = std::max(peak, std::abs(out[i]));
    EXPECT_LT(peak, 1.0e-4f) << "inverted lane failed to cancel: the fold is broken";
  }

  // Solo auditions one lane, at the fold's ½ share.
  proc.parameters.getParameter("chainInvertRight")->setValueNotifyingHost(0.0f);
  proc.parameters.getParameter("chainSoloLeft")->setValueNotifyingHost(1.0f);
  EXPECT_NEAR(settledGainDb(run(), in, 997.0, kFs), -6.02, 0.1);

  // The same fold covers a stereo buffer on a mono rig (standalone
  // one-channel output device: the buffer is stereo but only channel 0 is
  // audible). Both channels carry the sum, so the listener hears the whole
  // rig; the polarity null proves the fold ran here too.
  TONE3000Processor monoOutProc;
  monoOutProc.setPlayConfigDetails(2, 1, kFs, 512);
  monoOutProc.setStereoMode(true);
  monoOutProc.prepareToPlay(kFs, 512);
  monoOutProc.parameters.getParameter("chainInvertRight")->setValueNotifyingHost(1.0f);
  juce::AudioBuffer<float> buffer(2, 512);
  float peak = 0.0f;
  for (int off = 0; off < total; off += 512) {
    buffer.copyFrom(0, 0, in.data() + off, 512);
    buffer.copyFrom(1, 0, in.data() + off, 512);
    monoOutProc.processBlock(buffer, midi);
    if (off >= 16384)
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
          peak = std::max(peak, std::abs(buffer.getReadPointer(ch)[i]));
  }
  EXPECT_LT(peak, 1.0e-4f) << "a 2-channel buffer on a mono rig must fold both channels";
}

TEST(ProcessorTest, BoundaryReportedLatencyMatchesMeasuredDelay) {
  // At non-48k host rates the Lanczos boundary engages (even with an empty
  // chain: that's the PDC-stability invariant) and reports its latency to
  // the host. The report is only worth anything if it matches the physical
  // group delay.
  for (double hostRate : {44100.0, 96000.0}) {
    TONE3000Processor proc;
    proc.setPlayConfigDetails(2, 2, hostRate, 512);
    proc.prepareToPlay(hostRate, 512);
    const int reported = proc.getLatencySamples();
    EXPECT_GT(reported, 0) << hostRate << " Hz host must engage the boundary";

    const int total = 120 * 512;
    const auto noise = makeNoise(total, 777, 0.25f);
    const auto out = processThrough(proc, noise, 512);
    const int measured = bestCorrelationLag(out, noise, 16384, 8192, reported + 64);
    EXPECT_NEAR(measured, reported, 2) << hostRate << " Hz: PDC report drifted from reality";

    // And the boundary itself must be sonically transparent.
    TONE3000Processor proc2;
    proc2.setPlayConfigDetails(2, 2, hostRate, 512);
    proc2.prepareToPlay(hostRate, 512);
    const auto sine = makeSine(total, 997.0, 0.5f, hostRate);
    const auto sineOut = processThrough(proc2, sine, 512);
    EXPECT_NEAR(settledGainDb(sineOut, sine, 997.0, hostRate), 0.0, 0.1)
        << hostRate << " Hz: boundary not transparent";
  }
}

TEST(ProcessorTest, OversamplingTogglesWithoutPdcChange) {
  // The oversampler is minimum-phase precisely so enabling it never changes
  // reported latency; hosts re-compensate on PDC changes (an audible
  // hiccup). Verified at the worst case: 44.1k host, ×8.
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, 44100.0, 512);
  proc.prepareToPlay(44100.0, 512);
  const int latencyBefore = proc.getLatencySamples();

  proc.parameters.getParameter("osEnabled")->setValueNotifyingHost(1.0f);
  proc.parameters.getParameter("osFactor")->setValueNotifyingHost(1.0f);  // index 2 = 8x
  proc.prepareToPlay(44100.0, 512);  // hosts re-prepare freely; picks up the factor

  EXPECT_EQ(proc.getLatencySamples(), latencyBefore) << "enabling 8x moved reported latency";

  // Still transparent through boundary + oversampler, and the physical delay
  // may only grow by the oversampler's few samples of min-phase group delay.
  const int total = 120 * 512;
  const auto sine = makeSine(total, 997.0, 0.5f, 44100.0);
  const auto out = processThrough(proc, sine, 512);
  EXPECT_NEAR(settledGainDb(out, sine, 997.0, 44100.0), 0.0, 0.15);

  const auto noise = makeNoise(total, 888, 0.25f);
  TONE3000Processor procN;
  procN.setPlayConfigDetails(2, 2, 44100.0, 512);
  procN.parameters.getParameter("osEnabled")->setValueNotifyingHost(1.0f);
  procN.parameters.getParameter("osFactor")->setValueNotifyingHost(1.0f);
  procN.prepareToPlay(44100.0, 512);
  const auto noiseOut = processThrough(procN, noise, 512);
  const int measured = bestCorrelationLag(noiseOut, noise, 16384, 8192, latencyBefore + 64);
  EXPECT_LE(measured, latencyBefore + 10) << "8x added more than min-phase group delay";
  EXPECT_GE(measured, latencyBefore - 2);
}

TEST(ProcessorTest, ParameterStateSurvivesSaveRestore) {
  juce::MemoryBlock state;
  {
    TONE3000Processor a;
    a.parameters.getParameter("inputLevel")->setValueNotifyingHost(0.7f);
    a.parameters.getParameter("gateEnabled")->setValueNotifyingHost(0.0f);
    a.parameters.getParameter("osEnabled")->setValueNotifyingHost(1.0f);
    a.parameters.getParameter("osFactor")->setValueNotifyingHost(1.0f);  // 8x
    a.getStateInformation(state);
  }

  TONE3000Processor b;
  b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  EXPECT_NEAR(b.parameters.getRawParameterValue("inputLevel")->load(), 0.7f, 1e-5f);
  EXPECT_EQ(b.parameters.getRawParameterValue("gateEnabled")->load(), 0.0f);
  EXPECT_EQ(b.parameters.getRawParameterValue("osEnabled")->load(), 1.0f);
  EXPECT_EQ(b.parameters.getRawParameterValue("osFactor")->load(), 2.0f);  // choice index

  // The restored oversampling setting must actually take effect on the next
  // prepare; factor 8 leaves reported latency at zero for a 48k host.
  b.setPlayConfigDetails(2, 2, kFs, 512);
  b.prepareToPlay(kFs, 512);
  EXPECT_EQ(b.getLatencySamples(), 0);
}

// Calibration and oversampling double as machine-wide defaults (github issue
// #66): a Settings-page edit writes the parameter's denormalised value under
// its id, and a fresh instance seeds from those keys. Driven over a scratch
// PropertySet; the real file is never touched (main() disables the seeding).
TEST(ProcessorTest, MachineDefaultParametersRoundTrip) {
  juce::PropertySet settings;
  {
    TONE3000Processor a;
    a.parameters.getParameter("calibrateInput")->setValueNotifyingHost(1.0f);
    auto* dbu = a.parameters.getParameter("inputCalibrationLevel");
    dbu->setValueNotifyingHost(dbu->convertTo0to1(4.0f));
    a.parameters.getParameter("osEnabled")->setValueNotifyingHost(1.0f);
    a.parameters.getParameter("osFactor")->setValueNotifyingHost(0.5f);  // index 1 = 4x
    for (const auto* id : {"calibrateInput", "inputCalibrationLevel", "osEnabled", "osFactor"})
      a.writeMachineDefaultParameter(settings, id);
  }
  // Stored in real units, like preset files.
  EXPECT_EQ(settings.getIntValue("calibrateInput"), 1);
  EXPECT_NEAR(settings.getDoubleValue("inputCalibrationLevel"), 4.0, 1e-4);
  EXPECT_EQ(settings.getIntValue("osEnabled"), 1);
  EXPECT_EQ(settings.getIntValue("osFactor"), 1);

  TONE3000Processor b;
  b.applyMachineDefaultParameters(settings);
  EXPECT_EQ(b.parameters.getRawParameterValue("calibrateInput")->load(), 1.0f);
  EXPECT_NEAR(b.parameters.getRawParameterValue("inputCalibrationLevel")->load(), 4.0f, 1e-4f);
  EXPECT_EQ(b.parameters.getRawParameterValue("osEnabled")->load(), 1.0f);
  EXPECT_EQ(b.parameters.getRawParameterValue("osFactor")->load(), 1.0f);

  // A host restore that follows wins over the machine default, so a project
  // reopens exactly as it was saved.
  juce::MemoryBlock state;
  {
    TONE3000Processor saved;
    saved.parameters.getParameter("osEnabled")->setValueNotifyingHost(0.0f);
    saved.parameters.getParameter("calibrateInput")->setValueNotifyingHost(0.0f);
    saved.getStateInformation(state);
  }
  b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  EXPECT_EQ(b.parameters.getRawParameterValue("osEnabled")->load(), 0.0f);
  EXPECT_EQ(b.parameters.getRawParameterValue("calibrateInput")->load(), 0.0f);

  // Keys missing from the file leave the parameter at its default; a tone
  // parameter is never a machine default.
  juce::PropertySet partial;
  partial.setValue("osFactor", 2);
  partial.setValue("inputLevel", 0.9);
  TONE3000Processor c;
  c.applyMachineDefaultParameters(partial);
  EXPECT_EQ(c.parameters.getRawParameterValue("osFactor")->load(), 2.0f);
  EXPECT_EQ(c.parameters.getRawParameterValue("osEnabled")->load(), 0.0f);
  EXPECT_NEAR(c.parameters.getRawParameterValue("inputLevel")->load(), 0.5f, 1e-6f);
  EXPECT_FALSE(TONE3000Processor::isMachineDefaultParameter("inputLevel"));
  EXPECT_TRUE(TONE3000Processor::isMachineDefaultParameter("inputCalibrationLevel"));
}

TEST(ProcessorTest, RestoredNamsBeforePrepareMatchNamsLoadedAtFourTimes) {
  juce::MemoryBlock saved;
  {
    ChainTestProcessor source;
    juce::ValueTree snapshot("ChainSnapshot"), chain("ChainBlocks");
    chain.appendChild(makeNamBlockTree("first", 1, 101), nullptr);
    chain.appendChild(makeNamBlockTree("second", 2, 102, "a2-am-test-2.nam"), nullptr);
    snapshot.appendChild(chain, nullptr);
    source.restoreFromTree(snapshot);
    ASSERT_TRUE(waitForChainLoaded(source));
    source.parameters.getParameter("osEnabled")->setValueNotifyingHost(1.0f);
    source.parameters.getParameter("osFactor")->setValueNotifyingHost(0.5f);  // 4x
    source.getStateInformation(saved);
  }

  const auto input = makeNoise(96 * 512, 42, 0.05f);
  auto renderRestored = [&]() {
    TONE3000Processor proc;
    proc.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    EXPECT_TRUE(waitForChainLoaded(proc));  // engines installed at the initial 1x
    proc.setPlayConfigDetails(2, 2, kFs, 512);
    proc.prepareToPlay(kFs, 512);
    EXPECT_TRUE(waitForChainLoaded(proc));
    return processThrough(proc, input, 512);
  };
  const auto restored = renderRestored();

  TONE3000Processor reference;
  reference.parameters.getParameter("osEnabled")->setValueNotifyingHost(1.0f);
  reference.parameters.getParameter("osFactor")->setValueNotifyingHost(0.5f);
  reference.setPlayConfigDetails(2, 2, kFs, 512);
  reference.prepareToPlay(kFs, 512);
  reference.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  ASSERT_TRUE(waitForChainLoaded(reference));
  const auto expected = processThrough(reference, input, 512);
  float maxDiff = 0.0f, peak = 0.0f;
  for (size_t i = 16384; i < expected.size(); ++i) {
    maxDiff = std::max(maxDiff, std::abs(restored[i] - expected[i]));
    peak = std::max(peak, std::abs(expected[i]));
  }
  ASSERT_GT(peak, 1e-5f);
  EXPECT_LT(maxDiff, 1e-5f);
}

TEST(ProcessorTest, IgnoresGarbageLegacyAndNewerSchemaState) {
  // setStateInformation must leave the current state untouched for anything
  // it can't own: random bytes, the retired XML format (no T3KB magic), and
  // a well-formed state from a newer schema than this build understands.
  TONE3000Processor proc;
  proc.parameters.getParameter("inputLevel")->setValueNotifyingHost(0.7f);
  auto inputLevel = [&] { return proc.parameters.getRawParameterValue("inputLevel")->load(); };

  const char garbage[] = "definitely not a plugin state";
  proc.setStateInformation(garbage, static_cast<int>(sizeof(garbage)));
  EXPECT_NEAR(inputLevel(), 0.7f, 1e-5f);

  const juce::String legacyXml =
      "<?xml version=\"1.0\"?><TONE3000State><PARAMETERS/></TONE3000State>";
  proc.setStateInformation(legacyXml.toRawUTF8(),
                           static_cast<int>(legacyXml.getNumBytesAsUTF8()));
  EXPECT_NEAR(inputLevel(), 0.7f, 1e-5f);

  // A valid save from this build, re-framed with a bumped schemaVersion. It
  // carries inputLevel = 0.2; restoring it would be visible immediately.
  juce::MemoryBlock saved;
  {
    TONE3000Processor future;
    future.parameters.getParameter("inputLevel")->setValueNotifyingHost(0.2f);
    future.getStateInformation(saved);
  }
  juce::ValueTree tree = juce::ValueTree::readFromData(
      static_cast<const char*>(saved.getData()) + 4, saved.getSize() - 4);
  ASSERT_TRUE(tree.isValid());
  tree.setProperty("schemaVersion", static_cast<int>(tree.getProperty("schemaVersion")) + 1,
                   nullptr);
  juce::MemoryBlock reframed;
  {
    juce::MemoryOutputStream out(reframed, false);
    out.write("T3KB", 4);
    tree.writeToStream(out);
  }
  proc.setStateInformation(reframed.getData(), static_cast<int>(reframed.getSize()));
  EXPECT_NEAR(inputLevel(), 0.7f, 1e-5f) << "state from a newer schema must be ignored";
}

TEST(ProcessorTest, TailReportCoversDcBlockerWithEmptyChain) {
  // No IRs loaded → the report must still cover the 5 Hz DC blocker decay
  // (2 s), so hosts don't truncate render tails.
  TONE3000Processor proc;
  EXPECT_DOUBLE_EQ(proc.getTailLengthSeconds(), 2.0);
}

TEST(ProcessorTest, LoggerSurvivesOverlappingAndSequentialProcessorDestruction) {
  juce::Logger* logger = nullptr;
  {
    TONE3000Processor first;
    logger = juce::Logger::getCurrentLogger();
    ASSERT_NE(logger, nullptr);
    {
      TONE3000Processor second;
      EXPECT_EQ(juce::Logger::getCurrentLogger(), logger);
    }
    EXPECT_EQ(juce::Logger::getCurrentLogger(), logger);
  }
  EXPECT_EQ(juce::Logger::getCurrentLogger(), logger);
  {
    TONE3000Processor third;
    EXPECT_EQ(juce::Logger::getCurrentLogger(), logger);
  }
  EXPECT_EQ(juce::Logger::getCurrentLogger(), logger);
}

}  // namespace
