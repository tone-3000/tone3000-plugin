#include "StandaloneCapture.h"
#include "CaptureDsp.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/juce_audio_plugin_client.h>
#if JucePlugin_Build_Standalone && !JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif
#include <cmath>
#include <limits>

namespace t3k::capture {
namespace {
juce::StandalonePluginHolder* holder() {
#if JucePlugin_Build_Standalone && !JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP
  return juce::StandalonePluginHolder::getInstance();
#else
  return nullptr;
#endif
}
int packedIndex(const juce::BigInteger& mask, int physical) {
  int result = 0;
  for (int i = 0; i < physical; ++i)
    if (mask[i])
      ++result;
  return result;
}
juce::Result fail(const juce::String& message) {
  return juce::Result::fail(message);
}
}  // namespace
StandaloneCapture::StandaloneCapture() {
  if (auto* h = holder()) {
    manager_ = &h->deviceManager;
  }
}
StandaloneCapture::~StandaloneCapture() {
  stop();
}
bool StandaloneCapture::isAvailable() {
  return holder() != nullptr;
}
juce::StringArray StandaloneCapture::inputNames() const {
  if (manager_ != nullptr)
    if (auto* d = manager_->getCurrentAudioDevice())
      return d->getInputChannelNames();
  return {};
}
juce::StringArray StandaloneCapture::outputNames() const {
  if (manager_ != nullptr)
    if (auto* d = manager_->getCurrentAudioDevice())
      return d->getOutputChannelNames();
  return {};
}
juce::String StandaloneCapture::deviceDescription() const {
  if (manager_ == nullptr)
    return "Available in the standalone app only";
  const auto s = manager_->getAudioDeviceSetup();
  return "Input: " + s.inputDeviceName + "  |  Output: " + s.outputDeviceName;
}
juce::Result StandaloneCapture::start(const Config& config, bool levelsOnly) {
  if (attached_)
    return fail("A capture is already running.");
  if (manager_ == nullptr || holder() == nullptr || manager_->getCurrentAudioDevice() == nullptr)
    return fail("Open System Settings and select your Kemper or audio interface first.");
  if (config.dryInput < 0 || config.wetInput < 0 || config.sendOutput < 0)
    return fail("Select valid audio channels.");
  if (config.wetInput >= inputNames().size() ||
      (config.mode == Mode::dryWet && config.dryInput >= inputNames().size()))
    return fail("The selected input channel is unavailable.");
  if (config.mode == Mode::dryWet && config.dryInput == config.wetInput)
    return fail("Dry DI and wet return must use different input channels.");
  if (!levelsOnly && config.mode != Mode::dryWet && config.sendOutput >= outputNames().size())
    return fail("The selected send output is unavailable.");
  if (!std::isfinite(config.seconds) || config.seconds < 1 || config.seconds > 600 ||
      !std::isfinite(config.outputDb) || config.outputDb < -60 || config.outputDb > 0)
    return fail("Duration must be 1–600 seconds and output trim between −60 and 0 dB.");
  levelsOnly_ = levelsOnly;
  recording_ = {};
  recording_.config = config;
  if (!levelsOnly && config.mode == Mode::namSignal) {
    const auto result = readSignal(config.signal, recording_.reference, recording_.sourceWav);
    if (result.failed())
      return result;
  } else if (!levelsOnly && config.mode == Mode::impulseResponse) {
    if (config.irSamples < 48 || config.irSamples > sampleRate * 2)
      return fail("Choose an IR length between 1 and 2000 ms.");
    recording_.reference = makeSweep(config.sweepSeconds);
    if (recording_.reference.empty())
      return fail("Choose a valid IR sweep duration.");
  }
  auto* current = manager_->getCurrentAudioDevice();
  const auto rates = current->getAvailableSampleRates();
  deviceRate_ = rates.contains(static_cast<double>(sampleRate)) ? static_cast<double>(sampleRate)
                                                                : current->getCurrentSampleRate();
  if (!std::isfinite(deviceRate_) || deviceRate_ < 32000 || deviceRate_ > 192000)
    return fail("The audio device must provide a sample rate between 32 and 192 kHz.");
  recording_.deviceSampleRate = deviceRate_;
  playback_ = std::abs(deviceRate_ - sampleRate) < 0.01
                  ? recording_.reference
                  : resample(recording_.reference, sampleRate, deviceRate_);
  if (!recording_.reference.empty() && playback_.empty())
    return fail("The test signal could not be converted to the interface sample rate.");
  preroll_ = static_cast<int>(deviceRate_ * 0.5);
  target_ = config.mode == Mode::dryWet
                ? static_cast<int>(config.seconds * deviceRate_)
                : static_cast<int>(playback_.size()) + preroll_ + static_cast<int>(deviceRate_ * 4);
  if (levelsOnly)
    target_ = std::numeric_limits<int>::max();
  recording_.dry.assign(
      !levelsOnly && config.mode == Mode::dryWet ? static_cast<size_t>(target_) : 0, 0);
  recording_.wet.assign(levelsOnly ? 0 : static_cast<size_t>(target_), 0);
  recorded_.store(0);
  dryPeak_.store(0);
  wetPeak_.store(0);
  clipped_.store(false);
  stoppedUnexpectedly_.store(false);
  sendGain_ = std::pow(10.0f, config.outputDb / 20.0f);
  savedSetup_ = manager_->getAudioDeviceSetup();
  recording_.inputDevice = savedSetup_.inputDeviceName;
  recording_.outputDevice = savedSetup_.outputDeviceName;
  recording_.createdAt = juce::Time::getCurrentTime().toISO8601(true);
  // Suspending the normal standalone player guarantees no processed signal
  // is summed into the hardware send. Its processor state stays intact.
  holder()->player.setProcessor(nullptr);
  suspendedPlayer_ = true;
  auto setup = savedSetup_;
  setup.sampleRate = deviceRate_;
  setup.useDefaultInputChannels = false;
  setup.inputChannels.clear();
  setup.inputChannels.setBit(config.wetInput);
  if (config.mode == Mode::dryWet)
    setup.inputChannels.setBit(config.dryInput);
  setup.useDefaultOutputChannels = false;
  setup.outputChannels.clear();
  if (!levelsOnly && config.mode != Mode::dryWet)
    setup.outputChannels.setBit(config.sendOutput);
  const auto setupError = manager_->setAudioDeviceSetup(setup, false);
  if (setupError.isNotEmpty()) {
    stop();
    return fail("Capture routing could not be opened: " + setupError);
  }
  auto* device = manager_->getCurrentAudioDevice();
  if (device == nullptr || std::abs(device->getCurrentSampleRate() - deviceRate_) > 0.01) {
    stop();
    return fail(
        "The device did not open at the requested native sample rate. Check its clock and driver "
        "settings.");
  }
  const auto activeIn = device->getActiveInputChannels();
  const auto activeOut = device->getActiveOutputChannels();
  if (!activeIn[config.wetInput] || (config.mode == Mode::dryWet && !activeIn[config.dryInput]) ||
      (!levelsOnly && config.mode != Mode::dryWet && !activeOut[config.sendOutput])) {
    stop();
    return fail("The audio driver did not enable the selected channels.");
  }
  dryPacked_ = packedIndex(activeIn, config.dryInput);
  wetPacked_ = packedIndex(activeIn, config.wetInput);
  sendPacked_ = packedIndex(activeOut, config.sendOutput);
  attached_ = true;
  manager_->addAudioCallback(this);
  return juce::Result::ok();
}
void StandaloneCapture::stop() {
  if (manager_ == nullptr)
    return;
  if (attached_) {
    attached_ = false;
    manager_->removeAudioCallback(this);
  }
  if (suspendedPlayer_) {
    // Keep the routing restoration error visible; never claim the old setup
    // was restored if its device disconnected during the capture.
    const auto error = manager_->setAudioDeviceSetup(savedSetup_, false);
    if (error.isNotEmpty())
      stoppedUnexpectedly_.store(true);
    if (auto* h = holder())
      h->player.setProcessor(h->processor.get());
    suspendedPlayer_ = false;
  }
}
float StandaloneCapture::progress() const {
  return target_ > 0 ? juce::jlimit(0.0f, 1.0f, static_cast<float>(recorded_.load()) / target_) : 0;
}
double StandaloneCapture::elapsed() const {
  return static_cast<double>(recorded_.load()) / deviceRate_;
}
juce::String StandaloneCapture::error() const {
  return stoppedUnexpectedly_.load()
             ? "The audio device stopped, changed, or could not restore its previous routing. "
               "Check System Settings before recording again."
             : juce::String();
}
Recording StandaloneCapture::takeRecording() {
  stop();
  const auto count = static_cast<size_t>(juce::jlimit(0, target_, recorded_.load()));
  recording_.wet.resize(count);
  if (!recording_.dry.empty())
    recording_.dry.resize(count);
  recording_.clipped = clipped_.load();
  return std::move(recording_);
}
void StandaloneCapture::audioDeviceAboutToStart(juce::AudioIODevice* device) {
  if (device == nullptr || std::abs(device->getCurrentSampleRate() - deviceRate_) > 0.01)
    stoppedUnexpectedly_.store(true);
}
void StandaloneCapture::audioDeviceStopped() {
  if (attached_)
    stoppedUnexpectedly_.store(true);
}
void StandaloneCapture::audioDeviceError(const juce::String&) {
  stoppedUnexpectedly_.store(true);
}
void StandaloneCapture::audioDeviceIOCallbackWithContext(
    const float* const* input,
    int numInputs,
    float* const* output,
    int numOutputs,
    int numSamples,
    const juce::AudioIODeviceCallbackContext&) {
  for (int ch = 0; ch < numOutputs; ++ch)
    if (output[ch] != nullptr)
      juce::FloatVectorOperations::clear(output[ch], numSamples);
  const bool pair = recording_.config.mode == Mode::dryWet;
  if (wetPacked_ >= numInputs || input[wetPacked_] == nullptr ||
      (pair && (dryPacked_ >= numInputs || input[dryPacked_] == nullptr))) {
    stoppedUnexpectedly_.store(true);
    return;
  }
  float dp = 0, wp = 0;
  int position = recorded_.load(std::memory_order_relaxed);
  for (int i = 0; i < numSamples && position < target_; ++i, ++position) {
    const float wet = input[wetPacked_][i];
    const float dry = pair ? input[dryPacked_][i] : 0;
    if (!std::isfinite(wet) || !std::isfinite(dry)) {
      stoppedUnexpectedly_.store(true);
      continue;
    }
    if (!levelsOnly_) {
      recording_.wet[static_cast<size_t>(position)] = wet;
      if (pair)
        recording_.dry[static_cast<size_t>(position)] = dry;
    }
    dp = juce::jmax(dp, std::abs(dry));
    wp = juce::jmax(wp, std::abs(wet));
    if (std::abs(dry) >= 0.99997f || std::abs(wet) >= 0.99997f)
      clipped_.store(true, std::memory_order_relaxed);
    const int referencePosition = position - preroll_;
    if (!levelsOnly_ && !pair && referencePosition >= 0 &&
        referencePosition < static_cast<int>(playback_.size()) && sendPacked_ < numOutputs &&
        output[sendPacked_] != nullptr)
      output[sendPacked_][i] = playback_[static_cast<size_t>(referencePosition)] * sendGain_;
  }
  dryPeak_.store(dp, std::memory_order_relaxed);
  wetPeak_.store(wp, std::memory_order_relaxed);
  recorded_.store(position, std::memory_order_release);
}
}  // namespace t3k::capture
