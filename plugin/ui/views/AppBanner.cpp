#include "AppBanner.h"

#include "core/Design.h"
#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/RichText.h"
#include "core/Theme.h"

namespace t3k::ui {

AppBanner::AppBanner() {
  setOpaque(true);
  ignore_.setLabel("Ignore");
  action_.onClick = [this] {
    if (onAction) onAction(spec_.action);
  };
  ignore_.onClick = [this] {
    if (onDismiss) onDismiss(spec_.id);
  };
  addAndMakeVisible(icon_);
  addAndMakeVisible(action_);
  addChildComponent(ignore_);
}

AppBanner::~AppBanner() = default;

void AppBanner::setSpec(const BannerSpec& spec) {
  spec_ = spec;
  icon_.setVariant(spec.variant);
  action_.setLabel(spec.actionLabel);
  ignore_.setVisible(spec.dismissable);
  resized();
  repaint();
}

void AppBanner::resized() {
  // Border-box: the 1px bottom border leaves a 43px content row; everything
  // centres in it.
  const auto row = getLocalBounds().withTrimmedBottom(1).reduced(kPadX, 0);
  const auto centre = [&](int h) { return row.getY() + design::snap((row.getHeight() - h) / 2.0f); };
  icon_.setTopLeftPosition(row.getX(), centre(AlertIcon::kSize));
  int right = row.getRight();
  if (ignore_.isVisible()) {
    ignore_.setTopLeftPosition(right - ignore_.getWidth(), centre(ignore_.getHeight()));
    right -= ignore_.getWidth() + kGap;
  }
  action_.setTopLeftPosition(right - action_.getWidth(), centre(action_.getHeight()));
  const int textX = icon_.getRight() + kGap;
  textBox_ = {textX, row.getY(), action_.getX() - kGap - textX, row.getHeight()};
}

void AppBanner::paint(juce::Graphics& g) {
  g.fillAll(theme::kBlack);
  paint::hairlineH(g, 0, static_cast<float>(getWidth()), static_cast<float>(getHeight() - 1), theme::kBorder);
  const float lineTop = textBox_.getY() + (textBox_.getHeight() - kLineHeight) / 2;
  RichLine::draw(g, spec_.content, kTextPx, {static_cast<float>(textBox_.getX()), lineTop},
                 static_cast<float>(textBox_.getWidth()), kLineHeight, theme::kWhite);
}

}  // namespace t3k::ui
