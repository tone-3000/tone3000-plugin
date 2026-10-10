#include "TextFlow.h"

#include <cmath>

#include "Fonts.h"

namespace t3k::ui {

namespace {

// Break one paragraph into lines no wider than `width` (greedy, on spaces;
// a single over-long word gets its own line, as in CSS).
void wrapParagraph(const juce::Font& font, const juce::String& paragraph, float width,
                   std::vector<juce::String>& out) {
  juce::StringArray words;
  words.addTokens(paragraph, " \t\r", {});
  words.removeEmptyStrings();
  if (words.isEmpty()) {
    out.emplace_back();
    return;
  }
  juce::String line;
  for (const auto& word : words) {
    const auto candidate = line.isEmpty() ? word : line + " " + word;
    if (line.isNotEmpty() && juce::GlyphArrangement::getStringWidth(font, candidate) > width) {
      out.push_back(line);
      line = word;
    } else {
      line = candidate;
    }
  }
  out.push_back(line);
}

}  // namespace

TextFlow::TextFlow(const juce::Font& font, float lineHeightPx, const juce::String& text, float width,
                   bool preLine)
    : font_(font), lineHeightPx_(lineHeightPx), width_(width) {
  if (preLine) {
    juce::StringArray paragraphs;
    paragraphs.addLines(text);
    for (const auto& p : paragraphs) wrapParagraph(font_, p, width_, lines_);
  } else {
    wrapParagraph(font_, text.replaceCharacters("\n", " "), width_, lines_);
  }
}

void TextFlow::draw(juce::Graphics& g, juce::Point<float> origin, juce::Colour colour, int maxLines,
                    bool ellipsis, juce::Justification align) const {
  g.setFont(font_);
  g.setColour(colour);
  const int shown = maxLines > 0 ? juce::jmin(maxLines, lineCount()) : lineCount();
  const float baseline = Fonts::cssBaseline(font_, lineHeightPx_);
  for (int i = 0; i < shown; ++i) {
    const bool clamped = i == shown - 1 && ellipsis && shown < lineCount();
    const juce::String& line = clamped ? ellipsised(i) : lines_[static_cast<size_t>(i)];
    float x = origin.x;
    if (!align.testFlags(juce::Justification::left)) {
      const float slack = width_ - juce::GlyphArrangement::getStringWidth(font_, line);
      x += align.testFlags(juce::Justification::horizontallyCentred) ? slack / 2 : slack;
    }
    // Each line box's top snaps to the pixel grid where it lands (a flex
    // centred column can start at 275.4; its second line at 293.6), and the
    // baseline sits a whole number of pixels below that (Fonts::cssBaseline).
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font_, line, std::round(x), std::round(origin.y + lineHeightPx_ * static_cast<float>(i)) + baseline);
    glyphs.draw(g);
  }
}

// Trim words until "line…" fits.
const juce::String& TextFlow::ellipsised(int lineIndex) const {
  if (ellipsisIndex_ == lineIndex) return ellipsisLine_;
  const juce::String dots = juce::String::fromUTF8("\xe2\x80\xa6");
  auto line = lines_[static_cast<size_t>(lineIndex)];
  while (line.isNotEmpty() && juce::GlyphArrangement::getStringWidth(font_, line + dots) > width_) {
    const int space = line.lastIndexOfChar(' ');
    line = space > 0 ? line.substring(0, space) : line.dropLastCharacters(1);
  }
  ellipsisIndex_ = lineIndex;
  ellipsisLine_ = line + dots;
  return ellipsisLine_;
}

}  // namespace t3k::ui
