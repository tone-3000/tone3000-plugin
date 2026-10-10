// Right-click action sheet (TileMenu.tsx): a #141416 panel of icon + label
// rows in the shared dropdown style, opened at the click point (nudged 6px
// so the panel sits clearly past the cursor tip). Dismissed on outside
// press, Escape, or picking a row.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "MenuRow.h"
#include "Popover.h"
#include "core/Help.h"

namespace t3k::ui {

class ContextMenu : public Popover {
public:
  struct Item {
    juce::String label;
    Icon icon;
    help::Key help;
    std::function<void()> onSelect;
    bool disabled = false;
  };

  static constexpr int kWidth = 148;
  static constexpr int kPad = 6;
  // Visual-px nudge past the cursor tip.
  static constexpr int kCursorOffset = 6;

  explicit ContextMenu(std::vector<Item> items);
  ~ContextMenu() override;

  // Open with the top-left just past `point` (in `context` coordinates).
  void openAtPoint(juce::Component& context, juce::Point<int> point);

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  std::vector<std::unique_ptr<MenuRow>> rows_;
};

}  // namespace t3k::ui
