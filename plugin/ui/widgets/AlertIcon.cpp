#include "AlertIcon.h"

#include "core/Fonts.h"

namespace t3k::ui {

AlertIcon::AlertIcon(AlertVariant variant) : variant_(variant) {
  setInterceptsMouseClicks(false, false);
  setAccessible(false);  // decorative: the alert text says it
  setSize(kSize, kSize);
}

void AlertIcon::setVariant(AlertVariant variant) {
  if (variant == variant_) return;
  variant_ = variant;
  repaint();
}

void AlertIcon::paint(juce::Graphics& g) {
  const auto colour = alertColour(variant_);
  const auto box = getLocalBounds().toFloat();
  g.setColour(colour);
  g.drawEllipse(box.reduced(kStroke / 2), kStroke);
  // The "!" is a 10px line box (line-height 1) centred in the ring's
  // content box, inside the border.
  const auto font = Fonts::sans(kGlyphPx, true);
  const auto content = box.reduced(kStroke);
  const float lineTop = content.getY() + (content.getHeight() - kGlyphPx) / 2;
  juce::GlyphArrangement glyphs;
  glyphs.addLineOfText(font, "!", box.getCentreX() - Fonts::width(font, "!") / 2,
                       lineTop + Fonts::cssBaseline(font, kGlyphPx));
  glyphs.draw(g);
}

}  // namespace t3k::ui
