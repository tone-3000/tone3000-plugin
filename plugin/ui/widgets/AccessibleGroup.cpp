#include "AccessibleGroup.h"

#include <vector>

namespace t3k::ui::a11y {

namespace {
const juce::Identifier kGroupFlag{"t3k.a11yGroup"};
}  // namespace

void makeGroup(juce::Component& component, const juce::String& title) {
  component.setTitle(title);
  // Not keyboardFocusContainer: that would split the Tab order per group.
  component.setFocusContainerType(juce::Component::FocusContainerType::focusContainer);
  component.getProperties().set(kGroupFlag, true);
  component.invalidateAccessibilityHandler();
}

bool isGroup(const juce::Component& component) { return component.getProperties().contains(kGroupFlag); }

std::unique_ptr<juce::AccessibilityHandler> groupHandler(juce::Component& component) {
  return std::make_unique<juce::AccessibilityHandler>(component, juce::AccessibilityRole::group);
}

AccessibleGroup::AccessibleGroup(const juce::String& title, bool clickThrough) {
  makeGroup(*this, title);
  if (clickThrough) setInterceptsMouseClicks(false, true);
}

void AccessibleGroup::fitToChildren() {
  juce::Rectangle<int> box;
  bool any = false;
  for (auto* child : getChildren()) {
    box = any ? box.getUnion(child->getBounds()) : child->getBounds();
    any = true;
  }
  if (!any) return;
  for (auto* child : getChildren()) child->setTopLeftPosition(child->getPosition() - box.getPosition());
  setBounds(box + getPosition());
}

bool isGroupKey(const juce::KeyPress& key, bool& forward) {
  const auto mods = key.getModifiers();
  if (mods.isShiftDown() || mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown()) return false;
  const auto c = key.getTextCharacter();
  if (c == '.' || (c == 0 && key.getKeyCode() == '.')) {
    forward = true;
    return true;
  }
  if (c == ',' || (c == 0 && key.getKeyCode() == ',')) {
    forward = false;
    return true;
  }
  return false;
}

juce::Component* enclosingGroup(juce::Component* component, const juce::Component* scope) {
  for (auto* c = component; c != nullptr && c != scope; c = c->getParentComponent())
    if (isGroup(*c)) return c;
  return nullptr;
}

juce::Component* moveToGroup(juce::Component& scope, bool forward) {
  struct Run {
    juce::Component* group;
    juce::Component* first;
  };
  std::vector<Run> runs;
  int current = -1;
  auto* focused = juce::Component::getCurrentlyFocusedComponent();
  for (auto* c : juce::KeyboardFocusTraverser().getAllComponents(&scope)) {
    if (!c->isShowing()) continue;
    auto* group = enclosingGroup(c, &scope);
    if (runs.empty() || runs.back().group != group) runs.push_back({group, c});
    if (focused != nullptr && (c == focused || c->isParentOf(focused))) current = static_cast<int>(runs.size()) - 1;
  }
  const int n = static_cast<int>(runs.size());
  if (n == 0) return nullptr;

  // From nothing focused (or focus somewhere outside the order) start just
  // outside the ends, so the first step lands on the first or last run.
  int index = current >= 0 ? current : (forward ? -1 : n);
  for (int step = 0; step < n; ++step) {
    index = ((forward ? index + 1 : index - 1) % n + n) % n;
    const auto& run = runs[static_cast<size_t>(index)];
    if (run.group == nullptr) continue;
    run.first->grabKeyboardFocus();
    return run.group;
  }
  return nullptr;
}

}  // namespace t3k::ui::a11y
