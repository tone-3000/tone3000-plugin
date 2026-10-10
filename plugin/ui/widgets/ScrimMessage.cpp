#include "ScrimMessage.h"

#include <cmath>

#include "core/Fonts.h"
#include "core/Theme.h"

namespace t3k::ui {

ScrimMessage::ScrimMessage() { setOpaque(false); }

ScrimMessage::~ScrimMessage() = default;

void ScrimMessage::setIcon(std::optional<Icon> icon) {
  icon_ = icon;
  layout();
}

void ScrimMessage::setBusy(bool busy) {
  if (busy == (dots_ != nullptr)) return;
  if (busy) {
    dots_ = std::make_unique<LoadingDots>();
    addAndMakeVisible(*dots_);
  } else {
    dots_.reset();
  }
  layout();
}

void ScrimMessage::setCopy(const juce::String& copy, int maxWidth) {
  copy_ = copy;
  copyMaxW_ = maxWidth;
  if (copy_.isNotEmpty()) {
    const auto font = Fonts::sans(kBodyPx);
    flow_ = std::make_unique<TextFlow>(font, static_cast<float>(Fonts::normalLineHeight(font)), copy_,
                                       static_cast<float>(maxWidth));
    copyH_ = static_cast<int>(std::ceil(flow_->height()));
  } else {
    flow_.reset();
    copyH_ = 0;
  }
  layout();
}

PillButton& ScrimMessage::addButton(const juce::String& label, PillButton::Style style) {
  buttons_.push_back(std::make_unique<PillButton>(label, style));
  addAndMakeVisible(*buttons_.back());
  layout();
  return *buttons_.back();
}

int ScrimMessage::buttonsWidth() const {
  int w = 0;
  for (const auto& b : buttons_)
    if (b->isVisible()) w += (w > 0 ? kButtonGap : 0) + b->getWidth();
  return w;
}

// Rows stack with 16px gaps; the column is as wide as its widest row.
void ScrimMessage::layout() {
  const int topH = dots_ ? LoadingDots::kDot : icon_ ? kIconSize : 0;
  const int topW = dots_ ? LoadingDots::kWidth : icon_ ? kIconSize : 0;
  const int buttonsW = buttonsWidth();
  const int buttonsH = buttonsW > 0 ? buttons_.front()->getHeight() : 0;
  int w = juce::jmax(topW, buttonsW, copyH_ > 0 ? copyMaxW_ : 0);
  int h = 0;
  for (const int rowH : {topH, copyH_, buttonsH})
    if (rowH > 0) h += (h > 0 ? kGap : 0) + rowH;
  setSize(w, h);
  resized();
}

void ScrimMessage::resized() {
  auto column = getLocalBounds();
  auto takeRow = [&](int rowH) {
    if (column.getHeight() < getHeight()) column.removeFromTop(kGap);
    return column.removeFromTop(rowH);
  };
  const int topH = dots_ ? LoadingDots::kDot : icon_ ? kIconSize : 0;
  topBox_ = {};
  if (topH > 0) {
    topBox_ = takeRow(topH).withSizeKeepingCentre(dots_ ? LoadingDots::kWidth : kIconSize, topH);
    if (dots_) dots_->setBounds(topBox_);
  }
  // Lines centre within the copy's max-width box (text-align: center).
  copyBox_ = copyH_ > 0 ? takeRow(copyH_).withSizeKeepingCentre(copyMaxW_, copyH_) : juce::Rectangle<int>();
  const int buttonsW = buttonsWidth();
  if (buttonsW > 0) {
    auto row = takeRow(buttons_.front()->getHeight()).withSizeKeepingCentre(buttonsW, buttons_.front()->getHeight());
    for (const auto& b : buttons_) {
      if (!b->isVisible()) continue;
      b->setBounds(row.removeFromLeft(b->getWidth()));
      row.removeFromLeft(kButtonGap);
    }
  }
}

void ScrimMessage::paint(juce::Graphics& g) {
  if (icon_ && !dots_) Icons::draw(g, *icon_, topBox_.toFloat(), theme::kWhite.withAlpha(kIconOpacity));
  if (flow_)
    flow_->draw(g, copyBox_.getPosition().toFloat(), theme::kWhite.withAlpha(kBodyOpacity), 0, false,
                juce::Justification::centred);
}

}  // namespace t3k::ui
