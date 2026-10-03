// A scroll area along one axis with hidden scrollbars that a touch drag
// pans (the web's hide-scrollbar containers). Once a drag has become a
// scroll the press it started with is spent: the button under the finger
// (Clickable) sees the pan and lets go instead of firing on release.
// Components that drag for themselves (knobs, tiles) opt out of the pan
// with setViewportIgnoreDragFlag.
//
// A plain (vertical) wheel pans a sideways scroller, as the web's
// useHorizontalWheelScroll did, by the gesture's dominant axis. JUCE's own
// remap takes deltaX whenever it is non-zero, and a mostly vertical
// trackpad gesture jitters a few sideways pixels of either sign, so the pan
// flip-flopped between those and the real motion until the gesture was big
// enough to be purely vertical.
//
// The keyboard is the owner's call. juce::Viewport takes the arrow and page
// keys only while a scrollbar shows, and this one never does, so a page
// that scrolls on them (Settings) routes them here through scrollByKey and
// keeps the keys off the lanes that mean something else by them (the chain
// lane's arrows sort tiles). The scroller is not a Tab stop either: the
// controls inside are, and the owner reveals the one that has focus.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace t3k::ui {

class DragScroller : public juce::Viewport {
public:
  enum class Axis { horizontal, vertical };

  explicit DragScroller(Axis axis);

  // Keyboard scrolling along the axis, as a browser scrolls a document: the
  // arrows step a line, Page Up / Down a page less a line of overlap, Home
  // / End go to the ends. Plain keys only, so a modified arrow stays with
  // whoever else wants it. True when the key is one of these and the
  // content overflows (at an end the key is still spent, not handed on).
  bool scrollByKey(const juce::KeyPress& key);

  // Scroll the least that brings `target` (a descendant of the content)
  // into view with `margin` pixels to spare along the axis; nothing when it
  // already is (scrollIntoViewIfNeeded).
  void reveal(const juce::Component& target, int margin);

  // The view moved (scroll, drag or programmatic).
  std::function<void()> onScroll;

  void visibleAreaChanged(const juce::Rectangle<int>&) override;
  void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

  static constexpr int kKeyLineStep = 40;  // a browser's arrow-key step

private:
  // Viewport's rate (14 × its 16px single step), so a native sideways
  // gesture pans as it did.
  static constexpr float kWheelPixelsPerUnit = 14 * 16;

  // The view's offset, length and the content's length along the axis.
  struct Span {
    int position, visible, content;
  };
  Span span() const;
  void setSpanPosition(int position);

  const Axis axis_;
  float wheelRemainder_ = 0;  // sub-pixel carry between wheel events
};

}  // namespace t3k::ui
