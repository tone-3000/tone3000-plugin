#include "ToastView.h"

#include "core/Fonts.h"
#include "core/Help.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
constexpr int kPadX = 24, kPadY = 14;
constexpr float kRadius = 16;
}  // namespace

ToastView::ToastView(Toast& toast) : toast_(toast) {
  setInterceptsMouseClicks(false, false);
  toast_.addListener(this);
  toastChanged();
}

ToastView::~ToastView() { toast_.removeListener(this); }

void ToastView::setBottomOffset(int bottom) {
  bottom_ = bottom;
  layout();
}

void ToastView::toastChanged() {
  const bool showing = toast_.message().isNotEmpty();
  setVisible(showing);
  if (!showing) return;
  layout();
  help::announce(toast_.message());
}

void ToastView::layout() {
  auto* parent = getParentComponent();
  if (parent == nullptr) return;
  const auto font = Fonts::sans(16, true);
  // line-height 1 → the pill is exactly 16 + 2 * 14 tall.
  const int w = juce::roundToInt(Fonts::width(font, toast_.message())) + kPadX * 2;
  const int h = 16 + kPadY * 2;
  setBounds((parent->getWidth() - w) / 2, parent->getHeight() - bottom_ - h, w, h);
}

void ToastView::paint(juce::Graphics& g) {
  paint::fill(g, getLocalBounds().toFloat(), kRadius, theme::kWhite);
  paint::text(g, toast_.message(), getLocalBounds(), Fonts::sans(16, true), theme::kBlack,
              juce::Justification::centred);
}

}  // namespace t3k::ui
