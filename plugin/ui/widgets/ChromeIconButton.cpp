#include "ChromeIconButton.h"

#include <optional>

#include "SecondaryPress.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

ChromeIconButton::ChromeIconButton(Tone tone, help::Key help) : Clickable({}), tone_(tone) {
  setSize(theme::kIconBoxSize, theme::kIconBoxSize);
  setHelpText(help::text(help));
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

ChromeIconButton::ChromeIconButton(Icon icon, Tone tone, help::Key help)
    : ChromeIconButton(tone, help) {
  icon_ = icon;
}

ChromeIconButton::ChromeIconButton(const char* svg, Tone tone, help::Key help)
    : ChromeIconButton(tone, help) {
  svg_ = svg;
}

void ChromeIconButton::setIcon(Icon icon) {
  icon_ = icon;
  svg_ = nullptr;
  repaint();
}

void ChromeIconButton::setOn(bool on) {
  if (on_ == on) return;
  on_ = on;
  repaint();
}

void ChromeIconButton::setFilled(bool filled) {
  if (filled_ == filled) return;
  filled_ = filled;
  repaint();
}

void ChromeIconButton::setOpen(bool open) {
  if (open_ == open) return;
  open_ = open;
  repaint();
}

void ChromeIconButton::mouseDown(const juce::MouseEvent& e) {
  if (isSecondaryPress(e))
    forwardSecondaryPress(*this, e);
  else
    juce::Button::mouseDown(e);
}

void ChromeIconButton::paintButton(juce::Graphics& g, bool, bool) {
  // ChromeIconButton.tsx toneChrome(): colour, fill, border per tone.
  juce::Colour colour = theme::kWhite;
  std::optional<juce::Colour> fill, border;
  switch (tone_) {
    case Tone::power:
      colour = on_ ? theme::kWhite : theme::kGray;
      if (!on_) fill = theme::kHighlight;
      break;
    case Tone::armed:
      colour = on_ ? theme::kBlack : theme::kGray;
      if (on_) fill = theme::kBrandYellow;
      border = on_ ? theme::kBrandYellow : theme::kBorder;
      break;
    case Tone::link:
      colour = on_ ? theme::kWhite : theme::kGray;
      break;
    case Tone::plain:
      if (filled_) fill = theme::kHighlight;
      break;
  }
  if (open_) {
    colour = theme::kBlack;
    fill = theme::kWhite;
    border = theme::kWhite;
  }

  const float alpha = isEnabled() ? 1.0f : theme::kDisabledOpacity;
  const auto box = getLocalBounds().toFloat();
  if (fill) paint::fill(g, box, theme::kIconBoxRadius, fill->withMultipliedAlpha(alpha));
  if (border) paint::border(g, box, theme::kIconBoxRadius, border->withMultipliedAlpha(alpha));

  const auto glyph = juce::Rectangle<float>(theme::kIconSize, theme::kIconSize).withCentre(box.getCentre());
  if (svg_ != nullptr)
    Icons::draw(g, svg_, glyph, colour.withMultipliedAlpha(alpha));
  else if (icon_)
    Icons::draw(g, *icon_, glyph, colour.withMultipliedAlpha(alpha));
}

}  // namespace t3k::ui
