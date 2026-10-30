#include "ToneTable.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "core/Brand.h"
#include "core/Fonts.h"
#include "core/Labels.h"
#include "core/Theme.h"
#include "widgets/PillButton.h"
#include "widgets/DragScroller.h"
#include "widgets/MenuRow.h"
#include "widgets/Popover.h"

namespace t3k::ui {
namespace {
constexpr int rowHeight = 34;
constexpr int headerHeight = 34;
constexpr int toolbarHeight = 30;
const juce::String separator = juce::String::fromUTF8(" \xC2\xB7 ");
const juce::String dash = juce::String::fromUTF8("\xE2\x80\x94");

bool unavailable(const Tone& tone) { return tone.isNam() && tone.a2ModelsCount == 0; }
juce::String creator(const Tone& tone) {
  return tone.user && tone.user->name().isNotEmpty() ? tone.user->name() : "Unknown creator";
}
juce::String gear(const Tone& tone) {
  const auto text = labels::gear(tone.gear);
  return text.isNotEmpty() ? text : "Unspecified";
}
juce::String format(const Tone& tone) {
  const auto text = labels::format(tone.format);
  return text.isNotEmpty() ? text : dash;
}
juce::String date(const Tone& tone) {
  const auto& text = tone.publishedAt;
  return text.length() >= 10 && text[4] == '-' && text[7] == '-'
      ? text.substring(0, 10) : dash;
}
juce::String models(const Tone& tone) {
  return labels::count(tone.catalogModelCount()) + (tone.isNam() ? " A2" : "");
}
juce::String cellText(const Tone& tone, ToneColumn column) {
  switch (column) {
    case ToneColumn::title: return tone.title.isNotEmpty() ? tone.title : "Untitled tone";
    case ToneColumn::creator: return creator(tone);
    case ToneColumn::gear: return gear(tone);
    case ToneColumn::format: return format(tone);
    case ToneColumn::models: return models(tone);
    case ToneColumn::downloads: return labels::count(tone.downloadsCount);
    case ToneColumn::favorites: return labels::count(tone.favoritesCount);
    case ToneColumn::published: return date(tone);
  }
  return {};
}
juce::String rowName(const Tone& tone) {
  return cellText(tone, ToneColumn::title) + ", by " + creator(tone) + ", " + gear(tone) +
      ", " + format(tone) + ", " + juce::String(tone.catalogModelCount()) +
      " compatible models, " + juce::String(tone.downloadsCount) + " downloads, " +
      juce::String(tone.favoritesCount) + " favorites, published " +
      (tone.publishedAt.isNotEmpty() ? tone.publishedAt : "not supplied") +
      (tone.user && tone.user->isVerified ? ", verified creator" : "") +
      (unavailable(tone) ? ". No compatible NAM A2 model; inspection only." : "");
}
bool sameSort(const std::optional<ToneColumnSort>& a, const std::optional<ToneColumnSort>& b) {
  return a.has_value() == b.has_value() &&
      (!a || (a->column == b->column && a->ascending == b->ascending));
}
void noClickFocus(juce::Component& component) {
  component.setMouseClickGrabsKeyboardFocus(false);
  for (auto* child : component.getChildren()) noClickFocus(*child);
}
void labelStyle(juce::Label& label, float size, juce::Colour colour, bool bold = false) {
  label.setFont(Fonts::sans(size, bold));
  label.setColour(juce::Label::textColourId, colour);
  label.setJustificationType(juce::Justification::centredLeft);
  label.setBorderSize(juce::BorderSize<int>(0));
  label.setMinimumHorizontalScale(1.0f);
  label.setInterceptsMouseClicks(false, false);
  label.setWantsKeyboardFocus(false);
}

class TableLook final : public juce::LookAndFeel_V4 {
public:
  void drawTableHeaderBackground(juce::Graphics& g, juce::TableHeaderComponent& header) override {
    g.fillAll(theme::kSurfaceRaised);
    g.setColour(theme::kBorder);
    g.fillRect(0, header.getHeight() - 1, header.getWidth(), 1);
  }
  void drawTableHeaderColumn(juce::Graphics& g, juce::TableHeaderComponent&,
      const juce::String& name, int, int width, int height,
      bool over, bool down, int flags) override {
    const bool sorted = (flags & (juce::TableHeaderComponent::sortedForwards |
                                   juce::TableHeaderComponent::sortedBackwards)) != 0;
    if (over || down) g.fillAll(theme::kHighlight.withMultipliedAlpha(down ? 0.8f : 0.45f));
    g.setColour(theme::kBorder);
    g.fillRect(width - 1, 7, 1, height - 14);
    g.setFont(Fonts::sans(11.0f, true));
    g.setColour(sorted ? theme::kBrandYellow : theme::kMuted);
    g.drawText(name, 10, 0, std::max(0, width - (sorted ? 29 : 18)), height,
               juce::Justification::centredLeft, true);
    if (sorted) {
      const float x = static_cast<float>(width - 14);
      const float y = height * 0.5f;
      const bool ascending = (flags & juce::TableHeaderComponent::sortedForwards) != 0;
      juce::Path triangle;
      triangle.addTriangle(x - 3.0f, y + (ascending ? 2.0f : -2.0f),
                           x + 3.0f, y + (ascending ? 2.0f : -2.0f),
                           x, y + (ascending ? -2.0f : 2.0f));
      g.fillPath(triangle);
      g.fillRect(0, height - 2, width, 2);
    }
  }
};

/** Native table accessibility owns rows/cells; this gives each virtual
    painted cell its complete, untruncated metadata name. */
class MetadataCell final : public juce::Component, public juce::SettableTooltipClient {
public:
  std::function<void(juce::Graphics&, int, int)> draw;
  MetadataCell() {
    setInterceptsMouseClicks(false, false);
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
  }
  void paint(juce::Graphics& g) override { if (draw) draw(g, getWidth(), getHeight()); }
  std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
    return std::make_unique<juce::AccessibilityHandler>(
        *this, juce::AccessibilityRole::staticText, juce::AccessibilityActions{});
  }
};

class SortRow final : public MenuRow {
public:
  SortRow(const juce::String& text, bool selected)
      : MenuRow(text, selected ? std::optional<Icon>(Icon::Check) : std::nullopt,
                MenuRow::Metrics{28, 10, 8, 12, 12}), selected_(selected) {
    setName(text);
    setTitle(text);
    setLabelColour(selected ? theme::kBrandYellow : theme::kWhite);
  }
  void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
    if (selected_) {
      g.setColour(theme::kBrandYellow.withAlpha(0.10f));
      g.fillRoundedRectangle(getLocalBounds().toFloat(), 5.0f);
    }
    MenuRow::paintButton(g, highlighted, down);
  }
private:
  bool selected_;
};

class SortPopover final : public Popover {
public:
  using Choice = std::optional<ToneColumnSort>;
  SortPopover(Choice current, std::function<void(Choice)> pick) {
    setName("Sort page menu");
    setTitle("Sort page menu");
    setDescription("Sort only the visible page. Arrow keys choose a row; Return applies it.");
    viewport_.setViewedComponent(&list_, false);
    viewport_.setFocusMargin(3);
    addAndMakeVisible(viewport_);
    auto add = [this, current, pick](const juce::String& text, Choice choice) {
      auto row = std::make_unique<SortRow>(text, sameSort(current, choice));
      row->setExplicitFocusOrder(static_cast<int>(rows_.size()) + 1);
      row->setHelpText(text + ". Sorts only this page and preserves the selected tone.");
      row->onClick = [this, pick, choice] {
        // Keep the panel object owned until after this click unwinds; close
        // removes it from the overlay before any external callback runs.
        close();
        if (pick) pick(choice);
      };
      list_.addAndMakeVisible(*row);
      rows_.push_back(std::move(row));
    };
    add("API order", {});
    for (int column = 0; column < 8; ++column) {
      const auto field = static_cast<ToneColumn>(column);
      const auto name = juce::String(toneColumnName(field));
      add(name + " ascending", ToneColumnSort{field, true});
      add(name + " descending", ToneColumnSort{field, false});
    }
    setSize(240, 272);
  }
  void paint(juce::Graphics& g) override {
    g.setColour(theme::kSurfaceRaised);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 7.0f);
    g.setColour(theme::kBorder);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 7.0f, 1.0f);
  }
  void resized() override {
    const auto area = contentBounds().reduced(5);
    viewport_.setBounds(area);
    list_.setSize(area.getWidth(), static_cast<int>(rows_.size()) * 28);
    int y = 0;
    for (auto& row : rows_) {
      row->setBounds(0, y, list_.getWidth(), 28);
      y += 28;
    }
  }
private:
  DragScroller viewport_{DragScroller::Axis::vertical, DragScroller::Keys::none};
  juce::Component list_;
  std::vector<std::unique_ptr<SortRow>> rows_;
};

class SortVisibilityWatcher final : public juce::ComponentMovementWatcher {
public:
  SortVisibilityWatcher(juce::Component& target, std::function<void()> hide)
      : juce::ComponentMovementWatcher(&target), hide_(std::move(hide)) {}
  void componentMovedOrResized(bool, bool) override {}
  void componentPeerChanged() override {}
  void componentVisibilityChanged() override { if (hide_) hide_(); }
private:
  std::function<void()> hide_;
};

/** Native row virtualization/scrollbars, with the product's explicit key
    ownership and complete accessible row names. Mouse selection never
    grabs keyboard focus from the host. */
class CatalogList final : public juce::TableListBox {
public:
  std::function<bool(const juce::KeyPress&)> navigate;
  std::function<juce::String(int)> nameForRow;
  bool keyPressed(const juce::KeyPress& key) override {
    if (navigate && navigate(key)) return true;
    // Tab traverses the normal component focus order. Space and modified
    // shortcuts remain available to the host.
    return false;
  }
  juce::String getNameForRow(int row) override {
    return nameForRow ? nameForRow(row) : juce::String();
  }
};
} // namespace

struct ToneTable::Impl final : private juce::TableListBoxModel, private juce::ComponentListener {
  ToneTable& owner;
  TableLook look;
  CatalogList table;
  PillButton sortPage{"Sort page", PillButton::Style::outline};
  PillButton apiOrder{"API order", PillButton::Style::outline};
  PillButton favorite{"Favorite", PillButton::Style::outline};
  PillButton load{"Load selected", PillButton::Style::filled};
  juce::Label sortHint, count, title, metadata, description, status, keyboardHint, empty;
  TonePage page;
  std::vector<size_t> order;
  std::optional<ToneColumnSort> sorting;
  std::optional<int> selection, picking, favoritePending;
  bool busy = false, authenticated = false, suppressSelection = false, messageError = false;
  juce::String message;
  juce::Rectangle<int> footerBox, toolbarBox;
  std::unique_ptr<SortPopover> sortMenu;
  std::unique_ptr<SortVisibilityWatcher> visibilityWatcher;

  explicit Impl(ToneTable& parent) : owner(parent) {
    owner.addComponentListener(this);
    visibilityWatcher = std::make_unique<SortVisibilityWatcher>(owner, [this] { closeSortMenu(); });
    owner.setWantsKeyboardFocus(false);
    owner.setFocusContainerType(juce::Component::FocusContainerType::focusContainer);
    owner.setMouseClickGrabsKeyboardFocus(false);
    table.setName("Tone results table");
    table.setTitle("Tone results");
    table.setDescription("Eight columns. Arrow keys select a tone. Return loads the selected compatible tone. "
                         "Column sorting applies only to this page.");
    table.setHelpText("Select a row to inspect it. Return, Load selected, or double-click loads it.");
    table.setModel(this);
    table.setLookAndFeel(&look);
    table.setRowHeight(rowHeight);
    table.setHeaderHeight(headerHeight);
    table.setOutlineThickness(1);
    table.setColour(juce::ListBox::backgroundColourId, theme::kBlack);
    table.setColour(juce::ListBox::outlineColourId, theme::kBorder);
    table.setColour(juce::ListBox::textColourId, theme::kWhite);
    table.setMultipleSelectionEnabled(false);
    table.setRowSelectedOnMouseDown(false);
    table.setWantsKeyboardFocus(true);
    table.setMouseClickGrabsKeyboardFocus(false);
    table.setExplicitFocusOrder(1);
    table.setAutoSizeMenuOptionShown(false);
    table.getHeader().setStretchToFitActive(false);
    table.getHeader().setPopupMenuActive(false);
    table.getHeader().setMouseClickGrabsKeyboardFocus(false);
    constexpr std::array<int, 8> widths{286, 154, 126, 76, 126, 100, 96, 116};
    constexpr std::array<int, 8> minimum{160, 100, 94, 64, 100, 82, 80, 100};
    for (int i = 0; i < 8; ++i)
      table.getHeader().addColumn(toneColumnName(static_cast<ToneColumn>(i)), i + 1,
          widths[static_cast<size_t>(i)], minimum[static_cast<size_t>(i)], 900,
          juce::TableHeaderComponent::visible | juce::TableHeaderComponent::resizable |
          juce::TableHeaderComponent::sortable);
    if (auto* viewport = table.getViewport()) {
      viewport->setScrollBarsShown(true, true);
      viewport->setScrollBarThickness(10);
      viewport->setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
      viewport->getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId, theme::kGray.withAlpha(0.65f));
      viewport->getHorizontalScrollBar().setColour(juce::ScrollBar::thumbColourId, theme::kGray.withAlpha(0.65f));
    }
    table.navigate = [this](const juce::KeyPress& key) { return navigate(key); };
    table.nameForRow = [this](int row) {
      const auto* tone = toneAt(row);
      return tone ? rowName(*tone) : juce::String();
    };
    owner.addAndMakeVisible(table);
    for (auto* component : std::array<juce::Component*, 4>{&sortPage, &apiOrder, &favorite, &load})
      owner.addAndMakeVisible(component);
    for (auto* label : {&sortHint, &count, &title, &metadata, &description, &status, &keyboardHint, &empty})
      owner.addAndMakeVisible(*label);
    sortPage.setMetrics({10, 4, 11, 6});
    sortPage.setCornerRadius(4.0f);
    sortPage.setName("Sort page");
    sortPage.setTitle("Sort page");
    sortPage.setHelpText("Choose a column and direction to sort this page, or restore API order.");
    sortPage.setTooltip("Sort this page by any column. Use Tab and Return to open the menu.");
    sortPage.setWantsKeyboardFocus(true);
    sortPage.setExplicitFocusOrder(2);
    apiOrder.setMetrics({10, 4, 11, 6});
    apiOrder.setCornerRadius(4.0f);
    apiOrder.setExplicitFocusOrder(3);
    apiOrder.setTitle("Restore API order");
    apiOrder.setTooltip("Restore this page's original catalog order. This does not change the search query.");
    favorite.setMetrics({13, 6, 12, 6});
    favorite.setCornerRadius(5.0f);
    favorite.setExplicitFocusOrder(4);
    load.setMetrics({15, 6, 12, 6});
    load.setCornerRadius(5.0f);
    load.setExplicitFocusOrder(5);
    load.setTitle("Load selected tone");
    labelStyle(sortHint, 12, theme::kMuted);
    labelStyle(count, 11, theme::kSubtle);
    count.setJustificationType(juce::Justification::centredRight);
    labelStyle(title, 14, theme::kWhite, true);
    labelStyle(metadata, 11, theme::kMuted);
    labelStyle(description, 12, theme::kMuted);
    description.setJustificationType(juce::Justification::topLeft);
    labelStyle(status, 11, theme::kMuted);
    labelStyle(keyboardHint, 11, theme::kSubtle);
    keyboardHint.setJustificationType(juce::Justification::topLeft);
    labelStyle(empty, 13, theme::kMuted);
    empty.setJustificationType(juce::Justification::centred);
    sortPage.onClick = [this] { showSortMenu(); };
    apiOrder.onClick = [this] {
      if (!owner.isEnabled() || busy || picking || !sorting) return;
      applySort({});
      auto callback = owner.onSort;
      if (callback) callback({});
    };
    load.onClick = [this] { loadSelection(); };
    favorite.onClick = [this] {
      if (!canFavorite()) return;
      const auto id = selection;
      auto callback = owner.onToggleFavorite;
      if (id && callback) callback(*id);
    };
    syncFooter();
  }

  ~Impl() override {
    owner.removeComponentListener(this);
    visibilityWatcher.reset();
    closeSortMenu();
    sortMenu.reset();
    table.setModel(nullptr);
    table.setLookAndFeel(nullptr);
  }

  const Tone* toneAt(int row) const {
    if (row < 0 || static_cast<size_t>(row) >= order.size()) return nullptr;
    const auto index = order[static_cast<size_t>(row)];
    return index < page.data.size() ? &page.data[index] : nullptr;
  }
  const Tone* selectedTone() const {
    if (!selection) return nullptr;
    const auto found = std::find_if(page.data.begin(), page.data.end(),
        [this](const Tone& tone) { return tone.id == *selection; });
    return found == page.data.end() ? nullptr : &*found;
  }
  bool canLoad() const {
    const auto* tone = selectedTone();
    return owner.isEnabled() && tone && !busy && !picking && !unavailable(*tone);
  }
  bool canFavorite() const {
    return owner.isEnabled() && selectedTone() && authenticated && !busy && !picking && !favoritePending;
  }

  int getNumRows() override { return static_cast<int>(order.size()); }

  void paintRowBackground(juce::Graphics& g, int row, int width, int height, bool selected) override {
    g.fillAll(selected ? theme::kBrandYellow
                      : row % 2 == 0 ? theme::kSurface : theme::kBlack);
    g.setColour(selected ? theme::kBlack.withAlpha(0.1f) : theme::kBorder.withAlpha(0.45f));
    g.fillRect(0, height - 1, width, 1);
  }

  void paintCell(juce::Graphics& g, int row, int column, int width, int height, bool selected) override {
    const auto* tone = toneAt(row);
    if (!tone || column < 1 || column > 8) return;
    const auto field = static_cast<ToneColumn>(column - 1);
    auto text = cellText(*tone, field);
    const bool disabled = unavailable(*tone);
    const auto colour = selected ? theme::kBlack : disabled ? theme::kSubtle
        : field == ToneColumn::title ? theme::kWhite : theme::kMuted;
    const bool numeric = field == ToneColumn::models || field == ToneColumn::downloads ||
                         field == ToneColumn::favorites;
    g.setColour(colour);
    g.setFont(field == ToneColumn::format ? Fonts::mono(10.5f, true)
                                         : Fonts::sans(12.0f, field == ToneColumn::title));
    auto bounds = juce::Rectangle<int>(10, 0, std::max(0, width - 20), height);
    if (field == ToneColumn::title && disabled) text += "  " + separator.trim() + "  No A2";
    if (field == ToneColumn::creator && tone->user && tone->user->isVerified) {
      const int badgeX = std::min(bounds.getRight() - 13,
          bounds.getX() + juce::roundToInt(Fonts::width(Fonts::sans(12.0f), text)) + 6);
      Brand::drawVerifiedBadge(g, {static_cast<float>(badgeX), (height - 14.0f) / 2.0f, 12.25f, 14.0f});
      bounds.setRight(std::max(bounds.getX(), badgeX - 5));
    }
    g.drawText(text, bounds, numeric ? juce::Justification::centredRight
                                   : juce::Justification::centredLeft, true);
    if (column < 8) {
      g.setColour(selected ? theme::kBlack.withAlpha(0.1f) : theme::kBorder.withAlpha(0.3f));
      g.fillRect(width - 1, 7, 1, height - 14);
    }
  }

  juce::Component* refreshComponentForCell(int row, int column, bool selected,
                                           juce::Component* old) override {
    auto* cell = dynamic_cast<MetadataCell*>(old);
    if (!cell) { delete old; cell = new MetadataCell; }
    const auto* tone = toneAt(row);
    if (!tone) {
      cell->draw = {};
      cell->setTitle({});
      cell->setTooltip({});
      return cell;
    }
    const auto field = static_cast<ToneColumn>(column - 1);
    cell->setTitle(juce::String(toneColumnName(field)) + ": " + cellText(*tone, field));
    cell->setDescription(rowName(*tone));
    cell->setTooltip(getCellTooltip(row, column));
    cell->setHelpText(rowName(*tone) + " Return loads this tone.");
    cell->draw = [this, row, column, selected](juce::Graphics& g, int width, int height) {
      paintCell(g, row, column, width, height, selected);
    };
    cell->repaint();
    return cell;
  }

  juce::String getCellTooltip(int row, int column) override {
    const auto* tone = toneAt(row);
    if (!tone) return {};
    if (column == 1) return rowName(*tone);
    if (column == 6) return juce::String(tone->downloadsCount) + " downloads";
    if (column == 7) return juce::String(tone->favoritesCount) + " favorites";
    if (column == 8) return tone->publishedAt.isNotEmpty() ? tone->publishedAt : "Publication date not supplied";
    return juce::String(toneColumnName(static_cast<ToneColumn>(column - 1))) + ": " +
        cellText(*tone, static_cast<ToneColumn>(column - 1));
  }

  void selectedRowsChanged(int row) override {
    const auto previous = selection;
    const auto* tone = toneAt(row);
    selection = tone ? std::optional<int>(tone->id) : std::nullopt;
    syncFooter();
    if (!suppressSelection && selection && selection != previous) {
      const int id = *selection;
      auto callback = owner.onSelect;
      if (callback) callback(id);
    }
  }

  void cellDoubleClicked(int row, int, const juce::MouseEvent&) override {
    const auto* tone = toneAt(row);
    if (!tone) return;
    const juce::Component::SafePointer<ToneTable> safe(&owner);
    table.selectRow(row);
    if (safe != nullptr) safe->impl_->loadSelection();
  }
  void returnKeyPressed(int) override { loadSelection(); }

  void sortOrderChanged(int column, bool ascending) override {
    const auto next = column >= 1 && column <= 8
        ? std::optional<ToneColumnSort>(ToneColumnSort{static_cast<ToneColumn>(column - 1), ascending})
        : std::nullopt;
    // JUCE header notifications are asynchronous. State equality, rather
    // than a transient suppression flag, prevents programmatic setSort
    // from echoing a user callback after it returns.
    if (sameSort(next, sorting)) return;
    applySort(next);
    auto callback = owner.onSort;
    if (callback) callback(next);
  }

  void closeSortMenu() { if (sortMenu) sortMenu->close(); }

  void componentVisibilityChanged(juce::Component& component) override {
    if (!component.isVisible()) closeSortMenu();
  }
  void componentEnablementChanged(juce::Component& component) override {
    if (!component.isEnabled()) closeSortMenu();
  }

  void showSortMenu() {
    if (!owner.isEnabled() || busy || picking || order.empty()) return;
    if (sortMenu && sortMenu->isOpen()) {
      closeSortMenu();
      return;
    }
    const juce::Component::SafePointer<ToneTable> safe(&owner);
    sortMenu = std::make_unique<SortPopover>(sorting,
        [safe](std::optional<ToneColumnSort> next) {
          if (safe == nullptr) return;
          auto& state = *safe->impl_;
          if (!safe->isEnabled() || state.busy || state.picking) return;
          if (sameSort(next, state.sorting)) return;
          state.applySort(next);
          if (safe == nullptr) return;
          auto callback = safe->onSort;
          if (callback) callback(next);
        });
    sortMenu->open(sortPage, Popover::Align::right, 5);
  }

  void applySort(std::optional<ToneColumnSort> next) {
    closeSortMenu();
    sorting = next;
    rebuild(selection);
    table.getHeader().setSortColumnId(sorting ? static_cast<int>(sorting->column) + 1 : 0,
                                      sorting ? sorting->ascending : true);
    syncFooter();
  }

  void rebuild(std::optional<int> wanted) {
    const juce::ScopedValueSetter<bool> guard(suppressSelection, true);
    order = toneTableOrder(page.data, sorting);
    int row = -1;
    if (wanted)
      for (size_t i = 0; i < order.size(); ++i)
        if (page.data[order[i]].id == *wanted) { row = static_cast<int>(i); break; }
    if (row < 0 && !order.empty()) row = 0;
    table.deselectAllRows();
    table.updateContent();
    if (row >= 0) table.selectRow(row);
    else selection.reset();
    noClickFocus(table);
    table.repaint();
    syncFooter();
  }

  void loadSelection() {
    if (!canLoad()) return;
    const int id = *selection;
    auto callback = owner.onLoad;
    if (callback) callback(id);
  }

  bool navigate(const juce::KeyPress& key) {
    // Root delegates directly when no child owns focus, so JUCE's normal
    // disabled-component routing does not protect this entry point.
    if (!owner.isEnabled() || busy || picking) return false;
    // The overlay owns navigation while its rows are open. Direct Root
    // delegation must never move or load the underlying table.
    if (sortMenu && sortMenu->isOpen()) return sortMenu->keyPressed(key);
    if (key.getModifiers().isAnyModifierKeyDown()) return false;
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::returnKey) {
      if (!selectedTone()) return false;
      loadSelection();
      return true;
    }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) {
      auto* viewport = table.getViewport();
      if (!viewport || viewport->getViewWidth() >= table.getHeader().getTotalWidth()) return false;
      viewport->setViewPosition(viewport->getViewPositionX() +
          (code == juce::KeyPress::leftKey ? -80 : 80), viewport->getViewPositionY());
      return true;
    }
    if (order.empty()) return false;
    int row = table.getSelectedRow();
    const int pageStep = std::max(1, table.getNumRowsOnScreen() - 1);
    if (code == juce::KeyPress::upKey) row = row < 0 ? 0 : row - 1;
    else if (code == juce::KeyPress::downKey) row = row < 0 ? 0 : row + 1;
    else if (code == juce::KeyPress::pageUpKey) row = std::max(0, row) - pageStep;
    else if (code == juce::KeyPress::pageDownKey) row = std::max(0, row) + pageStep;
    else if (code == juce::KeyPress::homeKey) row = 0;
    else if (code == juce::KeyPress::endKey) row = static_cast<int>(order.size()) - 1;
    else return false;
    table.selectRow(juce::jlimit(0, static_cast<int>(order.size()) - 1, row));
    return true;
  }

  void syncFooter() {
    auto sortText = juce::String("Sort this page");
    if (sorting)
      sortText += separator + juce::String(toneColumnName(sorting->column)) +
          (sorting->ascending ? juce::String::fromUTF8(" \xE2\x86\x91") : juce::String::fromUTF8(" \xE2\x86\x93"));
    else sortText += separator + "API order";
    sortHint.setText(sortText, juce::dontSendNotification);
    sortHint.setTooltip(sortText + ". This changes only the visible page.");
    sortPage.setEnabled(!busy && !picking && !order.empty());
    apiOrder.setEnabled(sorting.has_value() && !busy && !picking);
    count.setText(juce::String(static_cast<int>(page.data.size())) + (page.data.size() == 1 ? " tone" : " tones") +
        separator + "Page " + juce::String(std::max(1, page.page)) + " / " +
        juce::String(std::max(1, page.totalPages)), juce::dontSendNotification);
    const auto* tone = selectedTone();
    title.setText(tone ? cellText(*tone, ToneColumn::title) : "Select a tone to inspect", juce::dontSendNotification);
    title.setTooltip(tone ? rowName(*tone) : juce::String());
    metadata.setText(tone ? creator(*tone) + separator + gear(*tone) + separator + format(*tone) +
        separator + juce::String(tone->catalogModelCount()) + " compatible models" : "Use the arrow keys or select a row.",
        juce::dontSendNotification);
    metadata.setTooltip(tone ? rowName(*tone) : juce::String());
    description.setText(tone ? tone->description.trim().replace("\r", "").replace("\n", " ")
                            : "Select a column heading to sort the visible page. Resize columns or scroll sideways for more details.",
                        juce::dontSendNotification);
    description.setTooltip(tone ? tone->description : juce::String());
    juce::String action;
    if (message.isNotEmpty()) action = message;
    else if (picking) action = "Loading tone...";
    else if (busy) action = "Loading tones...";
    else if (tone && unavailable(*tone)) action = "No compatible NAM A2 models. You can inspect this tone, but cannot load it.";
    else if (!authenticated) action = "Sign in to load tones and manage favorites.";
    else if (favoritePending) action = "Saving favorite...";
    status.setText(action, juce::dontSendNotification);
    status.setColour(juce::Label::textColourId, messageError && message.isNotEmpty()
        ? theme::kBrandRed : tone && unavailable(*tone) ? theme::kBrandYellow : theme::kMuted);
    status.setTooltip(action);
    load.setLabel(picking ? "Loading..." : authenticated ? "Load selected" : "Sign in to load");
    load.setEnabled(canLoad());
    load.setTooltip(tone && unavailable(*tone) ? "This NAM tone has no A2 model compatible with the plugin."
        : authenticated ? "Load the selected tone. Return or double-click does the same."
                        : "Continue to sign in and load the selected tone.");
    const bool knownFavorite = tone && authenticated && tone->isFavorite.value_or(false);
    favorite.setLabel(tone && favoritePending && *favoritePending == tone->id
        ? "Saving..." : knownFavorite ? "Unfavorite" : "Favorite");
    favorite.setTitle(knownFavorite ? "Remove selected tone from favorites" : "Favorite selected tone");
    favorite.setEnabled(canFavorite());
    favorite.setTooltip(!authenticated ? "Sign in to manage favorites."
        : favoritePending ? "A favorite update is in progress."
        : tone && !tone->isFavorite.has_value() ? "Favorite this tone. Your current favorite state was not supplied."
        : knownFavorite ? "Remove this tone from your favorites." : "Add this tone to your favorites.");
    keyboardHint.setText("Arrow keys select\nReturn or double-click loads", juce::dontSendNotification);
    empty.setText(busy ? "Loading tones..." : messageError && message.isNotEmpty() ? message
                     : "No tones on this page.\nTry a different search or filter.", juce::dontSendNotification);
    empty.setVisible(order.empty());
    owner.resized();
    owner.repaint();
  }

  void layout() {
    auto bounds = owner.getLocalBounds();
    toolbarBox = bounds.removeFromTop(std::min(toolbarHeight, bounds.getHeight()));
    auto toolbar = toolbarBox;
    apiOrder.setBounds(toolbar.removeFromRight(std::min(84, toolbar.getWidth())).withHeight(24));
    toolbar.removeFromRight(std::min(8, toolbar.getWidth()));
    sortPage.setBounds(toolbar.removeFromRight(std::min(90, toolbar.getWidth())).withHeight(24));
    toolbar.removeFromRight(std::min(12, toolbar.getWidth()));
    // Retain both keyboard actions at narrow widths; the page count is
    // supplementary and yields before the current-sort hint.
    const int countWidth = toolbar.getWidth() >= 420 ? 170
        : toolbar.getWidth() >= 280 ? 130 : 0;
    count.setBounds(toolbar.removeFromRight(countWidth).withHeight(24));
    count.setVisible(countWidth > 0);
    if (countWidth > 0) toolbar.removeFromRight(std::min(12, toolbar.getWidth()));
    sortHint.setBounds(toolbar.withHeight(24));

    const bool stacked = bounds.getWidth() < 700;
    const int preferredFooter = stacked ? 150 : bounds.getHeight() < 290 ? 102 : 120;
    const int footerHeight = std::min(preferredFooter, std::max(0, bounds.getHeight() - 82));
    footerBox = bounds.removeFromBottom(footerHeight);
    if (bounds.getHeight() > 8) bounds.removeFromBottom(8);
    table.setBounds(bounds);
    auto emptyBounds = bounds;
    emptyBounds.removeFromTop(headerHeight);
    empty.setBounds(emptyBounds.reduced(20, 10));
    if (footerBox.isEmpty()) {
      for (auto* c : std::array<juce::Component*, 7>{&title, &metadata, &description,
                      &status, &keyboardHint, &favorite, &load}) c->setBounds({});
      return;
    }
    auto inner = footerBox.reduced(12, 9);
    juce::Rectangle<int> actions;
    if (stacked) {
      actions = inner.removeFromBottom(32);
      inner.removeFromBottom(8);
      keyboardHint.setBounds({});
    } else {
      actions = inner.removeFromRight(std::min(284, std::max(240, inner.getWidth() / 3)));
      inner.removeFromRight(18);
      auto keyBox = actions.withTrimmedTop(43);
      keyboardHint.setBounds(keyBox.getHeight() >= 32 ? keyBox.withHeight(32) : juce::Rectangle<int>{});
    }
    title.setBounds(inner.removeFromTop(20));
    metadata.setBounds(inner.removeFromTop(18));
    auto statusBox = inner.removeFromBottom(std::min(19, inner.getHeight()));
    status.setBounds(statusBox);
    description.setBounds(inner.withTrimmedTop(std::min(5, inner.getHeight())));
    auto buttonRow = actions.removeFromTop(32);
    const int loadWidth = std::min(143, buttonRow.getWidth() / 2);
    load.setBounds(buttonRow.removeFromRight(loadWidth));
    buttonRow.removeFromRight(8);
    favorite.setBounds(buttonRow);
  }

  void paint(juce::Graphics& g) {
    if (!footerBox.isEmpty()) {
      g.setColour(theme::kSurface);
      g.fillRoundedRectangle(footerBox.toFloat(), 7.0f);
      g.setColour(theme::kBorder);
      g.drawRoundedRectangle(footerBox.toFloat().reduced(0.5f), 7.0f, 1.0f);
      g.setColour(theme::kBrandYellow);
      g.fillRoundedRectangle(static_cast<float>(footerBox.getX() + 1),
          static_cast<float>(footerBox.getY() + 10), 2.0f,
          static_cast<float>(std::max(0, footerBox.getHeight() - 20)), 1.0f);
    }
  }
};

ToneTable::ToneTable() {
  impl_ = std::make_unique<Impl>(*this);
  impl_->syncFooter();
}
ToneTable::~ToneTable() = default;

void ToneTable::setPage(const TonePage& page, std::optional<int> selected) {
  impl_->closeSortMenu();
  const auto wanted = selected ? selected : impl_->selection;
  impl_->page = page;
  impl_->rebuild(wanted);
}
void ToneTable::setSort(std::optional<ToneColumnSort> sort) { impl_->applySort(sort); }
void ToneTable::setBusy(bool busy, std::optional<int> pickingId) {
  if (busy || pickingId) impl_->closeSortMenu();
  impl_->busy = busy;
  impl_->picking = pickingId;
  impl_->table.repaint();
  impl_->syncFooter();
}
void ToneTable::setAuthenticated(bool authenticated) {
  impl_->authenticated = authenticated;
  impl_->syncFooter();
}
void ToneTable::setFavoriteBusy(std::optional<int> id) {
  impl_->favoritePending = id;
  impl_->syncFooter();
}
void ToneTable::setMessage(const juce::String& message, bool error) {
  impl_->message = message;
  impl_->messageError = error;
  impl_->syncFooter();
}
std::optional<int> ToneTable::selectedToneId() const { return impl_->selection; }
const Tone* ToneTable::selectedTone() const { return impl_->selectedTone(); }
bool ToneTable::handleNavigationKey(const juce::KeyPress& key) { return impl_->navigate(key); }
void ToneTable::paint(juce::Graphics& g) { if (impl_) impl_->paint(g); }
void ToneTable::resized() { if (impl_) impl_->layout(); }
bool ToneTable::keyPressed(const juce::KeyPress& key) { return handleNavigationKey(key); }

} // namespace t3k::ui
