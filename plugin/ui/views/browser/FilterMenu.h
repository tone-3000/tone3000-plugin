// The dropdown under a filter chip (the Select tone mockups' menus): a
// panel of 14px rows. A single-pick menu shows the current value in white;
// a multi-pick menu adds a check column so the picked names tick and every
// label lines up. Rows may lead with an icon or a creator avatar. A
// searchable menu (the taxonomy filters) adds a search field on top and
// tells its owner what was typed. The panel fits its widest row and grows
// with its rows up to a cap, then scrolls. Any pick reports and closes.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "core/Icons.h"
#include "services/ImageLoader.h"
#include "widgets/DragScroller.h"
#include "widgets/Popover.h"
#include "widgets/TextField.h"

namespace t3k::ui {

class FilterMenu : public Popover {
public:
  struct Option {
    juce::String id;
    juce::String label;
    // Set: the row leads with this avatar ("" = the fallback glyph).
    std::optional<juce::String> avatarUrl;
    // Set: the row leads with this icon (the Favorites bookmark).
    std::optional<Icon> icon;
  };
  enum class Picks { single, multi };
  static constexpr int kGap = 8;  // anchor → panel

  // `searchPlaceholder` set = a search field over the rows.
  FilterMenu(ImageLoader& images, Picks picks, juce::String searchPlaceholder = {});
  ~FilterMenu() override;

  // Replace the rows; `picked` ids read as current.
  void setOptions(std::vector<Option> options, const std::vector<juce::String>& picked);
  // A muted line instead of rows.
  void setStatus(juce::String status);
  // Open below `anchor` and focus the search field (if any).
  void openBelow(juce::Component& anchor);

  std::function<void(const juce::String& id)> onPick;
  // The search text changed (searchable menus).
  std::function<void(const juce::String& text)> onSearch;

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  class Row;
  static constexpr int kMinWidth = 200, kMaxWidth = 320;
  static constexpr int kPad = 8;
  static constexpr int kRowHeight = 36;
  static constexpr int kSearchHeight = 32;
  static constexpr int kSearchGap = 6;
  static constexpr int kMaxVisibleRows = 6;
  static constexpr float kStatusPx = 13;

  void layoutRows();

  ImageLoader& images_;
  const Picks picks_;
  const bool searchable_;
  TextField search_;
  DragScroller viewport_{DragScroller::Axis::vertical};
  juce::Component list_;
  std::vector<std::unique_ptr<Row>> rows_;
  juce::String status_;
  int width_ = kMinWidth;
};

}  // namespace t3k::ui
