#include "PresetBar.h"

#include <algorithm>

#include "core/Fonts.h"
#include "core/Help.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "widgets/Clickable.h"
#include "widgets/DragScroller.h"
#include "widgets/Popover.h"
#include "widgets/TextField.h"

namespace t3k::ui {

namespace {

// The bar uses GRAY for its muted text (PresetBar.tsx `MUTED = GRAY`).
const juce::Colour kMutedText = theme::kGray;
constexpr int kPanelGap = 10;    // top: calc(100% + 10)
constexpr int kPanelInset = -8;  // left: -8
constexpr int kRowHeight = 32;
constexpr int kSectionHeaderHeight = 32;  // 14px bold + 8px padding top/bottom
constexpr int kListMaxHeight = 362;
constexpr int kListPadTop = 10, kListPadBottom = 12;

// Borderless white icon button with `pad` around an `icon`-px glyph
// (PresetBar.tsx iconButtonStyle: radius 4).
class GlyphButton : public Clickable {
public:
  GlyphButton(Icon icon, int glyph, int pad, help::Key key)
      : Clickable({}), icon_(icon), glyph_(glyph) {
    setSize(glyph + pad * 2, glyph + pad * 2);
    setHelpText(help::text(key));
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }
  void setColour(juce::Colour c) { colour_ = c; repaint(); }
  void setFilled(bool filled) { filled_ = filled; repaint(); }
  void paintButton(juce::Graphics& g, bool, bool) override {
    if (filled_) paint::fill(g, getLocalBounds().toFloat(), 4.0f, juce::Colours::white.withAlpha(0.12f));
    const auto box = juce::Rectangle<float>(static_cast<float>(glyph_), static_cast<float>(glyph_))
                         .withCentre(getLocalBounds().toFloat().getCentre());
    Icons::draw(g, icon_, box, colour_);
  }

private:
  Icon icon_;
  int glyph_;
  juce::Colour colour_ = theme::kWhite;
  bool filled_ = false;
};

// Section header / empty-state text.
class Label : public juce::Component {
public:
  Label(juce::String text, juce::Font font, juce::Colour colour, juce::BorderSize<int> pad)
      : text_(std::move(text)), font_(std::move(font)), colour_(colour), pad_(pad) {
    setInterceptsMouseClicks(false, false);
  }
  void paint(juce::Graphics& g) override {
    paint::text(g, text_, pad_.subtractedFrom(getLocalBounds()), font_, colour_);
  }

private:
  juce::String text_;
  juce::Font font_;
  juce::Colour colour_;
  juce::BorderSize<int> pad_;
};

}  // namespace

// Pill buttons
class PresetBar::Chevron : public Clickable {
public:
  Chevron(Icon icon, help::Key key) : Clickable({}), icon_(icon) {
    setHelpText(help::text(key));
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }
  void setLit(bool lit) { lit_ = lit; repaint(); }
  void paintButton(juce::Graphics& g, bool, bool) override {
    const auto box = juce::Rectangle<float>(14, 14).withCentre(getLocalBounds().toFloat().getCentre());
    Icons::draw(g, icon_, box, lit_ ? theme::kWhite : kMutedText);
  }

private:
  Icon icon_;
  bool lit_ = true;
};

class PresetBar::NameButton : public Clickable {
public:
  NameButton() : Clickable({}) {
    setHelpText(help::text(help::Key::presetBrowse));
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }
  void set(const juce::String& text, bool active) {
    text_ = text;
    active_ = active;
    repaint();
  }
  void paintButton(juce::Graphics& g, bool, bool) override {
    paint::text(g, text_, getLocalBounds().reduced(6, 0), Fonts::sans(14),
                active_ ? theme::kWhite : kMutedText, juce::Justification::centred);
  }

private:
  juce::String text_;
  bool active_ = false;
};

// Save popover
class PresetBar::SavePanel : public Popover {
public:
  static constexpr int kWidth = 280;
  static constexpr int kPad = 16;
  static constexpr int kTitleHeight = 16;
  static constexpr int kInputHeight = 35;  // 13px text + 9px padding + 1px border
  static constexpr int kButtonHeight = 35;

  explicit SavePanel(PresetBar& owner) : owner_(owner) {
    setSize(kWidth, kBorder * 2 + kPad + kTitleHeight + 12 + kInputHeight + 12 + kButtonHeight + kPad);
    name_.setPlaceholder("Name");
    name_.onChange = [this](const juce::String&) { repaint(); };
    name_.onEnter = [this] { save(); };
    addAndMakeVisible(name_);
    saveButton_.onClick = [this] { save(); };
    saveButton_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible(saveButton_);
  }

  void show(const juce::String& prefill) {
    name_.setText(prefill);
    open(owner_, Align::left, kPanelGap, kPanelInset);
    name_.focus();
  }

  void paint(juce::Graphics& g) override {
    const auto box = getLocalBounds().toFloat();
    paint::fill(g, box, theme::kPanelCorner, theme::kPanelBg);
    paint::border(g, box, theme::kPanelCorner, theme::kBorder);
    paint::text(g, "Save Preset", contentBounds().reduced(kPad).removeFromTop(kTitleHeight),
                Fonts::sans(14, true), theme::kWhite);
    // Outline pill: border rgba(235,235,245,0.6), label white / muted.
    const bool enabled = name_.text().trim().isNotEmpty();
    const auto b = saveButton_.getBounds().toFloat();
    paint::border(g, b, b.getHeight() / 2, juce::Colour(235, 235, 245).withAlpha(0.6f));
    paint::text(g, "Save", saveButton_.getBounds(), Fonts::sans(13),
                enabled ? theme::kWhite : kMutedText, juce::Justification::centred);
  }

  void resized() override {
    auto area = contentBounds().reduced(kPad);
    area.removeFromTop(kTitleHeight + 12);
    name_.setBounds(area.removeFromTop(kInputHeight));
    area.removeFromTop(12);
    saveButton_.setBounds(area.removeFromTop(kButtonHeight));
  }

private:
  // Invisible hit target; the panel paints the pill so the label colour can
  // follow the field without a second component.
  class Hit : public Clickable {
  public:
    Hit() : Clickable("Save") {}
    void paintButton(juce::Graphics&, bool, bool) override {}
  };

  void save() {
    const auto name = name_.text().trim();
    if (name.isEmpty()) return;
    close();
    if (owner_.beforeSave) owner_.beforeSave();
    if (owner_.services_.presets.save(name)) owner_.services_.toast.show("Preset Saved");
  }

  PresetBar& owner_;
  TextField name_;
  Hit saveButton_;
};

// Preset browser
class PresetBar::BrowsePanel : public Popover {
public:
  static constexpr int kWidth = 360;
  static constexpr int kPad = 12;
  static constexpr int kSearchHeight = 33;  // 13px text + 8px padding + 1px border

  explicit BrowsePanel(PresetBar& owner) : owner_(owner) {
    search_.setPlaceholder("Search presets");
    search_.setPadding(8, 32, 12);
    search_.setLeadingIcon(Icon::Search, 14, 12, kMutedText);
    search_.onChange = [this](const juce::String&) { rebuild(); };
    addAndMakeVisible(search_);

    pcToggle_.onClick = [this] {
      owner_.services_.prefs.setBool(UiPrefs::kShowPresetPcNumbers, !showPc());
    };
    addChildComponent(pcToggle_);
    reorderToggle_.onClick = [this] {
      reordering_ = !reordering_;
      rebuild();
    };
    addChildComponent(reorderToggle_);

    viewport_.setViewedComponent(&content_, false);
    addAndMakeVisible(viewport_);
  }

  void show() {
    search_.setText({});
    renamingId_.clear();
    reordering_ = false;
    ordered_.reset();
    rebuild();
    open(owner_, Align::left, kPanelGap, kPanelInset);
    search_.focus();
  }

  // Store refresh: drop the optimistic order unless a drag is in flight.
  void presetsChanged() {
    if (!dragging_) ordered_.reset();
    if (isOpen()) rebuild();
  }

  void paint(juce::Graphics& g) override {
    const auto box = getLocalBounds().toFloat();
    paint::fill(g, box, theme::kPanelCorner, theme::kPanelBg);
    paint::border(g, box, theme::kPanelCorner, theme::kBorder);
  }

  void resized() override {
    auto area = contentBounds().reduced(kPad, 0).withTrimmedTop(kPad);
    auto row = area.removeFromTop(kSearchHeight);
    // Row: search (flex 1) · [PC toggle] · [reorder toggle], gap 6.
    if (reorderToggle_.isVisible())
      reorderToggle_.setBounds(row.removeFromRight(29).withSizeKeepingCentre(29, 29)),
          row.removeFromRight(6);
    if (pcToggle_.isVisible())
      pcToggle_.setBounds(row.removeFromRight(29).withSizeKeepingCentre(29, 29)), row.removeFromRight(6);
    search_.setBounds(row);
    viewport_.setBounds(area);
    content_.setSize(viewport_.getWidth(), content_.getHeight());
  }

private:
  class Row;

  bool showPc() const { return owner_.services_.prefs.getBool(UiPrefs::kShowPresetPcNumbers, false); }
  const std::vector<PresetInfo>& fullList() const {
    return ordered_ ? *ordered_ : owner_.presets();
  }

  // Rebuild the rows from the (optimistically ordered, search-filtered) list
  // and size the panel to fit (max kListMaxHeight of scrolling list).
  void rebuild();
  void layoutRows();

  // Drag-reorder within a section (grip in reorder mode).
  std::vector<Row*> sectionOf(const Row& row) const;
  void beginDrag(Row& row);
  void dragTo(Row& row, int dy);
  void endDrag(Row& row);

  PresetBar& owner_;
  TextField search_;
  GlyphButton pcToggle_{Icon::MidiPort, 15, 7, help::Key::presetPcToggle};
  GlyphButton reorderToggle_{Icon::ArrowUpDown, 15, 7, help::Key::presetReorder};
  DragScroller viewport_{DragScroller::Axis::vertical, DragScroller::Keys::none};  // the arrows walk the rows
  juce::Component content_;
  std::vector<std::unique_ptr<juce::Component>> items_;  // headers, rows, empty text
  std::vector<Row*> rows_;
  juce::String renamingId_;
  bool reordering_ = false;
  std::optional<std::vector<PresetInfo>> ordered_;
  bool dragging_ = false;
  int dragStartY_ = 0, dragSectionTop_ = 0, dragTarget_ = 0;
};

class PresetBar::BrowsePanel::Row : public juce::Component {
public:
  Row(BrowsePanel& panel, PresetInfo preset, int sectionIndex, bool active, bool sortable,
      std::optional<int> pc, bool renaming)
      : panel_(panel), preset_(std::move(preset)), sectionIndex_(sectionIndex), active_(active),
        sortable_(sortable), pc_(pc), renaming_(renaming) {
    name_.onClick = [this] { panel_.owner_.loadAndClose(preset_.id); };
    name_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible(name_);

    if (renaming_) {
      rename_ = std::make_unique<TextField>();
      rename_->setPadding(4, 8, 8);
      rename_->setCornerRadius(6);
      rename_->setText(preset_.name);
      rename_->onEnter = [this] { commitRename(); };
      rename_->onBlur = [this] { commitRename(); };
      rename_->onEscape = [this] {
        panel_.renamingId_.clear();
        panel_.rebuild();
      };
      addAndMakeVisible(*rename_);
      name_.setVisible(false);
    }

    if (sortable_) {
      grip_ = std::make_unique<GlyphButton>(Icon::GripVertical, 14, 3, help::Key::presetDrag);
      grip_->setMouseCursor(juce::MouseCursor::DraggingHandCursor);
      grip_->setViewportIgnoreDragFlag(true);  // a drag on the grip sorts the row; the list does not pan
      grip_->addMouseListener(this, false);
      addAndMakeVisible(*grip_);
    } else if (!preset_.factory && !renaming_) {
      pencil_ = std::make_unique<GlyphButton>(Icon::Pencil, 13, 3, help::Key::presetRename);
      pencil_->onClick = [this] {
        panel_.renamingId_ = preset_.id;
        panel_.rebuild();
      };
      addAndMakeVisible(*pencil_);
      trash_ = std::make_unique<GlyphButton>(Icon::Trash2, 13, 3, help::Key::presetDelete);
      trash_->onClick = [this] { panel_.owner_.services_.presets.remove(preset_.id); };
      addAndMakeVisible(*trash_);
    }
    setSize(0, kRowHeight);
  }

  const PresetInfo& preset() const { return preset_; }
  int sectionIndex() const { return sectionIndex_; }
  void focusRename() {
    if (rename_) rename_->focus();
  }
  void setDragging(bool dragging) { setAlpha(dragging ? 0.75f : 1.0f); }

  void paint(juce::Graphics& g) override {
    // padding 0 4, gap 8: check slot (16) · name · [PC] · actions.
    auto area = getLocalBounds().reduced(4, 0);
    const auto check = area.removeFromLeft(16);
    if (active_)
      Icons::draw(g, Icon::Check, juce::Rectangle<float>(14, 14).withCentre(check.toFloat().getCentre()),
                  theme::kWhite);
    if (pc_) {
      const auto font = Fonts::mono(11);
      const auto label = "PC " + juce::String(*pc_);
      paint::text(g, label, pcArea_, font, kMutedText);
    }
  }

  void resized() override {
    auto area = getLocalBounds().reduced(4, 0);
    area.removeFromLeft(16 + 8);
    // Trailing actions, right to left.
    auto takeRight = [&](juce::Component& c) {
      c.setBounds(area.removeFromRight(c.getWidth()).withSizeKeepingCentre(c.getWidth(), c.getHeight()));
      area.removeFromRight(8);
    };
    if (grip_) takeRight(*grip_);
    if (trash_) takeRight(*trash_);
    if (pencil_) takeRight(*pencil_);
    if (pc_) {
      const auto font = Fonts::mono(11);
      const int w = juce::roundToInt(Fonts::width(font, "PC " + juce::String(*pc_)));
      pcArea_ = area.removeFromRight(w);
      area.removeFromRight(8);
    }
    if (rename_) rename_->setBounds(area.withSizeKeepingCentre(area.getWidth(), 25));
    name_.setBounds(area);
  }

  // Grip drag (events forwarded from the grip button).
  void mouseDown(const juce::MouseEvent& e) override {
    if (grip_ && e.eventComponent == grip_.get()) panel_.beginDrag(*this);
  }
  void mouseDrag(const juce::MouseEvent& e) override {
    if (grip_ && e.eventComponent == grip_.get())
      panel_.dragTo(*this, e.getDistanceFromDragStartY());
  }
  void mouseUp(const juce::MouseEvent& e) override {
    if (grip_ && e.eventComponent == grip_.get()) panel_.endDrag(*this);
  }

private:
  class NameButton : public Clickable {
  public:
    NameButton(const juce::String& text, bool active) : Clickable(text), text_(text), active_(active) {}
    void paintButton(juce::Graphics& g, bool, bool) override {
      paint::text(g, text_, getLocalBounds(), Fonts::sans(14), active_ ? theme::kWhite : kMutedText);
    }

  private:
    juce::String text_;
    bool active_;
  };

  void commitRename() {
    if (!rename_) return;
    const auto value = rename_->text().trim();
    auto& panel = panel_;
    panel.renamingId_.clear();
    if (value.isNotEmpty() && value != preset_.name)
      panel.owner_.services_.presets.rename(preset_.id, value);  // rebuilds via store listener
    else
      panel.rebuild();
  }

  BrowsePanel& panel_;
  PresetInfo preset_;
  int sectionIndex_;
  bool active_, sortable_;
  std::optional<int> pc_;
  bool renaming_;
  NameButton name_{preset_.name, active_};
  std::unique_ptr<TextField> rename_;
  std::unique_ptr<GlyphButton> grip_, pencil_, trash_;
  juce::Rectangle<int> pcArea_;
};

void PresetBar::BrowsePanel::rebuild() {
  // Keep the search field's text; everything below it is regenerated.
  items_.clear();
  rows_.clear();

  const auto& presets = owner_.presets();
  const auto& list = fullList();
  const auto query = search_.text().trim().toLowerCase();
  std::vector<PresetInfo> filtered;
  for (const auto& p : list)
    if (query.isEmpty() || p.name.toLowerCase().contains(query)) filtered.push_back(p);

  pcToggle_.setVisible(!presets.empty());
  pcToggle_.setColour(showPc() ? theme::kWhite : kMutedText);
  pcToggle_.setFilled(showPc());
  reorderToggle_.setVisible(presets.size() > 1);
  reorderToggle_.setColour(reordering_ ? theme::kWhite : kMutedText);
  reorderToggle_.setFilled(reordering_);

  // Grips only on the full list: a filter's indices don't match the
  // persisted order, and moves never cross the factory/user boundary.
  const bool canDrag = reordering_ && query.isEmpty();
  // PC n loads the nth preset of the full list; the wire only carries 0-127.
  std::map<juce::String, int> pcById;
  for (size_t i = 0; i < list.size() && i <= 127; ++i) pcById[list[i].id] = static_cast<int>(i);

  const auto activeId = owner_.active() ? owner_.active()->id : juce::String();
  auto addSection = [&](const char* title, bool factory) {
    int index = 0;
    bool any = false;
    for (const auto& p : filtered) {
      if (p.factory != factory) continue;
      if (!any) {
        items_.push_back(std::make_unique<Label>(title, Fonts::sans(14, true), theme::kGray,
                                                 juce::BorderSize<int>(8, 4, 8, 4)));
        any = true;
      }
      std::optional<int> pc;
      if (showPc())
        if (auto it = pcById.find(p.id); it != pcById.end()) pc = it->second;
      auto row = std::make_unique<Row>(*this, p, index++, p.id == activeId, canDrag, pc,
                                       renamingId_ == p.id);
      rows_.push_back(row.get());
      items_.push_back(std::move(row));
    }
  };
  addSection("Your Presets", false);
  addSection("TONE3000", true);
  if (filtered.empty())
    items_.push_back(std::make_unique<Label>(
        presets.empty() ? "No presets yet. Save one to get started." : "No matches.", Fonts::sans(13),
        kMutedText, juce::BorderSize<int>(12, 4, 12, 4)));

  for (auto& item : items_) content_.addAndMakeVisible(*item);
  layoutRows();

  const int listHeight = std::min(kListMaxHeight, content_.getHeight());
  setSize(kWidth, kBorder * 2 + kPad + kSearchHeight + listHeight);
  resized();
  reposition();
  for (auto* row : rows_)
    if (row->preset().id == renamingId_) row->focusRename();
}

void PresetBar::BrowsePanel::layoutRows() {
  const int width = std::max(0, viewport_.getWidth() > 0 ? viewport_.getWidth() : kWidth - 2 * kPad);
  int y = kListPadTop;
  for (auto& item : items_) {
    const bool isRow = dynamic_cast<Row*>(item.get()) != nullptr;
    const int h = isRow ? kRowHeight
                        : (dynamic_cast<Label*>(item.get()) != nullptr &&
                                   items_.size() == 1  // empty-state text: 13px + 12px padding
                               ? 39
                               : kSectionHeaderHeight);
    item->setBounds(0, y, width, h);
    y += h;
  }
  content_.setSize(width, y + kListPadBottom);
}

std::vector<PresetBar::BrowsePanel::Row*> PresetBar::BrowsePanel::sectionOf(const Row& row) const {
  std::vector<Row*> section;
  for (auto* r : rows_)
    if (r->preset().factory == row.preset().factory) section.push_back(r);
  return section;
}

void PresetBar::BrowsePanel::beginDrag(Row& row) {
  dragging_ = true;
  row.setDragging(true);
  row.toFront(false);
  dragStartY_ = row.getY();
  dragSectionTop_ = dragStartY_ - row.sectionIndex() * kRowHeight;
  dragTarget_ = row.sectionIndex();
}

void PresetBar::BrowsePanel::dragTo(Row& row, int dy) {
  // Follow the pointer, clamped to the row's own section; slide siblings
  // out of the way so the drop position reads live.
  const auto section = sectionOf(row);
  const int maxY = dragSectionTop_ + kRowHeight * (static_cast<int>(section.size()) - 1);
  const int y = juce::jlimit(dragSectionTop_, maxY, dragStartY_ + dy);
  row.setTopLeftPosition(row.getX(), y);
  dragTarget_ = (y - dragSectionTop_ + kRowHeight / 2) / kRowHeight;
  int slot = 0;
  for (auto* r : section) {
    if (r == &row) continue;
    if (slot == dragTarget_) ++slot;
    r->setTopLeftPosition(r->getX(), dragSectionTop_ + slot * kRowHeight);
    ++slot;
  }
}

void PresetBar::BrowsePanel::endDrag(Row& row) {
  dragging_ = false;
  row.setDragging(false);
  const int from = row.sectionIndex();
  const int to = dragTarget_;
  // Everything needed from the row, taken now: rebuild() below destroys
  // every Row including this one, so `row` is dead from that point on.
  const juce::String id = row.preset().id;
  const bool factory = row.preset().factory;
  if (to == from) {
    layoutRows();
    return;
  }
  // Optimistic order until the native list refresh; user section first,
  // then factory, matching the native list (PC labels index into it).
  auto list = fullList();
  std::vector<PresetInfo> section, others;
  for (const auto& p : list) (p.factory == factory ? section : others).push_back(p);
  auto item = section[static_cast<size_t>(from)];
  section.erase(section.begin() + from);
  section.insert(section.begin() + to, item);
  // Factory presets list after the user's; the moved section keeps its place.
  auto& first = factory ? others : section;
  auto& second = factory ? section : others;
  first.insert(first.end(), second.begin(), second.end());
  ordered_ = std::move(first);
  rebuild();
  // The store refresh inside move() drops ordered_ and rebuilds from the
  // persisted order, so a failed move snaps the rows back.
  owner_.services_.presets.move(id, to - from);
}

// Bar
PresetBar::PresetBar(Services& services)
    : services_(services),
      prev_(std::make_unique<Chevron>(Icon::ChevronLeft, help::Key::presetPrev)),
      next_(std::make_unique<Chevron>(Icon::ChevronRight, help::Key::presetNext)),
      name_(std::make_unique<NameButton>()),
      savePanel_(std::make_unique<SavePanel>(*this)),
      browsePanel_(std::make_unique<BrowsePanel>(*this)) {
  prev_->onClick = [this] { step(-1); };
  next_->onClick = [this] { step(1); };
  name_->onClick = [this] { openBrowsePanel(); };
  addAndMakeVisible(*prev_);
  addAndMakeVisible(*name_);
  addAndMakeVisible(*next_);

  save_.setCornerRadius(4);
  save_.setHelpText(help::text(help::Key::presetSave));
  save_.onClick = [this] { openSavePanel(); };
  addAndMakeVisible(save_);

  newButton_.setHelpText(help::text(help::Key::presetNew));
  newButton_.onClick = [this] {
    closePanels();
    if (onReset) onReset();
  };
  addAndMakeVisible(newButton_);

  services_.presets.addListener(this);
  services_.chain.addListener(this);
  services_.prefs.addListener(this);
  setSize(kWidth, kHeight);
  refreshChrome();
}

PresetBar::~PresetBar() {
  services_.prefs.removeListener(this);
  services_.chain.removeListener(this);
  services_.presets.removeListener(this);
  closePanels();
}

void PresetBar::presetsChanged(const std::vector<PresetInfo>&) {
  refreshChrome();
  browsePanel_->presetsChanged();
}

void PresetBar::chainChanged(const ChainState&) {
  refreshChrome();
  browsePanel_->presetsChanged();
}

void PresetBar::prefChanged(const juce::String& key) {
  if (key == UiPrefs::kShowPresetPcNumbers) browsePanel_->presetsChanged();
}

void PresetBar::refreshChrome() {
  const bool any = !presets().empty();
  prev_->setLit(any);
  next_->setLit(any);
  name_->set(active() ? active()->name : juce::String("Presets"), active().has_value());
  newButton_.setEnabled(!services_.chain.state().atDefault);
}

void PresetBar::step(int direction) {
  const auto& list = presets();
  if (list.empty()) return;
  int index = -1;
  if (active())
    for (size_t i = 0; i < list.size(); ++i)
      if (list[i].id == active()->id) index = static_cast<int>(i);
  const int n = static_cast<int>(list.size());
  // Wrap at the ends; with no active preset, › starts at the first, ‹ at the last.
  const int next = index < 0 ? (direction > 0 ? 0 : n - 1) : (index + direction + n) % n;
  loadAndClose(list[static_cast<size_t>(next)].id);
}

void PresetBar::loadAndClose(const juce::String& id) {
  closePanels();
  if (beforeLoad) beforeLoad();
  services_.presets.load(id);
}

void PresetBar::openSavePanel() {
  if (savePanel_->isOpen()) {
    savePanel_->close();
    return;
  }
  browsePanel_->close();
  // Prefill with the active user preset's name: saving it again is the
  // one-click "update" path (same name overwrites in place).
  juce::String prefill;
  if (active())
    for (const auto& p : presets())
      if (p.id == active()->id && !p.factory) prefill = p.name;
  savePanel_->show(prefill);
}

void PresetBar::openBrowsePanel() {
  if (browsePanel_->isOpen()) {
    browsePanel_->close();
    return;
  }
  savePanel_->close();
  browsePanel_->show();
}

void PresetBar::closePanels() {
  savePanel_->close();
  browsePanel_->close();
}

void PresetBar::paint(juce::Graphics& g) {
  // The ‹ name › pill: SEGMENTED_TRACK fill, radius 8.
  paint::fill(g, juce::Rectangle<float>(0, 0, kPillWidth, kHeight), 8.0f, theme::kSegmentedTrack);
}

void PresetBar::resized() {
  auto pill = getLocalBounds().removeFromLeft(kPillWidth).reduced(4, 0);
  prev_->setBounds(pill.removeFromLeft(22));
  next_->setBounds(pill.removeFromRight(22));
  name_->setBounds(pill);
  save_.setBounds(kPillWidth + 8, (kHeight - 28) / 2, 28, 28);
  newButton_.setBounds(kPillWidth + 8 + 28 + 8, (kHeight - 28) / 2, 28, 28);
}

}  // namespace t3k::ui
