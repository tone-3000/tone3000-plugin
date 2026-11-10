// Tuner mute: the tuner screen's speaker toggle silences the plugin output
// (faded) while the tuner is open, and closing the tuner always restores it.
#include "chain_test_helpers.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int kBlock = 512;
// The mute glides over 20 ms (~960 samples); 4 blocks is 2048, well past it.
constexpr int kSettleBlocks = 4;
constexpr int kRunBlocks = 8;

float peakOf(const std::vector<float>& v) {
  float m = 0.0f;
  for (const float s : v) m = std::max(m, std::abs(s));
  return m;
}

// Output peak over a measured window, after a discarded settle window so the
// fade has finished either way.
float settledPeak(TONE3000Processor& proc) {
  processStereo(proc, makeNoise(kSettleBlocks * kBlock, 1111, 0.25f));
  return peakOf(processStereo(proc, makeNoise(kRunBlocks * kBlock, 4242, 0.25f)).first);
}

class TunerMuteTest : public ::testing::Test {
protected:
  void SetUp() override {
    proc.setPlayConfigDetails(2, 2, kFs, kBlock);
    proc.prepareToPlay(kFs, kBlock);
  }
  ChainTestProcessor proc;
};

TEST_F(TunerMuteTest, MutedWhileTunerOpenSilencesOutput) {
  const float open = settledPeak(proc);
  ASSERT_GT(open, 0.01f) << "baseline output should be audible";

  proc.setTunerEnabled(true);
  proc.setTunerMuted(true);
  EXPECT_LT(settledPeak(proc), 1e-4f);

  proc.setTunerMuted(false);
  EXPECT_NEAR(settledPeak(proc), open, 1e-3f);
}

TEST_F(TunerMuteTest, MuteHasNoEffectWhileTunerClosed) {
  const float open = settledPeak(proc);
  proc.setTunerMuted(true);
  EXPECT_NEAR(settledPeak(proc), open, 1e-3f);
}

TEST_F(TunerMuteTest, ClosingTunerClearsMute) {
  const float open = settledPeak(proc);

  proc.setTunerEnabled(true);
  proc.setTunerMuted(true);
  ASSERT_LT(settledPeak(proc), 1e-4f);

  proc.setTunerEnabled(false);
  EXPECT_NEAR(settledPeak(proc), open, 1e-3f);

  // Reopening starts unmuted: the old toggle state must not come back.
  proc.setTunerEnabled(true);
  EXPECT_NEAR(settledPeak(proc), open, 1e-3f);
}

// A host re-prepare (sample rate or buffer size change, device switch) while
// muted must not prime the output back to full level and glide down from it.
TEST_F(TunerMuteTest, RePrepareWhileMutedStaysSilent) {
  proc.setTunerEnabled(true);
  proc.setTunerMuted(true);
  ASSERT_LT(settledPeak(proc), 1e-4f);

  proc.releaseResources();
  proc.prepareToPlay(kFs, kBlock);
  EXPECT_LT(peakOf(processStereo(proc, makeNoise(kBlock, 7, 0.25f)).first), 1e-4f);
}

}  // namespace
