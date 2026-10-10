// CSS-style multi-line text: word-wrapped lines laid on a fixed line-height
// grid (`line-height: 1.4`), with the glyph box centred in each line box
// the way a browser does, plus -webkit-line-clamp / max-height clamping.
// JUCE's TextLayout paces lines by the font's own ascent+descent, so the web
// UI's paragraph rhythm is reproduced here instead.
#pragma once

#include <juce_graphics/juce_graphics.h>

#include <vector>

namespace t3k::ui {

class TextFlow {
public:
  // `preLine` keeps explicit newlines (CSS white-space: pre-line); runs of
  // other whitespace collapse either way.
  TextFlow(const juce::Font& font, float lineHeightPx, const juce::String& text, float width,
           bool preLine = false);

  int lineCount() const { return static_cast<int>(lines_.size()); }
  float height() const { return lineHeightPx_ * static_cast<float>(lineCount()); }
  // Height of the first `maxLines` lines (the clamped box).
  float clampedHeight(int maxLines) const {
    return lineHeightPx_ * static_cast<float>(juce::jmin(maxLines, lineCount()));
  }
  bool overflows(int maxLines) const { return lineCount() > maxLines; }

  // Draw from the top-left of `origin` (lines centred / right-aligned within
  // the flow width when asked); `maxLines` ≤ 0 draws everything. The final
  // drawn line gets an ellipsis when clamped (-webkit-line-clamp).
  void draw(juce::Graphics& g, juce::Point<float> origin, juce::Colour colour, int maxLines = 0,
            bool ellipsis = false, juce::Justification align = juce::Justification::left) const;

private:
  // The clamped last line with its ellipsis, for the line index it was
  // shortened for (draw runs per frame; the trimming loop measures text).
  const juce::String& ellipsised(int lineIndex) const;

  juce::Font font_;
  float lineHeightPx_;
  float width_;
  std::vector<juce::String> lines_;
  mutable int ellipsisIndex_ = -1;
  mutable juce::String ellipsisLine_;
};

}  // namespace t3k::ui
