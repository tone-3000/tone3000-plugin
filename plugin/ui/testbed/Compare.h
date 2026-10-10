// PNG diff for the capture suite: mismatch count/percentage, the worst
// kTile-square tile (a shifted line of text is a few hundred pixels in a
// million, invisible globally but dense locally), plus an optional diff
// image (reference faded to grey, mismatching pixels in red).
#pragma once

#include <juce_graphics/juce_graphics.h>

namespace t3k::ui::testbed {

struct CompareResult {
  bool ok = false;  // both images loaded and share a size
  juce::String error;
  int width = 0, height = 0;
  juce::int64 mismatched = 0;
  double mismatchPercent() const {
    return width * height == 0 ? 100.0 : 100.0 * mismatched / (double(width) * height);
  }
  // Densest tile: its mismatch share and top-left corner.
  double worstTilePercent = 0;
  juce::Point<int> worstTile;
};

inline constexpr int kTile = 64;

// Pixels whose max per-channel difference exceeds `tolerance` (0..255) count
// as mismatched; the default absorbs sub-pixel anti-aliasing noise between
// rasterisers without hiding a shifted edge.
CompareResult compareImages(const juce::Image& reference, const juce::Image& candidate,
                            juce::Image* diffOut = nullptr, int tolerance = 24);

CompareResult comparePngFiles(const juce::File& reference, const juce::File& candidate,
                              const juce::File& diffOut = {});

}  // namespace t3k::ui::testbed
