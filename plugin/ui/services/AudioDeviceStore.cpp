#include "AudioDeviceStore.h"

namespace t3k::ui {

AudioDeviceStore::AudioDeviceStore(Backend& backend) : backend_(backend) {
  backend_.addListener(this);
  refresh();
}

AudioDeviceStore::~AudioDeviceStore() { backend_.removeListener(this); }

void AudioDeviceStore::refresh() {
  const auto res = backend_.getAudioDeviceState();
  // Hosted builds (and the mock backend) return nothing useful.
  if (res.isObject() && res.getProperty("deviceTypes", juce::var()).isArray())
    state_ = AudioDeviceState::parse(res);
  else
    state_.reset();
  listeners.call([](Listener& l) { l.audioDeviceChanged(); });
}

template <typename Fn>
juce::String AudioDeviceStore::run(Fn&& fn) {
  const juce::var res = fn();
  juce::String error;
  if (!res.isObject() || !res.hasProperty("ok"))
    error = "Audio settings are unavailable.";
  else if (const auto r = AudioDeviceResult::parse(res); !r.ok)
    error = r.error.isNotEmpty() ? r.error : "Couldn't apply the audio settings.";
  refresh();
  return error;
}

juce::String AudioDeviceStore::setDeviceType(const juce::String& typeName) {
  return run([&] { return backend_.setAudioDeviceType(typeName); });
}
juce::String AudioDeviceStore::setInputDevice(const juce::String& name) {
  return run([&] { return backend_.setAudioDevice("input", name); });
}
juce::String AudioDeviceStore::setOutputDevice(const juce::String& name) {
  return run([&] { return backend_.setAudioDevice("output", name); });
}
juce::String AudioDeviceStore::setLinkedDevice(const juce::String& name) {
  return run([&] { return backend_.setAudioDevice("linked", name); });
}
juce::String AudioDeviceStore::setInputChannels(const std::vector<int>& indices) {
  juce::Array<juce::var> arr;
  for (int i : indices) arr.add(i);
  return run([&] { return backend_.setAudioInputChannels(arr); });
}
juce::String AudioDeviceStore::setOutputPair(int pairIndex) {
  return run([&] { return backend_.setAudioOutputPair(pairIndex); });
}
juce::String AudioDeviceStore::setSampleRate(double rate) {
  return run([&] { return backend_.setAudioSampleRate(rate); });
}
juce::String AudioDeviceStore::setBufferSize(int samples) {
  return run([&] { return backend_.setAudioBufferSize(samples); });
}
juce::String AudioDeviceStore::setHearYourself(bool hear) {
  return run([&] { return backend_.setHearYourself(hear); });
}
juce::String AudioDeviceStore::playTestTone() {
  return run([&] { return backend_.playTestTone(); });
}
juce::String AudioDeviceStore::openControlPanel() {
  return run([&] { return backend_.openAudioControlPanel(); });
}
juce::String AudioDeviceStore::restartDevice() {
  return run([&] { return backend_.restartAudioDevice(); });
}
juce::String AudioDeviceStore::openMicSettings() {
  return run([&] { return backend_.openMicSettings(); });
}
juce::String AudioDeviceStore::setMidiInputEnabled(const juce::String& id, bool enabled) {
  return run([&] { return backend_.setMidiInputEnabled(id, enabled); });
}
juce::String AudioDeviceStore::openBluetoothMidiPairing() {
  return run([&] { return backend_.openBluetoothMidiPairing(); });
}

// Input levels
namespace {
// dB the displayed level falls per tick when the signal drops.
constexpr float kFalloffDb = 4;
}  // namespace

AudioInputLevels::AudioInputLevels(Backend& backend, UiClock& clock, std::function<void()> onChange)
    : backend_(backend), clock_(clock), onChange_(std::move(onChange)) {
  backend_.setAudioInputMetering(true);
  clock_.addListener(this);
}

AudioInputLevels::~AudioInputLevels() {
  clock_.removeListener(this);
  backend_.setAudioInputMetering(false);
}

void AudioInputLevels::tick() {
  const auto raw = backend_.getAudioInputLevels();
  if (!raw.isArray()) return;
  const int n = raw.size();
  displayed_.resize(static_cast<size_t>(n), kFloorDb);
  for (int i = 0; i < n; ++i) {
    const float db = static_cast<float>(static_cast<double>(raw[i]));
    auto& shown = displayed_[static_cast<size_t>(i)];
    shown = std::max({db, shown - kFalloffDb, kFloorDb});
  }
  if (onChange_) onChange_();
}

}  // namespace t3k::ui
