#include "Popover.h"

#include <algorithm>

#include "SecondaryPress.h"
#include "core/NoDefaultFocus.h"

namespace t3k::ui {

namespace {

// The keyboard-focused control that opened a panel anchored on `anchor`:
// the anchor itself, something inside it, or a control in the same
// right-click group (a group's power button opening the panel on its knob).
// A mouse open leaves nothing focused here (no click takes focus), so it
// returns nullptr and the panel leaves focus alone on close, as before.
juce::Component* focusedOpener(juce::Component& anchor) {
  auto* focused = juce::Component::getCurrentlyFocusedComponent();
  if (focused == nullptr) return nullptr;
  if (focused == &anchor || anchor.isParentOf(focused)) return focused;
  for (auto* c = anchor.getParentComponent(); c != nullptr; c = c->getParentComponent())
    if (dynamic_cast<SecondaryPressTarget*>(c) != nullptr) return c->isParentOf(focused) ? focused : nullptr;
  return nullptr;
}

// A panel holding sliders or text fields (the Advanced panels, the save
// panel) is a dialog to a screen reader; one of rows only is a menu.
bool holdsControls(juce::Component& root) {
  for (auto* child : root.getChildren()) {
    if (auto* handler = child->getAccessibilityHandler()) {
      const auto role = handler->getRole();
      if (role == juce::AccessibilityRole::slider || role == juce::AccessibilityRole::editableText) return true;
    }
    if (holdsControls(*child)) return true;
  }
  return false;
}

}  // namespace

Popover::Popover() {
  setWantsKeyboardFocus(true);
  setMouseClickGrabsKeyboardFocus(false);
  setFocusContainerType(FocusContainerType::keyboardFocusContainer);
}

Popover::~Popover() { juce::Desktop::getInstance().removeGlobalMouseListener(&watcher_); }

void Popover::open(juce::Component& anchor, Align align, int gap, int inset, Placement placement) {
  auto* host = anchor.findParentComponentOfClass<OverlayHost>();
  jassert(host != nullptr);
  if (host == nullptr) return;

  anchor_ = &anchor;
  contextReturnFocus_ = focusedOpener(anchor);
  keyboardOpened_ = contextReturnFocus_ != nullptr;
  align_ = align;
  placement_ = placement;
  gap_ = gap;
  inset_ = inset;
  // Named after its anchor for screen readers ("Sort menu"), unless the
  // owner named it.
  if (getTitle().isEmpty()) {
    const auto name = anchor.getTitle().isNotEmpty() ? anchor.getTitle() : anchor.getName();
    setTitle((name.isNotEmpty() ? name + " " : juce::String()) + "menu");
  }
  host->overlayLayer().addAndMakeVisible(this);
  invalidateAccessibilityHandler();  // the role follows the rows built by now
  reposition();
  follow(anchor);
  watchOutsidePresses();
  if (isShowing()) grabKeyboardFocus();  // offscreen (testbed capture) has no peer
}

void Popover::openAt(juce::Component& context, juce::Point<int> point) {
  auto* host = context.findParentComponentOfClass<OverlayHost>();
  jassert(host != nullptr);
  if (host == nullptr) return;

  anchor_ = nullptr;
  contextReturnFocus_ = context.hasKeyboardFocus(true) ? getCurrentlyFocusedComponent() : nullptr;
  point_ = point;
  if (getTitle().isEmpty()) setTitle("Context menu");
  host->overlayLayer().addAndMakeVisible(this);
  invalidateAccessibilityHandler();
  placeAt(context, point);
  follow(context);
  watchOutsidePresses();
  if (isShowing()) grabKeyboardFocus();
}

void Popover::placeAt(juce::Component& context, juce::Point<int> point) {
  auto* overlay = getParentComponent();
  if (overlay == nullptr) return;
  // Positions are pre-transform: overlay px over the adopted scale.
  const float k = adoptScaleOf(context);
  const auto p = overlay->getLocalPoint(&context, point.toFloat()) / k;
  const int maxX = std::max(0, juce::roundToInt(overlay->getWidth() / k) - getWidth());
  const int maxY = std::max(0, juce::roundToInt(overlay->getHeight() / k) - getHeight());
  setTopLeftPosition(juce::jlimit(0, maxX, juce::roundToInt(p.x)), juce::jlimit(0, maxY, juce::roundToInt(p.y)));
}

void Popover::follow(juce::Component& target) { target_ = std::make_unique<TargetWatcher>(*this, target); }

namespace {
// The target has left a viewport it sits in: scrolled clean out of the
// lane, the list or the page.
bool scrolledAway(juce::Component& target, juce::Component& overlay) {
  const auto box = overlay.getLocalArea(&target, target.getLocalBounds());
  for (auto* v = target.findParentComponentOfClass<juce::Viewport>(); v != nullptr;
       v = v->findParentComponentOfClass<juce::Viewport>())
    if (!overlay.getLocalArea(v, v->getLocalBounds()).intersects(box)) return true;
  return false;
}
}  // namespace

void Popover::targetMoved() {
  auto* target = target_ != nullptr ? target_->getComponent() : nullptr;
  auto* overlay = getParentComponent();
  if (target == nullptr || overlay == nullptr) return;
  if (scrolledAway(*target, *overlay)) {
    // Not from inside the watcher's own callback: dismissing deletes it.
    juce::Component::SafePointer<Popover> self(this);
    juce::MessageManager::callAsync([self] {
      if (self != nullptr && self->isOpen()) self->dismiss();
    });
    return;
  }
  if (anchor_ != nullptr)
    reposition();
  else
    placeAt(*target, point_);
}

void Popover::watchOutsidePresses() {
  // The press in flight, if the panel is opening from one (see outsidePress).
  // A keyboard or timer open records a press already fully delivered, which
  // no later event carries.
  openingPress_ = juce::Desktop::getInstance().getMainMouseSource().getLastMouseDownTime();
  juce::Desktop::getInstance().addGlobalMouseListener(&watcher_);
}

void Popover::reposition() {
  auto* overlay = getParentComponent();
  if (overlay == nullptr || anchor_ == nullptr) return;
  // The anchor's box in the panel's own (pre-transform) space, where gap and
  // inset are in the anchor's px, as they were written.
  const float k = adoptScaleOf(*anchor_);
  const auto a = overlay->getLocalArea(anchor_.getComponent(), anchor_->getLocalBounds().toFloat()) / k;
  const float x = align_ == Align::left ? a.getX() + inset_ : a.getRight() - inset_ - getWidth();
  const float y = placement_ == Placement::below ? a.getBottom() + gap_ : a.getY() - gap_ - getHeight();
  setTopLeftPosition(juce::roundToInt(x), juce::roundToInt(y));
}

float Popover::adoptScaleOf(const juce::Component& source) {
  auto* overlay = getParentComponent();
  if (overlay == nullptr || source.getWidth() <= 0) return 1.0f;
  const auto a = overlay->getLocalArea(&source, source.getLocalBounds().toFloat());
  const float k = a.getWidth() / static_cast<float>(source.getWidth());
  setTransform(juce::approximatelyEqual(k, 1.0f) ? juce::AffineTransform() : juce::AffineTransform::scale(k));
  return k;
}

void Popover::close() {
  if (!isOpen()) return;
  // Opened from a focused control, or a row focused since (a click never
  // focuses a row): the keyboard is driving, so focus goes back to the
  // opener (a group's power button even when the panel sits on its knob),
  // else the anchor. A mouse-opened panel leaves nothing focused, and the
  // host's keys work again.
  auto* focused = getCurrentlyFocusedComponent();
  const bool toAnchor = keyboardOpened_ || (focused != nullptr && focused != this && isParentOf(focused));
  keyboardOpened_ = false;
  juce::Desktop::getInstance().removeGlobalMouseListener(&watcher_);
  target_.reset();
  if (auto* parent = getParentComponent()) parent->removeChildComponent(this);
  if (contextReturnFocus_ != nullptr && contextReturnFocus_->isShowing()
           && contextReturnFocus_->getWantsKeyboardFocus())
    contextReturnFocus_->grabKeyboardFocus();
  else if (toAnchor && anchor_ != nullptr && anchor_->isShowing() && anchor_->getWantsKeyboardFocus())
    anchor_->grabKeyboardFocus();
  contextReturnFocus_ = nullptr;
}

void Popover::dismiss() {
  close();
  if (onDismiss) onDismiss();
}

bool Popover::keyPressed(const juce::KeyPress& key) {
  if (key == juce::KeyPress::escapeKey) {
    dismiss();
    return true;
  }
  const bool tab = key.isKeyCode(juce::KeyPress::tabKey);
  if (key == juce::KeyPress::downKey || (tab && !key.getModifiers().isShiftDown())) {
    focusRow(true);
    return true;
  }
  if (key == juce::KeyPress::upKey || tab) {
    focusRow(false);
    return true;
  }
  return false;
}

std::unique_ptr<juce::ComponentTraverser> Popover::createKeyboardFocusTraverser() {
  return std::make_unique<NoDefaultFocus>();
}

std::unique_ptr<juce::AccessibilityHandler> Popover::createAccessibilityHandler() {
  // Screen readers only: the keys, never in the hint bar (no help text).
  struct Handler : juce::AccessibilityHandler {
    Handler(Popover& panel, bool dialog)
        : juce::AccessibilityHandler(panel, dialog ? juce::AccessibilityRole::dialogWindow
                                                   : juce::AccessibilityRole::popupMenu),
          dialog_(dialog) {}
    juce::String getHelp() const override {
      return dialog_ ? "Up and Down or Tab move between controls. Left and Right adjust. Escape closes."
                     : "Up and Down move. Enter chooses. Escape closes.";
    }
    bool dialog_;
  };
  return std::make_unique<Handler>(*this, holdsControls(*this));
}

void Popover::focusRow(bool next) {
  const auto rows = juce::KeyboardFocusTraverser().getAllComponents(this);
  if (rows.empty()) return;
  const auto at = std::find(rows.begin(), rows.end(), getCurrentlyFocusedComponent());
  juce::Component* target = nullptr;
  if (at == rows.end())
    target = next ? rows.front() : rows.back();
  else
    target = rows[(static_cast<size_t>(at - rows.begin()) + (next ? 1 : rows.size() - 1)) % rows.size()];
  target->grabKeyboardFocus();  // a row in a scrolled list: the list (DragScroller) follows the focus
}

void Popover::outsidePress(const juce::MouseEvent& e) {
  // JUCE delivers a press to the component first and to the global
  // listeners after, so a panel opened from a mouseDown is watching by the
  // time that same press reaches the desktop list; it must not count.
  if (e.mouseDownTime == openingPress_) return;
  if (primaryOnly && isSecondaryPress(e)) return;
  auto* c = e.eventComponent;
  // Other windows (another plugin instance) don't count as "outside".
  if (c == nullptr || c->getTopLevelComponent() != getTopLevelComponent()) return;
  if (isParentOf(c) || c == this) return;
  if (!dismissOnAnchorPress && anchor_ != nullptr && (c == anchor_ || anchor_->isParentOf(c))) return;
  dismiss();
}

}  // namespace t3k::ui
