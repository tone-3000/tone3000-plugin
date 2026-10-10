#include "ContextMenu.h"

#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

ContextMenu::ContextMenu(std::vector<Item> items) {
  for (auto& item : items) {
    auto row = std::make_unique<MenuRow>(item.label, item.icon, MenuRow::kContext);
    row->setHelpText(help::text(item.help));
    if (item.disabled) {
      row->setLabelColour(theme::kMuted);
      row->setDisabledLook(true);
      row->setEnabled(false);
      row->setMouseCursor(juce::MouseCursor::NormalCursor);
    }
    row->onClick = [this, onSelect = std::move(item.onSelect)] {
      dismiss();
      if (onSelect) onSelect();
    };
    addAndMakeVisible(*row);
    rows_.push_back(std::move(row));
  }
  const int inner = MenuRow::kContext.height * static_cast<int>(rows_.size());
  setSize(kWidth, kBorder * 2 + kPad * 2 + inner);
}

ContextMenu::~ContextMenu() = default;

void ContextMenu::openAtPoint(juce::Component& context, juce::Point<int> point) {
  openAt(context, point.translated(kCursorOffset, kCursorOffset));
}

void ContextMenu::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::fill(g, box, theme::kPanelCorner, theme::kPanelBg);
  paint::border(g, box, theme::kPanelCorner, theme::kBorder);
}

void ContextMenu::resized() {
  auto area = contentBounds().reduced(kPad);
  for (auto& row : rows_) row->setBounds(area.removeFromTop(MenuRow::kContext.height));
}

}  // namespace t3k::ui
