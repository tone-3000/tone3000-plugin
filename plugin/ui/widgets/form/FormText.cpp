#include "FormText.h"

#include "core/Paint.h"

namespace t3k::ui {

// FormLabel
FormLabel::FormLabel(juce::String text, float px, bool bold, juce::Colour colour)
    : text_(std::move(text)), px_(px), bold_(bold), colour_(colour) {
  setInterceptsMouseClicks(false, false);
}

void FormLabel::setText(const juce::String& text) {
  text_ = text;
  repaint();
}

void FormLabel::setStyle(float px, bool bold, juce::Colour colour) {
  px_ = px;
  bold_ = bold;
  colour_ = colour;
  heightChanged();
}

float FormLabel::preferredWidth() const { return Fonts::width(Fonts::sans(px_, bold_), text_); }

// A bare span in a block (rather than a flex item) shares its line box with
// the body strut: the line grows to fit both and the baseline sits at the
// taller ascent (Blink's line-height: normal leading goes below).
float FormLabel::ascent() const {
  const float own = std::round(Fonts::sans(px_, bold_).getAscent());
  return strutPx_ > 0 ? juce::jmax(own, std::round(Fonts::sans(strutPx_).getAscent())) : own;
}

float FormLabel::heightFor(float) const {
  const float own = static_cast<float>(Fonts::normalLineHeight(px_));
  if (strutPx_ <= 0) return own;
  const float ownDescent = own - std::round(Fonts::sans(px_, bold_).getAscent());
  const float strutAscent = std::round(Fonts::sans(strutPx_).getAscent());
  const float strutDescent = static_cast<float>(Fonts::normalLineHeight(strutPx_)) - strutAscent;
  return ascent() + juce::jmax(ownDescent, strutDescent);
}

void FormLabel::setStrut(float bodyPx) {
  strutPx_ = bodyPx;
  heightChanged();
}

void FormLabel::paint(juce::Graphics& g) {
  g.setColour(colour_);
  g.setFont(Fonts::sans(px_, bold_));
  g.drawSingleLineText(text_, 0, juce::roundToInt(subpixelTop() + ascent()));
}

// Paragraph
Paragraph::Paragraph(const juce::String& text, float px, juce::Colour colour, juce::Justification align,
                     float lineHeight)
    : Paragraph(RichText{TextRun::plain(text)}, px, colour, align, lineHeight) {}

Paragraph::Paragraph(RichText runs, float px, juce::Colour colour, juce::Justification align, float lineHeight)
    : RichTextView(px, px * lineHeight, colour, align) {
  setAutoHeight(false);
  RichTextView::setText(std::move(runs));
}

void Paragraph::setItalicText(const juce::String& text) {
  TextRun run = TextRun::plain(text);
  run.italic = true;
  setText(RichText{run});
}

}  // namespace t3k::ui
