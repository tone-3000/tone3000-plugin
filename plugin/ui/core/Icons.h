// Lucide icons (generated SVG in LucideIcons.h) plus the handful of custom
// glyphs the web UI drew inline (CustomIcons.h), parsed once and cached per
// (icon, colour). Icons are authored white; `colour` tints them.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LucideIcons.h"

namespace t3k::ui {

struct Icons {
  // Draw `icon` fitted (aspect preserved) and centred in `box`.
  static void draw(juce::Graphics& g, Icon icon, juce::Rectangle<float> box, juce::Colour colour);
  // Lucide's `strokeWidth` prop: the stroke in 24-unit icon space (default 2).
  static void draw(juce::Graphics& g, Icon icon, juce::Rectangle<float> box, juce::Colour colour,
                   float strokeWidth);
  // Same for a custom SVG (CustomIcons.h constants; keyed by pointer).
  static void draw(juce::Graphics& g, const char* svg, juce::Rectangle<float> box,
                   juce::Colour colour);
};

}  // namespace t3k::ui
