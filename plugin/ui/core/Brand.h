// Multi-colour brand artwork (Tone3000Logo.tsx, T3kMark.tsx), parsed once
// from the embedded SVGs. Unlike Icons these are never tinted.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace t3k::ui {

struct Brand {
  // The 210x32 wordmark (assets/t3k.svg).
  static const juce::Drawable& logo();
  // The 36x12 mark (assets/t3k-mark.svg).
  static const juce::Drawable& mark();
  // The A2-architecture badge (FormatBadge.tsx A2Mark): a white→#434343
  // gradient rounded square with a cut-out "A2", 24-unit viewBox.
  static const juce::Drawable& a2Mark();
  // The verified-creator badge: three stacked red/yellow/blue check strokes,
  // 17.5x20 viewBox. Drawn beside verified creators' names in the browser.
  static const juce::Drawable& verifiedBadge();

  static void drawLogo(juce::Graphics& g, juce::Rectangle<float> box);
  static void drawMark(juce::Graphics& g, juce::Rectangle<float> box);
  static void drawA2Mark(juce::Graphics& g, juce::Rectangle<float> box);
  static void drawVerifiedBadge(juce::Graphics& g, juce::Rectangle<float> box);
};

}  // namespace t3k::ui
