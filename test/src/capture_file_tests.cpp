#include <gtest/gtest.h>
#include "StandaloneCapture.h"
#include "CaptureDsp.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>

namespace {
struct CaptureFilesTest : ::testing::Test {
  juce::File folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("t3k-capture-test", "", false);
  void SetUp() override { ASSERT_TRUE(folder.createDirectory()); }
  void TearDown() override { folder.deleteRecursively(); }
  t3k::capture::Recording pair() {
    t3k::capture::Recording r;
    r.config.name = "Test capture";
    r.dry.resize(48000);
    r.wet.resize(48000);
    for (int i = 0; i < 48000; ++i) {
      r.dry[static_cast<size_t>(i)] = 0.2f * std::sin(i * 0.031f);
      r.wet[static_cast<size_t>(i)] = r.dry[static_cast<size_t>(i)] * 2;
    }
    return r;
  }
};
TEST_F(CaptureFilesTest, DryWetExportsEqualMono24Bit48kWithoutNormalization) {
  auto r = pair();
  juce::File out;
  ASSERT_TRUE(t3k::capture::StandaloneCapture::exportRecording(r, folder, 0, {}, out));
  juce::WavAudioFormat wav;
  std::unique_ptr<juce::AudioFormatReader> dry(
      wav.createReaderFor(out.getChildFile("DRY.wav").createInputStream().release(), true));
  std::unique_ptr<juce::AudioFormatReader> wet(
      wav.createReaderFor(out.getChildFile("WET.wav").createInputStream().release(), true));
  ASSERT_TRUE(dry);
  ASSERT_TRUE(wet);
  EXPECT_EQ(dry->numChannels, 1u);
  EXPECT_EQ(wet->numChannels, 1u);
  EXPECT_EQ(dry->sampleRate, 48000);
  EXPECT_EQ(wet->bitsPerSample, 24u);
  EXPECT_EQ(dry->lengthInSamples, wet->lengthInSamples);
  EXPECT_EQ(dry->lengthInSamples, 48000);
  juce::AudioBuffer<float> buffer(1, 48000);
  wet->read(&buffer, 0, 48000, 0, true, false);
  EXPECT_NEAR(buffer.getSample(0, 122), r.wet[122], 0.0000002f);
  EXPECT_TRUE(out.getChildFile("session.json").existsAsFile());
  juce::File again;
  ASSERT_TRUE(t3k::capture::StandaloneCapture::exportRecording(r, folder, 0, {}, again));
  EXPECT_NE(out, again);
}
TEST_F(CaptureFilesTest, RejectsSilentOrMissingPairWithoutCreatingSession) {
  auto r = pair();
  r.wet.assign(48000, 0);
  juce::File out;
  EXPECT_TRUE(t3k::capture::StandaloneCapture::exportRecording(r, folder, 0, {}, out).failed());
  EXPECT_EQ(folder.getNumberOfChildFiles(juce::File::findDirectories), 0);
}
TEST_F(CaptureFilesTest, RejectsInvalidAudioAndRemovesPartialSession) {
  auto r = pair();
  r.wet[3] = 1.5f;
  juce::File out;
  EXPECT_TRUE(t3k::capture::StandaloneCapture::exportRecording(r, folder, 0, {}, out).failed());
  EXPECT_EQ(folder.getNumberOfChildFiles(juce::File::findDirectories), 0);
}
TEST_F(CaptureFilesTest, FrozenReferenceSurvivesDeletionAndNamExportHasExactFrameCount) {
  auto r = pair();
  r.config.mode = t3k::capture::Mode::namSignal;
  auto source = folder.getChildFile("test-signal.wav");
  ASSERT_TRUE(t3k::capture::StandaloneCapture::writeWav(source, r.dry));
  ASSERT_TRUE(t3k::capture::StandaloneCapture::readSignal(source, r.reference, r.sourceWav));
  const auto snapshot = r.sourceWav;
  source.deleteFile();
  r.wet.insert(r.wet.begin(), 700, 0);
  r.wet.insert(r.wet.end(), 1200, 0);
  juce::File out;
  ASSERT_TRUE(t3k::capture::StandaloneCapture::exportRecording(r, folder, 700, {}, out));
  juce::MemoryBlock saved;
  ASSERT_TRUE(out.getChildFile("REFERENCE.wav").loadFileAsData(saved));
  EXPECT_EQ(saved, snapshot);
  juce::WavAudioFormat wav;
  std::unique_ptr<juce::AudioFormatReader> wet(
      wav.createReaderFor(out.getChildFile("WET.wav").createInputStream().release(), true));
  ASSERT_TRUE(wet);
  EXPECT_EQ(wet->lengthInSamples, 48000);
  juce::File invalid;
  EXPECT_TRUE(
      t3k::capture::StandaloneCapture::exportRecording(r, folder, 1901, {}, invalid).failed());
}
}  // namespace
