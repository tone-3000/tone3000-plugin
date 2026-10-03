#include "DragScroller.h"

#include <cmath>

namespace t3k::ui {

DragScroller::DragScroller(Axis axis) : axis_(axis) {
  const bool vertical = axis == Axis::vertical;
  setScrollBarsShown(false, false, vertical, !vertical);
  setScrollOnDragMode(ScrollOnDragMode::nonHover);
  // Not a Tab stop: the controls inside are, and the page follows them.
  setWantsKeyboardFocus(false);
}

void DragScroller::visibleAreaChanged(const juce::Rectangle<int>&) {
  if (onScroll) onScroll();
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

void DragScroller::reveal(const juce::Component& target, int margin) {
  const auto* content = getViewedComponent();
  if (content == nullptr || !content->isParentOf(&target)) return;
  const auto box = content->getLocalArea(&target, target.getLocalBounds());
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
