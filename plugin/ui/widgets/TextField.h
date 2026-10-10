// Single-line text input styled like the web UI's inputs (PresetBar.tsx
// inputStyle): #1C1C1E fill, hairline border, rounded, 13px body text with
// a placeholder. Owns the box painting itself so each instance can pick its
// own radius/padding; the embedded juce::TextEditor is chrome-less.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>

#include "core/Icons.h"
#include "IconButton.h"

namespace t3k::ui {

class TextField : public juce::Component, private juce::TextEditor::Listener, private juce::FocusChangeListener {
public:
  TextField();
  ~TextField() override;

  void setPlaceholder(const juce::String& text);
  void setText(const juce::String& text, bool notify = false);
  juce::String text() const { return editor_.getText(); }
  void setFontSize(float px);
  // Any face (the MIDI CC field is Roboto Mono).
  void setFont(const juce::Font& font);
  void setCornerRadius(float radius) { radius_ = radius; }
  // CSS padding: vertical, left, right.
  void setPadding(int vertical, int left, int right);
  void setBackground(juce::Colour colour) { background_ = colour; }
  void setBorder(std::optional<juce::Colour> colour) { border_ = colour; }
  void setJustification(juce::Justification just) { editor_.setJustification(just); }
  // Glyph inside the field at `left` px, vertically centred (the search
  // fields' magnifier); the left padding should leave room for it.
  void setLeadingIcon(Icon icon, float size, int left, juce::Colour colour);
  // An × `right` px from the right edge while there is text; pressing it
  // empties the field (onChange) and fires onClear. The right padding
  // should leave room for it.
  void setClearButton(int size, int right);
  // Select-all + focus.
  void focus();
  bool hasFocus() const { return editor_.hasKeyboardFocus(true); }

  std::function<void(const juce::String&)> onChange;
  std::function<void()> onEnter;
  std::function<void()> onEscape;
  std::function<void()> onBlur;
  std::function<void()> onFocus;
  std::function<void()> onClear;

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  void textEditorTextChanged(juce::TextEditor&) override;
  void textEditorReturnKeyPressed(juce::TextEditor&) override;
  void textEditorEscapeKeyPressed(juce::TextEditor&) override;
  void textEditorFocusLost(juce::TextEditor&) override;
  void globalFocusChanged(juce::Component* focused) override;

  juce::TextEditor editor_;
  bool focused_ = false;
  float radius_ = 10.0f;
  int padV_ = 9, padL_ = 12, padR_ = 12;
  juce::Colour background_{0xff1c1c1e};
  std::optional<juce::Colour> border_;
  struct LeadingIcon {
    Icon icon;
    float size;
    int left;
    juce::Colour colour;
  };
  std::optional<LeadingIcon> leading_;
  std::unique_ptr<IconButton> clear_;
  int clearRight_ = 0;
};

}  // namespace t3k::ui
