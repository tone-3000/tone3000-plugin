// Pins computeIrContentLengthSamples's effect (ProcessorModelLoader.cpp): the
// -60dB-relative-to-peak, backward-scanned pooled-RMS detector that is the
// single source of truth for both the waveform display's auto-fit trim
// (computeIrWaveformPeaks) and the length label shipped to the UI as
// irContentLengthMs. It's a private static helper (like its siblings
// computeIrNormalizationGain/computeIrWaveformPeaks, neither unit-tested in
// isolation either), so this drives it through the real load pipeline
// instead of calling it directly.
#include "chain_test_helpers.h"

namespace {

double contentLengthMsFor(ChainTestProcessor& proc, const juce::String& blockId) {
  const juce::var state = proc.getChainState(-1);
  for (const auto& item : *state["chain"].getArray())
    if (item["blockId"].toString() == blockId)
      return static_cast<double>(item["irContentLengthMs"]);
  ADD_FAILURE() << "block " << blockId << " not found in chain state";
  return -1.0;
}

}  // namespace

// cab-ir-test.wav (~500ms raw file): whatever the detector reports must be
// positive and can never exceed the file's own raw duration - the detector
// only ever trims, never extends.
TEST(IrContentLengthTest, CabIrLengthIsPositiveAndWithinRawDuration) {
  ChainTestProcessor proc;
  proc.setPlayConfigDetails(2, 2, kFs, 512);
  proc.prepareToPlay(kFs, 512);

  seedStereoChains(proc, {"blk-a"}, {});
  ASSERT_TRUE(waitForChainLoaded(proc));

  const double lengthMs = contentLengthMsFor(proc, "blk-a");
  EXPECT_GT(lengthMs, 0.0);
  // afinfo reports cab-ir-test.wav at 500.021ms; a couple ms of tolerance
  // for rounding across the file-rate <-> ms conversion.
  EXPECT_LT(lengthMs, 505.0) << "content length exceeds the cab IR's own raw duration";
}

// reverb-ir-mono-test.wav (~2.665s raw file): same bound, on a much longer,
// non-cab-classified file - the detector isn't cab-specific.
TEST(IrContentLengthTest, ReverbIrLengthIsPositiveAndWithinRawDuration) {
  ChainTestProcessor proc;
  proc.setPlayConfigDetails(2, 2, kFs, 512);
  proc.prepareToPlay(kFs, 512);

  juce::ValueTree state("ChainSnapshot");
  state.setProperty("stereoEnabled", false, nullptr);
  juce::ValueTree lane("ChainBlocks");
  lane.appendChild(makeIrBlockTree("blk-a", 1, 100, "reverb-ir-mono-test.wav"), nullptr);
  state.appendChild(lane, nullptr);
  state.appendChild(juce::ValueTree("RightChainBlocks"), nullptr);
  proc.restoreFromTree(state);
  ASSERT_TRUE(waitForChainLoaded(proc));

  const double lengthMs = contentLengthMsFor(proc, "blk-a");
  EXPECT_GT(lengthMs, 0.0);
  EXPECT_LT(lengthMs, 2670.0) << "content length exceeds the reverb IR's own raw duration";
}
