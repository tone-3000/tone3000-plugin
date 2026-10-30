#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <vector>

namespace t3k::capture {

enum class Mode { dryWet, namSignal, impulseResponse };
struct Config {
  Mode mode = Mode::dryWet;
  int dryInput = 0, wetInput = 1, sendOutput = 0;
  double seconds = 180.0, sweepSeconds = 10.0;
  int irSamples = 24000;
  float outputDb = -18.0f;
  juce::File signal;
  juce::String gear = "Kemper Head / PowerHead", name = "Kemper capture", rig, notes;
};
struct Recording {
  Config config;
  std::vector<float> dry, wet, reference;
  juce::MemoryBlock sourceWav;
  juce::String inputDevice, outputDevice, createdAt;
  bool clipped = false;
  double deviceSampleRate = 48000.0;
  bool convertedTo48k = false;
};

/** Raw standalone I/O only. Temporarily suspends the normal player callback,
    so no NAM, EQ, gate, monitor signal or normalization enters a capture.
    Control methods run on the message thread; callback writes preallocated audio.
    No host/processor parameters or preset state are changed. */
class StandaloneCapture : private juce::AudioIODeviceCallback {
public:
  StandaloneCapture();
  ~StandaloneCapture() override;
  static bool isAvailable();
  juce::StringArray inputNames() const;
  juce::StringArray outputNames() const;
  juce::String deviceDescription() const;
  juce::Result start(const Config&, bool levelsOnly = false);
  void stop();
  bool running() const { return attached_; }
  bool finished() const { return !levelsOnly_ && recorded_.load() >= target_; }
  float progress() const;
  double elapsed() const;
  float dryPeak() const { return dryPeak_.load(); }
  float wetPeak() const { return wetPeak_.load(); }
  bool clipped() const { return clipped_.load(); }
  juce::String error() const;
  Recording takeRecording();

  static juce::Result prepareRecording(Recording&);
  static juce::Result readSignal(const juce::File&,
                                 std::vector<float>&,
                                 juce::MemoryBlock& original);
  static juce::Result writeWav(const juce::File&, const std::vector<float>&);
  static juce::Result exportRecording(const Recording&,
                                      const juce::File& parent,
                                      int offset,
                                      const std::vector<float>& ir,
                                      juce::File& folder);

private:
  void audioDeviceAboutToStart(juce::AudioIODevice*) override;
  void audioDeviceStopped() override;
  void audioDeviceError(const juce::String&) override;
  void audioDeviceIOCallbackWithContext(const float* const*,
                                        int,
                                        float* const*,
                                        int,
                                        int,
                                        const juce::AudioIODeviceCallbackContext&) override;
  juce::AudioDeviceManager* manager_ = nullptr;
  juce::AudioDeviceManager::AudioDeviceSetup savedSetup_;
  Recording recording_;
  std::atomic<bool> attached_{false};
  bool suspendedPlayer_ = false, levelsOnly_ = false;
  int dryPacked_ = 0, wetPacked_ = 1, sendPacked_ = 0;
  int target_ = 0, preroll_ = 24000;
  double deviceRate_ = 48000.0;
  std::vector<float> playback_;
  std::atomic<int> recorded_{0};
  std::atomic<float> dryPeak_{0}, wetPeak_{0};
  std::atomic<bool> clipped_{false}, stoppedUnexpectedly_{false};
  float sendGain_ = 0.12589254f;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandaloneCapture)
};

}  // namespace t3k::capture
