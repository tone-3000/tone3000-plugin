#include "InlineChrome.h"

#include "core/Theme.h"
#include "widgets/SegmentedText.h"
#include "widgets/form/FormStyle.h"

namespace t3k::ui::inline_chrome {

namespace {
constexpr float kMarginX = 2;
// Arial's x-height, for `vertical-align: middle` (the box's midpoint sits
// half an x-height above the baseline).
constexpr float kArialXHeight = 0.519f;
}  // namespace

std::shared_ptr<const InlineBox> liteFullChip() {
  auto chip = std::make_shared<SegmentedText>(
      std::vector<SegmentedText::Cell>{{"LITE", {}}, {"FULL", {}}},
      SegmentedText::selection());
  chip->setInteractive(false);
  auto box = std::make_shared<InlineBox>();
  box->width = chip->getWidth() + 2 * kMarginX;
  box->height = static_cast<float>(chip->getHeight());
  box->descent = box->height / 2 - form::kBodyPx * kArialXHeight / 2;
  box->paint = [chip](juce::Graphics& g, juce::Rectangle<float> area) {
    juce::Graphics::ScopedSaveState state(g);
    g.setOrigin(juce::roundToInt(area.getX() + kMarginX), juce::roundToInt(area.getY()));
    chip->paintEntireComponent(g, false);
  };
  return box;
}

std::shared_ptr<const InlineBox> icon(Icon glyph, float px) {
  auto box = std::make_shared<InlineBox>();
  box->width = px + 2 * kMarginX;
  box->height = px;
  box->descent = 1;
  box->paint = [glyph, px](juce::Graphics& g, juce::Rectangle<float> area) {
    Icons::draw(g, glyph, juce::Rectangle<float>(area.getX() + kMarginX, area.getY(), px, px), theme::kWhite);
  };
  return box;
}

}  // namespace t3k::ui::inline_chrome
