// Right-click (or two-finger tap) handling for a control group: the
// `onContextMenu` on a wrapper div. JUCE mouse events don't bubble, so
// controls that only act on primary presses (Knob, ChromeIconButton, the
// advert pills) hand a secondary press to the nearest ancestor implementing
// SecondaryPressTarget, and the group's own empty space reaches it directly
// through Component::mouseDown.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace t3k::ui {

// The press that means "context", the way a browser fires contextmenu: any
// non-primary button, plus Ctrl-click on macOS (isPopupMenu carries that
// platform rule). Touch is never secondary here; a hold stands in for it.
inline bool isSecondaryPress(const juce::MouseEvent& e) {
  return !e.source.isTouch() && (!e.mods.isLeftButtonDown() || e.mods.isPopupMenu());
}

class SecondaryPressTarget {
public:
  virtual ~SecondaryPressTarget() = default;
  virtual void secondaryPress(const juce::MouseEvent& e) = 0;
};

// Called by a control from its mouseDown for a non-primary press. Returns
// true when an ancestor took it.
inline bool forwardSecondaryPress(juce::Component& from, const juce::MouseEvent& e) {
  if (auto* target = from.findParentComponentOfClass<SecondaryPressTarget>()) {
    target->secondaryPress(e);
    return true;
  }
  return false;
}

}  // namespace t3k::ui
