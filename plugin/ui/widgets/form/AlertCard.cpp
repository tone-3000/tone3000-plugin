#include "AlertCard.h"

#include "FormStyle.h"
#include "core/Paint.h"

namespace t3k::ui {

AlertCard::AlertCard(AlertVariant variant, RichText content, std::vector<Action> actions)
    : icon_(variant), content_(std::move(content)) {
  addAndMakeVisible(icon_);
  for (auto& action : actions) {
    auto button = std::make_unique<AlertActionButton>(action.secondary ? AlertActionButton::Style::secondaryOutlined
                                                                       : AlertActionButton::Style::primary);
    button->setLabel(action.label);
    button->onClick = action.onClick;
    addAndMakeVisible(*button);
    buttons_.push_back(std::move(button));
  }
}

AlertCard::~AlertCard() = default;

void AlertCard::setVariant(AlertVariant variant) { icon_.setVariant(variant); }

void AlertCard::setContent(RichText content) {
  content_ = std::move(content);
  flow_.reset();
  heightChanged();
  repaint();
}

const RichFlow& AlertCard::flowFor(float width) const {
  const float textW = textWidth(width);
  if (!flow_ || !juce::exactlyEqual(flowWidth_, textW)) {
    flow_ = std::make_unique<RichFlow>(content_, kTextPx, kTextPx * kLineHeight, textW);
    flowWidth_ = textW;
  }
  return *flow_;
}

// The copy takes what the icon and the action buttons leave in the row.
float AlertCard::textWidth(float width) const {
  float w = width - 2 * (1 + kPadX) - AlertIcon::kSize - kGap;
  for (const auto& b : buttons_) w -= kGap + b->getWidth();
  return juce::jmax(0.0f, w);
}

float AlertCard::heightFor(float width) const {
  float rowH = juce::jmax(flowFor(width).height(), static_cast<float>(AlertIcon::kSize + kIconLift));
  for (const auto& b : buttons_) rowH = juce::jmax(rowH, static_cast<float>(b->getHeight()));
  return 2 * (1 + kPadY) + rowH;
}

void AlertCard::resized() {
  const int top = 1 + kPadY;
  icon_.setTopLeftPosition(1 + kPadX, top + kIconLift);
  int right = getWidth() - 1 - kPadX;
  for (auto it = buttons_.rbegin(); it != buttons_.rend(); ++it) {
    (*it)->setTopLeftPosition(right - (*it)->getWidth(), top);
    right -= (*it)->getWidth() + kGap;
  }
}

void AlertCard::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::fill(g, box, form::kCardRadius, theme::kBlack);
  paint::border(g, box, form::kCardRadius, form::kFieldBorder);
  if (getWidth() <= 0) return;
  flowFor(static_cast<float>(getWidth()))
      .draw(g, {static_cast<float>(1 + kPadX + AlertIcon::kSize + kGap), static_cast<float>(1 + kPadY)}, theme::kWhite);
}

}  // namespace t3k::ui
