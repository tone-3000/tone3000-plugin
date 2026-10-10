// JUCE's tab order without its default: when the focused component goes
// away (the button that opened a takeover, a menu row after its pick) or the
// window activates, JUCE hands focus to the container's first focusable
// component. A browser leaves nothing focused instead, and so do the root
// and the popovers, by returning this traverser.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace t3k::ui {

class NoDefaultFocus : public juce::KeyboardFocusTraverser {
public:
  juce::Component* getDefaultComponent(juce::Component*) override { return nullptr; }
};

}  // namespace t3k::ui
