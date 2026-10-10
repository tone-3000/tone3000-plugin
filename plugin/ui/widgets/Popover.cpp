#include "Popover.h"

#include <algorithm>

#include "SecondaryPress.h"
#include "core/NoDefaultFocus.h"

namespace t3k::ui {

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
  keyboardOpened_ = anchor.hasKeyboardFocus(false);
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
  point_ = point;
  if (getTitle().isEmpty()) setTitle("Context menu");
  host->overlayLayer().addAndMakeVisible(this);
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
  // Opened from a focused anchor, or a row focused since (a click never
  // focuses a row): the keyboard is driving, so it is back on the anchor
  // when the panel goes. A mouse-opened panel leaves nothing focused, and
  // the host's keys work again.
  auto* focused = getCurrentlyFocusedComponent();
  const bool toAnchor = keyboardOpened_ || (focused != nullptr && focused != this && isParentOf(focused));
  keyboardOpened_ = false;
  juce::Desktop::getInstance().removeGlobalMouseListener(&watcher_);
  target_.reset();
  if (auto* parent = getParentComponent()) parent->removeChildComponent(this);
  if (toAnchor && anchor_ != nullptr && anchor_->isShowing() && anchor_->getWantsKeyboardFocus())
    anchor_->grabKeyboardFocus();
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
  return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::popupMenu);
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
