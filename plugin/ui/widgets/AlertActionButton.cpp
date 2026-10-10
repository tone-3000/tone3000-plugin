#include "AlertActionButton.h"

#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "widgets/form/FormStyle.h"

namespace t3k::ui {

AlertActionButton::AlertActionButton(Style style) : Clickable({}), style_(style) {
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

juce::Font AlertActionButton::font() { return Fonts::sans(kTextPx, true); }

void AlertActionButton::setLabel(const juce::String& label) {
  label_ = label;
  setName(label);
  setSize(juce::roundToInt(Fonts::width(font(), label_)) + 2 * (padX() + border()),
          Fonts::normalLineHeight(kTextPx) + 2 * (kPadY + border()));
}

void AlertActionButton::paintButton(juce::Graphics& g, bool, bool) {
  const auto box = getLocalBounds().toFloat();
  const bool primary = style_ == Style::primary;
  if (style_ == Style::primary) paint::border(g, box, kRadius, theme::kWhite);
  if (style_ == Style::secondaryOutlined) paint::border(g, box, kRadius, form::kFieldBorder);
  const float lineTop = static_cast<float>(border() + kPadY);
  const float lineH = static_cast<float>(Fonts::normalLineHeight(kTextPx));
  paint::cssLine(g, label_, static_cast<float>(border() + padX()), lineTop, lineH, Fonts::width(font(), label_) + 1,
                 font(), primary ? theme::kWhite : theme::kMuted);
}

}  // namespace t3k::ui
