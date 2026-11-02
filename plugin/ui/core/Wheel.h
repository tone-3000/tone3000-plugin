// Scroll-wheel units. The web UI tuned its wheel gestures in CSS pixels of
// WheelEvent.deltaY (useHorizontalWheelScroll.ts); JUCE reports deltas in
// abstract "pages" instead. A precise trackpad delta is pixels × 0.5 / 256,
// and so is a notched click: one WHEEL_DELTA (120) arrives as 120 × 0.5 / 256
// = 0.234375 pages. One conversion keeps the web's per-pixel constants
// meaningful.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace t3k::ui::wheel {

inline constexpr float kPixelsPerUnit = 512.0f;

// One notched wheel step, in those pixels.
//
// A real click is WHEEL_DELTA (120) through JUCE's 0.5/256 page scaling,
// which is 0.234375 pages, i.e. 120px here, not the 0.04 pages a trackpad's
// fine deltas can average to. Per-control rates divide by this, so "one
// notch" means one physical click and not a platform-dependent fraction of
// one. Written as its derivation rather than a literal 120 so the source is
// obvious.
inline constexpr float kNotchPixels = 0.5f * 120.0f / 256.0f * kPixelsPerUnit;

// WheelEvent.deltaY: positive scrolls the content down; JUCE's sign is the
// opposite. Both already follow the OS's natural-scrolling setting.
inline float pixelsY(const juce::MouseWheelDetails& w) { return -w.deltaY * kPixelsPerUnit; }

}  // namespace t3k::ui::wheel
