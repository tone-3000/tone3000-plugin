#include "TextField.h"

#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

TextField::TextField() : border_(theme::kBorder) {
  editor_.setMultiLine(false);
  editor_.setReturnKeyStartsNewLine(false);
  editor_.setScrollbarsShown(false);
  editor_.setPopupMenuEnabled(false);
  editor_.setBorder({});
  editor_.setIndents(0, 0);
  editor_.setJustification(juce::Justification::centredLeft);
  editor_.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
  editor_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
  editor_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
  editor_.setColour(juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
  editor_.setColour(juce::TextEditor::textColourId, theme::kWhite);
  editor_.setColour(juce::TextEditor::highlightColourId, theme::kHighlight);
  editor_.setColour(juce::TextEditor::highlightedTextColourId, theme::kWhite);
  editor_.setColour(juce::CaretComponent::caretColourId, theme::kWhite);
  editor_.setMouseCursor(juce::MouseCursor::IBeamCursor);
  editor_.addListener(this);
  setViewportIgnoreDragFlag(true);  // a drag on text selects it; the list or page around it stays put
  setFontSize(13.0f);
  addAndMakeVisible(editor_);
  juce::Desktop::getInstance().addFocusChangeListener(this);
}

TextField::~TextField() { juce::Desktop::getInstance().removeFocusChangeListener(this); }

void TextField::setPlaceholder(const juce::String& text) {
  editor_.setTextToShowWhenEmpty(text, theme::kGray);
  editor_.setTitle(text);  // the field's name to a screen reader
}

void TextField::setText(const juce::String& text, bool notify) {
  editor_.setText(text, notify);
  if (clear_) clear_->setVisible(text.isNotEmpty());
}

void TextField::setFontSize(float px) { setFont(Fonts::sans(px)); }

void TextField::setFont(const juce::Font& font) {
  editor_.applyFontToAllText(font);
  editor_.setFont(font);
}

void TextField::setPadding(int vertical, int left, int right) {
  padV_ = vertical;
  padL_ = left;
  padR_ = right;
  resized();
}

void TextField::setLeadingIcon(Icon icon, float size, int left, juce::Colour colour) {
  leading_ = LeadingIcon{icon, size, left, colour};
  repaint();
}

void TextField::setClearButton(int size, int right) {
  clear_ = std::make_unique<IconButton>(Icon::X, size, size);
  clear_->setActive(false);
  clear_->setHelpText("Clear");
  clear_->onClick = [this] {
    editor_.setText({}, /*sendChangeMessage=*/true);
    focus();  // the press dropped the field's focus; clearing is a prelude to retyping
    if (onClear) onClear();
  };
  clearRight_ = right;
  addChildComponent(*clear_);
  clear_->setVisible(editor_.getText().isNotEmpty());
  resized();
}

void TextField::focus() {
  if (editor_.isShowing()) editor_.grabKeyboardFocus();
  editor_.selectAll();
}

void TextField::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::fill(g, box, radius_, background_);
  if (border_) paint::border(g, box, radius_, *border_);
  if (leading_)
    Icons::draw(g, leading_->icon,
                juce::Rectangle<float>(leading_->size, leading_->size)
                    .withCentre({leading_->left + leading_->size / 2, box.getCentreY()}),
                leading_->colour);
}

void TextField::resized() {
  editor_.setBounds(getLocalBounds().withTrimmedLeft(padL_).withTrimmedRight(padR_).reduced(0, padV_));
  if (clear_) clear_->setCentrePosition(getWidth() - clearRight_ - clear_->getWidth() / 2, getHeight() / 2);
}

void TextField::textEditorTextChanged(juce::TextEditor&) {
  if (clear_) clear_->setVisible(editor_.getText().isNotEmpty());
  if (onChange) onChange(editor_.getText());
}
void TextField::textEditorReturnKeyPressed(juce::TextEditor&) {
  if (onEnter) onEnter();
}
void TextField::textEditorEscapeKeyPressed(juce::TextEditor&) {
  if (onEscape) onEscape();
}
void TextField::textEditorFocusLost(juce::TextEditor&) {
  if (onBlur) onBlur();
}

// The editor has no focus-gained callback of its own.
void TextField::globalFocusChanged(juce::Component* focused) {
  const bool now = focused == &editor_;
  if (now == focused_) return;
  focused_ = now;
  if (now && onFocus) onFocus();
}

}  // namespace t3k::ui
