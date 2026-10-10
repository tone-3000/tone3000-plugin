// Scroll-wheel units. The web UI tuned its wheel gestures in CSS pixels of
// WheelEvent.deltaY (useHorizontalWheelScroll.ts); JUCE reports deltas in abstract "pages" (a precise
// trackpad delta is pixels × 0.5 / 256, a notched wheel step lands around
// 0.04). One conversion keeps the web's per-pixel constants meaningful.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace t3k::ui::wheel {

inline constexpr float kPixelsPerUnit = 512.0f;

// WheelEvent.deltaY: positive scrolls the content down; JUCE's sign is the
// opposite. Both already follow the OS's natural-scrolling setting.
inline float pixelsY(const juce::MouseWheelDetails& w) { return -w.deltaY * kPixelsPerUnit; }

}  // namespace t3k::ui::wheel
