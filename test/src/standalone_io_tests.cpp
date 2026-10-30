#include "StandaloneIoLease.h"

#include <gtest/gtest.h>
#include <memory>

namespace {
using namespace t3k::standalone_io;

class StandaloneIoTest : public ::testing::Test {
protected:
  std::shared_ptr<RoutingOwner> token;
  std::function<juce::Result()> restore;
  void SetUp() override {
    setRoutingActive(false);
    releaseCapture(restore);
  }
  void TearDown() override {
    releaseCapture(restore);
    setRoutingActive(false);
    if (token) unregisterRoutingOwner(token);
    token.reset();
  }
};

TEST_F(StandaloneIoTest, CaptureExcludesOtherCapturesAndLocksSettingsUntilRelease) {
  ASSERT_FALSE(settingsLocked());
  ASSERT_TRUE(acquireCapture(restore).wasOk());
  EXPECT_TRUE(captureOwnsDevice()); EXPECT_TRUE(settingsLocked());
  std::function<juce::Result()> second;
  EXPECT_TRUE(acquireCapture(second).failed());
  EXPECT_TRUE(captureOwnsDevice());
  EXPECT_TRUE(releaseCapture(restore).wasOk());
  EXPECT_FALSE(captureOwnsDevice()); EXPECT_FALSE(settingsLocked());
  EXPECT_FALSE(static_cast<bool>(restore));
}

TEST_F(StandaloneIoTest, RouterSuspendsBeforeCaptureAndResumesOnlyAfterCaptureRelease) {
  int suspends = 0, resumes = 0;
  token = std::make_shared<RoutingOwner>();
  token->suspend = [&] { ++suspends; setRoutingActive(false); };
  token->resume = [&] {
    EXPECT_FALSE(captureOwnsDevice()); ++resumes; setRoutingActive(true); return juce::Result::ok();
  };
  registerRoutingOwner(token); setRoutingActive(true);
  ASSERT_TRUE(acquireCapture(restore).wasOk());
  EXPECT_EQ(suspends, 1); EXPECT_EQ(resumes, 0);
  EXPECT_TRUE(captureOwnsDevice()); EXPECT_FALSE(routingOwnsDevice());
  EXPECT_TRUE(releaseCapture(restore).wasOk());
  EXPECT_EQ(resumes, 1); EXPECT_TRUE(routingOwnsDevice()); EXPECT_TRUE(settingsLocked());
  EXPECT_TRUE(releaseCapture(restore).wasOk()); EXPECT_EQ(resumes, 1);
}

TEST_F(StandaloneIoTest, RouterWhichCannotYieldLeavesCaptureUnacquired) {
  token = std::make_shared<RoutingOwner>();
  token->suspend = [] {}; // device ownership was not actually relinquished
  token->resume = [] { return juce::Result::ok(); };
  registerRoutingOwner(token); setRoutingActive(true);
  EXPECT_TRUE(acquireCapture(restore).failed());
  EXPECT_FALSE(captureOwnsDevice()); EXPECT_TRUE(routingOwnsDevice());
  EXPECT_FALSE(static_cast<bool>(restore));
}

TEST_F(StandaloneIoTest, MissingOrIncompleteOwnerFailsWithoutClaimingTheDevice) {
  token = std::make_shared<RoutingOwner>();
  registerRoutingOwner(token); setRoutingActive(true);
  EXPECT_TRUE(acquireCapture(restore).failed());
  EXPECT_FALSE(captureOwnsDevice());
  unregisterRoutingOwner(token); token.reset();
  EXPECT_TRUE(acquireCapture(restore).failed());
  EXPECT_FALSE(captureOwnsDevice());
}

TEST_F(StandaloneIoTest, ResumeFailureIsReturnedAndCaptureOwnershipStillEnds) {
  token = std::make_shared<RoutingOwner>();
  token->suspend = [] { setRoutingActive(false); };
  token->resume = [] { return juce::Result::fail("Interface disconnected"); };
  registerRoutingOwner(token); setRoutingActive(true);
  ASSERT_TRUE(acquireCapture(restore).wasOk());
  const auto result = releaseCapture(restore);
  EXPECT_TRUE(result.failed()); EXPECT_EQ(result.getErrorMessage(), "Interface disconnected");
  EXPECT_FALSE(captureOwnsDevice()); EXPECT_FALSE(routingOwnsDevice()); EXPECT_FALSE(settingsLocked());
}

TEST_F(StandaloneIoTest, DestroyedRouterTokenCannotResumeOrAccessItsFormerOwner) {
  int resumed = 0;
  token = std::make_shared<RoutingOwner>();
  token->suspend = [] { setRoutingActive(false); };
  token->resume = [&] { ++resumed; setRoutingActive(true); return juce::Result::ok(); };
  registerRoutingOwner(token); setRoutingActive(true);
  ASSERT_TRUE(acquireCapture(restore).wasOk());
  token.reset();
  EXPECT_TRUE(releaseCapture(restore).wasOk());
  EXPECT_EQ(resumed, 0); EXPECT_FALSE(captureOwnsDevice()); EXPECT_FALSE(routingOwnsDevice());
}

TEST_F(StandaloneIoTest, UnregisteredOwnerIsNotResumedEvenIfAnotherReferenceKeepsItAlive) {
  int resumed = 0;
  token = std::make_shared<RoutingOwner>();
  token->suspend = [] { setRoutingActive(false); };
  token->resume = [&] { ++resumed; setRoutingActive(true); return juce::Result::ok(); };
  registerRoutingOwner(token); setRoutingActive(true);
  ASSERT_TRUE(acquireCapture(restore).wasOk());
  unregisterRoutingOwner(token);
  EXPECT_TRUE(releaseCapture(restore).wasOk());
  EXPECT_EQ(resumed, 0); EXPECT_FALSE(routingOwnsDevice());
}
} // namespace
