// The one scroll area: along one axis, no scrollbars (the web's
// hide-scrollbar containers), and every way of moving it built in, so each
// list, lane and page in the UI scrolls the same on every device:
//
//   wheel / trackpad  Viewport's own, plus the sideways remap below.
//   drag              Any pointer's drag pans (ScrollOnDragMode::all): a
//                     finger, a pen, and a mouse, which is the only pan a
//                     wheel-less mouse or a touchscreen the OS presents as a
//                     mouse (Raspberry Pi OS's labwc default) ever gets. A
//                     press moves 8px before it is a pan, so a click is
//                     still a click. Once it is one the press is spent: the
//                     control under the pointer (Clickable, or any handler
//                     asking panning()) lets go instead of firing on release.
//                     Controls that drag for themselves (knobs, tiles, EQ
//                     dots, text) opt out with setViewportIgnoreDragFlag.
//   keyboard          Arrows, Page Up / Down, Home / End (scrollByKey), as a
//                     browser scrolls a document. The keys arrive two ways:
//                     bubbling up from a focused control inside that did not
//                     take them (keyPressed), or from the root with nothing
//                     focused (PluginRoot::FocusPolicy, to the screen's main
//                     scroller). A list whose arrows mean something else (a
//                     menu's rows, which Popover walks) is built with
//                     Keys::none and leaves them to its owner.
//   focus             The view follows keyboard focus: a Tab stop or a menu
//                     row that lands out of view scrolls into it (reveal),
//                     with the owner's focusMargin to spare.
//
// A plain (vertical) wheel pans a sideways scroller, as the web's
// useHorizontalWheelScroll did, by the gesture's dominant axis. JUCE's own
// remap takes deltaX whenever it is non-zero, and a mostly vertical
// trackpad gesture jitters a few sideways pixels of either sign, so the pan
// flip-flopped between those and the real motion until the gesture was big
// enough to be purely vertical.
//
// Not a Tab stop itself: the controls inside are.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace t3k::ui {

class DragScroller : public juce::Viewport, private juce::FocusChangeListener {
public:
  enum class Axis { horizontal, vertical };
  // Whether the scroll keys bubbling up from a focused control inside are
  // this scroller's (scroll) or its owner's (none: a menu walks its rows).
  enum class Keys { scroll, none };

  explicit DragScroller(Axis axis, Keys keys = Keys::scroll);
  ~DragScroller() override;

  Axis axis() const { return axis_; }

  // Keyboard scrolling along the axis, as a browser scrolls a document: the
  // arrows step a line, Page Up / Down a page less a line of overlap, Home
  // / End go to the ends. Plain keys only, so a modified arrow stays with
  // whoever else wants it. True when the key is one of these and the
  // content overflows (at an end the key is still spent, not handed on).
  bool scrollByKey(const juce::KeyPress& key);

  // Scroll the least that brings `target` (a descendant of the content)
  // into view with `margin` pixels to spare along the axis; nothing when it
  // already is (scrollIntoViewIfNeeded). A target inside a nested scroller
  // counts by that scroller's frame, which is where it will show.
  void reveal(const juce::Component& target, int margin);

  // Room kept around a control the keyboard focuses when the view scrolls
  // to it (a field's label above it, the next row below). Default none.
  void setFocusMargin(int margin) { focusMargin_ = margin; }

  // A press inside `c` has become a pan of a scroller around it, so the
  // release should not count as a click (Clickable does this itself).
  static bool panning(const juce::Component& c);

  // The view moved (scroll, drag or programmatic).
  std::function<void()> onScroll;

  void visibleAreaChanged(const juce::Rectangle<int>&) override;
  void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
  bool keyPressed(const juce::KeyPress& key) override;

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
  void globalFocusChanged(juce::Component* focused) override;

  const Axis axis_;
  const Keys keys_;
  int focusMargin_ = 0;
  float wheelRemainder_ = 0;  // sub-pixel carry between wheel events
};

}  // namespace t3k::ui
