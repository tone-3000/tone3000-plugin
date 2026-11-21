// Named groups of related controls, for screen readers and the group keys.
//
// JUCE builds its accessibility tree from focus containers, not from the
// component tree: a control's accessible parent is its nearest focus
// container. Without containers every control in the plugin is one flat
// list under the root. A group here is a plain focus container (never a
// *keyboard* focus container, so Tab and Shift+Tab still walk the whole
// window in one order) with the group role and a title, which VoiceOver
// presents as a container to step into (VO+Shift+Down) and out of
// (VO+Shift+Up), and which NVDA and Narrator announce as focus enters it.
//
// The same groups are the stops for the period and comma keys (moveToGroup): the
// next or previous run of controls belonging to a different group. Runs,
// not groups, because a group with a nested group in the middle (a tone
// editor with its EQ row) appears twice in the Tab order and should be
// reached at each appearance.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace t3k::ui::a11y {

// Turn an existing component into a named group. Its class overrides
// createAccessibilityHandler() to return groupHandler(*this) so the role is
// a group on Windows too (an unspecified role is a custom control there).
void makeGroup(juce::Component& component, const juce::String& title);
bool isGroup(const juce::Component& component);
std::unique_ptr<juce::AccessibilityHandler> groupHandler(juce::Component& component);

// A wrapper for controls that have no common parent of their own. With
// `clickThrough` the wrapper itself never takes the pointer, so it can sit
// over a parent that paints or handles clicks in the gaps.
class AccessibleGroup : public juce::Component {
public:
  explicit AccessibleGroup(const juce::String& title, bool clickThrough = false);

  // Shrink to the children's bounding box (hidden ones included, since
  // their slots are reserved), keeping them where they are on screen. Lay
  // the children out with the group at the parent's origin, then call this,
  // so the frame VoiceOver outlines matches what the group covers.
  void fitToChildren();

  std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override { return groupHandler(*this); }
};

// The plain layout components between groups add an empty, unnamed group
// to the screen reader's list; they return this from
// createAccessibilityHandler() to stay out of it.
inline std::unique_ptr<juce::AccessibilityHandler> ignoredHandler(juce::Component& component) {
  return std::make_unique<juce::AccessibilityHandler>(component, juce::AccessibilityRole::ignored);
}

// Comma (backward) or period (forward), unmodified. A modifier leaves the
// key alone: Shift types < and > on most layouts, and the host may bind
// Ctrl / Command / Alt combinations.
bool isGroupKey(const juce::KeyPress& key, bool& forward);

// The innermost group around `component`, stopping at `scope`.
juce::Component* enclosingGroup(juce::Component* component, const juce::Component* scope);

// Focus the first control of the next (or previous) run of controls in a
// group, in `scope`'s Tab order, wrapping at the ends; from nothing
// focused, the first (or last) run. Controls outside any group are skipped.
// Returns the group moved to, or nullptr if `scope` has no groups.
juce::Component* moveToGroup(juce::Component& scope, bool forward);

}  // namespace t3k::ui::a11y
