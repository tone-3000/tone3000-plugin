#include "views/preset/PresetBrowsePanel.h"

#include <algorithm>
#include <map>

#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "views/preset/PresetBrowsePanelParts.h"

namespace t3k::ui {

using presetlist::EmptyZone;
using presetlist::GlyphButton;
using presetlist::GroupHeader;
using presetlist::Item;
using presetlist::PresetRow;
using presetlist::TextLine;

namespace {

const juce::Colour kMutedText = theme::kGray;
constexpr int kPanelGap = 10;    // top: calc(100% + 10)
constexpr int kPanelInset = -8;  // left: -8
constexpr int kListPadTop = 10, kListPadBottom = 12;
constexpr int kGroupGap = 6;     // marginTop between category groups
constexpr int kAutoScrollEdge = 24, kAutoScrollStep = 10;

const char* const kFavouritesKey = "__favourites";
const char* const kRootKey = "__uncategorized";
const char* const kFactoryKey = "__factory";

juce::String movedToast(int count, const juce::String& category) {
  const auto dest = category.isEmpty() ? juce::String("Your Presets") : category;
  return count == 1 ? "Moved to " + dest : "Moved " + juce::String(count) + " presets to " + dest;
}

}  // namespace

PresetBrowsePanel::PresetBrowsePanel(Services& services, juce::Component& anchor,
                                     std::function<juce::String()> activeId,
                                     std::function<void(const juce::String&)> load)
    : services_(services),
      anchor_(anchor),
      activeId_(std::move(activeId)),
      load_(std::move(load)),
      pcToggle_(Icon::MidiPort, 15, 7, help::Key::presetPcToggle),
      reorderToggle_(Icon::ArrowUpDown, 15, 7, help::Key::presetReorder),
      multiToggle_(Icon::CheckSquare, 15, 7, help::Key::presetMultiSelectToggle),
      bulkBar_(std::make_unique<BulkBar>()),
      picker_(std::make_unique<MovePicker>()) {
  search_.setPlaceholder("Search presets");
  search_.setPadding(8, 32, 12);
  search_.setLeadingIcon(Icon::Search, 14, 12, kMutedText);
  search_.onChange = [this](const juce::String&) { rebuild(); };
  addAndMakeVisible(search_);

  pcToggle_.onClick = [this] { services_.prefs.setBool(UiPrefs::kShowPresetPcNumbers, !showPc()); };
  reorderToggle_.onClick = [this] {
    reordering_ = !reordering_;
    rebuild();
  };
  multiToggle_.onClick = [this] {
    multiSelect_ = !multiSelect_;
    if (!multiSelect_) clearSelection();
    pickerOpen_ = false;
    rebuild();
  };
  for (auto* b : {&pcToggle_, &reorderToggle_, &multiToggle_}) addChildComponent(*b);

  bulkBar_->onMove = [this] {
    pickerOpen_ = !pickerOpen_;
    rebuild();
  };
  bulkBar_->onDuplicate = [this] { bulkDuplicate(); };
  bulkBar_->onDelete = [this] { askBulkDelete(); };
  bulkBar_->onClear = [this] {
    clearSelection();
    rebuild();
  };
  addChildComponent(*bulkBar_);

  picker_->onPick = [this](const juce::String& category) { bulkMove(category); };
  pickerScroll_.setViewedComponent(picker_.get(), false);
  addChildComponent(pickerScroll_);

  viewport_.setViewedComponent(&content_, false);
  addAndMakeVisible(viewport_);
}

PresetBrowsePanel::~PresetBrowsePanel() = default;

bool PresetBrowsePanel::showPc() const { return services_.prefs.getBool(UiPrefs::kShowPresetPcNumbers, false); }

void PresetBrowsePanel::show() {
  search_.setText({});
  renamingId_.clear();
  reordering_ = multiSelect_ = creating_ = pickerOpen_ = false;
  newCategory_.clear();
  clearSelection();
  closeConfirm();
  dragging_ = false;
  highlighted_ = nullptr;
  rebuild();
  open(anchor_, Align::left, kPanelGap, kPanelInset);
  search_.focus();
}

void PresetBrowsePanel::presetsChanged() {
  // Drop selections of presets that are gone (deleted here or by another instance).
  juce::StringArray still;
  for (const auto& id : selected_)
    if (std::any_of(list().begin(), list().end(), [&id](const PresetInfo& p) { return p.id == id; })) still.add(id);
  selected_ = still;
  // Not mid-drag, and not under a rename in progress (a rebuild would put the old name back).
  if (!dragging_ && renamingId_.isEmpty() && isOpen()) rebuild();
}

void PresetBrowsePanel::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::fill(g, box, theme::kPanelCorner, theme::kPanelBg);
  paint::border(g, box, theme::kPanelCorner, theme::kBorder);
}

void PresetBrowsePanel::resized() {
  auto area = contentBounds().reduced(kPad, 0).withTrimmedTop(kPad);
  auto row = area.removeFromTop(kSearchHeight);
  // Row: search (flex 1) · [PC] · [reorder] · [bulk select], gap 6.
  for (auto* toggle : {&multiToggle_, &reorderToggle_, &pcToggle_}) {
    if (!toggle->isVisible()) continue;
    toggle->setBounds(row.removeFromRight(29).withSizeKeepingCentre(29, 29));
    row.removeFromRight(6);
  }
  search_.setBounds(row);
  if (bulkBar_->isVisible()) {
    area.removeFromTop(8);
    bulkBar_->setBounds(area.removeFromTop(kBulkBarHeight));
  }
  if (pickerScroll_.isVisible()) {
    area.removeFromTop(6);
    const int h = picker_->preferredHeight();
    pickerScroll_.setBounds(area.removeFromTop(h));
    picker_->setSize(pickerScroll_.getWidth(), picker_->getHeight());
  }
  viewport_.setBounds(area);
  content_.setSize(viewport_.getWidth(), content_.getHeight());
  if (!items_.empty() && !dragging_) layoutRows();  // the viewport's real width, once it has one
  if (confirm_ != nullptr) confirm_->setBounds(contentBounds());
}

void PresetBrowsePanel::toggleCollapsed(const juce::String& key) {
  if (!collapsed_.erase(key)) collapsed_.insert(key);
  rebuild();
}

// List

void PresetBrowsePanel::addHeader(const juce::String& title, int count, const juce::String& key,
                                  GroupHeader::Glyph glyph, bool deletable, bool favourites,
                                  const juce::String& category, bool dropTarget) {
  auto header = std::make_unique<GroupHeader>(title, count, collapsed(key), glyph, deletable);
  header->isFavourites = favourites;
  header->category = category;
  header->isDropTarget = dropTarget;
  header->onToggle = [this, key] { toggleCollapsed(key); };
  if (deletable) header->onDelete = [this, category] { askDeleteCategory(category); };
  if (dropTarget) dropItems_.push_back(header.get());
  items_.push_back(std::move(header));
}

void PresetBrowsePanel::addEmpty(const juce::String& label, bool favourites, const juce::String& category) {
  auto zone = std::make_unique<EmptyZone>(label);
  zone->setSize(0, presetlist::kEmptyZoneHeight);
  zone->isFavourites = favourites;
  zone->category = category;
  dropItems_.push_back(zone.get());
  items_.push_back(std::move(zone));
}

void PresetBrowsePanel::buildFavourites(const PresetGroups& groups,
                                        const std::function<void(const PresetInfo&)>& addRow) {
  // A search with no starred match, or no presets at all, has no use for the group.
  if (groups.favourites.empty() && (searching() || list().empty())) return;
  addHeader("Favourites", static_cast<int>(groups.favourites.size()), kFavouritesKey, GroupHeader::Glyph::star,
            false, true, {}, true);
  if (collapsed(kFavouritesKey)) return;
  if (groups.favourites.empty()) addEmpty("Empty favourites (drag presets here to star)", true, {});
  for (const auto& p : groups.favourites) addRow(p);
}

void PresetBrowsePanel::buildUserGroups(const PresetGroups& groups,
                                        const std::function<void(const PresetInfo&)>& addRow) {
  const auto& categories = services_.presets.categories();
  if (groups.userCount == 0 && categories.empty()) return;

  auto section = std::make_unique<SectionHeader>(creating_);
  section->onCreate = [this] {
    creating_ = !creating_;
    newCategory_.clear();
    rebuild();
  };
  items_.push_back(std::move(section));

  if (creating_) {
    auto row = std::make_unique<CreateRow>();
    createRow_ = row.get();
    row->setText(newCategory_);
    row->onChange = [this](const juce::String& t) { newCategory_ = t; };
    row->onCommit = [this](const juce::String& t) {
      newCategory_ = t;
      createCategory();
    };
    row->onCancel = [this] {
      creating_ = false;
      newCategory_.clear();
      rebuild();
    };
    items_.push_back(std::move(row));
  }

  addHeader("All Uncategorized", static_cast<int>(groups.root.size()), kRootKey, GroupHeader::Glyph::folder, false,
            false, {}, true);
  if (!collapsed(kRootKey)) {
    for (const auto& p : groups.root) addRow(p);
    if (groups.root.empty() && !groups.categories.empty() && !searching())
      addEmpty("No uncategorized presets (drag here to uncategorize)", false, {});
  }
  for (const auto& [name, presets] : groups.categories) {
    addHeader(name, static_cast<int>(presets.size()), name, GroupHeader::Glyph::folder, true, false, name, true);
    if (collapsed(name)) continue;
    if (presets.empty()) addEmpty("Empty category (drag presets here)", false, name);
    for (const auto& p : presets) addRow(p);
  }
}

void PresetBrowsePanel::buildFactory(const PresetGroups& groups,
                                     const std::function<void(const PresetInfo&)>& addRow) {
  if (groups.factory.empty()) return;
  addHeader("TONE3000", static_cast<int>(groups.factory.size()), kFactoryKey, GroupHeader::Glyph::none, false, false,
            {}, false);
  if (collapsed(kFactoryKey)) return;
  for (const auto& p : groups.factory) addRow(p);
}

void PresetBrowsePanel::rebuild() {
  // Keep the search field's text; everything below it is regenerated.
  items_.clear();
  rows_.clear();
  dropItems_.clear();
  highlighted_ = nullptr;
  createRow_ = nullptr;

  const auto& all = list();
  const auto groups = groupPresets(all, services_.presets.categories(), search_.text());
  const bool anyUser = std::any_of(all.begin(), all.end(), [](const PresetInfo& p) { return !p.factory; });
  const auto activeId = activeId_();
  const bool canDrag = !searching();

  pcToggle_.setVisible(!all.empty());
  pcToggle_.setColour(showPc() ? theme::kWhite : kMutedText);
  pcToggle_.setFilled(showPc());
  reorderToggle_.setVisible(all.size() > 1 && !multiSelect_);
  reorderToggle_.setColour(reordering_ ? theme::kWhite : kMutedText);
  reorderToggle_.setFilled(reordering_);
  multiToggle_.setVisible(anyUser && !reordering_);
  multiToggle_.setColour(multiSelect_ ? theme::kWhite : kMutedText);
  multiToggle_.setFilled(multiSelect_);
  bulkBar_->setVisible(multiSelect_ && !selected_.isEmpty());
  bulkBar_->setCount(selected_.size());
  pickerOpen_ = pickerOpen_ && bulkBar_->isVisible();
  pickerScroll_.setVisible(pickerOpen_);
  if (pickerOpen_) picker_->set(services_.presets.categories());

  // PC n loads the nth preset of the full list; the wire only carries 0-127.
  std::map<juce::String, int> pcById;
  for (size_t i = 0; i < all.size() && i <= 127; ++i) pcById[all[i].id] = static_cast<int>(i);

  auto addRow = [&](const PresetInfo& p) {
    PresetRow::State state;
    state.preset = p;
    state.active = p.id == activeId;
    state.selectable = multiSelect_ && !p.factory;
    state.selected = selected_.contains(p.id);
    state.draggable = canDrag && (!p.factory || reordering_);
    state.renaming = renamingId_ == p.id;
    if (showPc())
      if (const auto it = pcById.find(p.id); it != pcById.end()) state.pc = it->second;
    presetlist::RowHost& host = *this;  // private base: converted here, where it is accessible
    auto row = std::make_unique<PresetRow>(host, std::move(state));
    rows_.push_back(row.get());
    if (!p.factory || reordering_) dropItems_.push_back(row.get());
    items_.push_back(std::move(row));
  };

  buildFavourites(groups, addRow);
  buildUserGroups(groups, addRow);
  buildFactory(groups, addRow);
  if (groups.matchCount == 0) {
    auto text = std::make_unique<TextLine>(all.empty() ? "No presets yet. Save one to get started." : "No matches.",
                                           Fonts::sans(13), kMutedText, juce::BorderSize<int>(12, 4, 12, 4));
    text->setSize(0, 39);
    items_.push_back(std::move(text));
  }

  for (auto& item : items_) content_.addAndMakeVisible(*item);
  layoutRows();
  sizeToFit();
  for (auto* row : rows_)
    if (row->preset().id == renamingId_) row->focusRename();
  if (createRow_ != nullptr) createRow_->focus();
}

void PresetBrowsePanel::layoutRows() {
  const int width = std::max(0, viewport_.getWidth() > 0 ? viewport_.getWidth() : kWidth - 2 * kPad);
  int y = kListPadTop;
  bool first = true;
  for (auto& item : items_) {
    // A little air above each group's header, but not above the first.
    if (!first && dynamic_cast<GroupHeader*>(item.get()) != nullptr) y += kGroupGap;
    first = false;
    item->setBounds(0, y, width, item->getHeight());
    y += item->getHeight();
  }
  content_.setSize(width, y + kListPadBottom);
}

void PresetBrowsePanel::sizeToFit() {
  int height = kBorder * 2 + kPad + kSearchHeight;
  if (bulkBar_->isVisible()) height += 8 + kBulkBarHeight;
  if (pickerScroll_.isVisible()) height += 6 + picker_->preferredHeight();
  height += std::min(kListMaxHeight, content_.getHeight());
  if (confirm_ != nullptr) height = std::max(height, kConfirmMinHeight);
  setSize(kWidth, height);
  resized();
  if (isOpen()) reposition();
}

// Rows

void PresetBrowsePanel::rowToggleSelected(const juce::String& id) {
  if (selected_.contains(id)) selected_.removeString(id);
  else selected_.add(id);
  rebuild();
}

void PresetBrowsePanel::rowToggleStar(const PresetInfo& preset) {
  services_.presets.setFavorite(preset.id, !preset.favorite);
}

void PresetBrowsePanel::rowBeginRename(const juce::String& id) {
  renamingId_ = id;
  rebuild();
}

void PresetBrowsePanel::rowCommitRename(const PresetInfo& preset, const juce::String& name) {
  if (renamingId_ != preset.id) return;  // already committed or cancelled
  renamingId_.clear();
  if (name.isNotEmpty() && name != preset.name) services_.presets.rename(preset.id, name);  // rebuilds via the store
  else rebuild();
}

void PresetBrowsePanel::rowCancelRename() {
  renamingId_.clear();
  rebuild();
}

void PresetBrowsePanel::rowDelete(const juce::String& id) {
  selected_.removeString(id);
  services_.presets.remove(id);
}

// Categories

void PresetBrowsePanel::createCategory() {
  const auto name = newCategory_.trim();
  if (name.isEmpty()) {
    creating_ = false;
    rebuild();
    return;
  }
  if (name.length() > kMaxCategoryNameLength) {
    services_.toast.show("Category name must be " + juce::String(kMaxCategoryNameLength) + " characters or less");
    return;
  }
  const auto& existing = services_.presets.categories();
  if (std::any_of(existing.begin(), existing.end(),
                  [&name](const juce::String& c) { return c.compareIgnoreCase(name) == 0; })) {
    services_.toast.show("Category \"" + name + "\" already exists");
    return;
  }
  creating_ = false;
  newCategory_.clear();
  if (services_.presets.addCategory(name)) services_.toast.show("Category \"" + name + "\" created");
  else rebuild();
}

void PresetBrowsePanel::askDeleteCategory(const juce::String& name) {
  showConfirm("Delete Category",
              "Are you sure you want to delete \"" + name + "\"? Presets inside will be moved to Your Presets.",
              [this, name] {
                if (services_.presets.deleteCategory(name)) services_.toast.show("Category \"" + name + "\" deleted");
              });
}

// Bulk actions

void PresetBrowsePanel::clearSelection() {
  selected_.clear();
  pickerOpen_ = false;
}

void PresetBrowsePanel::bulkMove(const juce::String& category) {
  const auto ids = selected_;
  if (ids.isEmpty()) return;
  clearSelection();
  if (services_.presets.moveToCategory(ids, category)) services_.toast.show(movedToast(ids.size(), category));
  else rebuild();
}

void PresetBrowsePanel::bulkDuplicate() {
  const auto ids = selected_;
  if (ids.isEmpty()) return;
  const auto copies = services_.presets.duplicate(ids);
  if (copies.empty()) return;
  clearSelection();
  services_.toast.show(copies.size() == 1 ? "Preset duplicated"
                                          : "Duplicated " + juce::String(static_cast<int>(copies.size())) + " presets");
  rebuild();
}

void PresetBrowsePanel::askBulkDelete() {
  if (selected_.isEmpty()) return;
  showConfirm("Delete Presets",
              "Are you sure you want to delete " + juce::String(selected_.size()) +
                  " selected preset(s)? This cannot be undone.",
              [this] {
                const auto ids = selected_;
                clearSelection();
                if (services_.presets.removeMany(ids))
                  services_.toast.show(ids.size() == 1 ? "Preset deleted" : "Deleted " + juce::String(ids.size()) + " presets");
              });
}

void PresetBrowsePanel::showConfirm(const juce::String& title, const juce::String& message,
                                    std::function<void()> onConfirm) {
  confirm_ = std::make_unique<ConfirmCard>(title, message);
  confirm_->onCancel = [this] { closeConfirm(); };
  confirm_->onConfirm = [this, onConfirm = std::move(onConfirm)] {
    closeConfirm();
    onConfirm();
  };
  addAndMakeVisible(*confirm_);
  sizeToFit();
  confirm_->toFront(false);
}

void PresetBrowsePanel::closeConfirm() {
  if (confirm_ == nullptr) return;
  // Taken off the panel first: the card's own button is mid-click.
  std::unique_ptr<ConfirmCard> gone = std::move(confirm_);
  removeChildComponent(gone.get());
  if (isOpen()) sizeToFit();
  juce::MessageManager::callAsync([keep = std::shared_ptr<ConfirmCard>(std::move(gone))] {});
}

// Drag and drop

DropTarget PresetBrowsePanel::targetAt(juce::Point<int> pointer, const PresetRow& dragged) const {
  for (auto* item : dropItems_) {
    if (item == &dragged || !item->getBounds().contains(pointer)) continue;
    DropTarget target;
    if (const auto* header = dynamic_cast<const GroupHeader*>(item)) {
      target.kind = header->isFavourites ? DropTarget::Kind::favourites : DropTarget::Kind::category;
      target.name = header->category;
    } else if (const auto* zone = dynamic_cast<const EmptyZone*>(item)) {
      target.kind = zone->isFavourites ? DropTarget::Kind::favourites : DropTarget::Kind::category;
      target.name = zone->category;
    } else if (const auto* row = dynamic_cast<const PresetRow*>(item)) {
      target.kind = DropTarget::Kind::preset;
      target.name = row->preset().id;
    }
    return target;
  }
  return {};
}

DropOutcome PresetBrowsePanel::outcomeAt(juce::Point<int> pointer, const PresetRow& dragged) const {
  return decideDrop(list(), dragged.preset().id, selected_, reordering_, targetAt(pointer, dragged));
}

void PresetBrowsePanel::highlightDropAt(juce::Point<int> pointer, const PresetRow& dragged) {
  Item* next = nullptr;
  if (outcomeAt(pointer, dragged).kind != DropOutcome::Kind::none)
    for (auto* item : dropItems_)
      if (item != &dragged && item->getBounds().contains(pointer)) next = item;
  if (next == highlighted_) return;
  if (highlighted_ != nullptr) highlighted_->setDropHighlight(false);
  highlighted_ = next;
  if (highlighted_ != nullptr) highlighted_->setDropHighlight(true);
}

void PresetBrowsePanel::rowDragBegin(PresetRow& row, juce::Point<int> pointer) {
  dragging_ = true;
  dragGrabDy_ = pointer.y - row.getY();
  row.setDragging(true);
  row.toFront(false);
}

void PresetBrowsePanel::rowDragMove(PresetRow& row, juce::Point<int> pointer) {
  // The row follows the pointer; near the list's edge the list scrolls.
  const int top = juce::jlimit(0, std::max(0, content_.getHeight() - row.getHeight()), pointer.y - dragGrabDy_);
  row.setTopLeftPosition(row.getX(), top);
  const int inView = pointer.y - viewport_.getViewPositionY();
  if (inView < kAutoScrollEdge) viewport_.setViewPosition(0, std::max(0, viewport_.getViewPositionY() - kAutoScrollStep));
  else if (inView > viewport_.getHeight() - kAutoScrollEdge)
    viewport_.setViewPosition(0, viewport_.getViewPositionY() + kAutoScrollStep);
  highlightDropAt(pointer, row);
}

void PresetBrowsePanel::rowDragEnd(PresetRow& row, juce::Point<int> pointer) {
  const auto outcome = outcomeAt(pointer, row);
  dragging_ = false;
  if (highlighted_ != nullptr) highlighted_->setDropHighlight(false);
  highlighted_ = nullptr;
  row.setDragging(false);
  // Snap back now; a drop that applies rebuilds the list from the store. The
  // store call runs after this event returns: it rebuilds (and so deletes)
  // every row, this one included.
  layoutRows();
  // A refresh that arrived mid-drag was skipped: catch up either way.
  juce::MessageManager::callAsync([safe = juce::Component::SafePointer<PresetBrowsePanel>(this), outcome] {
    if (safe == nullptr) return;
    if (outcome.kind == DropOutcome::Kind::none) safe->rebuild();
    else safe->apply(outcome);
  });
}

void PresetBrowsePanel::apply(const DropOutcome& outcome) {
  auto& store = services_.presets;
  bool ok = false;
  switch (outcome.kind) {
    case DropOutcome::Kind::star: ok = store.setFavorites(outcome.ids, true); break;
    case DropOutcome::Kind::unstar: ok = store.setFavorites(outcome.ids, false); break;
    case DropOutcome::Kind::unstarAndMove:
      ok = store.setFavorites(outcome.ids, false);
      ok = store.moveToCategory(outcome.ids, outcome.category) && ok;
      break;
    case DropOutcome::Kind::move: ok = store.moveToCategory(outcome.ids, outcome.category); break;
    case DropOutcome::Kind::reorder: ok = store.move(outcome.reorderId, outcome.delta); break;
    case DropOutcome::Kind::none: return;
  }
  if (outcome.kind != DropOutcome::Kind::reorder) {
    selected_.clear();
    if (ok) services_.toast.show(outcome.toast);
  }
  if (!ok) services_.toast.show("Couldn't move the preset");
  rebuild();  // whatever the store did, show it
}

}  // namespace t3k::ui
