#include "IconButton.h"

#include "core/Paint.h"

namespace t3k::ui {

IconButton::IconButton(Icon icon, int boxSize) : IconButton(icon, boxSize, glyphSizeFor(boxSize)) {}

IconButton::IconButton(Icon icon, int boxSize, int glyphSize)
    : IconButton(icon, nullptr, boxSize, glyphSize) {}

IconButton::IconButton(const char* svg, int boxSize, int glyphSize)
    : IconButton(std::nullopt, svg, boxSize, glyphSize) {}

IconButton::IconButton(std::optional<Icon> icon, const char* svg, int boxSize, int glyphSize)
    : Clickable({}), icon_(icon), svg_(svg), glyphSize_(glyphSize) {
  setSize(boxSize, boxSize);
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void IconButton::setIcon(Icon icon) {
  icon_ = icon;
  svg_ = nullptr;
  repaint();
}

void IconButton::setActive(bool active) {
  if (active_ == active) return;
  active_ = active;
  repaint();
}

void IconButton::setFillWhenActive(bool fill) {
  fillWhenActive_ = fill;
  repaint();
}

void IconButton::setColour(std::optional<juce::Colour> colour) {
  colour_ = colour;
  repaint();
}

void IconButton::setBackgroundColour(std::optional<juce::Colour> colour) {
  background_ = colour;
  repaint();
}

void IconButton::paintButton(juce::Graphics& g, bool, bool) {
  const auto box = getLocalBounds().toFloat();
  const float alpha = isEnabled() ? 1.0f : theme::kDisabledOpacity;

  if (background_)
    paint::fill(g, box, radius_, background_->withMultipliedAlpha(alpha));
  else if (active_ && fillWhenActive_)
    paint::fill(g, box, radius_, theme::kHighlight.withMultipliedAlpha(alpha));

  const auto colour = (colour_ ? *colour_ : (active_ ? theme::kWhite : theme::kGray))
                          .withMultipliedAlpha(alpha);
  const auto glyph = juce::Rectangle<float>(static_cast<float>(glyphSize_),
                                            static_cast<float>(glyphSize_))
                         .withCentre(box.getCentre());
  if (svg_ != nullptr)
    Icons::draw(g, svg_, glyph, colour);
  else if (icon_)
    Icons::draw(g, *icon_, glyph, colour);
}

}  // namespace t3k::ui
