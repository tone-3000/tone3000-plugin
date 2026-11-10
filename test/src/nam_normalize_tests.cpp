// NAM loudness normalization: the block gain is target loudness minus the
// model's metadata loudness, limited to ±24 dB.
#include "chain_test_helpers.h"

#include <cmath>

namespace {

constexpr int kBlock = 512;
constexpr int kSecond = 48000 / kBlock * kBlock;
constexpr const char* kModel = "a2-amp-test.nam";

double rmsDb(const std::vector<float>& v) {
  double sum = 0.0;
  for (const float s : v) sum += static_cast<double>(s) * s;
  return 10.0 * std::log10(std::max(sum / static_cast<double>(v.size()), 1e-30));
}

class NamNormalizeTest : public ::testing::Test {
protected:
  void SetUp() override {
    juce::ValueTree snapshot("ChainSnapshot"), chain("ChainBlocks");
    chain.appendChild(makeNamBlockTree("amp", 1, 100, kModel), nullptr);
    snapshot.appendChild(chain, nullptr);
    proc.restoreFromTree(snapshot);
    proc.setPlayConfigDetails(2, 2, kFs, kBlock);
    proc.prepareToPlay(kFs, kBlock);
    ASSERT_TRUE(waitForChainLoaded(proc));

    const auto metadata = juce::JSON::parse(testFile(kModel))["metadata"];
    ASSERT_TRUE(metadata.hasProperty("loudness"));
    modelLoudness = static_cast<double>(metadata["loudness"]);
  }

  // Output level after the gain smoother has settled.
  double settledDb() {
    processStereo(proc, makeNoise(kSecond, 1111, 0.05f));
    return rmsDb(processStereo(proc, makeNoise(kSecond, 4242, 0.05f)).first);
  }

  // The normalization gain at a target: normalized level minus raw level.
  double gainDbAt(double targetDb) {
    auto* target = proc.parameters.getParameter("targetLoudness");
    target->setValueNotifyingHost(target->convertTo0to1(static_cast<float>(targetDb)));
    EXPECT_TRUE(proc.setBlockParam("amp", "normalize", 1.0));
    const double normalized = settledDb();
    EXPECT_TRUE(proc.setBlockParam("amp", "normalize", 0.0));
    return normalized - settledDb();
  }

  ChainTestProcessor proc;
  double modelLoudness = 0.0;
};

TEST_F(NamNormalizeTest, GainFollowsTargetWithinTheLimit) {
  EXPECT_NEAR(gainDbAt(modelLoudness - 20.0), -20.0, 0.25);
  EXPECT_NEAR(gainDbAt(0.0), -modelLoudness, 0.25);  // +14 dB for this model
}

TEST_F(NamNormalizeTest, GainIsLimitedTo24Db) {
  EXPECT_NEAR(gainDbAt(-60.0), -24.0, 0.25);
}

}  // namespace
