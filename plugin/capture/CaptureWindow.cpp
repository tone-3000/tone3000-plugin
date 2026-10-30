#include "CaptureWindow.h"

#include "CaptureDsp.h"
#include "KemperMidi.h"
#include "StandaloneCapture.h"
#include "core/Fonts.h"
#include "core/Theme.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <optional>

namespace t3k::capture {
namespace {
namespace theme = t3k::ui::theme;
using t3k::ui::Fonts;

constexpr int kMargin = 24;
constexpr int kGap = 16;
constexpr float kMinimumCorrelation = 0.15f;

float peak(const std::vector<float>& samples) {
  float result = 0.0f;
  for (float sample : samples)
    result = std::max(result, std::abs(sample));
  return result;
}

const char* modeName(Mode mode) {
  switch (mode) {
    case Mode::dryWet:
      return "Dry / wet pair";
    case Mode::namSignal:
      return "NAM test signal";
    case Mode::impulseResponse:
      return "Impulse response";
  }
  return "Capture";
}

juce::String describe(Mode mode) {
  switch (mode) {
    case Mode::dryWet:
      return "Record a clean DI and Kemper return from the same performance, at the same time.";
    case Mode::namSignal:
      return "Play the original TONE3000 test WAV through your Kemper and record the return.";
    case Mode::impulseResponse:
      return "Measure a linear response, such as a cabinet. An IR cannot reproduce amp distortion.";
  }
  return {};
}

struct Prepared {
  std::shared_ptr<Recording> recording;
  Alignment alignment;
  std::vector<float> ir;
  juce::String error;
};

Prepared prepare(Recording recording) {
  Prepared result;
  result.recording = std::make_shared<Recording>(std::move(recording));
  const auto conversion = StandaloneCapture::prepareRecording(*result.recording);
  if (conversion.failed()) {
    result.error = conversion.getErrorMessage();
    return result;
  }
  const auto& take = *result.recording;
  if (take.wet.size() < static_cast<size_t>(sampleRate) || peak(take.wet) < 0.00001f) {
    result.error =
        "Record at least one second of a non-silent Kemper return. Check the return channel and "
        "routing.";
    return result;
  }
  if (take.config.mode == Mode::dryWet) {
    if (take.dry.size() != take.wet.size() || peak(take.dry) < 0.00001f)
      result.error =
          "The dry DI is silent or its length differs from the return. Select the clean guitar "
          "channel.";
  } else if (take.config.mode == Mode::namSignal) {
    result.alignment = estimateDelay(take.reference, take.wet, sampleRate * 3);
  } else {
    auto ir = extractIr(take.reference, take.wet, take.config.irSamples);
    result.ir = std::move(ir.samples);
    result.error = ir.error;
  }
  return result;
}

struct Exported {
  juce::Result result = juce::Result::ok();
  juce::File folder;
};
}  // namespace

class CaptureWindow::Panel final : public juce::Component, private juce::Timer {
public:
  explicit Panel(std::function<void()> openAudioSettings)
      : openAudioSettings_(std::move(openAudioSettings)), canvas_(*this) {
    setOpaque(true);
    viewport_.setViewedComponent(&canvas_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    statusLabel_.setName("Capture status");
    statusLabel_.setTitle("Capture status");
    statusLabel_.setFont(Fonts::sans(12));
    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel_);

    initialise(mode_, "Capture method");
    mode_.addItem("Dry / wet pair", 1);
    mode_.addItem("NAM test signal", 2);
    mode_.addItem("Impulse response", 3);
    mode_.setSelectedId(1, juce::dontSendNotification);
    mode_.onChange = [this] {
      syncControls();
      resized();
    };
    initialise(gear_, "Kemper model to capture");
    gear_.addItem("Kemper Head / PowerHead", 1);
    gear_.addItem("Kemper Rack / PowerRack", 2);
    gear_.addItem("Kemper Stage", 3);
    gear_.addItem("Kemper Player", 4);
    gear_.setSelectedId(1, juce::dontSendNotification);
    initialise(dry_, "Dry DI input channel");
    initialise(wet_, "Kemper return input channel");
    initialise(send_, "Test signal output channel");

    button(audioSettings_, "Audio settings", [this] {
      if (openAudioSettings_)
        openAudioSettings_();
    });
    button(refresh_, "Refresh channels", [this] {
      refreshAudio();
      refreshMidi();
    });
    button(chooseSignal_, "Import original WAV...", [this] { chooseSignal(); });
    button(getSignal_, "Get test signal",
           [] { juce::URL("https://www.tone3000.com/capture").launchInDefaultBrowser(); });
    button(checkLevels_, "Check input levels", [this] { start(true); });
    button(start_, "Start capture", [this] { start(false); });
    start_.setColour(juce::TextButton::buttonColourId, theme::kBrandYellow);
    start_.setColour(juce::TextButton::textColourOffId, theme::kBlack);
    button(stop_, "Stop & keep take", [this] { finish(); });
    button(cancel_, "Cancel", [this] {
      capture_.stop();
      if (capture_.error().isNotEmpty())
        error_ = capture_.error();
      levelsOnly_ = false;
      status_ = "Stopped. No files were saved for this cancelled recording.";
      syncControls();
    });
    button(chooseExport_, "Export location...", [this] { chooseExport(); });
    button(export_, "Export capture files", [this] { save(); });
    button(reveal_, "Open export folder", [this] {
      if (lastExport_.isDirectory())
        lastExport_.revealToUser();
    });
    button(training_, "Open TONE3000",
           [] { juce::URL("https://www.tone3000.com/capture").launchInDefaultBrowser(); });
    button(connect_, "Connect MIDI", [this] { connectMidi(); });
    button(loadProgram_, "Load program", [this] {
      midiResult(midi_.selectProgram(static_cast<int>(program_.getValue()) - 1),
                 "Program change sent. Check the Kemper display.");
    });
    button(apply_, "Apply settings to Kemper", [this] {
      auto result = midi_.setGain(static_cast<float>(gain_.getValue()));
      if (result.wasOk())
        result = midi_.setEq(
            static_cast<float>(bass_.getValue()), static_cast<float>(middle_.getValue()),
            static_cast<float>(treble_.getValue()), static_cast<float>(presence_.getValue()));
      if (result.wasOk())
        result = midi_.setCabinet(cabinet_.getToggleState());
      midiResult(result, "Settings sent. Check the Kemper display; no rig has been stored.");
    });

    slider(duration_, "Dry wet duration in seconds", 1, 600, 1, 180, " s");
    slider(sweep_, "Impulse response sweep duration", 3, 30, 1, 10, " s");
    slider(irLength_, "Exported impulse response length", 100, 2000, 50, 500, " ms");
    slider(trim_, "Test signal output trim", -48, 0, 1, -18, " dB");
    slider(offset_, "Recorded return offset in samples", 0, sampleRate * 3, 1, 0, " samples");
    offset_.onValueChange = [this] { syncControls(); };
    slider(program_, "Kemper MIDI program number", 1, 128, 1, 1, {});
    slider(gain_, "Requested Kemper Generic Gain", 0, 10, 0.1, 5, {});
    slider(bass_, "Requested Kemper bass", -5, 5, 0.1, 0, {});
    slider(middle_, "Requested Kemper middle", -5, 5, 0.1, 0, {});
    slider(treble_, "Requested Kemper treble", -5, 5, 0.1, 0, {});
    slider(presence_, "Requested Kemper presence", -5, 5, 0.1, 0, {});

    editor(name_, "Capture name", "Kemper capture", false);
    editor(rig_, "Currently loaded Kemper rig name", {}, false);
    editor(notes_, "Capture session notes", {}, true);
    rig_.setTextToShowWhenEmpty("Enter the rig currently loaded on the Kemper", theme::kSubtle);
    notes_.setTextToShowWhenEmpty("Profile, signal chain, mic or knob positions...",
                                  theme::kSubtle);
    initialise(midiPort_, "Kemper MIDI output destination");
    initialise(midiChannel_, "Kemper global MIDI channel");
    for (int i = 1; i <= 16; ++i)
      midiChannel_.addItem("Channel " + juce::String(i), i);
    midiChannel_.setSelectedId(1, juce::dontSendNotification);
    midiPort_.onChange = midiChannel_.onChange = [this] {
      midi_.close();
      midiStatus_ = "MIDI destination changed. Reconnect to send controls.";
      syncControls();
    };
    cabinet_.setButtonText("Cabinet block enabled");
    initialise(cabinet_, "Requested Kemper cabinet block state");
    cabinet_.setToggleState(true, juce::dontSendNotification);
    manual_.setButtonText("I checked this manual offset");
    initialise(manual_, "Confirm manually checked NAM return alignment");
    manual_.onClick = [this] { syncControls(); };

    exportFolder_ = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                        .getChildFile("TONE3000 Captures");
    status_ =
        "Choose your interface in Audio settings, then select the dry and processed channels.";
    midiStatus_ = "Optional. Audio capture works without MIDI control.";
    refreshAudio();
    refreshMidi();
    syncControls();
    startTimerHz(20);
  }

  ~Panel() override {
    stopTimer();
    capture_.stop();
    if (capture_.error().isNotEmpty())
      error_ = capture_.error();
    // Futures own their data and never capture this view. Their destructors
    // wait for any final disk write rather than abandoning a partial session.
  }

  bool busy() const { return capture_.running() || processing_.valid() || exporting_.valid(); }

  void prepareToHide() {
    if (!capture_.running())
      return;
    if (levelsOnly_) {
      capture_.stop();
      if (capture_.error().isNotEmpty())
        error_ = capture_.error();
      levelsOnly_ = false;
      status_ = "Level check stopped. Your reviewed take is unchanged.";
      syncControls();
    } else if (mode() == Mode::dryWet) {
      // Keep the take while restoring the normal player's audio callback.
      // Preparation continues in the background, even with this window hidden.
      finish();
    } else {
      // A partial sweep is not a usable measurement. Never leave the normal
      // player suspended, or continue sending test audio from a hidden window.
      capture_.stop();
      if (capture_.error().isNotEmpty())
        error_ = capture_.error();
      status_ =
          "Test-signal capture cancelled when the capture window closed. No files were saved.";
      syncControls();
    }
  }

  void paint(juce::Graphics& g) override {
    g.fillAll(theme::kBlack);
    g.setColour(theme::kBorder);
    g.drawHorizontalLine(getHeight() - 54, 0.0f, static_cast<float>(getWidth()));
  }
  void resized() override {
    viewport_.setBounds(getLocalBounds().withTrimmedBottom(54));
    statusLabel_.setBounds(16, getHeight() - 50, getWidth() - 32, 46);
    canvas_.setSize(juce::jmax(850, getWidth() - viewport_.getScrollBarThickness()), 1448);
    layout();
  }

private:
  struct Canvas final : juce::Component {
    explicit Canvas(Panel& owner) : owner_(owner) { setOpaque(true); }
    void paint(juce::Graphics& g) override { owner_.paintCanvas(g); }
    Panel& owner_;
  };

  template <typename Component>
  void initialise(Component& component, const juce::String& title) {
    component.setName(title);
    component.setTitle(title);
    component.setWantsKeyboardFocus(true);
    component.setMouseClickGrabsKeyboardFocus(false);
    canvas_.addAndMakeVisible(component);
  }
  void button(juce::TextButton& control, const juce::String& text, std::function<void()> action) {
    initialise(control, text);
    control.setButtonText(text);
    control.onClick = std::move(action);
  }
  void slider(juce::Slider& control,
              const juce::String& title,
              double low,
              double high,
              double step,
              double value,
              const juce::String& suffix) {
    initialise(control, title);
    control.setSliderStyle(juce::Slider::LinearHorizontal);
    control.setTextBoxStyle(juce::Slider::TextBoxRight, false, 92, 26);
    control.setRange(low, high, step);
    control.setValue(value, juce::dontSendNotification);
    control.setTextValueSuffix(suffix);
    control.setColour(juce::Slider::trackColourId, theme::kBrandYellow);
  }
  void editor(juce::TextEditor& control,
              const juce::String& title,
              const juce::String& value,
              bool multiline) {
    initialise(control, title);
    control.setMultiLine(multiline);
    control.setReturnKeyStartsNewLine(multiline);
    control.setFont(Fonts::sans(14));
    control.setText(value, false);
  }
  Mode mode() const { return static_cast<Mode>(juce::jlimit(0, 2, mode_.getSelectedId() - 1)); }
  Config config() const {
    Config c;
    c.mode = mode();
    c.dryInput = dry_.getSelectedId() - 1;
    c.wetInput = wet_.getSelectedId() - 1;
    c.sendOutput = send_.getSelectedId() - 1;
    c.seconds = duration_.getValue();
    c.sweepSeconds = sweep_.getValue();
    c.irSamples = juce::roundToInt(irLength_.getValue() * sampleRate / 1000.0);
    c.outputDb = static_cast<float>(trim_.getValue());
    c.signal = signal_;
    c.gear = gear_.getText();
    c.name = name_.getText();
    c.rig = rig_.getText();
    c.notes = notes_.getText() +
              "\nKemper knob values in this window are requested values, not hardware readback. "
              "No Kemper rig is stored automatically.";
    return c;
  }
  static void setChannels(juce::ComboBox& combo, const juce::StringArray& names, int fallback) {
    const int selected = combo.getSelectedId();
    combo.clear(juce::dontSendNotification);
    for (int i = 0; i < names.size(); ++i)
      combo.addItem(juce::String(i + 1) + " - " + names[i], i + 1);
    combo.setTextWhenNothingSelected("No channels available");
    if (!names.isEmpty())
      combo.setSelectedId(juce::jlimit(1, names.size(), selected > 0 ? selected : fallback),
                          juce::dontSendNotification);
  }
  void refreshAudio() {
    if (capture_.running())
      return;
    setChannels(dry_, capture_.inputNames(), 1);
    setChannels(wet_, capture_.inputNames(), 2);
    setChannels(send_, capture_.outputNames(), 1);
    deviceDescription_ = capture_.deviceDescription();
    canvas_.repaint();
  }
  void refreshMidi() {
    if (capture_.running() || midi_.isOpen())
      return;
    const auto previous = midiPort_.getSelectedId();
    midiDevices_ = juce::MidiOutput::getAvailableDevices();
    midiPort_.clear(juce::dontSendNotification);
    for (int i = 0; i < midiDevices_.size(); ++i)
      midiPort_.addItem(midiDevices_[i].name, i + 1);
    midiPort_.setTextWhenNothingSelected("Choose a MIDI destination");
    if (juce::isPositiveAndBelow(previous - 1, midiDevices_.size()))
      midiPort_.setSelectedId(previous, juce::dontSendNotification);
  }
  void connectMidi() {
    if (midi_.isOpen()) {
      midi_.close();
      midiStatus_ = "MIDI disconnected.";
    } else {
      const int index = midiPort_.getSelectedId() - 1;
      if (!juce::isPositiveAndBelow(index, midiDevices_.size())) {
        midiStatus_ = "Choose the MIDI port connected to your Kemper.";
        syncControls();
        return;
      }
      midiResult(midi_.connect(midiDevices_[index].identifier, midiChannel_.getSelectedId()),
                 "MIDI port open. Verify the Kemper responds; no hardware readback is available.");
    }
    syncControls();
  }
  void midiResult(const juce::Result& result, const juce::String& success) {
    midiStatus_ = result.wasOk() ? success : result.getErrorMessage();
    if (result.failed())
      midi_.close();
    syncControls();
  }
  void chooseSignal() {
    chooser_ = std::make_unique<juce::FileChooser>(
        "Import the original TONE3000 mono 48 kHz test WAV", signal_, "*.wav");
    const juce::Component::SafePointer<Panel> safe(this);
    chooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& chooser) {
          if (safe == nullptr)
            return;
          const auto selected = chooser.getResult();
          if (selected.existsAsFile()) {
            safe->signal_ = selected;
            safe->canvas_.repaint();
          }
        });
  }
  void chooseExport() {
    chooser_ =
        std::make_unique<juce::FileChooser>("Choose a capture export location", exportFolder_);
    const juce::Component::SafePointer<Panel> safe(this);
    chooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe](const juce::FileChooser& chooser) {
          if (safe == nullptr)
            return;
          const auto selected = chooser.getResult();
          if (selected.isDirectory()) {
            safe->exportFolder_ = selected;
            safe->canvas_.repaint();
          }
        });
  }
  void start(bool levels) {
    if (busy())
      return;
    if (!levels && take_) {
      const juce::Component::SafePointer<Panel> safe(this);
      juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,
                                         "Replace the reviewed take?",
                                         "Starting a new capture replaces the recording in memory. "
                                         "Export it first if you want to keep it.",
                                         "Start new capture", "Keep current take", this,
                                         juce::ModalCallbackFunction::create([safe](int choice) {
                                           if (safe != nullptr && choice != 0)
                                             safe->begin(false);
                                         }));
      return;
    }
    begin(levels);
  }
  void begin(bool levels) {
    error_.clear();
    const auto result = capture_.start(config(), levels);
    if (result.failed()) {
      error_ = result.getErrorMessage();
      syncControls();
      return;
    }
    levelsOnly_ = levels;
    if (!levels) {
      take_.reset();
      ir_.clear();
      alignment_ = {};
      manual_.setToggleState(false, juce::dontSendNotification);
    }
    status_ =
        levels ? "Checking raw input levels. No test signal is playing and no audio is being saved."
               : "Recording raw audio. Keep the routing and Kemper settings unchanged.";
    syncControls();
  }
  void finish() {
    if (!capture_.running())
      return;
    if (levelsOnly_) {
      capture_.stop();
      if (capture_.error().isNotEmpty())
        error_ = capture_.error();
      levelsOnly_ = false;
      status_ = "Level check stopped. Ready to capture.";
    } else {
      auto recording = capture_.takeRecording();
      if (capture_.error().isNotEmpty())
        error_ = capture_.error();
      status_ = "Preparing the capture and checking its return...";
      processing_ = std::async(std::launch::async, [rawTake = std::move(recording)]() mutable {
        return prepare(std::move(rawTake));
      });
    }
    syncControls();
  }
  void save() {
    if (!take_ || busy())
      return;
    error_.clear();
    const auto details = config();
    take_->config.name = details.name;
    take_->config.rig = details.rig;
    take_->config.notes = details.notes;
    const auto take = take_;
    const auto ir = ir_;
    const auto folder = exportFolder_;
    const int offset = juce::roundToInt(offset_.getValue());
    status_ = "Writing WAV files and session notes...";
    exporting_ = std::async(std::launch::async, [take, ir, folder, offset] {
      Exported exported;
      exported.result =
          StandaloneCapture::exportRecording(*take, folder, offset, ir, exported.folder);
      return exported;
    });
    syncControls();
  }
  bool usableAlignment() const {
    return !take_ || take_->config.mode != Mode::namSignal ||
           (alignment_.error.isEmpty() && alignment_.confidence >= kMinimumCorrelation) ||
           manual_.getToggleState();
  }
  void syncControls() {
    const bool working = busy();
    const bool running = capture_.running();
    const bool dryWet = mode() == Mode::dryWet;
    const bool nam = mode() == Mode::namSignal;
    const bool ir = mode() == Mode::impulseResponse;
    for (auto* c : std::initializer_list<juce::Component*>{
             &mode_, &gear_, &dry_, &wet_, &send_, &audioSettings_, &refresh_, &chooseSignal_,
             &getSignal_, &duration_, &sweep_, &irLength_, &trim_, &name_, &rig_, &notes_,
             &chooseExport_})
      c->setEnabled(!working);
    dry_.setVisible(dryWet);
    send_.setVisible(!dryWet);
    trim_.setVisible(!dryWet);
    duration_.setVisible(dryWet);
    sweep_.setVisible(ir);
    irLength_.setVisible(ir);
    chooseSignal_.setVisible(nam);
    getSignal_.setVisible(nam);
    start_.setEnabled(!working);
    checkLevels_.setEnabled(!working);
    stop_.setVisible(running && (levelsOnly_ || dryWet));
    stop_.setButtonText(levelsOnly_ ? "Stop level check" : "Stop & keep take");
    stop_.setName(stop_.getButtonText());
    stop_.setTitle(stop_.getButtonText());
    cancel_.setVisible(running && !levelsOnly_);
    start_.setVisible(!running);
    checkLevels_.setVisible(!running);
    offset_.setVisible(take_ && take_->config.mode == Mode::namSignal);
    offset_.setEnabled(!working);
    manual_.setVisible(
        take_ && take_->config.mode == Mode::namSignal &&
        (alignment_.error.isNotEmpty() || alignment_.confidence < kMinimumCorrelation));
    manual_.setEnabled(!working);
    export_.setEnabled(take_ && !working && usableAlignment());
    reveal_.setEnabled(lastExport_.isDirectory());
    const bool midiEnabled = !working;
    midiPort_.setEnabled(midiEnabled && !midi_.isOpen());
    midiChannel_.setEnabled(midiEnabled && !midi_.isOpen());
    connect_.setEnabled(midiEnabled);
    connect_.setButtonText(midi_.isOpen() ? "Disconnect MIDI" : "Connect MIDI");
    connect_.setName(connect_.getButtonText());
    connect_.setTitle(connect_.getButtonText());
    for (auto* c :
         std::initializer_list<juce::Component*>{&program_, &loadProgram_, &gain_, &bass_, &middle_,
                                                 &treble_, &presence_, &cabinet_, &apply_})
      c->setEnabled(midiEnabled && midi_.isOpen());
    statusLabel_.setText(error_.isNotEmpty() ? error_ : status_, juce::dontSendNotification);
    statusLabel_.setColour(juce::Label::textColourId,
                           error_.isNotEmpty() ? theme::kBrandRed : theme::kWhite);
    layout();
    canvas_.repaint();
  }
  void timerCallback() override {
    if (capture_.running()) {
      if (capture_.error().isNotEmpty()) {
        error_ = capture_.error();
        capture_.stop();
        if (capture_.error().isNotEmpty())
          error_ = capture_.error();
        levelsOnly_ = false;
        status_ = "Capture stopped because the audio device changed or failed.";
        syncControls();
      } else if (!levelsOnly_ && capture_.finished()) {
        finish();
      }
      canvas_.repaint();
    }
    if (processing_.valid() &&
        processing_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      try {
        auto prepared = processing_.get();
        if (prepared.error.isNotEmpty()) {
          error_ = prepared.error;
          status_ = "Capture needs attention. Correct the routing or levels and try again.";
        } else {
          take_ = std::move(prepared.recording);
          ir_ = std::move(prepared.ir);
          alignment_ = prepared.alignment;
          const auto maximum = static_cast<double>(
              take_->wet.size() - std::min(take_->wet.size(), take_->reference.size()));
          offset_.setRange(0, std::max(1.0, maximum), 1);
          offset_.setValue(alignment_.offset, juce::dontSendNotification);
          status_ = "Capture ready. Review the take, then export the files.";
        }
      } catch (const std::exception& e) {
        error_ = "Capture processing failed: " + juce::String(e.what());
      }
      syncControls();
    }
    if (exporting_.valid() &&
        exporting_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      try {
        auto exported = exporting_.get();
        if (exported.result.failed())
          error_ = exported.result.getErrorMessage();
        else {
          lastExport_ = exported.folder;
          status_ = "Export complete. Open the folder and upload the WAV files to TONE3000.";
        }
      } catch (const std::exception& e) {
        error_ = "Export failed: " + juce::String(e.what());
      }
      syncControls();
    }
  }

  void layout() {
    const int width = canvas_.getWidth();
    const int leftWidth = juce::roundToInt((width - kMargin * 2 - kGap) * 0.57);
    const int rightX = kMargin + leftWidth + kGap;
    const int rightWidth = width - kMargin - rightX;
    routingBox_ = {kMargin, 124, leftWidth, 274};
    methodBox_ = {rightX, 124, rightWidth, 274};
    liveBox_ = {kMargin, 414, leftWidth, 210};
    sessionBox_ = {kMargin, 640, leftWidth, 306};
    midiBox_ = {rightX, 414, rightWidth, 716};
    reviewBox_ = {kMargin, 962, leftWidth, 306};
    auto routing = routingBox_.reduced(18).withTrimmedTop(48);
    gear_.setBounds(routing.removeFromTop(32));
    routing.removeFromTop(28);
    dry_.setBounds(routing.removeFromTop(32));
    routing.removeFromTop(28);
    wet_.setBounds(routing.removeFromTop(32));
    routing.removeFromTop(10);
    auto buttons = routing.removeFromTop(30);
    audioSettings_.setBounds(buttons.removeFromLeft(126));
    buttons.removeFromLeft(8);
    refresh_.setBounds(buttons.removeFromLeft(142));

    const int methodX = methodBox_.getX() + 18;
    const int methodWidth = methodBox_.getWidth() - 36;
    const int methodY = methodBox_.getY();
    mode_.setBounds(methodX, methodY + 66, methodWidth, 30);
    duration_.setBounds(methodX, methodY + 114, methodWidth, 30);
    auto importRow = juce::Rectangle<int>(methodX, methodY + 108, methodWidth, 30);
    chooseSignal_.setBounds(
        importRow.removeFromLeft(juce::jmin(190, importRow.getWidth() * 2 / 3)));
    importRow.removeFromLeft(8);
    getSignal_.setBounds(importRow);
    sweep_.setBounds(methodX + 54, methodY + 106, methodWidth - 54, 28);
    irLength_.setBounds(methodX + 54, methodY + 139, methodWidth - 54, 28);
    send_.setBounds(methodX, methodY + 182, methodWidth, 28);
    trim_.setBounds(methodX, methodY + 225, methodWidth, 28);

    auto live = liveBox_.reduced(18).withTrimmedTop(132);
    auto actions = live.removeFromTop(38);
    const int half = (actions.getWidth() - 10) / 2;
    start_.setBounds(actions.removeFromLeft(half));
    actions.removeFromLeft(10);
    checkLevels_.setBounds(actions);
    stop_.setBounds(start_.getBounds());
    cancel_.setBounds(checkLevels_.getBounds());

    auto session = sessionBox_.reduced(18).withTrimmedTop(56);
    name_.setBounds(session.removeFromTop(31));
    session.removeFromTop(24);
    rig_.setBounds(session.removeFromTop(31));
    session.removeFromTop(24);
    notes_.setBounds(session.removeFromTop(64));
    session.removeFromTop(12);
    chooseExport_.setBounds(session.removeFromTop(30).removeFromLeft(150));

    auto midi = midiBox_.reduced(18).withTrimmedTop(64);
    midiPort_.setBounds(midi.removeFromTop(32));
    midi.removeFromTop(10);
    auto connectRow = midi.removeFromTop(32);
    midiChannel_.setBounds(connectRow.removeFromLeft(juce::jmin(130, connectRow.getWidth() / 2)));
    connectRow.removeFromLeft(8);
    connect_.setBounds(connectRow);
    midi.removeFromTop(79);
    auto program = midi.removeFromTop(32);
    program_.setBounds(program.removeFromLeft(juce::jmax(120, program.getWidth() - 122)));
    program.removeFromLeft(8);
    loadProgram_.setBounds(program);
    midi.removeFromTop(30);
    for (auto* slider : {&gain_, &bass_, &middle_, &treble_, &presence_}) {
      slider->setBounds(midi.removeFromTop(36));
      midi.removeFromTop(18);
    }
    cabinet_.setBounds(midi.removeFromTop(30));
    midi.removeFromTop(8);
    apply_.setBounds(midi.removeFromTop(38));

    auto review = reviewBox_.reduced(18).withTrimmedTop(94);
    offset_.setBounds(review.removeFromTop(30));
    manual_.setBounds(review.removeFromTop(28));
    review.removeFromTop(12);
    export_.setBounds(review.removeFromTop(36));
    review.removeFromTop(10);
    auto links = review.removeFromTop(32);
    reveal_.setBounds(links.removeFromLeft((links.getWidth() - 8) / 2));
    links.removeFromLeft(8);
    training_.setBounds(links);
  }
  void text(juce::Graphics& g,
            const juce::String& value,
            juce::Rectangle<int> area,
            float size = 13,
            juce::Colour colour = theme::kMuted,
            bool bold = false,
            int lines = 1) {
    g.setColour(colour);
    g.setFont(Fonts::sans(size, bold));
    g.drawFittedText(value, area, juce::Justification::centredLeft, lines, 0.85f);
  }
  void card(juce::Graphics& g,
            juce::Rectangle<int> box,
            const juce::String& eyebrow,
            const juce::String& heading) {
    g.setColour(theme::kSurface);
    g.fillRoundedRectangle(box.toFloat(), 10);
    g.setColour(theme::kBorder);
    g.drawRoundedRectangle(box.toFloat().reduced(0.5f), 10, 1);
    text(g, eyebrow, juce::Rectangle<int>(box.getX() + 18, box.getY() + 8, box.getWidth() - 36, 12),
         10, theme::kBrandYellow, true);
    text(g, heading, {box.getX() + 18, box.getY() + 24, box.getWidth() - 36, 22}, 17, theme::kWhite,
         true);
  }
  void meter(juce::Graphics& g,
             const juce::String& label,
             float amplitude,
             juce::Rectangle<int> area) {
    const float db = juce::Decibels::gainToDecibels(amplitude, -120.0f);
    auto heading = area.removeFromTop(20);
    text(g, label, heading, 10, theme::kMuted, true);
    const auto reading = amplitude >= 0.99997f ? "CLIP"
                         : db < -60            ? juce::String("-inf dBFS")
                                               : juce::String(db, 1) + " dBFS";
    g.setColour(amplitude >= 0.99997f ? theme::kBrandRed : theme::kMuted);
    g.setFont(Fonts::mono(11));
    g.drawText(reading, heading, juce::Justification::centredRight);
    area = area.withHeight(8);
    g.setColour(theme::kBlack);
    g.fillRoundedRectangle(area.toFloat(), 3);
    const auto filled =
        area.toFloat().withWidth(area.getWidth() * juce::jlimit(0.0f, 1.0f, (db + 60) / 60));
    g.setColour(db > -1 ? theme::kBrandRed : theme::kBrandYellow);
    g.fillRoundedRectangle(filled, 3);
  }
  void paintCanvas(juce::Graphics& g) {
    g.fillAll(theme::kBlack);
    const auto width = canvas_.getWidth();
    text(g, "KEMPER  /  LOCAL CAPTURE", {kMargin, 16, width - 48, 20}, 11, theme::kBrandYellow,
         true);
    text(g, "Capture your Kemper.", {kMargin, 42, width - 48, 38}, 28, theme::kWhite, true);
    text(g, describe(mode()), {kMargin, 88, width - 48, 28}, 14, theme::kMuted, false, 2);
    card(g, routingBox_, "01  CONNECT", "Audio & gear");
    card(g, methodBox_, "02  CHOOSE", "Capture method");
    card(g, liveBox_, "03  RECORD",
         capture_.running() ? (levelsOnly_ ? "Checking levels" : "Recording") : "Capture desk");
    card(g, sessionBox_, "04  NAME", "Session details");
    card(g, midiBox_, "OPTIONAL  /  DIRECT HARDWARE CONTROL", "Kemper MIDI");
    card(g, reviewBox_, "05  EXPORT", take_ ? "Reviewed capture" : "Your next take");

    text(g, "Kemper model",
         {routingBox_.getX() + 18, routingBox_.getY() + 48, routingBox_.getWidth() - 36, 16}, 11);
    const int routingX = routingBox_.getX() + 18;
    if (mode() == Mode::dryWet) {
      text(g, "Dry DI input", {routingX, routingBox_.getY() + 108, routingBox_.getWidth() - 36, 16},
           11);
    } else {
      text(g, "The raw processed return is recorded without the plugin chain.",
           {routingX, routingBox_.getY() + 105, routingBox_.getWidth() - 36, 38}, 12, theme::kMuted,
           false, 2);
    }
    text(g, "Kemper return input",
         {routingX, routingBox_.getY() + 168, routingBox_.getWidth() - 36, 16}, 11);
    text(g, deviceDescription_,
         {routingX, routingBox_.getBottom() - 13, routingBox_.getWidth() - 36, 12}, 9);

    const int methodX = methodBox_.getX() + 18;
    if (mode() == Mode::dryWet) {
      text(g, "Performance length",
           {methodX, methodBox_.getY() + 98, methodBox_.getWidth() - 36, 16}, 11);
      text(g,
           "Play varied notes, chords and dynamics for 2-3 minutes. Split the guitar before the "
           "Kemper, or select its USB DI and processed-return channels.",
           {methodX, methodBox_.getY() + 160, methodBox_.getWidth() - 36, 80}, 12, theme::kMuted,
           false, 4);
    } else {
      text(g,
           mode() == Mode::namSignal
               ? (signal_.existsAsFile() ? signal_.getFileName()
                                         : "Use the original mono 48 kHz WAV from TONE3000.")
               : "",
           {methodX, methodBox_.getY() + 141, methodBox_.getWidth() - 36, 24}, 11, theme::kMuted,
           false, 2);
      text(g, "Send only to Kemper input / reamp path",
           {methodX, methodBox_.getY() + 166, methodBox_.getWidth() - 36, 16}, 11);
      text(g, "Output trim", {methodX, methodBox_.getY() + 209, methodBox_.getWidth() - 36, 16},
           11);
      if (mode() == Mode::impulseResponse) {
        text(g, "Sweep", {methodX, methodBox_.getY() + 106, 54, 28}, 11);
        text(g, "IR length", {methodX, methodBox_.getY() + 139, 54, 28}, 11);
      }
      if (mode() == Mode::impulseResponse)
        text(g, "Cabinet IR: bypass amp and moving/time-based FX.",
             {methodX, methodBox_.getBottom() - 17, methodBox_.getWidth() - 36, 13}, 10);
    }

    const float dryPeak = capture_.running() ? capture_.dryPeak() : 0;
    const float wetPeak = capture_.running() ? capture_.wetPeak() : 0;
    auto meters = liveBox_.reduced(18).withTrimmedTop(38);
    const int meterHalf = (meters.getWidth() - 16) / 2;
    meter(g, mode() == Mode::dryWet ? "DRY DI" : "INPUT", dryPeak,
          meters.removeFromLeft(meterHalf).withHeight(30));
    meters.removeFromLeft(16);
    meter(g, "KEMPER RETURN", wetPeak, meters.withHeight(30));
    const auto elapsed = capture_.elapsed();
    const auto progress =
        capture_.running() ? juce::String(static_cast<int>(elapsed) / 60).paddedLeft('0', 2) + ":" +
                                 juce::String(static_cast<int>(elapsed) % 60).paddedLeft('0', 2)
                           : "Ready";
    text(g,
         capture_.clipped() && capture_.running()
             ? progress + "  |  CLIPPING - lower the input level."
             : progress + "  |  Aim for peaks around -12 to -8 dBFS; avoid clipping.",
         {liveBox_.getX() + 18, liveBox_.getY() + 96, liveBox_.getWidth() - 36, 30}, 11,
         capture_.clipped() && capture_.running() ? theme::kBrandRed : theme::kMuted, false, 2);
    if (capture_.running() && !levelsOnly_) {
      const juce::Rectangle<float> bar(liveBox_.getX() + 18.0f, liveBox_.getBottom() - 22.0f,
                                       liveBox_.getWidth() - 36.0f, 5);
      g.setColour(theme::kBlack);
      g.fillRoundedRectangle(bar, 2);
      g.setColour(theme::kBrandYellow);
      g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * capture_.progress()), 2);
    }

    const int sessionX = sessionBox_.getX() + 18;
    text(g, "Capture name", {sessionX, sessionBox_.getY() + 48, sessionBox_.getWidth() - 36, 18},
         11);
    text(g, "Current rig / profile name",
         {sessionX, sessionBox_.getY() + 104, sessionBox_.getWidth() - 36, 18}, 11);
    text(g, "Notes", {sessionX, sessionBox_.getY() + 160, sessionBox_.getWidth() - 36, 18}, 11);
    text(g, exportFolder_.getFullPathName(),
         {sessionX, sessionBox_.getBottom() - 12, sessionBox_.getWidth() - 36, 12}, 9);

    const int midiX = midiBox_.getX() + 18;
    text(g, "MIDI output destination", {midiX, midiBox_.getY() + 55, midiBox_.getWidth() - 36, 18},
         11);
    text(g, midiStatus_, {midiX, midiBox_.getY() + 168, midiBox_.getWidth() - 36, 40}, 12,
         theme::kMuted, false, 4);
    text(g, "Program (use Kemper's MIDI assignments)",
         {midiX, midiBox_.getY() + 207, midiBox_.getWidth() - 36, 22}, 11);
    int knobY = midiBox_.getY() + 270;
    for (const auto* label : {"Generic Gain", "Bass", "Middle", "Treble", "Presence"}) {
      text(g, label, {midiX, knobY, midiBox_.getWidth() - 36, 18}, 11);
      knobY += 54;
    }
    text(g,
         "These are requested values, not hardware readback. Apply sends the values shown. No rig "
         "is stored automatically; Rig Manager is optional.",
         {midiX, midiBox_.getBottom() - 68, midiBox_.getWidth() - 36, 54}, 12, theme::kMuted, false,
         4);

    const int reviewX = reviewBox_.getX() + 18;
    juce::String review;
    if (!take_)
      review =
          "Your recording will appear here. WAV files stay local until you upload them yourself.";
    else {
      review = juce::String(modeName(take_->config.mode)) + "  |  " +
               juce::String(take_->wet.size() / static_cast<double>(sampleRate), 1) +
               " s  |  native interface rate -> 48 kHz / mono 24-bit export";
      if (take_->clipped)
        review +=
            "\nClipping detected. Lower your input level and recapture for an accurate model.";
      if (take_->config.mode == Mode::namSignal) {
        review +=
            "\nReturn alignment confidence: " + juce::String(alignment_.confidence * 100, 0) + "%.";
        if (!usableAlignment())
          review += " Check and confirm a manual sample offset before export.";
      } else if (take_->config.mode == Mode::impulseResponse)
        review +=
            "\nIR arrival delay removed; peak normalized to 0.9. Weak early arrivals may be "
            "omitted.";
    }
    text(g, review, {reviewX, reviewBox_.getY() + 58, reviewBox_.getWidth() - 36, 44}, 12,
         take_ && take_->clipped ? theme::kBrandRed : theme::kMuted, false, 3);
    const juce::String transfer =
        "Dry/wet: upload DRY.wav + WET.wav. NAM test signal: upload WET.wav. TONE3000 trains the "
        "NAM; load the saved tone in its app with the same account. IR.wav is already finished and "
        "needs no NAM training.";
    text(g, transfer, {kMargin, 1382, width - kMargin * 2, 45}, 12, theme::kMuted, false, 3);
    text(g, error_.isNotEmpty() ? error_ : status_, {kMargin, 1284, width - kMargin * 2, 40}, 13,
         error_.isNotEmpty() ? theme::kBrandRed : theme::kWhite, false, 3);
    text(g,
         "TONE3000's public sharing rules exclude captures of current Kemper modelers. No upload "
         "or training happens automatically.",
         {kMargin, 1334, width - kMargin * 2, 35}, 11, theme::kMuted, false, 2);
  }

  std::function<void()> openAudioSettings_;
  juce::Viewport viewport_;
  juce::Label statusLabel_;
  Canvas canvas_;
  Fonts::Hold fontsHold_;
  StandaloneCapture capture_;
  kemper::Controller midi_;
  juce::Array<juce::MidiDeviceInfo> midiDevices_;
  juce::ComboBox mode_, gear_, dry_, wet_, send_, midiPort_, midiChannel_;
  juce::Slider duration_, sweep_, irLength_, trim_, offset_, program_, gain_, bass_, middle_,
      treble_, presence_;
  juce::TextEditor name_, rig_, notes_;
  juce::TextButton audioSettings_, refresh_, chooseSignal_, getSignal_, start_, stop_, cancel_,
      checkLevels_, chooseExport_, export_, reveal_, training_, connect_, loadProgram_, apply_;
  juce::ToggleButton cabinet_, manual_;
  juce::File signal_, exportFolder_, lastExport_;
  std::unique_ptr<juce::FileChooser> chooser_;
  std::shared_ptr<Recording> take_;
  std::vector<float> ir_;
  Alignment alignment_;
  std::future<Prepared> processing_;
  std::future<Exported> exporting_;
  juce::String status_, error_, midiStatus_, deviceDescription_;
  bool levelsOnly_ = false;
  juce::Rectangle<int> routingBox_, methodBox_, liveBox_, sessionBox_, midiBox_, reviewBox_;
};

CaptureWindow::CaptureWindow(juce::Component* centreAround,
                             std::function<void()> onOpenAudioSettings)
    : DocumentWindow("TONE3000 - Capture gear", theme::kBlack, DocumentWindow::allButtons),
      panel_(std::make_unique<Panel>(std::move(onOpenAudioSettings))) {
  setUsingNativeTitleBar(true);
  setResizable(true, true);
  setResizeLimits(900, 620, 1600, 1300);
  setContentNonOwned(panel_.get(), true);
  if (centreAround != nullptr)
    centreAroundComponent(centreAround, 1020, 800);
  else
    centreWithSize(1020, 800);
  setVisible(true);
}
CaptureWindow::~CaptureWindow() {
  clearContentComponent();
}
void CaptureWindow::closeButtonPressed() {
  panel_->prepareToHide();
  setVisible(false);
}
bool CaptureWindow::busy() const {
  return panel_->busy();
}

}  // namespace t3k::capture
