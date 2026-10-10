#include "BrowserPrompt.h"

#include <algorithm>
#include <cmath>

#include "core/Brand.h"
#include "core/Design.h"
#include "core/Fonts.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
constexpr float kMarkAspect = 3.0f;  // assets/t3k-mark.svg is 36 x 12
}

BrowserPrompt::BrowserPrompt(bool mark, const juce::String& copy, int copyMaxWidth, std::unique_ptr<PillButton> button)
    : mark_(mark), copy_(copy), copyMaxW_(copyMaxWidth), button_(std::move(button)) {
  addAndMakeVisible(*button_);
}

BrowserPrompt::~BrowserPrompt() = default;

int BrowserPrompt::heightFor(int width) {
  layout(width);
  return height_;
}

void BrowserPrompt::layout(int width) {
  if (width == builtWidth_) return;
  builtWidth_ = width;
  const float cx = width / 2.0f;
  float y = kPadY;
  if (mark_) {
    markBox_ = {cx - kMarkHeight * kMarkAspect / 2, y, kMarkHeight * kMarkAspect, static_cast<float>(kMarkHeight)};
    y += kMarkHeight + kGap;
  }
  if (copy_.isNotEmpty()) {
    // The span wraps at max-width and its lines centre within that box (the
    // flow centres lines within its own wrap width, so the box must be it).
    const float avail = static_cast<float>(std::min(copyMaxW_, width - 2 * kPadX));
    const int line = Fonts::normalLineHeight(kCopyPx);
    flow_ = std::make_unique<TextFlow>(Fonts::sans(kCopyPx), static_cast<float>(line), copy_, avail);
    copyBox_ = {cx - avail / 2, y, avail, flow_->height()};
    y += flow_->height() + kGap;
  } else {
    flow_.reset();
  }
  button_->setTopLeftPosition(design::snap(cx - button_->getWidth() / 2.0f), design::snap(y));
  y += button_->getHeight() + kPadY;
  height_ = static_cast<int>(std::ceil(y));
}

void BrowserPrompt::resized() {
  builtWidth_ = -1;
  layout(getWidth());
}

void BrowserPrompt::paint(juce::Graphics& g) {
  if (mark_) Brand::drawMark(g, markBox_);
  if (flow_) flow_->draw(g, copyBox_.getTopLeft(), theme::kWhite, 0, false, juce::Justification::horizontallyCentred);
}

}  // namespace t3k::ui
