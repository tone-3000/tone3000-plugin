#include "RoutingWindow.h"

#include "StandaloneRouting.h"
#include "KemperRouting.h"
#include "core/Fonts.h"
#include "core/Theme.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace t3k::routing {
namespace {
namespace theme = t3k::ui::theme;
namespace kemper = t3k::capture::kemper;
using t3k::ui::Fonts;
constexpr int kPad = 24, kGap = 16;

int channel(const juce::ComboBox& c) { return c.getSelectedId() - 2; }
void setChannel(juce::ComboBox& c, int index) { c.setSelectedId(index + 2, juce::dontSendNotification); }

void drawText(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> box,
              float size = 13, juce::Colour colour = theme::kMuted, bool bold = false,
              int lines = 1) {
  g.setColour(colour);
  g.setFont(Fonts::sans(size, bold));
  g.drawFittedText(text, box, juce::Justification::centredLeft, lines, 0.85f);
}
void drawCard(juce::Graphics& g, juce::Rectangle<int> box, const juce::String& tag,
              const juce::String& title) {
  g.setColour(theme::kSurface);
  g.fillRoundedRectangle(box.toFloat(), 10);
  g.setColour(theme::kBorder);
  g.drawRoundedRectangle(box.toFloat().reduced(0.5f), 10, 1);
  drawText(g, tag, {box.getX() + 18, box.getY() + 10, box.getWidth() - 36, 15},
           10, theme::kBrandYellow, true);
  drawText(g, title, {box.getX() + 18, box.getY() + 29, box.getWidth() - 36, 24},
           18, theme::kWhite, true);
}
void drawMeter(juce::Graphics& g, const juce::String& name, float peak,
               juce::Rectangle<int> box) {
  const auto db = juce::Decibels::gainToDecibels(peak, -120.0f);
  auto text = box.removeFromTop(18);
  drawText(g, name, text, 10, theme::kMuted, true);
  g.setColour(peak >= 1 ? theme::kBrandRed : theme::kMuted);
  g.setFont(Fonts::mono(10));
  g.drawText(db < -60 ? juce::String("-inf") : juce::String(db, 1) + " dBFS", text,
             juce::Justification::centredRight);
  const auto bar = box.toFloat().withHeight(7);
  g.setColour(theme::kBlack);
  g.fillRoundedRectangle(bar, 2);
  g.setColour(peak >= 1 ? theme::kBrandRed : theme::kBrandYellow);
  g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, (db + 60) / 60)), 2);
}
}  // namespace

class RoutingWindow::Panel final : public juce::Component, private juce::Timer {
public:
  Panel(StandaloneRouting& router, std::function<void()> openSettings)
      : router_(router), draft_(router.config()), openSettings_(std::move(openSettings)),
        software_(*this, false), hardware_(*this, true) {
    setOpaque(true);
    viewport_.setViewedComponent(&software_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    addButton(softwareTab_, "TONE3000 routing", *this, [this] { setHardware(false); });
    addButton(hardwareTab_, "Kemper hardware", *this, [this] { setHardware(true); });
    addButton(apply_, "Bind & enable routing", *this, [this] { applySoftware(); });
    addButton(stop_, "Turn routing off", *this, [this] {
      router_.stop(); status_ = router_.status(); error_.clear(); updateStatus();
    });
    addButton(sendHardware_, "Apply selected MIDI values", *this, [this] { applyHardware(); });
    apply_.setColour(juce::TextButton::buttonColourId, theme::kBrandYellow);
    apply_.setColour(juce::TextButton::textColourOffId, theme::kBlack);
    statusLabel_.setName("Routing status");
    statusLabel_.setTitle("Routing status");
    statusLabel_.setFont(Fonts::sans(12));
    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel_);

    addButton(audioSettings_, "Audio device settings", software_, [this] { openAudioSettings(); });
    addButton(refresh_, "Refresh channels", software_, [this] { refreshChannels(); });
    addButton(reload_, "Reload applied values", software_, [this] { draft_ = router_.config(); loadDraft(); });
    addCombo(inputSource_, "Software input source alias", software_);
    for (int i = 0; i < 4; ++i) inputSource_.addItem(inputSourceName(static_cast<InputSource>(i)), i + 1);
    const char* aliases[] = {"Instrument input channel", "Alternative input channel", "Return reamp input channel", "Digital reamp input channel"};
    for (int i = 0; i < 4; ++i) addCombo(aliases_[static_cast<size_t>(i)], aliases[i], software_);
    addCombo(stereoRight_, "Optional stereo input right channel", software_);
    addCombo(di_, "Raw DI input channel", software_);
    addCombo(returnL_, "Return input left", software_);
    addCombo(returnR_, "Return input right", software_);
    addCombo(auxL_, "Auxiliary input left", software_);
    addCombo(auxR_, "Auxiliary input right", software_);
    addSlider(inputGain_, "Software input gain in dB", software_, -24, 24, 0.1, " dB");
    addSlider(reamp_, "Software reamp sensitivity in dB", software_, -24, 24, 0.1, " dB");
    addSlider(master_, "Linked software master volume in dB", software_, -60, 12, 0.1, " dB");
    const char* auxNames[] = {"Auxiliary mix to Main", "Auxiliary mix to Monitor", "Auxiliary mix to Phones"};
    for (int i = 0; i < 3; ++i) addSlider(aux_[static_cast<size_t>(i)], auxNames[i], software_, 0, 1, 0.01, {});

    addCombo(port_, "Logical output destination", software_);
    for (size_t i = 0; i < portCount; ++i) port_.addItem(portName(static_cast<Port>(i)), static_cast<int>(i) + 1);
    port_.setSelectedId(1, juce::dontSendNotification);
    port_.onChange = [this] {
      savePort(); selectedPort_ = juce::jlimit(0, static_cast<int>(portCount) - 1, port_.getSelectedId() - 1);
      loadPort();
    };
    addCombo(source_, "Output signal source", software_);
    for (size_t i = 0; i < sources().size(); ++i) source_.addItem(sourceName(sources()[i]), static_cast<int>(i) + 1);
    addCombo(destinationL_, "Physical output left or mono channel", software_);
    addCombo(destinationR_, "Physical output right channel", software_);
    addSlider(volume_, "Destination volume in dB", software_, -60, 12, 0.1, " dB");
    addToggle(linkMaster_, "Link to software Master Volume", software_);
    addToggle(pad_, "Output pad -12 dB", software_);
    addToggle(stereo_, "Stereo destination", software_);
    addToggle(cabOff_, "Cabinet off (separate block)", software_);
    const char* eqNames[] = {"Output Bass", "Output Middle", "Output Treble", "Output Presence"};
    for (int i = 0; i < 4; ++i) addSlider(eq_[static_cast<size_t>(i)], eqNames[i], software_, -12, 12, 0.1, " dB");
    addSlider(lowCut_, "Output low cut, zero means off", software_, 0, 20000, 1, " Hz");
    addSlider(highCut_, "Output high cut, zero means off", software_, 0, 20000, 1, " Hz");
    lowCut_.setSkewFactorFromMidPoint(500); highCut_.setSkewFactorFromMidPoint(5000);
    for (auto* cut : {&lowCut_, &highCut_}) cut->onValueChange = [cut] {
      if (cut->getValue() > 0 && cut->getValue() < 20) cut->setValue(20, juce::dontSendNotification);
    };
    const char* stageNames[] = {"Stack ends after", "Mod ends after", "Wet effects start at", "Separate cabinet block"};
    for (size_t lane = 0; lane < 2; ++lane)
      for (size_t stage = 0; stage < 4; ++stage)
        addCombo(stage_[lane][stage], juce::String(lane == 0 ? "Left lane " : "Right lane ") + stageNames[stage], software_);

    addCombo(model_, "Connected Kemper model", hardware_);
    model_.addItem("Head / PowerHead", 1); model_.addItem("Rack / PowerRack", 2);
    model_.addItem("Stage", 3); model_.addItem("Player", 4);
    model_.setSelectedId(1, juce::dontSendNotification);
    model_.onChange = [this] { updateHardwareAvailability(); };
    addCombo(playerLevel_, "Kemper Player feature level", hardware_);
    for (int i = 1; i <= 3; ++i) playerLevel_.addItem("Player Level " + juce::String(i), i);
    playerLevel_.setSelectedId(1, juce::dontSendNotification);
    playerLevel_.onChange = [this] { updateHardwareAvailability(); };
    addCombo(midiOut_, "Kemper MIDI output destination", hardware_);
    addCombo(midiChannel_, "Kemper global MIDI channel", hardware_);
    for (int i = 1; i <= 16; ++i) midiChannel_.addItem("Channel " + juce::String(i), i);
    midiChannel_.setSelectedId(1, juce::dontSendNotification);
    addButton(connect_, "Connect output", hardware_, [this] { connectMidi(); });
    addButton(refreshMidi_, "Refresh MIDI ports", hardware_, [this] { refreshMidi(); });
    addCombo(midiIn_, "Optional Kemper MIDI reply input", hardware_);
    addToggle(listen_, "Listen for actual parameter replies (opt-in)", hardware_);
    listen_.onClick = [this] {
      if (!listen_.getToggleState()) { midi_.disableInput(); updateStatus(); return; }
      const int index = midiIn_.getSelectedId() - 1;
      if (!juce::isPositiveAndBelow(index, midiInputs_.size())) {
        listen_.setToggleState(false, juce::dontSendNotification); error_ = "Choose the MIDI input carrying Kemper replies first.";
      } else {
        const auto result = midi_.enableInput(midiInputs_[index].identifier);
        if (result.failed()) { error_ = result.getErrorMessage(); listen_.setToggleState(false, juce::dontSendNotification); }
        else { error_.clear(); status_ = "Reply listener enabled. Nothing has been requested or sent automatically."; }
      }
      updateStatus();
    };
    addCombo(readParameter_, "Parameter to request from Kemper", hardware_);
    addCombo(product_, "Kemper SysEx product header", hardware_);
    product_.addItem("Product 0 (MIDI header table)", 1);
    product_.addItem("Product 2 (MIDI examples)", 2);
    product_.setSelectedId(1, juce::dontSendNotification);
    addButton(read_, "Request selected parameter", hardware_, [this] { requestReadback(); });
    int parameterIndex = 1;
    for (const auto& spec : kemper::ioParameters()) {
      auto row = std::make_unique<HardwareRow>(*this, spec);
      hardware_.addAndMakeVisible(*row);
      readParameter_.addItem(spec.section + " / " + spec.label, parameterIndex++);
      rows_.push_back(std::move(row));
    }
    readParameter_.setSelectedId(1, juce::dontSendNotification);
    status_ = "Software changes are staged until Bind & enable. Check channel mappings before binding a different interface. Opening this window sends no MIDI and enables no routes.";
    refreshChannels(); refreshMidi(); loadDraft(); updateHardwareAvailability();
    setHardware(false);
    startTimerHz(15);
  }
  ~Panel() override {
    stopTimer();
    viewport_.setViewedComponent(nullptr, false);
    // Software routing belongs to the standalone controller and keeps running
    // when this editor is hidden. MIDI reception stops with this view.
  }
  bool busy() const { return router_.busy(); }
  void refreshWhenShown() {
    const bool changed = draft_.inputDevice != router_.currentInputDevice() ||
                         draft_.outputDevice != router_.currentOutputDevice();
    refreshChannels();
    if (changed && !router_.busy()) {
      error_.clear();
      status_ = "The audio interface changed. Review each input/output channel mapping before Bind & enable.";
      updateStatus();
    }
  }
  void paint(juce::Graphics& g) override {
    g.fillAll(theme::kBlack);
    drawText(g, "INPUTS & OUTPUTS", {24, 12, getWidth() - 48, 20}, 11, theme::kBrandYellow, true);
    drawText(g, "Route your sound.", {24, 34, getWidth() - 48, 30}, 25, theme::kWhite, true);
    g.setColour(theme::kBorder);
    g.drawHorizontalLine(getHeight() - 88, 0.0f, static_cast<float>(getWidth()));
  }
  void resized() override {
    softwareTab_.setBounds(24, 76, 185, 32);
    hardwareTab_.setBounds(217, 76, 170, 32);
    viewport_.setBounds(0, 120, getWidth(), juce::jmax(80, getHeight() - 208));
    const int width = juce::jmax(850, getWidth() - viewport_.getScrollBarThickness());
    software_.setSize(width, 1760);
    hardware_.setSize(width, hardwareHeight());
    apply_.setBounds(getWidth() - 404, getHeight() - 78, 230, 34);
    stop_.setBounds(getWidth() - 164, getHeight() - 78, 140, 34);
    sendHardware_.setBounds(getWidth() - 284, getHeight() - 78, 260, 34);
    statusLabel_.setBounds(16, getHeight() - 40, getWidth() - 32, 36);
    layoutSoftware(); layoutHardware();
  }

private:
  struct Canvas final : juce::Component {
    Canvas(Panel& owner, bool hardware) : owner_(owner), hardware_(hardware) { setOpaque(true); }
    void paint(juce::Graphics& g) override {
      if (hardware_) owner_.paintHardware(g); else owner_.paintSoftware(g);
    }
    Panel& owner_;
    bool hardware_;
  };

  class HardwareRow final : public juce::Component {
  public:
    HardwareRow(Panel& owner, const kemper::IoParameterSpec& spec) : owner_(owner), spec_(spec) {
      const auto fullName = spec.section + " " + spec.label;
      setName(fullName);
      owner_.addToggle(armed_, "Send " + fullName, *this);
      armed_.setButtonText("Send");
      armed_.onClick = [this] { owner_.updateButtons(); repaint(); };
      if (spec.kind == kemper::IoParameterSpec::Kind::continuous) {
        owner_.addSlider(value_, "Requested raw " + fullName, *this, 0, 16383, 1, " raw");
        value_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 102, 25);
        value_.onValueChange = [this] { arm(); };
      } else if (spec.kind == kemper::IoParameterSpec::Kind::toggle) {
        owner_.addToggle(toggle_, "Requested " + fullName, *this);
        toggle_.setButtonText("Enabled"); toggle_.onClick = [this] { arm(); };
      } else {
        owner_.addCombo(choice_, "Requested " + fullName, *this);
        for (int i = 0; i < spec.choices.size(); ++i) choice_.addItem(spec.choices[i], i + 1);
        choice_.setSelectedId(1, juce::dontSendNotification);
        choice_.onChange = [this] { arm(); };
      }
    }
    int rawValue() const {
      if (spec_.kind == kemper::IoParameterSpec::Kind::continuous) return juce::roundToInt(value_.getValue());
      if (spec_.kind == kemper::IoParameterSpec::Kind::toggle) return toggle_.getToggleState() ? 1 : 0;
      return juce::jmax(0, choice_.getSelectedId() - 1);
    }
    bool armed() const { return available_ && armed_.getToggleState(); }
    void unarm() { armed_.setToggleState(false, juce::dontSendNotification); repaint(); }
    void clearReply() { receivedAt_ = 0; receivedRaw_ = 0; repaint(); }
    const kemper::IoParameterSpec& spec() const { return spec_; }
    void updateAvailability() {
      available_ = owner_.supports(spec_);
      for (auto* control : std::initializer_list<juce::Component*>{&armed_, &value_, &toggle_, &choice_})
        control->setEnabled(available_ && !owner_.router_.busy());
      if (!available_) unarm();
      repaint();
    }
    void receive() {
      const auto reply = owner_.midi_.cachedParameter(spec_.page, spec_.index);
      if (!reply) return;
      if (reply->receivedAtMilliseconds != receivedAt_) {
        receivedAt_ = reply->receivedAtMilliseconds;
        receivedRaw_ = reply->value.value14;
        if (!armed_.getToggleState()) {
          if (spec_.kind == kemper::IoParameterSpec::Kind::continuous)
            value_.setValue(receivedRaw_, juce::dontSendNotification);
          else if (spec_.kind == kemper::IoParameterSpec::Kind::toggle)
            toggle_.setToggleState(receivedRaw_ != 0, juce::dontSendNotification);
          else if (receivedRaw_ >= 0 && receivedRaw_ < spec_.choices.size())
            choice_.setSelectedId(receivedRaw_ + 1, juce::dontSendNotification);
        }
      }
      repaint();
    }
    void resized() override {
      auto row = getLocalBounds().withHeight(30);
      const int labelWidth = juce::jlimit(210, 330, getWidth() * 35 / 100);
      row.removeFromLeft(labelWidth);
      armed_.setBounds(row.removeFromRight(74)); row.removeFromRight(12);
      value_.setBounds(row); toggle_.setBounds(row); choice_.setBounds(row);
    }
    void paint(juce::Graphics& g) override {
      const auto colour = available_ ? theme::kWhite : theme::kSubtle;
      drawText(g, spec_.label, {0, 2, juce::jlimit(210, 330, getWidth() * 35 / 100) - 12, 26}, 13, colour, true);
      juce::String note = spec_.supportNotes;
      if (!available_) note = "Unavailable for the selected model/feature level. " + note;
      else if (receivedAt_ > 0) {
        const auto age = juce::jmax(0.0, (juce::Time::getMillisecondCounterHiRes() - receivedAt_) / 1000.0);
        note = "Actual reply: " + juce::String(receivedRaw_) + " raw, " + juce::String(age, 0) + " s ago. " + note;
      } else note = "Requested value; no hardware readback. " + note;
      drawText(g, note, {0, 33, getWidth(), 27}, 11, theme::kMuted, false, 2);
      g.setColour(theme::kBorder.withAlpha(0.4f));
      g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
    }
  private:
    void arm() { armed_.setToggleState(true, juce::dontSendNotification); owner_.updateButtons(); repaint(); }
    Panel& owner_;
    const kemper::IoParameterSpec& spec_;
    juce::ToggleButton armed_, toggle_;
    juce::Slider value_;
    juce::ComboBox choice_;
    bool available_ = true;
    double receivedAt_ = 0;
    int receivedRaw_ = 0;
  };

  template <typename Component>
  void initialise(Component& c, const juce::String& name, juce::Component& parent) {
    c.setName(name); c.setTitle(name);
    c.setWantsKeyboardFocus(true); c.setMouseClickGrabsKeyboardFocus(false);
    parent.addAndMakeVisible(c);
  }
  void addCombo(juce::ComboBox& c, const juce::String& name, juce::Component& parent) {
    initialise(c, name, parent);
  }
  void addButton(juce::TextButton& c, const juce::String& name, juce::Component& parent, std::function<void()> action) {
    initialise(c, name, parent); c.setButtonText(name); c.onClick = std::move(action);
  }
  void addToggle(juce::ToggleButton& c, const juce::String& name, juce::Component& parent) {
    initialise(c, name, parent); c.setButtonText(name);
  }
  void addSlider(juce::Slider& c, const juce::String& name, juce::Component& parent,
                  double minimum, double maximum, double interval, const juce::String& suffix) {
    initialise(c, name, parent);
    c.setSliderStyle(juce::Slider::LinearHorizontal);
    c.setTextBoxStyle(juce::Slider::TextBoxRight, false, 92, 25);
    c.setRange(minimum, maximum, interval); c.setTextValueSuffix(suffix);
    c.setColour(juce::Slider::trackColourId, theme::kBrandYellow);
  }
  void setHardware(bool shown) {
    showingHardware_ = shown;
    viewport_.setViewedComponent(shown ? &hardware_ : &software_, false);
    viewport_.setViewPosition(0, 0);
    softwareTab_.setToggleState(!shown, juce::dontSendNotification);
    hardwareTab_.setToggleState(shown, juce::dontSendNotification);
    updateButtons(); resized(); updateStatus();
  }
  void refreshChannels() {
    if (router_.busy()) { error_ = "Capture owns the audio device. Finish or cancel the capture before refreshing routes."; updateStatus(); return; }
    saveDraft();
    router_.refresh();
    inputs_ = router_.inputNames(); outputs_ = router_.outputNames(); stages_ = router_.stageChoices();
    auto fill = [](juce::ComboBox& combo, const juce::StringArray& names) {
      combo.clear(juce::dontSendNotification);
      combo.addItem("Unassigned", 1);
      for (int i = 0; i < names.size(); ++i) combo.addItem(juce::String(i + 1) + " - " + names[i], i + 2);
    };
    for (auto& c : aliases_) fill(c, inputs_);
    for (auto* c : {&stereoRight_, &di_, &returnL_, &returnR_, &auxL_, &auxR_}) fill(*c, inputs_);
    fill(destinationL_, outputs_); fill(destinationR_, outputs_);
    for (size_t lane = 0; lane < 2; ++lane) {
      for (size_t kind = 0; kind < 4; ++kind) {
        auto& combo = stage_[lane][kind]; auto& ids = stageIds_[lane][kind];
        combo.clear(juce::dontSendNotification); combo.addItem("Not assigned", 1); ids.clear();
        combo.setTooltip("Choose a block on this lane's effective signal path, including shared trunk-prefix blocks. "
                         "A block outside the effective path leaves this tap unavailable. Cabinet Off requires a separable cabinet block.");
        for (const auto& choice : stages_) {
          ids.push_back(choice.id); combo.addItem(choice.label, static_cast<int>(ids.size()) + 1);
        }
      }
    }
    device_ = router_.deviceDescription();
    draft_.inputDevice = router_.currentInputDevice();
    draft_.outputDevice = router_.currentOutputDevice();
    loadDraft();
  }
  void loadDraft() {
    inputSource_.setSelectedId(static_cast<int>(draft_.inputSource) + 1, juce::dontSendNotification);
    for (size_t i = 0; i < aliases_.size(); ++i) setChannel(aliases_[i], draft_.inputChannels[i]);
    setChannel(stereoRight_, draft_.stereoInputRight); setChannel(di_, draft_.diInput);
    setChannel(returnL_, draft_.returnInputs[0]); setChannel(returnR_, draft_.returnInputs[1]);
    setChannel(auxL_, draft_.auxInputs[0]); setChannel(auxR_, draft_.auxInputs[1]);
    inputGain_.setValue(draft_.inputGainDb, juce::dontSendNotification);
    reamp_.setValue(draft_.reampSensitivityDb, juce::dontSendNotification);
    master_.setValue(draft_.masterVolumeDb, juce::dontSendNotification);
    for (size_t i = 0; i < aux_.size(); ++i) aux_[i].setValue(draft_.auxMix[i], juce::dontSendNotification);
    for (size_t lane = 0; lane < 2; ++lane) {
      const std::array<std::string, 4> ids = {draft_.stackEnd[lane], draft_.modEnd[lane], draft_.wetStart[lane], draft_.cabinet[lane]};
      for (size_t kind = 0; kind < 4; ++kind) {
        const auto& choices = stageIds_[lane][kind];
        const auto it = std::find(choices.begin(), choices.end(), ids[kind]);
        stage_[lane][kind].setSelectedId(it == choices.end() ? 1 : static_cast<int>(std::distance(choices.begin(), it)) + 2, juce::dontSendNotification);
      }
    }
    loadPort(); updateStatus();
  }
  void saveDraft() {
    // Constructors may refresh before channel lists exist. Preserve the
    // controller's initial configuration until controls have actual choices.
    if (aliases_[0].getNumItems() == 0) return;
    draft_.inputSource = static_cast<InputSource>(juce::jlimit(0, 3, inputSource_.getSelectedId() - 1));
    for (size_t i = 0; i < aliases_.size(); ++i) draft_.inputChannels[i] = channel(aliases_[i]);
    draft_.stereoInputRight = channel(stereoRight_); draft_.diInput = channel(di_);
    draft_.returnInputs = {channel(returnL_), channel(returnR_)};
    draft_.auxInputs = {channel(auxL_), channel(auxR_)};
    draft_.inputGainDb = static_cast<float>(inputGain_.getValue());
    draft_.reampSensitivityDb = static_cast<float>(reamp_.getValue());
    draft_.masterVolumeDb = static_cast<float>(master_.getValue());
    for (size_t i = 0; i < aux_.size(); ++i) draft_.auxMix[i] = static_cast<float>(aux_[i].getValue());
    for (size_t lane = 0; lane < 2; ++lane) {
      std::array<std::string*, 4> ids = {&draft_.stackEnd[lane], &draft_.modEnd[lane], &draft_.wetStart[lane], &draft_.cabinet[lane]};
      for (size_t kind = 0; kind < 4; ++kind) {
        const int index = stage_[lane][kind].getSelectedId() - 2;
        const auto& choices = stageIds_[lane][kind];
        *ids[kind] = juce::isPositiveAndBelow(index, static_cast<int>(choices.size())) ? choices[static_cast<size_t>(index)] : std::string();
      }
    }
    savePort();
  }
  void loadPort() {
    const auto& out = draft_.outputs[static_cast<size_t>(selectedPort_)];
    const auto found = std::find(sources().begin(), sources().end(), out.source);
    source_.setSelectedId(found == sources().end() ? 1 : static_cast<int>(std::distance(sources().begin(), found)) + 1, juce::dontSendNotification);
    setChannel(destinationL_, out.left); setChannel(destinationR_, out.right);
    volume_.setValue(out.volumeDb, juce::dontSendNotification);
    linkMaster_.setToggleState(out.linkMaster, juce::dontSendNotification);
    pad_.setToggleState(out.pad12, juce::dontSendNotification);
    stereo_.setToggleState(out.stereo, juce::dontSendNotification);
    cabOff_.setToggleState(out.cabinetOff, juce::dontSendNotification);
    const std::array<float, 4> eq = {out.bassDb, out.middleDb, out.trebleDb, out.presenceDb};
    for (size_t i = 0; i < eq_.size(); ++i) eq_[i].setValue(eq[i], juce::dontSendNotification);
    lowCut_.setValue(out.lowCutHz, juce::dontSendNotification);
    highCut_.setValue(out.highCutHz, juce::dontSendNotification);
    software_.repaint();
  }
  void savePort() {
    auto& out = draft_.outputs[static_cast<size_t>(selectedPort_)];
    const int source = source_.getSelectedId() - 1;
    if (juce::isPositiveAndBelow(source, static_cast<int>(sources().size()))) out.source = sources()[static_cast<size_t>(source)];
    out.left = channel(destinationL_); out.right = channel(destinationR_);
    out.volumeDb = static_cast<float>(volume_.getValue());
    out.linkMaster = linkMaster_.getToggleState(); out.pad12 = pad_.getToggleState();
    out.stereo = stereo_.getToggleState(); out.cabinetOff = cabOff_.getToggleState();
    out.bassDb = static_cast<float>(eq_[0].getValue()); out.middleDb = static_cast<float>(eq_[1].getValue());
    out.trebleDb = static_cast<float>(eq_[2].getValue()); out.presenceDb = static_cast<float>(eq_[3].getValue());
    out.lowCutHz = static_cast<float>(lowCut_.getValue()); out.highCutHz = static_cast<float>(highCut_.getValue());
  }
  void applySoftware() {
    if (router_.busy()) { error_ = "Finish or cancel the raw capture before changing routing."; updateStatus(); return; }
    saveDraft();
    draft_.inputDevice = router_.currentInputDevice();
    draft_.outputDevice = router_.currentOutputDevice();
    draft_.enabled = true;
    const auto result = router_.apply(draft_);
    if (result.failed()) error_ = result.getErrorMessage();
    else { error_.clear(); draft_ = router_.config(); status_ = "Software routing applied. Kemper hardware settings were not changed."; }
    updateStatus();
  }
  void openAudioSettings() {
    if (router_.busy()) { error_ = "Finish or cancel the capture before opening audio device settings."; updateStatus(); return; }
    if (router_.running()) router_.stop();
    error_.clear();
    status_ = "Routing stopped for audio device settings. Review channel mappings before enabling it again.";
    updateStatus();
    if (openSettings_) openSettings_();
  }

  bool supports(const kemper::IoParameterSpec& spec) const {
    const bool player = model_.getSelectedId() == 4;
    if (spec.headRackOnly && model_.getSelectedId() >= 3) return false;
    return !player || (spec.playerSupported && playerLevel_.getSelectedId() >= spec.minimumPlayerLevel);
  }
  void refreshMidi() {
    if (!midi_.isOpen()) {
      midiOutputs_ = juce::MidiOutput::getAvailableDevices();
      midiOut_.clear(juce::dontSendNotification);
      for (int i = 0; i < midiOutputs_.size(); ++i) midiOut_.addItem(midiOutputs_[i].name, i + 1);
      midiOut_.setTextWhenNothingSelected("Choose MIDI output");
    }
    if (!midi_.hasInput()) {
      midiInputs_ = juce::MidiInput::getAvailableDevices();
      midiIn_.clear(juce::dontSendNotification);
      for (int i = 0; i < midiInputs_.size(); ++i) midiIn_.addItem(midiInputs_[i].name, i + 1);
      midiIn_.setTextWhenNothingSelected("Choose optional reply input");
    }
  }
  void connectMidi() {
    for (auto& row : rows_) row->clearReply();
    if (midi_.isOpen()) {
      midi_.close(); listen_.setToggleState(false, juce::dontSendNotification);
      status_ = "MIDI disconnected. Software routing is unchanged."; error_.clear();
    } else {
      const int index = midiOut_.getSelectedId() - 1;
      if (!juce::isPositiveAndBelow(index, midiOutputs_.size())) error_ = "Choose the MIDI output connected to the Kemper.";
      else {
        const auto result = midi_.connect(midiOutputs_[index].identifier, midiChannel_.getSelectedId());
        if (result.failed()) error_ = result.getErrorMessage();
        else { error_.clear(); status_ = "MIDI port open. No settings or requests were sent. Select the values you intend to apply."; }
      }
    }
    updateButtons(); updateStatus();
  }
  void applyHardware() {
    if (!midi_.isOpen()) { error_ = "Connect the Kemper MIDI output first."; updateStatus(); return; }
    if (router_.busy()) { error_ = "Finish or cancel capture before changing hardware settings."; updateStatus(); return; }
    int count = 0;
    for (auto& row : rows_) {
      if (!row->armed()) continue;
      const auto result = midi_.setIoParameter(row->spec().id, row->rawValue());
      if (result.failed()) { error_ = result.getErrorMessage(); updateStatus(); return; }
      row->unarm(); ++count;
    }
    error_.clear();
    status_ = count == 0 ? "No MIDI values were selected. Tick Send beside each value you intend to apply." :
      juce::String(count) + " requested MIDI values sent. Sending does not confirm hardware state; request replies if needed. No rig was stored.";
    updateButtons(); updateStatus();
  }
  void requestReadback() {
    const int index = readParameter_.getSelectedId() - 1;
    if (!midi_.isOpen() || !midi_.hasInput()) { error_ = "Connect MIDI output and opt in to the reply input before requesting a parameter."; updateStatus(); return; }
    if (!juce::isPositiveAndBelow(index, static_cast<int>(kemper::ioParameters().size()))) return;
    const auto& spec = kemper::ioParameters()[static_cast<size_t>(index)];
    if (!supports(spec)) { error_ = "This parameter is not documented for the selected model/level."; updateStatus(); return; }
    const auto result = midi_.requestParameter(spec.page, spec.index, product_.getSelectedId() == 2 ? 2 : 0);
    if (result.failed()) error_ = result.getErrorMessage();
    else { error_.clear(); status_ = "Read request sent for " + spec.label + ". A value is shown as actual only after a valid reply arrives."; }
    updateStatus();
  }
  void updateHardwareAvailability() {
    playerLevel_.setEnabled(model_.getSelectedId() == 4);
    for (auto& row : rows_) row->updateAvailability();
    updateButtons(); hardware_.repaint();
  }
  void updateButtons() {
    const bool busy = router_.busy();
    apply_.setVisible(!showingHardware_); stop_.setVisible(!showingHardware_);
    sendHardware_.setVisible(showingHardware_);
    apply_.setEnabled(!busy); stop_.setEnabled(!busy && router_.running());
    sendHardware_.setEnabled(!busy && midi_.isOpen());
    const auto connectionText = midi_.isOpen() ? "Disconnect output" : "Connect output";
    connect_.setButtonText(connectionText);
    connect_.setName(connectionText); connect_.setTitle(connectionText);
    connect_.setEnabled(!busy);
    midiOut_.setEnabled(!midi_.isOpen() && !busy); midiChannel_.setEnabled(!midi_.isOpen() && !busy);
    midiIn_.setEnabled(!midi_.hasInput() && !busy);
    listen_.setEnabled(!busy && midi_.isOpen()); read_.setEnabled(!busy && midi_.isOpen() && midi_.hasInput());
    refresh_.setEnabled(!busy); audioSettings_.setEnabled(!busy); reload_.setEnabled(!busy);
  }
  void updateStatus() {
    updateButtons();
    const auto text = error_.isNotEmpty() ? error_ : status_;
    statusLabel_.setText(text, juce::dontSendNotification);
    statusLabel_.setColour(juce::Label::textColourId, error_.isNotEmpty() ? theme::kBrandRed : theme::kWhite);
    software_.repaint(); hardware_.repaint();
  }
  void timerCallback() override {
    const auto liveStatus = router_.status();
    if (liveStatus != lastRouterStatus_) {
      lastRouterStatus_ = liveStatus;
      if (!showingHardware_ && error_.isEmpty() && liveStatus.isNotEmpty()) {
        status_ = liveStatus;
        updateStatus();
      }
    }
    updateButtons();
    for (auto& row : rows_) { row->updateAvailability(); row->receive(); }
    if (showingHardware_) hardware_.repaint(); else software_.repaint();
  }
  struct LabelPlacement { juce::String text; juce::Rectangle<int> box; };
  void field(std::vector<LabelPlacement>& labels, juce::Component& control, const juce::String& text,
              juce::Rectangle<int> box) {
    labels.push_back({text, box.removeFromTop(18)});
    box.removeFromTop(3);
    control.setBounds(box.withHeight(30));
  }
  void layoutSoftware() {
    softwareLabels_.clear();
    const int width = software_.getWidth();
    const int left = juce::roundToInt((width - kPad * 2 - kGap) * 0.55);
    const int rightX = kPad + left + kGap, right = width - kPad - rightX;
    deviceBox_ = {kPad, 16, width - kPad * 2, 126};
    inputBox_ = {kPad, 158, left, 660};
    masterBox_ = {kPad, 834, left, 286};
    outputBox_ = {rightX, 158, right, 962};
    stageBox_ = {kPad, 1136, width - kPad * 2, 398};
    diagnosticBox_ = {kPad, 1550, width - kPad * 2, 154};
    auto deviceButtons = juce::Rectangle<int>(kPad + 18, 108, width - kPad * 2 - 36, 28);
    audioSettings_.setBounds(deviceButtons.removeFromLeft(174)); deviceButtons.removeFromLeft(8);
    refresh_.setBounds(deviceButtons.removeFromLeft(150)); deviceButtons.removeFromLeft(8);
    reload_.setBounds(deviceButtons.removeFromLeft(174));

    auto inputs = inputBox_.reduced(18).withTrimmedTop(44);
    auto nextInput = [&] { return inputs.removeFromTop(58); };
    field(softwareLabels_, inputSource_, "Input source alias", nextInput());
    const char* aliasLabels[] = {"Instrument / Front", "Alternative input", "Return Input Reamp", "Digital / S/PDIF Reamp"};
    for (size_t i = 0; i < aliases_.size(); ++i) field(softwareLabels_, aliases_[i], aliasLabels[i], nextInput());
    auto pair = [&](juce::Component& leftControl, const char* leftLabel, juce::Component& rightControl, const char* rightLabel) {
      auto row = nextInput(); const int half = (row.getWidth() - 10) / 2;
      field(softwareLabels_, leftControl, leftLabel, row.removeFromLeft(half)); row.removeFromLeft(10);
      field(softwareLabels_, rightControl, rightLabel, row);
    };
    pair(stereoRight_, "Stereo input right (optional)", di_, "Untouched DI input");
    pair(returnL_, "Return left", returnR_, "Return right (optional)");
    pair(auxL_, "Aux left", auxR_, "Aux right (optional)");
    field(softwareLabels_, inputGain_, "Input gain", nextInput());
    field(softwareLabels_, reamp_, "Reamp sensitivity (software)", nextInput());

    auto output = outputBox_.reduced(18).withTrimmedTop(44);
    field(softwareLabels_, port_, "Logical destination", output.removeFromTop(58));
    field(softwareLabels_, source_, "Signal source", output.removeFromTop(58));
    auto mapped = output.removeFromTop(58); const int half = (mapped.getWidth() - 10) / 2;
    field(softwareLabels_, destinationL_, "Left / mono physical channel", mapped.removeFromLeft(half)); mapped.removeFromLeft(10);
    field(softwareLabels_, destinationR_, "Right physical channel", mapped);
    field(softwareLabels_, volume_, "Destination volume", output.removeFromTop(58));
    auto flags = output.removeFromTop(32);
    linkMaster_.setBounds(flags.removeFromLeft(juce::jmax(200, flags.getWidth() - 114)));
    pad_.setBounds(flags);
    flags = output.removeFromTop(32);
    stereo_.setBounds(flags.removeFromLeft(juce::jmax(165, flags.getWidth() / 2)));
    cabOff_.setBounds(flags);
    const char* eqLabels[] = {"Bass", "Middle", "Treble", "Presence"};
    for (size_t i = 0; i < eq_.size(); ++i) {
      auto row = output.removeFromTop(44);
      softwareLabels_.push_back({eqLabels[i], row.removeFromLeft(72).withHeight(30)});
      eq_[i].setBounds(row.withHeight(30));
    }
    field(softwareLabels_, lowCut_, "Low cut (0 = off)", output.removeFromTop(58));
    field(softwareLabels_, highCut_, "High cut (0 = off)", output.removeFromTop(58));
    outputHelp_ = output.removeFromTop(72);
    outputMeters_ = output.removeFromTop(212);

    auto master = masterBox_.reduced(18).withTrimmedTop(44);
    field(softwareLabels_, master_, "Master volume for linked destinations", master.removeFromTop(58));
    const char* auxLabels[] = {"Aux to Main", "Aux to Monitor", "Aux to Phones"};
    for (size_t i = 0; i < aux_.size(); ++i) {
      auto row = master.removeFromTop(44);
      softwareLabels_.push_back({auxLabels[i], row.removeFromLeft(100).withHeight(30)});
      aux_[i].setBounds(row.withHeight(30));
    }
    masterHelp_ = master.removeFromTop(34);

    auto stages = stageBox_.reduced(18).withTrimmedTop(46);
    auto laneNames = stages.removeFromTop(26);
    const int laneWidth = (laneNames.getWidth() - 18) / 2;
    stageLaneLabels_[0] = laneNames.removeFromLeft(laneWidth); laneNames.removeFromLeft(18);
    stageLaneLabels_[1] = laneNames;
    const char* stageLabels[] = {"Stack ends after block", "Mod ends after block", "Wet effects start at block", "Separate cabinet block"};
    for (size_t kind = 0; kind < 4; ++kind) {
      auto row = stages.removeFromTop(62);
      field(softwareLabels_, stage_[0][kind], stageLabels[kind], row.removeFromLeft(laneWidth)); row.removeFromLeft(18);
      field(softwareLabels_, stage_[1][kind], stageLabels[kind], row);
    }
    stageHelp_ = stages.removeFromTop(48);
  }
  int layoutHardwareRows(bool place) {
    int y = 616;
    juce::String previous;
    if (place) sectionLabels_.clear();
    for (auto& row : rows_) {
      if (row->spec().section != previous) {
        previous = row->spec().section;
        if (place) sectionLabels_.push_back({previous, {kPad + 18, y, hardware_.getWidth() - kPad * 2 - 36, 26}});
        y += 38;
      }
      if (place) row->setBounds(kPad + 18, y, hardware_.getWidth() - kPad * 2 - 36, 68);
      y += 76;
    }
    y += 18;
    if (place) manualTop_ = y;
    y += 78;
    y += static_cast<int>(kemper::manualIoSettings().size()) * 86;
    return y + 38;
  }
  int hardwareHeight() { return layoutHardwareRows(false); }
  void layoutHardware() {
    hardwareLabels_.clear();
    const int width = hardware_.getWidth();
    hardwareHeader_ = {kPad, 16, width - kPad * 2, 580};
    const int x = kPad + 18, innerWidth = width - kPad * 2 - 36;
    auto row = juce::Rectangle<int>(x, 150, innerWidth, 56);
    const int half = (innerWidth - 16) / 2;
    field(hardwareLabels_, model_, "Connected Kemper model", row.removeFromLeft(half)); row.removeFromLeft(16);
    field(hardwareLabels_, playerLevel_, "Player feature level (when applicable)", row);
    field(hardwareLabels_, midiOut_, "MIDI output to Kemper", {x, 216, innerWidth, 56});
    row = {x, 280, innerWidth, 32};
    midiChannel_.setBounds(row.removeFromLeft(160)); row.removeFromLeft(10);
    connect_.setBounds(row.removeFromLeft(170)); row.removeFromLeft(10);
    refreshMidi_.setBounds(row.removeFromLeft(174));
    field(hardwareLabels_, midiIn_, "Optional MIDI input carrying Kemper replies", {x, 328, innerWidth, 56});
    listen_.setBounds(x, 391, innerWidth, 30);
    field(hardwareLabels_, readParameter_, "Explicit read request", {x, 432, innerWidth, 56});
    row = {x, 497, innerWidth, 32};
    product_.setBounds(row.removeFromLeft(juce::jmax(270, row.getWidth() - 246))); row.removeFromLeft(10);
    read_.setBounds(row);
    layoutHardwareRows(true);
  }
  void paintSoftware(juce::Graphics& g) {
    g.fillAll(theme::kBlack);
    drawCard(g, deviceBox_, "STANDALONE DEVICE", "Your audio interface");
    drawCard(g, inputBox_, "INPUT", "Source & interface mapping");
    drawCard(g, outputBox_, "OUTPUT", "Destination routing");
    drawCard(g, masterBox_, "MASTER & AUXILIARY", "Linked outputs and aux sends");
    drawCard(g, stageBox_, "SIGNAL STAGES", "Assign actual blocks to the source taps");
    drawCard(g, diagnosticBox_, "STATUS", "Routing diagnostics");
    drawText(g, device_, {deviceBox_.getX() + 18, deviceBox_.getY() + 62, deviceBox_.getWidth() - 36, 27}, 12, theme::kMuted, false, 2);
    for (const auto& label : softwareLabels_) drawText(g, label.text, label.box, 11);
    drawText(g, "Left / primary lane", stageLaneLabels_[0], 13, theme::kWhite, true);
    drawText(g, "Right lane", stageLaneLabels_[1], 13, theme::kWhite, true);
    drawText(g, "Main, Monitor, Direct, Phones, S/PDIF and USB are logical software destinations mapped to this interface. These controls do not change Kemper hardware sources. A separate cab is required for Monitor Cabinet Off.",
             outputHelp_, 11, theme::kMuted, false, 5);
    auto meters = outputMeters_;
    for (size_t i = 0; i < portCount; ++i) drawMeter(g, portName(static_cast<Port>(i)), router_.outputPeak(static_cast<Port>(i)), meters.removeFromTop(34));
    drawText(g, "Aux mix is linear 0-1. Output links follow this software Master Volume. Output EQ is per destination.", masterHelp_, 11, theme::kMuted, false, 3);
    drawText(g, "Stack and Mod tap after the selected block; Wet FX uses designated effect contributions. Both lanes may select shared trunk-prefix blocks. The selected block must be on that lane's effective path, or its tap stays unavailable. Cabinet Off requires a separable cabinet; a combined amp+cab NAM cannot be separated.",
             stageHelp_, 11, theme::kMuted, false, 4);
    const auto diagnostics = router_.diagnostics();
    const auto state = router_.busy() ? "Capture owns I/O; routing changes are unavailable until it finishes." :
      router_.running() ? "Software routing is running. " + router_.status() : "Software routing is off. " + router_.status();
    drawText(g, state, {diagnosticBox_.getX() + 18, diagnosticBox_.getY() + 60, diagnosticBox_.getWidth() - 36, 28}, 12, theme::kWhite, false, 2);
    drawText(g, diagnostics.isEmpty() ? "No current routing diagnostics." : diagnostics.joinIntoString("\n"),
             {diagnosticBox_.getX() + 18, diagnosticBox_.getY() + 89, diagnosticBox_.getWidth() - 36, 54}, 11,
             diagnostics.isEmpty() ? theme::kMuted : theme::kBrandYellow, false, 4);
    drawText(g, "Input aliases only select interface channels. Configure the Kemper's actual Input Source separately. DI here is raw digital audio; software cannot create an analog DI bypass or guarantee analog output voltage.",
             {kPad, 1716, software_.getWidth() - kPad * 2, 34}, 11, theme::kMuted, false, 3);
    const auto inputMeter = juce::Rectangle<int>(software_.getWidth() - 245, 31, 200, 28);
    drawMeter(g, "INPUT", router_.inputPeak(), inputMeter);
  }
  void paintHardware(juce::Graphics& g) {
    g.fillAll(theme::kBlack);
    drawCard(g, hardwareHeader_, "DIRECT MIDI  /  REQUESTED VALUES", "Kemper Input / Output controls");
    drawText(g, "This tab controls the Kemper itself. Nothing is sent on connect. Tick Send beside each value you intend to change, then Apply selected MIDI values. Continuous controls show raw hardware positions (0-16383), not guessed dB, Hz or percent. No rig Store command is sent.",
             {kPad + 18, 82, hardware_.getWidth() - kPad * 2 - 36, 54}, 13, theme::kMuted, false, 4);
    for (const auto& label : hardwareLabels_) drawText(g, label.text, label.box, 11);
    drawText(g, "Readback is optional. Listening alone sends no request. A control is marked Actual reply only after a valid Kemper SysEx response arrives. MIDI13's header table and worked examples use different product values; choose the header appropriate to your hardware.",
             {kPad + 18, 540, hardware_.getWidth() - kPad * 2 - 36, 42}, 11, theme::kMuted, false, 3);
    for (const auto& label : sectionLabels_) drawText(g, label.text, label.box, 18, theme::kWhite, true);
    drawText(g, "Manual hardware settings", {kPad + 18, manualTop_, hardware_.getWidth() - kPad * 2 - 36, 28}, 20, theme::kWhite, true);
    drawText(g, "The accessed MIDI documentation does not provide a verified command for these fields. Set them at the destination shown in Rig Manager or on the Kemper. Software routes above do not confirm or replace these hardware settings.",
             {kPad + 18, manualTop_ + 32, hardware_.getWidth() - kPad * 2 - 36, 40}, 12, theme::kMuted, false, 3);
    int y = manualTop_ + 78;
    for (const auto& manual : kemper::manualIoSettings()) {
      const auto box = juce::Rectangle<int>(kPad, y, hardware_.getWidth() - kPad * 2, 76);
      g.setColour(theme::kSurface); g.fillRoundedRectangle(box.toFloat(), 7);
      drawText(g, manual.section + " / " + manual.label, box.reduced(14).removeFromTop(20), 13, theme::kWhite, true);
      drawText(g, manual.destination + "\n" + manual.notes, {box.getX() + 14, box.getY() + 30, box.getWidth() - 28, 38}, 11, theme::kMuted, false, 3);
      y += 86;
    }
  }

  StandaloneRouting& router_;
  Config draft_;
  std::function<void()> openSettings_;
  Fonts::Hold fontsHold_;
  juce::Viewport viewport_;
  Canvas software_, hardware_;
  juce::Label statusLabel_;
  juce::TextButton softwareTab_, hardwareTab_, apply_, stop_, sendHardware_;
  juce::TextButton audioSettings_, refresh_, reload_;
  juce::ComboBox inputSource_, stereoRight_, di_, returnL_, returnR_, auxL_, auxR_;
  std::array<juce::ComboBox, 4> aliases_;
  juce::Slider inputGain_, reamp_, master_;
  std::array<juce::Slider, 3> aux_;
  juce::ComboBox port_, source_, destinationL_, destinationR_;
  juce::Slider volume_, lowCut_, highCut_;
  std::array<juce::Slider, 4> eq_;
  juce::ToggleButton linkMaster_, pad_, stereo_, cabOff_;
  std::array<std::array<juce::ComboBox, 4>, 2> stage_;
  std::array<std::array<std::vector<std::string>, 4>, 2> stageIds_;
  std::vector<StageChoice> stages_;
  juce::StringArray inputs_, outputs_;
  juce::String device_, status_, error_, lastRouterStatus_;
  int selectedPort_ = 0;
  bool showingHardware_ = false;
  kemper::Controller midi_;
  juce::Array<juce::MidiDeviceInfo> midiOutputs_, midiInputs_;
  juce::ComboBox model_, playerLevel_, midiOut_, midiChannel_, midiIn_, readParameter_, product_;
  juce::TextButton connect_, refreshMidi_, read_;
  juce::ToggleButton listen_;
  std::vector<std::unique_ptr<HardwareRow>> rows_;
  std::vector<LabelPlacement> softwareLabels_, hardwareLabels_, sectionLabels_;
  juce::Rectangle<int> deviceBox_, inputBox_, outputBox_, masterBox_, stageBox_, diagnosticBox_, hardwareHeader_;
  juce::Rectangle<int> outputHelp_, outputMeters_, masterHelp_, stageHelp_;
  std::array<juce::Rectangle<int>, 2> stageLaneLabels_;
  int manualTop_ = 0;
};

RoutingWindow::RoutingWindow(juce::Component* centreAround, StandaloneRouting& router,
                              std::function<void()> openAudioSettings)
    : DocumentWindow("TONE3000 - Inputs & Outputs", theme::kBlack, DocumentWindow::allButtons),
      panel_(std::make_unique<Panel>(router, std::move(openAudioSettings))) {
  setUsingNativeTitleBar(true);
  setResizable(true, true);
  setResizeLimits(920, 640, 1700, 1400);
  setContentNonOwned(panel_.get(), true);
  if (centreAround != nullptr) centreAroundComponent(centreAround, 1080, 850);
  else centreWithSize(1080, 850);
  setVisible(true);
}
RoutingWindow::~RoutingWindow() { clearContentComponent(); }
bool RoutingWindow::busy() const { return panel_->busy(); }
void RoutingWindow::closeButtonPressed() { setVisible(false); }
void RoutingWindow::visibilityChanged() {
  if (isVisible() && panel_) panel_->refreshWhenShown();
}

}  // namespace t3k::routing
