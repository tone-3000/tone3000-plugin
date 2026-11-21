// Readout chip that doubles as text entry (BlockEqView.tsx EditableChip): a
// 20px SEGMENTED_TRACK box with a SUBTLE mono label and a fixed-width white
// mono value. Click to type (the box empties and the current reading becomes
// the placeholder); Enter / blur commit, Escape cancels, an empty commit is a
// cancel. The value area is sized to the longest possible reading so the
// chip never resizes while values change or while editing.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

#include "core/Help.h"

namespace t3k::ui {

class EditableChip : public juce::Component, private juce::TextEditor::Listener {
public:
  EditableChip(juce::String label, int valueWidth, help::Key help);

  void setText(const juce::String& text, const juce::String& editText);
  // Cut bands have no gain: DISABLED_OPACITY, plain cursor, no hint.
  void setDisabledLook(bool disabled);

  std::function<void(const juce::String& raw)> onCommit;

  // Keyboard and screen readers. The chip is a slider named by its title
  // (setTitle; the painted label by default), valued in `spoken` text with
  // its unit. Enter or Space opens the editor; arrows step the live
  // reading `numeric` returns (Shift: fine) through onSetValue, which
  // clamps. A mouse click never takes keyboard focus.
  struct Numeric {
    double value = 0, min = 0, max = 1, step = 1, fineStep = 0.1;
  };
  std::function<std::optional<Numeric>()> numeric;
  std::function<void(double)> onSetValue;
  void setSpokenValue(const juce::String& spoken);
  juce::String spokenValue() const { return spoken_.isNotEmpty() ? spoken_ : text_; }
  bool isDisabledLook() const { return disabled_; }
  // Opens the editor from the keyboard; focus comes back to the chip after.
  void beginKeyboardEdit();

  void paint(juce::Graphics& g) override;
  void resized() override;
  void mouseUp(const juce::MouseEvent& e) override;
  bool keyPressed(const juce::KeyPress& key) override;
  void focusGained(FocusChangeType) override { repaint(); }
  void focusLost(FocusChangeType) override { repaint(); }
  std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
  static constexpr float kFontPx = 12;
  static constexpr int kPadX = 4;
  static constexpr int kGap = 6;

  void beginEdit();
  void commit();
  void cancel();
  void textEditorReturnKeyPressed(juce::TextEditor&) override { commit(); }
  void textEditorEscapeKeyPressed(juce::TextEditor&) override { cancel(); }
  void textEditorFocusLost(juce::TextEditor&) override { commit(); }
  juce::Rectangle<int> valueBounds() const;

  void endEdit();

  juce::String label_, text_, editText_, spoken_;
  int valueWidth_;
  help::Key help_;
  bool disabled_ = false;
  bool editing_ = false;
  bool keyboardEdit_ = false;  // opened from the keyboard: focus returns to the chip
  juce::TextEditor editor_;
};

}  // namespace t3k::ui
