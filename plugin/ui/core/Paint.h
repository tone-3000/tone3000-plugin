// Small drawing helpers that encode the CSS box conventions the web UI used
// (inside borders, rounded fills, ellipsised single-line text) so every
// component paints them the same way.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Design.h"
#include "Fonts.h"

namespace t3k::ui::paint {

// Rounded fill of the whole box.
inline void fill(juce::Graphics& g, juce::Rectangle<float> box, float radius, juce::Colour colour) {
  g.setColour(colour);
  if (radius <= 0.0f)
    g.fillRect(box);
  else
    g.fillRoundedRectangle(box, radius);
}

// CSS `border: Npx solid` on a border-box: the stroke sits fully inside.
inline void border(juce::Graphics& g, juce::Rectangle<float> box, float radius, juce::Colour colour,
                   float width = 1.0f) {
  g.setColour(colour);
  const auto inner = box.reduced(width * 0.5f);
  if (radius <= 0.0f)
    g.drawRect(box, width);
  else
    g.drawRoundedRectangle(inner, std::max(0.0f, radius - width * 0.5f), width);
}

// CSS `border: Npx dashed` on a border-box: Chromium draws dashes 3× the
// width long with equal gaps; the stroke sits fully inside the box.
inline void dashedBorder(juce::Graphics& g, juce::Rectangle<float> box, float radius,
                         juce::Colour colour, float width) {
  juce::Path outline;
  outline.addRoundedRectangle(box.reduced(width / 2), std::max(0.0f, radius - width / 2));
  const float dash[] = {width * 3, width * 3};
  juce::Path dashed;
  juce::PathStrokeType(width).createDashedStroke(dashed, outline, dash, 2);
  g.setColour(colour);
  g.fillPath(dashed);
}

// Single line of text, ellipsised when too wide (CSS text-overflow: ellipsis).
inline void text(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> area,
                 const juce::Font& font, juce::Colour colour,
                 juce::Justification just = juce::Justification::centredLeft) {
  g.setFont(font);
  g.setColour(colour);
  g.drawText(s, area, just, true);
}

// One line of text in a CSS line box `lineHeightPx` tall whose top is at
// `lineTop`: the baseline lands where Blink puts it (Fonts::cssBaseline) and
// the text is ellipsised at a character boundary to fit `width`
// (white-space: nowrap; text-overflow: ellipsis). Returns the drawn width.
inline float cssLine(juce::Graphics& g, const juce::String& s, float x, float lineTop, float lineHeightPx,
                     float width, const juce::Font& font, juce::Colour colour,
                     juce::Justification just = juce::Justification::left) {
  static const juce::String ellipsis = juce::String::fromUTF8("\xe2\x80\xa6");
  juce::String text = s;
  float textW = juce::GlyphArrangement::getStringWidth(font, text);
  if (textW > width) {
    while (text.isNotEmpty() && juce::GlyphArrangement::getStringWidth(font, text + ellipsis) > width)
      text = text.dropLastCharacters(1);
    text += ellipsis;
    textW = juce::GlyphArrangement::getStringWidth(font, text);
  }
  if (just.testFlags(juce::Justification::horizontallyCentred)) x += (width - textW) / 2;
  else if (just.testFlags(juce::Justification::right)) x += width - textW;
  g.setColour(colour);
  g.setFont(font);
  g.drawSingleLineText(text, design::snap(x), design::snap(lineTop + Fonts::cssBaseline(font, lineHeightPx)));
  return textW;
}

// `.cap-trim` text (text-box: trim-both cap alphabetic): the glyphs' cap
// height, not the line box, is what centres vertically. `px` is the CSS
// font size; both faces here have a cap height of ~0.71 em.
inline void capText(juce::Graphics& g, const juce::String& s, juce::Rectangle<float> area,
                    const juce::Font& font, float px, juce::Colour colour,
                    juce::Justification just = juce::Justification::centred) {
  const float cap = px * 0.71f;
  const float baseline = area.getCentreY() + cap / 2.0f;
  const float width = juce::GlyphArrangement::getStringWidth(font, s);
  float x = area.getX();
  if (just.testFlags(juce::Justification::horizontallyCentred)) x = area.getCentreX() - width / 2;
  else if (just.testFlags(juce::Justification::right)) x = area.getRight() - width;
  g.setFont(font);
  g.setColour(colour);
  g.drawSingleLineText(s, design::snap(x), design::snap(baseline));
}

// Draw an SVG-derived drawable fitted by its viewBox, the way <img>/<svg>
// sizing works. Drawable::drawWithin fits the *painted* bounds instead, which
// blows a 6x12 chevron path up to fill a 16px icon box.
inline void svg(juce::Graphics& g, const juce::Drawable& drawable, juce::Rectangle<float> box,
                float opacity = 1.0f) {
  auto content = drawable.getDrawableBounds();
  if (auto* composite = dynamic_cast<const juce::DrawableComposite*>(&drawable))
    if (!composite->getContentArea().isEmpty()) content = composite->getContentArea();
  drawable.draw(g, opacity, juce::RectanglePlacement(juce::RectanglePlacement::centred)
                                .getTransformToFit(content, box));
}

// A hairline (1px) horizontal rule at `y`, spanning `x0..x1`.
inline void hairlineH(juce::Graphics& g, float x0, float x1, float y, juce::Colour colour) {
  g.setColour(colour);
  g.fillRect(juce::Rectangle<float>(x0, y, x1 - x0, 1.0f));
}

// Gutter fades over a horizontally scrolled row: `colour` to transparent
// across `width` at both edges of `area`. The fades overhang the outer edge
// by a design px so no subpixel strip shows past them at fractional UI
// scales.
inline void edgeFades(juce::Graphics& g, juce::Rectangle<float> area, float width, juce::Colour colour) {
  const auto clear = colour.withAlpha(0.0f);
  const float w = width + 1;
  g.setGradientFill(juce::ColourGradient(colour, area.getX() - 1, 0, clear, area.getX() + width, 0, false));
  g.fillRect(juce::Rectangle<float>(area.getX() - 1, area.getY(), w, area.getHeight()));
  g.setGradientFill(juce::ColourGradient(colour, area.getRight() + 1, 0, clear, area.getRight() - width, 0, false));
  g.fillRect(juce::Rectangle<float>(area.getRight() - width, area.getY(), w, area.getHeight()));
}

}  // namespace t3k::ui::paint
