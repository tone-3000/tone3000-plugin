#include "FormatBadge.h"

#include <cmath>

#include "core/Brand.h"
#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
// zinc-400 (the web ToneCard badge colour), not a theme grey.
const juce::Colour kChipBg{0xffa1a1aa};
// 12px mono line box (Roboto Mono's normal line-height ≈ 1.32) + 1px pads.
constexpr int kChipHeight = 18;
}  // namespace

FormatBadge::FormatBadge() {
  setInterceptsMouseClicks(false, false);
  setAccessible(false);  // decorative: the card names its format
}

void FormatBadge::setFormat(const juce::String& label, bool a2) {
  label_ = label;
  a2_ = a2;
  chipWidth_ = label_.isEmpty()
                   ? 0
                   : static_cast<int>(std::ceil(Fonts::width(Fonts::mono(kFontPx), label_))) + 2 * kPadX;
  const int width = label_.isEmpty() ? 0 : chipWidth_ + (a2_ ? kGap + kA2Size : 0);
  setSize(width, std::max(kChipHeight, a2_ ? kA2Size : 0));
  setVisible(!label_.isEmpty());
  repaint();
}

void FormatBadge::paint(juce::Graphics& g) {
  if (label_.isEmpty()) return;
  const auto box = getLocalBounds().toFloat();
  const auto chip = juce::Rectangle<float>(static_cast<float>(chipWidth_), static_cast<float>(kChipHeight))
                        .withCentre({chipWidth_ / 2.0f, box.getCentreY()});
  paint::fill(g, chip, theme::kIconBoxRadius, kChipBg);
  paint::capText(g, label_, chip, Fonts::mono(kFontPx), kFontPx, theme::kBlack);
  if (a2_) {
    const auto mark = juce::Rectangle<float>(kA2Size, kA2Size)
                          .withCentre({chip.getRight() + kGap + kA2Size / 2.0f, box.getCentreY()});
    Brand::drawA2Mark(g, mark);
  }
}

}  // namespace t3k::ui
