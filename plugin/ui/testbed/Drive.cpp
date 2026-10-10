#include "Drive.h"

namespace t3k::ui::testbed::drive {

namespace {
// isShowing() needs a window peer; offscreen captures have none.
bool shown(const juce::Component& c) {
  for (auto* p = &c; p != nullptr; p = p->getParentComponent())
    if (!p->isVisible()) return false;
  return true;
}
}  // namespace

juce::Component* find(juce::Component& root,
                      const std::function<bool(juce::Component&)>& pred) {
  for (auto* child : root.getChildren()) {
    if (child == nullptr) continue;
    if (pred(*child)) return child;
    if (auto* hit = find(*child, pred)) return hit;
  }
  return nullptr;
}

juce::Component* byHelpPrefix(juce::Component& root, const juce::String& prefix) {
  auto* hit = find(root, [&](juce::Component& c) {
    return shown(c) && c.getHelpText().startsWith(prefix);
  });
  jassert(hit != nullptr);
  return hit;
}

juce::Button* buttonNamed(juce::Component& root, const juce::String& name) {
  auto* hit = find(root, [&](juce::Component& c) {
    return shown(c) && dynamic_cast<juce::Button*>(&c) != nullptr && c.getName() == name;
  });
  return dynamic_cast<juce::Button*>(hit);
}

juce::TextEditor* inputWithPlaceholder(juce::Component& root, const juce::String& placeholder) {
  auto* hit = find(root, [&](juce::Component& c) {
    auto* e = dynamic_cast<juce::TextEditor*>(&c);
    return e != nullptr && shown(*e) && e->getTextToShowWhenEmpty() == placeholder;
  });
  return dynamic_cast<juce::TextEditor*>(hit);
}

void hover(PluginRoot& root, juce::Component& target) {
  // Nearest hinted ancestor, as HintTracker resolves a real pointer.
  for (auto* c = &target; c != nullptr; c = c->getParentComponent()) {
    if (c->getHelpText().isNotEmpty()) {
      root.services().hints.setHover(c->getHelpText());
      return;
    }
  }
}

void unhover(PluginRoot& root) { root.services().hints.setHover({}); }

void hoverAt(PluginRoot& root, juce::Component& target, juce::Point<int> local) {
  auto source = juce::Desktop::getInstance().getMainMouseSource();
  const auto pos = local.toFloat();
  const auto now = juce::Time::getCurrentTime();
  const juce::MouseEvent move(source, pos, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                              &target, &target, now, pos, now, 0, false);
  target.mouseEnter(move);
  target.mouseMove(move);
  hover(root, target);
}

void scrollIntoView(juce::Component& target) {
  for (auto* c = target.getParentComponent(); c != nullptr; c = c->getParentComponent()) {
    auto* viewport = dynamic_cast<juce::Viewport*>(c);
    if (viewport == nullptr || viewport->getViewedComponent() == nullptr) continue;
    const auto box = viewport->getViewedComponent()->getLocalArea(&target, target.getLocalBounds());
    auto view = viewport->getViewArea();
    if (box.getRight() > view.getRight()) view.setX(box.getRight() - view.getWidth());
    if (box.getX() < view.getX()) view.setX(box.getX());
    if (box.getBottom() > view.getBottom()) view.setY(box.getBottom() - view.getHeight());
    if (box.getY() < view.getY()) view.setY(box.getY());
    if (view.getPosition() != viewport->getViewPosition()) {
      viewport->setViewPosition(view.getPosition());
      wait(20);
    }
  }
}

void hoverPoint(PluginRoot& root, juce::Point<int> rootPos) {
  if (auto* under = root.getComponentAt(rootPos)) hoverAt(root, *under, under->getLocalPoint(&root, rootPos));
}

void click(PluginRoot& root, juce::Component& target, bool right) {
  // Like a user: scroll the target into view and move the pointer onto it
  // before pressing.
  scrollIntoView(target);
  hoverAt(root, target, target.getLocalBounds().getCentre());
  const auto rootPos = root.getLocalPoint(&target, target.getLocalBounds().getCentre());
  const juce::Component::SafePointer<juce::Component> alive(&target);
  // juce::Button's click needs the real pointer over it, so primary clicks
  // on buttons go through triggerClick; everything else gets a synthesized
  // press/release pair.
  if (auto* button = dynamic_cast<juce::Button*>(&target); button != nullptr && !right) {
    button->triggerClick();
    wait(20);
  } else {
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto pos = target.getLocalBounds().getCentre().toFloat();
    const auto mods = right ? juce::ModifierKeys::rightButtonModifier
                            : juce::ModifierKeys::leftButtonModifier;
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent down(source, pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now,
                                pos, now, 1, false);
    target.mouseDown(down);
    wait(20);
    // JUCE's mouseUp still carries the released button in mods.
    const juce::MouseEvent up(source, pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, pos, now,
                              1, false);
    target.mouseUp(up);
    wait(20);
  }
  // The pointer stays put after a click; if the target went away (a popover
  // closed), whatever is underneath now is hovered, as in the web shots.
  if (alive == nullptr || !alive->isShowing()) hoverPoint(root, rootPos);
}

void clickByHelp(PluginRoot& root, const juce::String& helpPrefix, bool right) {
  if (auto* c = byHelpPrefix(root, helpPrefix)) click(root, *c, right);
}

void fill(PluginRoot& root, const juce::String& placeholder, const juce::String& text) {
  if (auto* e = inputWithPlaceholder(root, placeholder)) {
    hover(root, *e);
    e->setText(text, true);
  }
}

void submit(PluginRoot& root, const juce::String& placeholder, const juce::String& text) {
  fill(root, placeholder, text);
  if (auto* e = inputWithPlaceholder(root, placeholder)) e->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
}

void wait(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

namespace {
void collectFocusables(juce::Component& container, std::vector<juce::Component*>& out);

// Nested keyboard focus containers keep their own order out of the outer
// one, whether they are Tab stops themselves (popovers) or not (the Settings
// takeover); walk into each that is showing.
void collectNested(juce::Component& parent, std::vector<juce::Component*>& out) {
  for (auto* c : parent.getChildren()) {
    if (!c->isVisible()) continue;
    if (c->isKeyboardFocusContainer())
      collectFocusables(*c, out);
    else
      collectNested(*c, out);
  }
}

void collectFocusables(juce::Component& container, std::vector<juce::Component*>& out) {
  for (auto* c : juce::KeyboardFocusTraverser().getAllComponents(&container)) out.push_back(c);
  collectNested(container, out);
}
}  // namespace

juce::StringArray unnamedFocusables(juce::Component& root) {
  std::vector<juce::Component*> focusables;
  collectFocusables(root, focusables);
  juce::StringArray problems;
  for (auto* c : focusables) {
    // Offscreen (no window) JUCE hands out no handlers; build the one the
    // component would have had.
    const auto handler = c->isAccessible() ? c->createAccessibilityHandler() : nullptr;
    if (handler != nullptr && handler->getTitle().isNotEmpty()) continue;
    // The mangled class name reads well enough ("N3t3k2ui10IconButtonE").
    const auto pos = root.getLocalPoint(c, juce::Point<int>());
    juce::String where = juce::String(typeid(*c).name()) + " at " + juce::String(pos.x) + "," + juce::String(pos.y);
    if (auto* parent = c->getParentComponent()) where << " in " << typeid(*parent).name();
    problems.add(where);
  }
  return problems;
}

void openSettings(PluginRoot& root) {
  clickByHelp(root, "Account:");
  wait(50);
  // Exact name: banner actions like "Open Settings" would substring-match too.
  if (auto* settings = buttonNamed(root, "Settings")) click(root, *settings);
  wait(200);
  unhover(root);
}

void openSystemSettings(PluginRoot& root) {
  openSettings(root);
  if (auto* tab = buttonNamed(root, "System Settings")) click(root, *tab);
  wait(500);
}

void scrollSettingsTo(PluginRoot& root, const juce::String& label) {
  if (auto* settings = root.settings()) settings->scrollToHeading(label);
  wait(300);
}

}  // namespace t3k::ui::testbed::drive
