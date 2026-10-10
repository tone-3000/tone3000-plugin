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

void EditableChip::beginEdit() {
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
  editing_ = false;
  editor_.setVisible(false);
  if (raw.trim().isNotEmpty() && onCommit) onCommit(raw);
  repaint();
}

void EditableChip::cancel() {
  if (!editing_) return;
  editing_ = false;
  editor_.setVisible(false);
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
}

}  // namespace t3k::ui
