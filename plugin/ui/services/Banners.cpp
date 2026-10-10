#include "Banners.h"

#include <algorithm>
#include <cmath>

#include "core/Design.h"
#include "core/Labels.h"

namespace t3k::ui {

namespace {

juce::String u8(const char* s) { return juce::String::fromUTF8(s); }

TextRun bold(const char* s) { return TextRun::strong(u8(s)); }
TextRun bold(const juce::String& s) { return TextRun::strong(s); }
TextRun plain(const char* s) { return TextRun::plain(u8(s)); }
TextRun plain(const juce::String& s) { return TextRun::plain(s); }

const TextRun kHearYourself = bold("\xe2\x80\x9cHear Yourself\xe2\x80\x9d");

bool noActiveInput(const AudioDeviceState& s) {
  return std::none_of(s.inputChannels.begin(), s.inputChannels.end(),
                      [](const AudioInputChannel& c) { return c.active; });
}

// The number the web's toFixed(1) prints for a buffer's latency.
juce::String bufferMs(const AudioDeviceState& s) {
  return juce::String(s.bufferSize * 1000.0 / (s.sampleRate > 0 ? s.sampleRate : 48000.0), 1);
}

// 48 → "48", 44100 → "44.1".
juce::String kHz(double rate) {
  const bool whole = std::fmod(rate, 1000.0) == 0;
  return labels::toFixed(rate / 1000.0, whole ? 0 : 1);
}

std::vector<BannerRule> makeRules() {
  return {
      // The OS is blocking mic access (macOS privacy). This gates ALL audio
      // input, so the device opens but the stream is silent and nothing
      // else explains why. Highest priority and not ignorable: the only fix
      // is the OS privacy page + a relaunch.
      {"mic-denied", AlertVariant::error, false,
       [](const AudioDeviceState& s) { return s.micPermission == MicPermission::denied; },
       [](const AudioDeviceState&) -> RichText {
         return {bold("Microphone access is off."),
                 plain(" TONE3000 can\xe2\x80\x99t hear your instrument until you allow it in your privacy "
                       "settings, then relaunch.")};
       },
       "Allow Access", BannerAction::openMicSettings},
      // The plugin can't hear the instrument: no input selected, or the
      // device itself failed to open / was disconnected.
      {"no-input", AlertVariant::error, false,
       [](const AudioDeviceState& s) {
         return (s.inputDevice.isNotEmpty() || s.outputDevice.isNotEmpty()) &&
                (!s.deviceOpen || s.inputDevice.isEmpty() || noActiveInput(s));
       },
       [](const AudioDeviceState& s) -> RichText {
         return {bold("No audio input."),
                 plain(juce::String(" ") +
                       u8(!s.deviceOpen ? "Your audio device was disconnected or couldn\xe2\x80\x99t be opened."
                                        : "No input device is selected.") +
                       u8(" The plugin can\xe2\x80\x99t hear your instrument."))};
       },
       "Open Settings", BannerAction::openSettings},
      // Device is open but nothing is routed out. Ranks just below no-input
      // (which owns the disconnected case).
      {"no-output", AlertVariant::error, false,
       [](const AudioDeviceState& s) { return s.deviceOpen && s.outputDevice.isEmpty(); },
       [](const AudioDeviceState&) -> RichText {
         return {bold("No audio output."),
                 plain(" No output device is selected, so you won\xe2\x80\x99t hear the plugin.")};
       },
       "Open Settings", BannerAction::openSettings},
      // Muting feeds the amp sim silence; always surface it. Clears the
      // instant Hear Yourself goes back on, so it needn't be dismissable.
      {"input-muted", AlertVariant::warn, false,
       [](const AudioDeviceState& s) { return s.deviceOpen && !s.hearYourself && !noActiveInput(s); },
       [](const AudioDeviceState& s) -> RichText {
         if (s.feedbackRisk)
           return {bold("Input muted to prevent feedback."),
                   plain(" You\xe2\x80\x99re using a microphone with speakers. Plug in headphones or an "
                         "interface, then turn "),
                   kHearYourself, plain(" on.")};
         return {bold("Input is muted."), plain(" Turn "), kHearYourself,
                 plain(" on so the plugin can hear your instrument.")};
       },
       "Open Settings", BannerAction::openSettings},
      // Monitoring with a mic-into-speakers setup: nothing is muted but a
      // squeal is one gain bump away. The user's call once warned.
      {"feedback-risk", AlertVariant::warn, true,
       [](const AudioDeviceState& s) { return s.feedbackRisk && s.hearYourself; },
       [](const AudioDeviceState&) -> RichText {
         return {bold("Feedback risk."), plain(" Your mic can hear your speakers. Use headphones, or turn "),
                 kHearYourself, plain(" off.")};
       },
       "Open Settings", BannerAction::openSettings},
      // Requires ASIO to actually report devices, not just that the type exists.
      {"asio-nudge", AlertVariant::info, true,
       [](const AudioDeviceState& s) { return s.asioAvailable && s.currentType != "ASIO"; },
       [](const AudioDeviceState&) -> RichText {
         return {bold("Lower latency available."),
                 plain(" Your interface has an ASIO driver, which is faster than Windows Audio.")};
       },
       "Switch to ASIO", BannerAction::switchToAsio},
      // Skipped when no ≤512 option exists or the driver owns the buffer;
      // never nag about something the user can't fix here.
      {"buffer-latency", AlertVariant::info, true,
       [](const AudioDeviceState& s) {
         return s.bufferSize > 512 && s.bufferSizes.size() > 1 &&
                std::any_of(s.bufferSizes.begin(), s.bufferSizes.end(), [](int b) { return b <= 512; });
       },
       [](const AudioDeviceState& s) -> RichText {
         return {bold("Noticeable delay?"),
                 plain(" Your buffer is " + juce::String(s.bufferSize) + " samples (" + bufferMs(s) +
                       " ms). Lowering it to 256 or less will feel more responsive.")};
       },
       "Open Settings", BannerAction::openSettings},
      // iOS only: a Bluetooth headset route caps the rate at 16 / 24 kHz and
      // adds latency. Above rate-not-48k because it is the *cause*.
      {"bluetooth-route", AlertVariant::info, true,
       [](const AudioDeviceState& s) { return design::kIos && s.shouldShowBluetoothTip(); },
       [](const AudioDeviceState& s) -> RichText {
         return {bold(s.bluetoothTipHeadline()),
                 plain(" For playing, use wired headphones, the iPad speaker, or a USB audio interface.")};
       },
       "Open Settings", BannerAction::openSettings},
      // Off 48 kHz the chain resamples, which costs a little CPU. Lowest priority.
      {"rate-not-48k", AlertVariant::info, true,
       [](const AudioDeviceState& s) {
         return s.deviceOpen && s.sampleRate > 0 && juce::roundToInt(s.sampleRate) != 48000;
       },
       [](const AudioDeviceState& s) -> RichText {
         return {bold("Running at " + kHz(s.sampleRate) + " kHz."),
                 plain(" TONE3000 runs lightest at 48 kHz. Any rate works fine, this one just uses a bit "
                       "more CPU.")};
       },
       "Open Settings", BannerAction::openSettings},
  };
}

}  // namespace

const std::vector<BannerRule>& bannerRules() {
  static const std::vector<BannerRule> rules = makeRules();
  return rules;
}

const BannerRule* bannerRuleById(const juce::String& id) {
  for (const auto& rule : bannerRules())
    if (rule.id == id) return &rule;
  return nullptr;
}

BannerStore::BannerStore(AudioDeviceStore& devices, UiPrefs& prefs) : devices_(devices), prefs_(prefs) {
  devices_.addListener(this);
  evaluate();
}

BannerStore::~BannerStore() { devices_.removeListener(this); }

bool BannerStore::ignored(const juce::String& id) const {
  const auto map = prefs_.getJson(UiPrefs::kDismissedBanners);
  const auto until = map.getProperty(id, 0);
  return static_cast<juce::int64>(static_cast<double>(until)) > juce::Time::currentTimeMillis();
}

void BannerStore::dismiss(const juce::String& id) {
  auto map = prefs_.getJson(UiPrefs::kDismissedBanners);
  if (!map.isObject()) map = juce::var(new juce::DynamicObject());
  map.getDynamicObject()->setProperty(id, static_cast<double>(juce::Time::currentTimeMillis() + kIgnoreMs));
  prefs_.setJson(UiPrefs::kDismissedBanners, map);
  evaluate();
}

void BannerStore::evaluate() {
  std::optional<BannerSpec> next;
  if (const auto& state = devices_.state()) {
    for (const auto& rule : bannerRules()) {
      if (!rule.when(*state) || (rule.dismissable && ignored(rule.id))) continue;
      next = BannerSpec{rule.id, rule.variant, rule.content(*state), rule.actionLabel, rule.action,
                        rule.dismissable};
      break;
    }
  }
  const bool same =
      next.has_value() == active_.has_value() && (!next || (next->id == active_->id && next->content == active_->content));
  if (same) return;
  active_ = std::move(next);
  listeners_.call([](Listener& l) { l.bannerChanged(); });
}

}  // namespace t3k::ui
