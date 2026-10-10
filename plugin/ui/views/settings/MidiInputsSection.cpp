#include "MidiInputsSection.h"

#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
const juce::String kEmptyCopy = "No MIDI devices found. Connect one and it will appear here.";
}  // namespace

// InputRow
// Check indicator, device name, and "enabled" on the right while it is.
class MidiInputsSection::InputRow : public juce::Component {
public:
  InputRow(MidiInputDevice input, std::function<void(bool)> onToggle)
      : input_(std::move(input)), onToggle_(std::move(onToggle)) {
    setName(input_.name);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }

  static int height() { return 2 * kRowPadY + ChoiceIndicator::kSize; }
  const juce::String& id() const { return input_.id; }

  void setEnabled(bool enabled) {
    if (input_.enabled == enabled) return;
    input_.enabled = enabled;
    repaint();
  }

  void paint(juce::Graphics& g) override {
    if (isMouseOver()) g.fillAll(form::kRowHover);
    const auto box = getLocalBounds().reduced(kRowPadX, kRowPadY);
    ChoiceIndicator::paint(g, juce::Rectangle<float>(box.getX(), box.getY(), ChoiceIndicator::kSize,
                                                     ChoiceIndicator::kSize),
                           input_.enabled, true);
    float right = static_cast<float>(box.getRight());
    if (input_.enabled) {
      const auto font = Fonts::sans(kEnabledPx);
      const float w = Fonts::width(font, "enabled");
      const float lineH = static_cast<float>(Fonts::normalLineHeight(kEnabledPx));
      paint::cssLine(g, "enabled", right - w, box.getCentreY() - lineH / 2, lineH, w + 2, font, theme::kSubtle);
      right -= w + kRowGap;
    }
    const float x = static_cast<float>(box.getX() + ChoiceIndicator::kSize + kRowGap);
    const float lineH = static_cast<float>(Fonts::normalLineHeight(form::kBodyPx));
    paint::cssLine(g, input_.name, x, box.getCentreY() - lineH / 2, lineH, right - x, Fonts::sans(form::kBodyPx),
                   input_.enabled ? theme::kWhite : theme::kMuted);
  }

  void mouseEnter(const juce::MouseEvent&) override { repaint(); }
  void mouseExit(const juce::MouseEvent&) override { repaint(); }
  void mouseUp(const juce::MouseEvent& e) override {
    if (getLocalBounds().contains(e.getPosition()) && !e.mouseWasDraggedSinceMouseDown())
      onToggle_(!input_.enabled);
  }

private:
  MidiInputDevice input_;
  std::function<void(bool)> onToggle_;
};

// Card
// The bordered list (rows clipped to its rounded corners) or the italic
// empty-state note.
class MidiInputsSection::Card : public FormItem {
public:
  Card() : empty_(RichText{}, form::kSmallPx, theme::kSubtle, juce::Justification::centred, 14.0f / form::kSmallPx) {
    empty_.setItalicText(kEmptyCopy);
    addChildComponent(empty_);
  }

  void setRows(std::vector<std::unique_ptr<InputRow>> rows) {
    rows_ = std::move(rows);
    for (auto& row : rows_) addAndMakeVisible(*row);
    empty_.setVisible(rows_.empty());
    heightChanged();
    resized();
  }
  std::vector<std::unique_ptr<InputRow>>& rows() { return rows_; }

  float heightFor(float width) const override {
    if (rows_.empty()) return 2 + 2 * kEmptyPad + empty_.heightFor(width - 2 - 2 * kEmptyPad);
    return static_cast<float>(2 + static_cast<int>(rows_.size()) * InputRow::height());
  }

  void paint(juce::Graphics& g) override {
    paint::border(g, getLocalBounds().toFloat(), form::kCardRadius, form::kFieldBorder);
  }

  void resized() override {
    const auto inner = getLocalBounds().reduced(1);
    if (rows_.empty()) {
      empty_.setBounds(inner.reduced(kEmptyPad));
      return;
    }
    int y = inner.getY();
    for (auto& row : rows_) {
      row->setBounds(inner.getX(), y, inner.getWidth(), InputRow::height());
      y += InputRow::height();
    }
  }

private:
  std::vector<std::unique_ptr<InputRow>> rows_;
  Paragraph empty_;
};

// MidiInputsSection
MidiInputsSection::MidiInputsSection(Services& services)
    : FieldRow("MIDI Inputs",
               juce::String::fromUTF8("Enable the devices you want to control the plugin with. Set what each knob "
                                      "or pedal does in Plugin Settings \xe2\x86\x92 MIDI Mapping.")),
      services_(services),
      card_(std::make_unique<Card>()),
      bluetooth_("Bluetooth MIDI",
                 [] {
                   auto look = FormButton::outlined();
                   look.fontPx = 13;
                   look.bold = true;
                   return look;
                 }()),
      bluetoothBox_(bluetooth_, static_cast<float>(bluetooth_.preferredHeight())) {
  bluetooth_.setLeadingIcon(Icon::Bluetooth, 14, 8);
  bluetooth_.onClick = [this] { services_.audioDevice.openBluetoothMidiPairing(); };
  content().setGap(form::kControlGap);
  content().add(*card_);
  content().add(bluetoothBox_);
}

MidiInputsSection::~MidiInputsSection() = default;

void MidiInputsSection::update(const AudioDeviceState& state) {
  const auto& inputs = state.midiInputs;
  auto& rows = card_->rows();
  bool same = rows.size() == inputs.size();
  for (size_t i = 0; same && i < inputs.size(); ++i)
    same = rows[i]->id() == inputs[i].id && rows[i]->getName() == inputs[i].name;
  if (same) {
    for (size_t i = 0; i < inputs.size(); ++i) rows[i]->setEnabled(inputs[i].enabled);
  } else {
    std::vector<std::unique_ptr<InputRow>> fresh;
    for (const auto& input : inputs)
      fresh.push_back(std::make_unique<InputRow>(input, [this, id = input.id](bool enabled) {
        services_.audioDevice.setMidiInputEnabled(id, enabled);
      }));
    card_->setRows(std::move(fresh));
  }
  content().setShown(bluetoothBox_, state.btMidiAvailable);
}

void MidiInputsSection::syncPolling() {
  if (isShowing()) {
    if (!isTimerRunning()) startTimer(kHotplugPollMs);
  } else {
    stopTimer();
  }
}

}  // namespace t3k::ui
