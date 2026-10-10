// Base for every button in the UI: juce::Button plus the shared keyboard
// and screen-reader policy, so no subclass has to remember it.
//
// Focus. A button is in the Tab order but a mouse click never focuses it
// (macOS convention). So after any click nothing is focused and the host's
// transport keys keep working: unhandled Space / Enter go back to the DAW
// (NativeEditor::keyPressed). With a button Tab-focused, Enter presses it
// (juce::Button) while Space is still left for the host; Escape or a click
// elsewhere drops the focus (PluginRoot).
//
// Pans. A press that a scroll area turns into a pan (DragScroller, any
// pointer) is spent: the button lets go and does not fire on release.
// juce::Button alone would, since it counts "still over" by bounds, and
// the content pans along under the pointer.
//
// Name. Screen readers get accessibleName() and the full help hint as help.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace t3k::ui {

class Clickable : public juce::Button {
public:
  explicit Clickable(const juce::String& text = {});

  // The name a screen reader announces: setTitle(), else the button text,
  // else the component name, else the help hint's lead ("Undo: ..." -> "Undo").
  juce::String accessibleName() const;

  std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

  void mouseDrag(const juce::MouseEvent& e) override;
  void mouseUp(const juce::MouseEvent& e) override;
};

}  // namespace t3k::ui
