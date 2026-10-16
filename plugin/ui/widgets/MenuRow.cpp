#include "MenuRow.h"

#include <cmath>

#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

MenuRow::MenuRow(const juce::String& label, std::optional<Icon> icon, Metrics metrics)
    : Clickable(label),
      label_(label),
      icon_(icon),
      metrics_(metrics),
      labelColour_(theme::kWhite) {
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void MenuRow::setLabelColour(juce::Colour colour) {
  labelColour_ = colour;
  repaint();
}

void MenuRow::setDisabledLook(bool disabled) {
  disabledLook_ = disabled;
  repaint();
}

int MenuRow::preferredWidth() const {
  // Rounded up, with a pixel spare: a label a fraction too wide for its box
  // is ellipsised ("Put in Own Fol...").
  const int text = static_cast<int>(std::ceil(Fonts::width(Fonts::sans(metrics_.fontPx), label_))) + 1;
  return metrics_.padX * 2 + (icon_ ? metrics_.icon + metrics_.gap : 0) + text;
}

void MenuRow::paintButton(juce::Graphics& g, bool highlighted, bool) {
  auto box = getLocalBounds();
  if (highlighted && isEnabled() && !disabledLook_)
    paint::fill(g, box.toFloat(), 8.0f, juce::Colours::white.withAlpha(0.08f));

  const float alpha = disabledLook_ ? theme::kDisabledOpacity : 1.0f;
  auto content = box.reduced(metrics_.padX, 0);
  if (icon_) {
    const auto iconBox =
        content.removeFromLeft(metrics_.icon).withSizeKeepingCentre(metrics_.icon, metrics_.icon);
    Icons::draw(g, *icon_, iconBox.toFloat(), labelColour_.withMultipliedAlpha(alpha));
    content.removeFromLeft(metrics_.gap);
  }
  paint::text(g, label_, content, Fonts::sans(metrics_.fontPx),
              labelColour_.withMultipliedAlpha(alpha));
}

}  // namespace t3k::ui
