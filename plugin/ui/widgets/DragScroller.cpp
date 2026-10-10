#include "DragScroller.h"

#include <cmath>

namespace t3k::ui {

DragScroller::DragScroller(Axis axis, Keys keys) : axis_(axis), keys_(keys) {
  const bool vertical = axis == Axis::vertical;
  setScrollBarsShown(false, false, vertical, !vertical);
  setScrollOnDragMode(ScrollOnDragMode::all);
  // Not a Tab stop: the controls inside are, and the view follows them.
  setWantsKeyboardFocus(false);
  juce::Desktop::getInstance().addFocusChangeListener(this);
}

DragScroller::~DragScroller() { juce::Desktop::getInstance().removeFocusChangeListener(this); }

void DragScroller::visibleAreaChanged(const juce::Rectangle<int>&) {
  if (onScroll) onScroll();
}

bool DragScroller::panning(const juce::Component& c) {
  for (auto* v = c.findParentComponentOfClass<juce::Viewport>(); v != nullptr;
       v = v->findParentComponentOfClass<juce::Viewport>())
    if (v->isCurrentlyScrollingOnDrag()) return true;
  return false;
}

DragScroller::Span DragScroller::span() const {
  const auto* content = getViewedComponent();
  if (axis_ == Axis::vertical)
    return {getViewPositionY(), getMaximumVisibleHeight(), content != nullptr ? content->getHeight() : 0};
  return {getViewPositionX(), getMaximumVisibleWidth(), content != nullptr ? content->getWidth() : 0};
}

void DragScroller::setSpanPosition(int position) {
  // Viewport clamps to the content's range.
  if (axis_ == Axis::vertical)
    setViewPosition(getViewPositionX(), position);
  else
    setViewPosition(position, getViewPositionY());
}

bool DragScroller::scrollByKey(const juce::KeyPress& key) {
  using KP = juce::KeyPress;
  const auto s = span();
  if (s.content <= s.visible) return false;
  const bool vertical = axis_ == Axis::vertical;
  const int back = vertical ? KP::upKey : KP::leftKey, forward = vertical ? KP::downKey : KP::rightKey;
  const int page = juce::jmax(kKeyLineStep, s.visible - kKeyLineStep);
  int target;
  if (key == back)
    target = s.position - kKeyLineStep;
  else if (key == forward)
    target = s.position + kKeyLineStep;
  else if (key == KP::pageUpKey)
    target = s.position - page;
  else if (key == KP::pageDownKey)
    target = s.position + page;
  else if (key == KP::homeKey)
    target = 0;
  else if (key == KP::endKey)
    target = s.content - s.visible;
  else
    return false;
  setSpanPosition(target);
  return true;
}

bool DragScroller::keyPressed(const juce::KeyPress& key) { return keys_ == Keys::scroll && scrollByKey(key); }

void DragScroller::reveal(const juce::Component& target, int margin) {
  const auto* content = getViewedComponent();
  if (content == nullptr || !content->isParentOf(&target)) return;
  // The target's box in the content's space. Crossing a nested scroller on
  // the way up, the box is the part of its frame the target shows in (or
  // the frame itself while it is scrolled out of it): that scroller brings
  // the target into its own frame, and the frame is what this one shows.
  auto box = target.getLocalBounds();
  for (const auto* c = &target; c != content; c = c->getParentComponent()) {
    const auto* parent = c->getParentComponent();
    box = parent->getLocalArea(c, box);
    if (dynamic_cast<const juce::Viewport*>(parent) != nullptr) {
      const auto frame = parent->getLocalBounds();
      box = box.intersects(frame) ? box.getIntersection(frame) : frame;
    }
  }
  const auto s = span();
  const bool vertical = axis_ == Axis::vertical;
  const int start = (vertical ? box.getY() : box.getX()) - margin;
  const int end = (vertical ? box.getBottom() : box.getRight()) + margin;
  // Past the top edge (or too tall to fit): align its start. Past the
  // bottom edge: align its end. Within view: leave the page where it is.
  if (start < s.position || end - start > s.visible)
    setSpanPosition(start);
  else if (end > s.position + s.visible)
    setSpanPosition(end - s.visible);
}

void DragScroller::globalFocusChanged(juce::Component* focused) {
  if (focused != nullptr) reveal(*focused, focusMargin_);  // only a descendant of the content moves the view
}

void DragScroller::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
  if (axis_ != Axis::horizontal || e.mods.isAltDown() || e.mods.isCtrlDown() || e.mods.isCommandDown()) {
    juce::Viewport::mouseWheelMove(e, wheel);
    return;
  }
  // The dominant axis alone; a tie is native sideways input. Whole pixels
  // move the view and the fraction carries over, so a slow gesture creeps
  // instead of jumping a rounded-up pixel per event.
  const float delta = std::abs(wheel.deltaY) > std::abs(wheel.deltaX) ? wheel.deltaY : wheel.deltaX;
  wheelRemainder_ += delta * kWheelPixelsPerUnit;
  const int step = static_cast<int>(wheelRemainder_);
  wheelRemainder_ -= static_cast<float>(step);
  if (step == 0) return;
  const auto before = getViewPosition();
  setViewPosition(before.translated(-step, 0));
  if (getViewPosition() == before) juce::Component::mouseWheelMove(e, wheel);  // at an end: the parent's
}

}  // namespace t3k::ui
