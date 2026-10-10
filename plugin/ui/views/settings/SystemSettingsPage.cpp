#include "SystemSettingsPage.h"

#include <cmath>

#include "core/CustomIcons.h"
#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "widgets/Clickable.h"
#include "widgets/form/FormControls.h"

namespace t3k::ui {

namespace {
// Sentinel for the "no device" dropdown entry (JUCE's empty device name).
const juce::String kNoDevice;

// Device dropdown options: real devices, a "no device" entry, and (when a
// selected device vanished mid-session) its stale name so the picker shows
// what broke instead of going blank.
std::vector<SelectField::Option> deviceOptions(const std::vector<juce::String>& devices, const juce::String& current,
                                               const juce::String& noneLabel) {
  std::vector<SelectField::Option> options;
  if (current != kNoDevice && std::find(devices.begin(), devices.end(), current) == devices.end())
    options.push_back({current, current + " (disconnected)", {}});
  for (const auto& name : devices) options.push_back({name, name, {}});
  options.push_back({kNoDevice, noneLabel, {}});
  return options;
}

// Captions vary by backend, per the UX spec.
juce::String bufferCaption(const AudioDeviceState& s) {
  if (s.bufferSizes.size() <= 1)
    return s.currentType == "JACK" ? "Set by the JACK server. Change it in qjackctl or PipeWire."
                                   : "Set by the audio driver.";
  if (s.currentType == "ASIO")
    return "Some ASIO drivers override this. If it snaps back, set it in the driver's Control Panel below.";
  return {};
}

juce::String rateCaption(const AudioDeviceState& s) {
  if (s.sampleRates.size() > 1) return {};
  if (s.currentType == "JACK") return "Set by the JACK server.";
  if (s.currentType == "Windows Audio")
    return "Fixed by Windows in shared mode. Switch the Audio Driver to Exclusive Mode for other rates.";
  return "Fixed by the current audio driver.";
}

juce::String formatBufferOption(int samples, double rate, bool recommendable) {
  juce::String label = juce::String(samples) + " samples";
  if (rate > 0) label += " (" + juce::String(samples * 1000.0 / rate, 1) + " ms)";
  if (recommendable && samples == 128) label += " (recommended)";
  return label;
}

juce::String formatRate(double rate) {
  const auto whole = static_cast<int>(std::round(rate));
  return juce::approximatelyEqual(rate, static_cast<double>(whole)) ? juce::String(whole) : juce::String(rate);
}

RichText boldThen(const juce::String& strong, const juce::String& rest) {
  return {TextRun::strong(strong), TextRun::plain(rest)};
}
}  // namespace

// TestButton
// Fixed width + constant label so the active state never shifts layout; the
// icon turns green (and pulses) while the tone plays. Playing is busy, not
// blocked; only a missing output device reads as not-allowed.
class SystemSettingsPage::TestButton : public Clickable, private juce::Timer {
public:
  static constexpr int kWidth = 104, kIcon = 15, kGap = 7;
  static constexpr float kPulseMs = 900, kPulseMinAlpha = 0.35f;

  TestButton() : Clickable("Test") {}

  void setDisabled(bool disabled) {
    disabled_ = disabled;
    setMouseCursor(disabled ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    setEnabled(!disabled && !playing_);
    repaint();
  }

  void setPlaying(bool playing) {
    playing_ = playing;
    setEnabled(!disabled_ && !playing_);
    setMouseCursor(playing || disabled_ ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    if (playing) startTimerHz(30);
    else stopTimer();
    repaint();
  }

  void paintButton(juce::Graphics& g, bool, bool) override {
    const auto box = getLocalBounds().toFloat();
    paint::border(g, box, form::kFieldRadius, form::kFieldBorder);
    const auto fg = playing_ ? form::kToggleOn : disabled_ ? theme::kSubtle : theme::kWhite;
    const auto font = Fonts::sans(form::kFieldPx);
    const float lineH = static_cast<float>(Fonts::normalLineHeight(form::kFieldPx));
    const float textW = Fonts::width(font, getButtonText());
    float x = std::round((box.getWidth() - (kIcon + kGap + textW)) / 2);
    const float top = std::round((box.getHeight() - lineH) / 2);
    Icons::draw(g, Icon::Volume2, juce::Rectangle<float>(x, top + (lineH - kIcon) / 2, kIcon, kIcon),
                fg.withMultipliedAlpha(playing_ ? pulseAlpha() : 1.0f));
    x += kIcon + kGap;
    paint::cssLine(g, getButtonText(), x, top, lineH, textW + 2, font, fg);
  }

private:
  float pulseAlpha() const {
    const double t = std::fmod(static_cast<double>(juce::Time::getMillisecondCounter()), kPulseMs) / kPulseMs;
    const double wave = 0.5 - 0.5 * std::cos(t * juce::MathConstants<double>::twoPi);
    return static_cast<float>(1.0 - (1.0 - kPulseMinAlpha) * wave);
  }
  void timerCallback() override { repaint(); }

  bool disabled_ = false, playing_ = false;
};

// OutputRow
// Output device select (separate-I/O drivers only) beside the fixed-width
// test button, 8px apart.
class SystemSettingsPage::OutputRow : public FormItem {
public:
  static constexpr int kGap = 8;

  OutputRow() {
    addAndMakeVisible(select);
    addAndMakeVisible(test);
  }

  SelectField select{"Output device"};
  TestButton test;

  void setSelectShown(bool shown) {
    select.setVisible(shown);
    resized();
  }

  float heightFor(float) const override { return static_cast<float>(form::fieldHeight()); }

  void resized() override {
    auto area = getLocalBounds();
    test.setBounds(select.isVisible() ? area.removeFromRight(TestButton::kWidth) : area.removeFromLeft(TestButton::kWidth));
    if (select.isVisible()) select.setBounds(area.withTrimmedRight(kGap));
  }
};

// ButtonPair
// Two outlined buttons sharing the row (Open Control Panel / Reset Device).
class SystemSettingsPage::ButtonPair : public FormItem {
public:
  static constexpr int kGap = 8;

  ButtonPair(const juce::String& leftLabel, const juce::String& rightLabel)
      : left(leftLabel, FormButton::outlined()), right(rightLabel, FormButton::outlined()) {
    addAndMakeVisible(left);
    addAndMakeVisible(right);
  }

  FormButton left, right;

  float heightFor(float) const override { return static_cast<float>(left.preferredHeight()); }

  void resized() override {
    const int w = (getWidth() - kGap) / 2;
    left.setBounds(0, 0, w, getHeight());
    right.setBounds(getWidth() - w, 0, w, getHeight());
  }
};

// SystemSettingsPage
SystemSettingsPage::SystemSettingsPage(Services& services)
    : FormStack(form::kSectionGap),
      services_(services),
      micDenied_(AlertVariant::error,
                 boldThen("Microphone access is off.",
                          juce::String::fromUTF8(" TONE3000 can\xe2\x80\x99t hear your instrument until you allow "
                                                 "it in your privacy settings, then relaunch.")),
                 {{"Allow Access", [this] { apply([&] { return services_.audioDevice.openMicSettings(); }); }}}),
      inlineError_(AlertVariant::error, {},
                   {{"Retry", [this] { apply([&] { return services_.audioDevice.restartDevice(); }); }}}),
      hearYourself_("Hear Yourself", "Hear your instrument while playing."),
      group_("Audio Interface", custom_icons::kAudioInterface),
      driver_("Audio Driver"),
      inputDevice_("Input Device", "The interface or microphone your instrument is plugged into."),
      linkedDevice_("Device", "This driver handles input and output together."),
      channels_(services),
      output_("Output Device", "Select where you want to hear the sound."),
      outputRow_(std::make_unique<OutputRow>()),
      outputPairs_("Output Channels", "This interface has more than one output pair."),
      buffer_("Buffer Size",
              "Hearing a delay between picking and hearing the note? Choose a smaller size. Hearing crackles or "
              "pops? Choose a bigger one."),
      rate_("Sample Rate",
            "48000 Hz is the sweet spot and uses the least CPU. Other rates work fine, pick one if your device "
            "needs it or you prefer it."),
      driverSettings_("Driver Settings", "Buffer and clock options live in the manufacturer's app for this driver."),
      panelButtons_(std::make_unique<ButtonPair>("Open Control Panel", "Reset Device")),
      midiInputs_(services) {
  auto& dev = services_.audioDevice;

  // OS mic gate: the one form error whose fix lives outside the app, so
  // (unlike the other inline mirrors) it keeps its action button.
  add(micDenied_);
  add(inlineError_);

  // Output monitoring goes at the top of the tab; this is how sound starts.
  hearYourself_.onChange = [this, &dev](bool hear) { apply([&] { return dev.setHearYourself(hear); }); };
  hearYourself_.content().add(feedbackRisk_);
  hearYourself_.content().add(inputMuted_);
  add(hearYourself_);

  // Audio interface card.
  driverSelect_.onChange = [this, &dev](const juce::String& t) { apply([&] { return dev.setDeviceType(t); }); };
  driver_.content().add(driverSelect_);
  driver_.content().add(asioNudge_, form::kControlGap);
  group_.content().add(driver_);

  inputSelect_.onChange = [this, &dev](const juce::String& n) { apply([&] { return dev.setInputDevice(n); }); };
  inputDevice_.content().add(inputSelect_);
  group_.content().add(inputDevice_);
  linkedSelect_.onChange = [this, &dev](const juce::String& n) { apply([&] { return dev.setLinkedDevice(n); }); };
  linkedDevice_.content().add(linkedSelect_);
  group_.content().add(linkedDevice_);

  group_.content().add(channels_);

  outputRow_->select.onChange = [this, &dev](const juce::String& n) {
    apply([&] { return dev.setOutputDevice(n); });
  };
  outputRow_->test.onClick = [this, &dev] {
    outputRow_->test.setPlaying(true);
    apply([&] { return dev.playTestTone(); });
    juce::Timer::callAfterDelay(kTestPlayingMs, [safe = juce::Component::SafePointer<TestButton>(&outputRow_->test)] {
      if (safe != nullptr) safe->setPlaying(false);
    });
  };
  output_.content().add(*outputRow_);
  output_.content().add(noOutput_, form::kControlGap);
  group_.content().add(output_);

  pairSelect_.onChange = [this, &dev](const juce::String& i) {
    apply([&] { return dev.setOutputPair(i.getIntValue()); });
  };
  outputPairs_.content().add(pairSelect_);
  group_.content().add(outputPairs_);

  bufferSelect_.onChange = [this, &dev](const juce::String& s) {
    apply([&] { return dev.setBufferSize(s.getIntValue()); });
  };
  buffer_.content().add(bufferSelect_);
  buffer_.content().add(bufferCaption_, form::kControlGap);
  buffer_.content().add(bufferLatency_, form::kControlGap);
  group_.content().add(buffer_);

  rateSelect_.onChange = [this, &dev](const juce::String& r) {
    apply([&] { return dev.setSampleRate(r.getDoubleValue()); });
  };
  rate_.content().add(rateSelect_);
  rate_.content().add(rateCaption_, form::kControlGap);
  // iOS: the Bluetooth route is why the rate list is short and the rate is
  // low, so it goes first and the generic note is dropped when it fires.
  rate_.content().add(bluetoothRoute_, form::kControlGap);
  rate_.content().add(rateNot48k_, form::kControlGap);
  group_.content().add(rate_);

  panelButtons_->left.onClick = [this, &dev] { apply([&] { return dev.openControlPanel(); }); };
  panelButtons_->right.onClick = [this, &dev] { apply([&] { return dev.restartDevice(); }); };
  driverSettings_.content().add(*panelButtons_);
  group_.content().add(driverSettings_);
  add(group_);

  // MIDI hardware: which devices feed the plugin. Every section carries the
  // SECTION_GAP below it, the last one included.
  add(midiInputs_);
  setTrailing(form::kSectionGap);

  dev.addListener(this);
  sync();
}

SystemSettingsPage::~SystemSettingsPage() { services_.audioDevice.removeListener(this); }

// Errors from mutations render inline (never a modal); Retry reopens the
// device, the standard recovery for "device in use" and panel weirdness.
template <typename Fn>
void SystemSettingsPage::apply(Fn&& fn) {
  error_ = fn();
  sync();
}

void SystemSettingsPage::sync() {
  const auto& maybe = services_.audioDevice.state();
  if (!maybe) return;
  const auto& s = *maybe;
  const bool hasSelection = s.inputDevice != kNoDevice || s.outputDevice != kNoDevice;

  // A configured-but-closed device (unplugged interface, exclusively-held
  // ALSA card) is the top-priority inline error; explicit "no device" is not
  // an error here (the main-window banner covers the no-input consequence).
  const bool deviceFailed = hasSelection && !s.deviceOpen;
  const juce::String inlineError =
      error_.isNotEmpty() ? error_ : deviceFailed ? juce::String::fromUTF8("The audio device couldn\xe2\x80\x99t be opened.") : juce::String();
  setShown(micDenied_, s.micPermission == MicPermission::denied);
  if (inlineError.isNotEmpty())
    inlineError_.setContent(boldThen(inlineError, " Plug the interface back in, hit Retry, or choose another device."));
  setShown(inlineError_, inlineError.isNotEmpty());

  // Inline mirrors of the main-window banners, gated to a running device (a
  // dead device is already covered by the top-of-form error above), placed
  // under the control that resolves each one.
  bool anyActive = false;
  for (const auto& c : s.inputChannels) anyActive |= c.active;
  const bool showMuted = s.deviceOpen && !s.hearYourself && anyActive;
  hearYourself_.setValue(s.hearYourself);
  {
    auto& stack = hearYourself_.content();
    const bool risk = feedbackRisk_.update(s, s.hearYourself && s.feedbackRisk);
    const bool muted = inputMuted_.update(s, showMuted);
    stack.setShown(feedbackRisk_, risk);
    stack.setShown(inputMuted_, muted);
    hearYourself_.setExpanded(risk || muted);
  }

  auto& card = group_.content();
  // Driver picker, only when the platform has more than one backend.
  {
    const bool asio = std::find(s.deviceTypes.begin(), s.deviceTypes.end(), "ASIO") != s.deviceTypes.end();
    driver_.setHelp(asio ? "ASIO gives the lowest latency with a dedicated interface."
                         : "Choose which audio system drives your devices.");
    std::vector<SelectField::Option> types;
    for (const auto& t : s.deviceTypes) types.push_back({t, t, {}});
    driverSelect_.setOptions(std::move(types));
    driverSelect_.setValue(s.currentType);
    driver_.content().setShown(asioNudge_, asioNudge_.update(s));
    card.setShown(driver_, s.deviceTypes.size() > 1);
  }

  // Device picker(s): one for linked-I/O drivers, separate otherwise.
  inputSelect_.setOptions(deviceOptions(s.inputDevices, s.inputDevice, "No input device"));
  inputSelect_.setValue(s.inputDevice);
  card.setShown(inputDevice_, s.separateIO);
  linkedSelect_.setOptions(deviceOptions(s.outputDevices, s.outputDevice, "No device"));
  linkedSelect_.setValue(s.outputDevice);
  card.setShown(linkedDevice_, !s.separateIO);

  channels_.update(s);

  // Output device + test tone.
  outputRow_->select.setOptions(deviceOptions(s.outputDevices, s.outputDevice, "No output device"));
  outputRow_->select.setValue(s.outputDevice);
  outputRow_->setSelectShown(s.separateIO);
  outputRow_->test.setDisabled(!s.deviceOpen || s.outputDevice == kNoDevice);
  const bool showNoOutput = s.separateIO && s.deviceOpen && s.outputDevice == kNoDevice;
  output_.content().setShown(noOutput_, noOutput_.update(s, showNoOutput));

  // Stereo output pair, only for multi-out interfaces.
  {
    std::vector<SelectField::Option> pairs;
    for (size_t i = 0; i < s.outputPairs.size(); ++i)
      pairs.push_back({juce::String(static_cast<int>(i)), s.outputPairs[i], {}});
    pairSelect_.setOptions(std::move(pairs));
    pairSelect_.setValue(juce::String(juce::jmax(s.activeOutputPair, 0)));
    card.setShown(outputPairs_, s.outputPairs.size() > 1);
  }

  // Buffer size; list and current value are device readback.
  {
    std::vector<SelectField::Option> sizes;
    for (int samples : s.bufferSizes)
      sizes.push_back({juce::String(samples), formatBufferOption(samples, s.sampleRate, s.bufferSizes.size() > 1), {}});
    bufferSelect_.setOptions(std::move(sizes));
    bufferSelect_.setValue(juce::String(s.bufferSize));
    bufferSelect_.setDisabled(s.bufferSizes.size() <= 1);
    const auto caption = bufferCaption(s);
    bufferCaption_.setText(caption);
    buffer_.content().setShown(bufferCaption_, caption.isNotEmpty());
    buffer_.content().setShown(bufferLatency_, bufferLatency_.update(s));
    card.setShown(buffer_, !s.bufferSizes.empty());
  }

  // Sample rate.
  {
    std::vector<SelectField::Option> rates;
    for (double rate : s.sampleRates) {
      juce::String label = formatRate(rate) + " Hz";
      if (juce::approximatelyEqual(rate, 48000.0) && s.sampleRates.size() > 1) label += " (recommended)";
      rates.push_back({formatRate(rate), label, {}});
    }
    rateSelect_.setOptions(std::move(rates));
    rateSelect_.setValue(formatRate(s.sampleRate));
    rateSelect_.setDisabled(s.sampleRates.size() <= 1);
    const auto caption = rateCaption(s);
    rateCaption_.setText(caption);
    rate_.content().setShown(rateCaption_, caption.isNotEmpty());
    const bool bluetooth = bluetoothRoute_.update(s);
    rate_.content().setShown(bluetoothRoute_, bluetooth);
    rate_.content().setShown(rateNot48k_, rateNot48k_.update(s, !bluetooth && rateNot48k_.firesFor(s)));
    card.setShown(rate_, !s.sampleRates.empty());
  }

  // Vendor control panel (ASIO); buffer / clock often live there.
  card.setShown(driverSettings_, s.hasControlPanel);

  midiInputs_.update(s);
}

}  // namespace t3k::ui
