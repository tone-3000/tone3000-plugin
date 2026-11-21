#include "EditableChip.h"

#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

EditableChip::EditableChip(juce::String label, int valueWidth, help::Key help)
    : label_(std::move(label)), valueWidth_(valueWidth), help_(help) {
  setHelpText(help::text(help_));
  setMouseCursor(juce::MouseCursor::IBeamCursor);
  setViewportIgnoreDragFlag(true);  // a drag on the value selects it; the column around it stays put
  setTitle(label_);
  setWantsKeyboardFocus(true);
  setMouseClickGrabsKeyboardFocus(false);  // a click opens the editor, as it always has

  editor_.setMultiLine(false);
  editor_.setReturnKeyStartsNewLine(false);
  editor_.setScrollbarsShown(false);
  editor_.setPopupMenuEnabled(false);
  editor_.setBorder({});
  editor_.setIndents(0, 0);
  editor_.setJustification(juce::Justification::centredLeft);
  for (auto id : {juce::TextEditor::backgroundColourId, juce::TextEditor::outlineColourId,
                  juce::TextEditor::focusedOutlineColourId, juce::TextEditor::shadowColourId})
    editor_.setColour(id, juce::Colours::transparentBlack);
  editor_.setColour(juce::TextEditor::textColourId, theme::kWhite);
  editor_.setColour(juce::TextEditor::highlightColourId, theme::kHighlight);
  editor_.setColour(juce::TextEditor::highlightedTextColourId, theme::kWhite);
  editor_.setColour(juce::CaretComponent::caretColourId, theme::kWhite);
  editor_.setFont(Fonts::mono(kFontPx));
  editor_.addListener(this);
  addChildComponent(editor_);

  const int labelW = juce::roundToInt(Fonts::width(Fonts::mono(kFontPx), label_));
  setSize(kPadX + labelW + kGap + valueWidth_ + kPadX, theme::kTextBoxHeight);
}

void EditableChip::setText(const juce::String& text, const juce::String& editText) {
  text_ = text;
  editText_ = editText;
  if (editing_) editor_.setTextToShowWhenEmpty(editText_, theme::kGray);
  repaint();
}

void EditableChip::setSpokenValue(const juce::String& spoken) {
  if (spoken_ == spoken) return;
  spoken_ = spoken;
  // Only a focused chip speaks its changes (a dot drag moves all three).
  if (hasKeyboardFocus(false))
    if (auto* handler = getAccessibilityHandler())
      handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

void EditableChip::setDisabledLook(bool disabled) {
  disabled_ = disabled;
  setAlpha(disabled ? theme::kDisabledOpacity : 1.0f);
  setMouseCursor(disabled ? juce::MouseCursor::NormalCursor : juce::MouseCursor::IBeamCursor);
  setHelpText(disabled ? juce::String() : help::text(help_));
  if (disabled && editing_) cancel();
}

juce::Rectangle<int> EditableChip::valueBounds() const {
  return {getWidth() - kPadX - valueWidth_, 0, valueWidth_, getHeight()};
}

void EditableChip::mouseUp(const juce::MouseEvent& e) {
  if (disabled_ || editing_ || !e.mods.isLeftButtonDown() || !getLocalBounds().contains(e.getPosition())) return;
  beginEdit();
}

bool EditableChip::keyPressed(const juce::KeyPress& key) {
  using KP = juce::KeyPress;
  if (editing_ || !hasKeyboardFocus(false)) return false;
  if (key == KP::returnKey || key == KP::spaceKey) {
    if (!disabled_) beginKeyboardEdit();
    return true;
  }
  const bool up = key.isKeyCode(KP::upKey) || key.isKeyCode(KP::rightKey);
  const bool down = key.isKeyCode(KP::downKey) || key.isKeyCode(KP::leftKey);
  if (!up && !down) return false;
  if (disabled_ || !numeric || !onSetValue) return true;
  if (const auto n = numeric()) {
    const double step = key.getModifiers().isShiftDown() ? n->fineStep : n->step;
    onSetValue(juce::jlimit(n->min, n->max, n->value + (up ? step : -step)));
  }
  return true;
}

void EditableChip::beginKeyboardEdit() {
  if (disabled_ || editing_) return;
  beginEdit();
  keyboardEdit_ = true;
}

void EditableChip::beginEdit() {
  keyboardEdit_ = false;
  editing_ = true;
  editor_.setText({}, false);
  editor_.setTextToShowWhenEmpty(editText_, theme::kGray);
  editor_.setBounds(valueBounds());
  editor_.setVisible(true);
  if (editor_.isShowing()) editor_.grabKeyboardFocus();
  repaint();
}

void EditableChip::commit() {
  if (!editing_) return;
  const auto raw = editor_.getText();
  endEdit();
  if (raw.trim().isNotEmpty() && onCommit) onCommit(raw);
}

void EditableChip::cancel() {
  if (!editing_) return;
  endEdit();
}

void EditableChip::endEdit() {
  const bool returnFocus = keyboardEdit_ && editor_.hasKeyboardFocus(true);
  editing_ = false;
  keyboardEdit_ = false;
  editor_.setVisible(false);
  if (returnFocus && isShowing()) grabKeyboardFocus();
  repaint();
}

void EditableChip::resized() { editor_.setBounds(valueBounds()); }

void EditableChip::paint(juce::Graphics& g) {
  const auto box = getLocalBounds();
  paint::fill(g, box.toFloat(), theme::kIconBoxRadius, theme::kSegmentedTrack);
  const auto font = Fonts::mono(kFontPx);
  const int labelW = juce::roundToInt(Fonts::width(font, label_));
  paint::text(g, label_, {kPadX, 0, labelW + 1, getHeight()}, font, theme::kSubtle);
  // The value area is sized to the longest reading; a wider one overflows
  // visibly (white-space: nowrap, no clipping) rather than ellipsising.
  if (!editing_) {
    g.setFont(font);
    g.setColour(theme::kWhite);
    g.drawText(text_, valueBounds().withWidth(valueWidth_ + 40), juce::Justification::centredLeft, false);
  }
  // Keyboard focus only (a click never focuses the chip), in the EQ faders' style.
  if (hasKeyboardFocus(false)) {
    g.setColour(theme::kWhite);
    g.drawRoundedRectangle(box.toFloat().reduced(0.5f), theme::kIconBoxRadius, 1.0f);
  }
}

namespace {
// The chip's reading for screen readers: the spoken value with its unit, and
// the numbers behind it so VoiceOver's increment and decrement step it.
class ChipValue : public juce::AccessibilityValueInterface {
public:
  explicit ChipValue(EditableChip& chip) : chip_(chip) {}

  bool isReadOnly() const override {
    return chip_.isDisabledLook() || !chip_.numeric || !chip_.onSetValue || !chip_.numeric();
  }
  double getCurrentValue() const override {
    const auto n = chip_.numeric ? chip_.numeric() : std::nullopt;
    return n ? n->value : 0.0;
  }
  juce::String getCurrentValueAsString() const override { return chip_.spokenValue(); }
  void setValue(double value) override {
    if (isReadOnly()) return;
    if (const auto n = chip_.numeric()) chip_.onSetValue(juce::jlimit(n->min, n->max, value));
  }
  void setValueAsString(const juce::String& text) override {
    if (!chip_.isDisabledLook() && text.trim().isNotEmpty() && chip_.onCommit) chip_.onCommit(text);
  }
  AccessibleValueRange getRange() const override {
    const auto n = chip_.numeric ? chip_.numeric() : std::nullopt;
    if (!n) return {};
    return {{n->min, n->max}, n->step};
  }

private:
  EditableChip& chip_;
};

struct ChipHandler : juce::AccessibilityHandler {
  ChipHandler(EditableChip& chip, Interfaces interfaces, juce::AccessibilityActions actions)
      : juce::AccessibilityHandler(chip, juce::AccessibilityRole::slider, std::move(actions), std::move(interfaces)),
        chip_(chip) {}
  // The hint bar's copy speaks of clicking; the keys are what a reader needs.
  juce::String getHelp() const override {
    return chip_.isDisabledLook() ? juce::String() : juce::String("Arrows adjust, Enter to type a value.");
  }
  EditableChip& chip_;
};
}  // namespace

std::unique_ptr<juce::AccessibilityHandler> EditableChip::createAccessibilityHandler() {
  juce::AccessibilityHandler::Interfaces interfaces;
  interfaces.value = std::make_unique<ChipValue>(*this);
  juce::AccessibilityActions actions;
  actions.addAction(juce::AccessibilityActionType::press, [this] {
    if (isShowing()) grabKeyboardFocus();
    beginKeyboardEdit();
  });
  return std::make_unique<ChipHandler>(*this, std::move(interfaces), std::move(actions));
}

}  // namespace t3k::ui
