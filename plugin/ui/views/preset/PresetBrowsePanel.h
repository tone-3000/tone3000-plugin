// The preset browser popover (port of the browse half of PresetBar.tsx):
// search, the MIDI-PC and reorder and bulk-select toggles, then the grouped
// list: Favourites, "Your Presets" (All Uncategorized + the user's
// categories, each a drop target) and the TONE3000 factory section.
//
// Grips drag presets between groups (a drop on a header, an empty group or
// another preset files it there; Favourites stars it). Reorder mode instead
// makes a drop on a preset move the dragged one to its place in the global
// order. Bulk mode swaps the row actions for checkboxes and adds a bar to
// move, duplicate or delete the selection. The grouping and what a drop means
// live in model/PresetGroups (tested without a window); this view only draws
// them and carries the pointer.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <set>

#include "model/PresetGroups.h"
#include "services/Services.h"
#include "views/preset/PresetRowViews.h"
#include "widgets/DragScroller.h"
#include "widgets/Popover.h"
#include "widgets/TextField.h"

namespace t3k::ui {

class PresetBrowsePanel : public Popover, private presetlist::RowHost {
public:
  static constexpr int kWidth = 360;
  static constexpr int kPad = 12;
  static constexpr int kSearchHeight = 33;  // 13px text + 8px padding + 1px border
  static constexpr int kBulkBarHeight = 36;
  static constexpr int kListMaxHeight = 362;
  static constexpr int kConfirmMinHeight = 200;

  // `activeId` names the loaded preset; `load` loads one and closes the
  // browser (the bar owns tuner/takeover handling).
  PresetBrowsePanel(Services& services, juce::Component& anchor, std::function<juce::String()> activeId,
                    std::function<void(const juce::String&)> load);
  ~PresetBrowsePanel() override;

  void show();
  // The store, the active preset or a preference changed: redraw the list.
  void presetsChanged();

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  class BulkBar;
  class MovePicker;
  class ConfirmCard;
  class CreateRow;
  class SectionHeader;

  // RowHost
  void rowLoad(const juce::String& id) override { load_(id); }
  void rowToggleSelected(const juce::String& id) override;
  void rowToggleStar(const PresetInfo& preset) override;
  void rowBeginRename(const juce::String& id) override;
  void rowCommitRename(const PresetInfo& preset, const juce::String& name) override;
  void rowCancelRename() override;
  void rowDelete(const juce::String& id) override;
  void rowDragBegin(presetlist::PresetRow& row, juce::Point<int> pointer) override;
  void rowDragMove(presetlist::PresetRow& row, juce::Point<int> pointer) override;
  void rowDragEnd(presetlist::PresetRow& row, juce::Point<int> pointer) override;
  juce::Component& rowContentSpace() override { return content_; }

  bool showPc() const;
  const std::vector<PresetInfo>& list() const { return services_.presets.presets(); }
  bool collapsed(const juce::String& key) const { return collapsed_.count(key) != 0; }
  void toggleCollapsed(const juce::String& key);
  bool searching() const { return search_.text().trim().isNotEmpty(); }

  void rebuild();
  void buildFavourites(const PresetGroups& groups, const std::function<void(const PresetInfo&)>& addRow);
  void buildUserGroups(const PresetGroups& groups, const std::function<void(const PresetInfo&)>& addRow);
  void buildFactory(const PresetGroups& groups, const std::function<void(const PresetInfo&)>& addRow);
  void addHeader(const juce::String& title, int count, const juce::String& key, presetlist::GroupHeader::Glyph glyph,
                 bool deletable, bool favourites, const juce::String& category, bool dropTarget);
  void addEmpty(const juce::String& label, bool favourites, const juce::String& category);
  void layoutRows();
  void sizeToFit();

  // Categories
  void createCategory();
  void askDeleteCategory(const juce::String& name);
  // Bulk actions
  void clearSelection();
  void bulkMove(const juce::String& category);
  void bulkDuplicate();
  void askBulkDelete();
  void showConfirm(const juce::String& title, const juce::String& message, std::function<void()> onConfirm);
  void closeConfirm();

  // Drag and drop
  DropTarget targetAt(juce::Point<int> pointer, const presetlist::PresetRow& dragged) const;
  DropOutcome outcomeAt(juce::Point<int> pointer, const presetlist::PresetRow& dragged) const;
  void highlightDropAt(juce::Point<int> pointer, const presetlist::PresetRow& dragged);
  void apply(const DropOutcome& outcome);

  Services& services_;
  juce::Component& anchor_;
  std::function<juce::String()> activeId_;
  std::function<void(const juce::String&)> load_;

  TextField search_;
  presetlist::GlyphButton pcToggle_, reorderToggle_, multiToggle_;
  std::unique_ptr<BulkBar> bulkBar_;
  std::unique_ptr<MovePicker> picker_;
  DragScroller pickerScroll_{DragScroller::Axis::vertical, DragScroller::Keys::none};
  DragScroller viewport_{DragScroller::Axis::vertical, DragScroller::Keys::none};  // the arrows walk the rows
  juce::Component content_;
  std::vector<std::unique_ptr<juce::Component>> items_;  // headers, rows, zones, text
  std::vector<presetlist::PresetRow*> rows_;
  std::vector<presetlist::Item*> dropItems_;  // what a drag can land on
  std::unique_ptr<ConfirmCard> confirm_;
  CreateRow* createRow_ = nullptr;

  juce::String renamingId_;
  bool reordering_ = false, multiSelect_ = false, creating_ = false, pickerOpen_ = false;
  juce::StringArray selected_;
  std::set<juce::String> collapsed_;  // "__favourites", "__uncategorized", "__factory", or a category name
  juce::String newCategory_;

  // Drag
  bool dragging_ = false;
  int dragGrabDy_ = 0;
  presetlist::Item* highlighted_ = nullptr;
};

}  // namespace t3k::ui
