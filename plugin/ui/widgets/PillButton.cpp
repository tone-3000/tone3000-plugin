#include "PillButton.h"

#include "core/Brand.h"
#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
// Outline pill border/label colours (rgba(235,235,245,0.6) when the form
// state mutes it, WHITE otherwise).
const juce::Colour kMutedOutline = juce::Colour(235, 235, 245).withAlpha(0.6f);
// assets/t3k-mark.svg is 36 x 12.
constexpr float kMarkAspect = 3.0f;
}  // namespace

PillButton::PillButton(juce::String label, Style style)
    : Clickable(label),
      label_(std::move(label)),
      style_(style),
      metrics_(style == Style::filled ? kFilled : kOutline) {
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  fitToContent();
}

juce::Font PillButton::font() const { return Fonts::sans(metrics_.fontPx); }

void PillButton::setLabel(const juce::String& label) {
  label_ = label;
  setButtonText(label);
  fitToContent();
  repaint();
}

void PillButton::setMetrics(Metrics metrics) {
  metrics_ = metrics;
  fitToContent();
}

void PillButton::setLeadingIcon(Icon icon, float size) {
  icon_ = {icon, size};
  fitToContent();
}

void PillButton::setTrailingMark(float height) {
  markHeight_ = height;
  fitToContent();
}

void PillButton::setCornerRadius(std::optional<float> radius) {
  radius_ = radius;
  repaint();
}

void PillButton::fitToContent() {
  int w = 2 * metrics_.padX + juce::roundToInt(Fonts::width(font(), label_));
  int h = 2 * metrics_.padY + Fonts::normalLineHeight(metrics_.fontPx);
  if (icon_) {
    w += juce::roundToInt(icon_->second) + metrics_.gap;
    h = std::max(h, 2 * metrics_.padY + juce::roundToInt(icon_->second));
  }
  if (markHeight_ > 0) w += juce::roundToInt(markHeight_ * kMarkAspect) + metrics_.gap;
  // The outline pill's 1px border is inside the padding box (border-box).
  if (style_ == Style::outline) {
    w += 2;
    h += 2;
  }
  setSize(w, h);
}

void PillButton::paintButton(juce::Graphics& g, bool, bool) {
  const auto box = getLocalBounds().toFloat();
  const bool enabled = isEnabled();
  const float radius = radius_.value_or(box.getHeight() / 2);
  juce::Colour fg;
  if (style_ == Style::filled) {
    if (!enabled) g.setOpacity(theme::kDisabledOpacity);
    paint::fill(g, box, radius, theme::kWhite);
    fg = theme::kBlack;
  } else {
    paint::border(g, box, radius, enabled ? theme::kWhite : kMutedOutline);
    fg = enabled ? theme::kWhite : theme::kGray;
  }

  const float textW = Fonts::width(font(), label_);
  const float iconW = icon_ ? icon_->second + metrics_.gap : 0.0f;
  const float markW = markHeight_ > 0 ? markHeight_ * kMarkAspect + metrics_.gap : 0.0f;
  float x = box.getCentreX() - (textW + iconW + markW) / 2;
  if (icon_) {
    Icons::draw(g, icon_->first,
                juce::Rectangle<float>(icon_->second, icon_->second)
                    .withCentre({x + icon_->second / 2, box.getCentreY()}),
                fg);
    x += iconW;
  }
  g.setFont(font());
  g.setColour(fg);
  g.drawText(label_, juce::Rectangle<float>(x, box.getY(), textW + 1, box.getHeight()),
             juce::Justification::centredLeft, false);
  if (markHeight_ > 0) {
    x += textW + metrics_.gap;
    Brand::drawMark(g, juce::Rectangle<float>(x, box.getCentreY() - markHeight_ / 2, markHeight_ * kMarkAspect, markHeight_));
  }
}

}  // namespace t3k::ui
