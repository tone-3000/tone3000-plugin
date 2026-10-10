#include "ChromeTextButton.h"

#include <cmath>

#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

ChromeTextButton::ChromeTextButton(juce::String label, help::Key help)
    : Clickable(label), label_(std::move(label)) {
  setHelpText(help::text(help));
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  // border-box: 4px padding inside a 1px border each side.
  const float text = Fonts::width(Fonts::mono(kFontPx), label_);
  setSize(juce::roundToInt(text + 2 * (kPadX + 1)), theme::kTextBoxHeight);
}

void ChromeTextButton::setArmed(bool armed) {
  if (armed_ == armed) return;
  armed_ = armed;
  repaint();
}

void ChromeTextButton::setOpen(bool open) {
  if (open_ == open) return;
  open_ = open;
  repaint();
}

void ChromeTextButton::paintButton(juce::Graphics& g, bool, bool) {
  const auto box = getLocalBounds().toFloat();
  const float alpha = isEnabled() ? 1.0f : theme::kDisabledOpacity;
  juce::Colour fg = theme::kMuted;
  if (open_ || armed_) {
    fg = theme::kBlack;
    paint::fill(g, box, theme::kIconBoxRadius, (open_ ? theme::kWhite : theme::kBrandYellow).withMultipliedAlpha(alpha));
  } else {
    paint::border(g, box, theme::kIconBoxRadius, theme::kBorder.withMultipliedAlpha(alpha));
  }
  paint::capText(g, label_, box, Fonts::mono(kFontPx), kFontPx, fg.withMultipliedAlpha(alpha));
}

}  // namespace t3k::ui
