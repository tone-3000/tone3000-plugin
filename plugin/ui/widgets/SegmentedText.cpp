#include "SegmentedText.h"

#include "core/Fonts.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "widgets/Clickable.h"

namespace t3k::ui {

namespace {
constexpr int kCellPadX = 4;
}  // namespace

class SegmentedText::Segment : public Clickable {
public:
  Segment(const Cell& cell, const Style& style) : Clickable(cell.label), cell_(cell), style_(style) {
    setHelpText(cell.help);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }

  bool on = false;

  int preferredWidth() const {
    const float content = cell_.svg != nullptr ? cell_.iconPx : Fonts::width(Fonts::mono(style_.fontPx), cell_.label);
    return std::max(style_.minCellWidth, juce::roundToInt(content) + 2 * kCellPadX);
  }

  // Fills are painted by the group (clipped to the rounded track); the cell
  // paints only its label / glyph.
  void paintButton(juce::Graphics& g, bool, bool) override {
    const auto fg = on ? style_.onFg : style_.offFg;
    const auto box = getLocalBounds().toFloat();
    if (cell_.svg != nullptr)
      Icons::draw(g, cell_.svg, juce::Rectangle<float>(cell_.iconPx, cell_.iconPx).withCentre(box.getCentre()), fg);
    else
      paint::capText(g, cell_.label, box, Fonts::mono(style_.fontPx), style_.fontPx, fg);
  }

private:
  Cell cell_;
  const Style& style_;
};

SegmentedText::Style SegmentedText::armed() {
  return {theme::kBrandYellow, theme::kBlack, juce::Colours::transparentBlack, theme::kMuted, 12, std::nullopt,
          theme::kIconBoxSize};
}

SegmentedText::Style SegmentedText::selection() {
  return {juce::Colours::transparentBlack, theme::kWhite, juce::Colours::transparentBlack, theme::kMuted, 12,
          std::nullopt, 0};
}

SegmentedText::SegmentedText(std::vector<Cell> cells, Style style) : style_(style) {
  int width = 0;
  for (size_t i = 0; i < cells.size(); ++i) {
    auto seg = std::make_unique<Segment>(cells[i], style_);
    seg->onClick = [this, i] { if (onCellClick) onCellClick(static_cast<int>(i)); };
    addAndMakeVisible(*seg);
    width += seg->preferredWidth();
    cells_.push_back(std::move(seg));
  }
  setSize(width, theme::kTextBoxHeight);
}

SegmentedText::~SegmentedText() = default;

void SegmentedText::setOn(int index, bool on) {
  if (index < 0 || index >= static_cast<int>(cells_.size())) return;
  auto& cell = *cells_[static_cast<size_t>(index)];
  if (cell.on == on) return;
  cell.on = on;
  cell.repaint();
}

bool SegmentedText::isOn(int index) const {
  return index >= 0 && index < static_cast<int>(cells_.size()) && cells_[static_cast<size_t>(index)]->on;
}

void SegmentedText::select(int index) {
  for (int i = 0; i < static_cast<int>(cells_.size()); ++i) setOn(i, i == index);
}

void SegmentedText::setInteractive(bool interactive) {
  for (auto& cell : cells_) {
    cell->setInterceptsMouseClicks(interactive, interactive);
    cell->setMouseCursor(interactive ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
  }
}

// The track clips its cells to the rounded corners (overflow: hidden).
void SegmentedText::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::fill(g, box, theme::kIconBoxRadius, style_.track.value_or(theme::kSegmentedTrack));
  juce::Path clip;
  clip.addRoundedRectangle(box, theme::kIconBoxRadius);
  g.reduceClipRegion(clip);
  for (auto& cell : cells_) {
    g.setColour(cell->on ? style_.onBg : style_.offBg);
    g.fillRect(cell->getBounds());
  }
}

void SegmentedText::resized() {
  int x = 0;
  for (auto& cell : cells_) {
    cell->setBounds(x, 0, cell->preferredWidth(), getHeight());
    x += cell->getWidth();
  }
}

}  // namespace t3k::ui
