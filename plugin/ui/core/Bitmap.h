// Photo handling that matches a browser <img>: artwork is resampled once,
// at the physical pixel density it will be shown at, and drawn back pixel
// for pixel. Drawing the decoded photo through the component transform on
// every paint would bilinear-shrink a 1000px JPEG straight to ~350 device
// pixels (aliased, fuzzy) and pay for it each frame.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace t3k::ui::bitmap {

// Device pixels per logical unit under `g`: the display's backing scale
// times the root's UI scale. Cached bitmaps are rasterised at this size.
float pixelScale(const juce::Graphics& g);

// A `w` × `h` logical box at `scale`, filled with `image` cover-fitted
// (object-fit: cover): centred crop to the box's aspect, then a resample
// that halves repeatedly before the final pass, so heavy reductions average
// every source pixel instead of skipping most of them. Always ARGB so
// callers can composite into it.
juce::Image cover(const juce::Image& image, int w, int h, float scale);

// Draws a bitmap rasterised at `pixelScale` back into its logical `bounds`.
void draw(juce::Graphics& g, const juce::Image& bitmap, juce::Rectangle<int> bounds);

// Overwrites `dst` with `src`'s pixels (same size and format) without
// allocating: the way to refresh a reused composite buffer.
void copyPixels(juce::Image& dst, const juce::Image& src);

}  // namespace t3k::ui::bitmap
