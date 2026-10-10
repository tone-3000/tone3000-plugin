#include "LibraryDrawer.h"

#include "T3kConfig.h"

#include "core/Fonts.h"
#include "core/GearGlyphs.h"
#include "core/Help.h"
#include "core/Labels.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "widgets/Clickable.h"
#include "widgets/Popover.h"

namespace t3k::ui {

namespace {

constexpr int kPadX = 12;
constexpr int kHeaderHeight = 48;
constexpr int kSearchHeight = 32;
constexpr int kStripHeight = 40;
constexpr int kFooterHeight = 30;
constexpr int kRowHeight = 28;
constexpr int kRowTop = 4;  // the list's top padding
// Indent per level: full for the first few, then half, so a deeply nested
// collection (eight levels is not unusual) keeps room for its names.
constexpr int kIndent = 12, kDeepIndent = 6, kFullIndentLevels = 3;
int indentFor(int depth) {
  return juce::jmin(depth, kFullIndentLevels) * kIndent + juce::jmax(0, depth - kFullIndentLevels) * kDeepIndent;
}
constexpr int kChevron = 12;
constexpr int kGlyph = 16;
constexpr float kRowRadius = 6.0f;

// Drop target: the gallery's file-drop green, as a tint and an outline.
const juce::Colour kDropColour{0xff30d158};

bool isWithin(const juce::String& path, const juce::String& ancestor) {
  return path.startsWith(ancestor + juce::File::getSeparatorString());
}

// "…/TONE3000/Library": the end of a path that says where it is.
juce::String shortPath(const juce::String& path) {
  const juce::File f(path);
  return juce::String::fromUTF8("\xe2\x80\xa6/") + f.getParentDirectory().getFileName() + "/" + f.getFileName();
}

}  // namespace

// The gear filter under the search: one toggle per catalog gear kind and
// one for presets; any number on, the list shows only those kinds, in every
// library (with the folders holding them). All off: everything.
class LibraryDrawer::GearRow : public juce::Component {
public:
  static constexpr int kHeight = 28;

  explicit GearRow(LibraryDrawer& drawer) : drawer_(drawer) {
    for (const auto& f : labels::gearFilters()) add(f.id, f.label);
    add("preset", "Presets");
  }

  void resized() override {
    const int n = static_cast<int>(buttons_.size());
    if (n == 0) return;
    const int w = getWidth() / n;
    for (int i = 0; i < n; ++i) buttons_[static_cast<size_t>(i)]->setBounds(i * w, 0, w, getHeight());
  }

  void sync() {
    for (auto& b : buttons_) b->repaint();
  }

private:
  class Toggle : public Clickable {
  public:
    Toggle(GearRow& row, juce::String id, const juce::String& label) : row_(row), id_(std::move(id)) {
      setTitle(label);
      setHelpText(label + ": show only " + (id_ == "preset" ? juce::String("presets") : label.toLowerCase()) +
                  " in every library (any number at once; all off shows everything).");
      setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    bool on() const { return row_.drawer_.services_.library.gearFilter.count(id_) != 0; }
    void paintButton(juce::Graphics& g, bool over, bool) override {
      const auto box = getLocalBounds().toFloat().reduced(1.5f, 1.0f);
      if (on()) paint::fill(g, box, 6.0f, theme::kHighlight);
      else if (over) paint::fill(g, box, 6.0f, juce::Colour(theme::kWhite).withAlpha(0.06f));
      const auto glyph = box.withSizeKeepingCentre(18, 18);
      const auto colour = on() ? theme::kWhite : theme::kGray;
      if (id_ == "preset") Icons::draw(g, Icon::SlidersHorizontal, glyph.reduced(1), colour);
      else Icons::draw(g, gear::svgFor(id_), glyph, colour);
    }
    void clicked() override {
      auto& library = row_.drawer_.services_.library;
      if (on()) library.gearFilter.erase(id_);
      else library.gearFilter.insert(id_);
      library.saveView();
      row_.drawer_.rebuild();
      row_.sync();
    }

  private:
    GearRow& row_;
    juce::String id_;
  };

  void add(const juce::String& id, const juce::String& label) {
    buttons_.push_back(std::make_unique<Toggle>(*this, id, label));
    addAndMakeVisible(*buttons_.back());
  }

  LibraryDrawer& drawer_;
  std::vector<std::unique_ptr<Toggle>> buttons_;
};

// One tree row (see the header for the gestures).
class LibraryDrawer::Row : public juce::Component, public juce::DragAndDropTarget {
public:
  Row(LibraryDrawer& drawer, const LibraryRow& row) : drawer_(drawer), node_(*row.node), depth_(row.depth), open_(row.open) {
    // A kept copy (KEEP on a block card): drawn apart, and it knows its original.
    if (node_.kind == LibraryNode::Kind::capture) {
      original_ = drawer_.services_.library.knownOriginalOf(node_.path);  // no file reads while scrolling
      siteOriginal_ = drawer_.services_.library.siteOriginalOf(node_.path);
    }
    setHelpText(hint());
    getProperties().set(HintBus::kIconProperty, hintIcon());
    setTitle(node_.name);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    // A tile's tone waiting for a folder: the folders that can take it say
    // so with a button, and a click on the row still opens or closes it.
    const auto& library = drawer_.services_.library;
    const bool yours = node_.kind == LibraryNode::Kind::library && node_.mine && !node_.site;
    // Favorites take a tone added, not a capture kept or downloaded.
    const bool favoritesTake = library.pendingKind() == LibraryStore::AddKind::add;
    if (library.pendingAdd() &&
        ((node_.isContainer() && node_.writable && !node_.isPresets() && (favoritesTake || !node_.favorites)) ||
         yours)) {
      addHere_ = std::make_unique<AddHere>();
      // Your library: its Captures, listed or not yet.
      const auto* captures = library.capturesRoot();
      const auto target = yours ? (captures != nullptr ? captures->path : juce::String()) : node_.path;
      addHere_->onClick = [&drawer = drawer_, path = target] {
        // Posted: the add rebuilds the rows, this button with them.
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<LibraryDrawer>(&drawer), path] {
          if (safe == nullptr) return;
          auto& lib = safe->services_.library;
          lib.finishAdd(path);
        });
      };
      addAndMakeVisible(*addHere_);
    }
  }

  void resized() override {
    if (addHere_) addHere_->setBounds(getWidth() - kAddHereWidth - 8, 3, kAddHereWidth, getHeight() - 6);
  }

  const LibraryNode& node() const { return node_; }
  void setDropTarget(bool on) {
    if (dropTarget_ == on) return;
    dropTarget_ = on;
    repaint();
  }

  void paint(juce::Graphics& g) override {
    auto& library = drawer_.services_.library;
    const auto box = getLocalBounds().toFloat().reduced(4, 1);
    if (dropTarget_) {
      paint::fill(g, box, kRowRadius, kDropColour.withAlpha(0.16f));
      g.setColour(kDropColour);
      g.drawRoundedRectangle(box.reduced(0.5f), kRowRadius, 1.0f);
    } else if (library.selected() == node_.path) {
      paint::fill(g, box, kRowRadius, theme::kHighlight);
    }
    if (drawer_.dropLine_ == node_.path) {
      // The insertion line on this row's top or bottom edge, from its indent.
      const float lx = 8.0f + static_cast<float>(indentFor(depth_));
      const float ly = drawer_.dropLineAfter_ ? static_cast<float>(getHeight()) - 1.5f : 1.5f;
      g.setColour(kDropColour);
      g.fillRoundedRectangle(lx, ly - 1.0f, static_cast<float>(getWidth()) - lx - 8.0f, 2.0f, 1.0f);
      g.fillEllipse(lx - 3.0f, ly - 3.0f, 6.0f, 6.0f);
    }

    int x = 8 + indentFor(depth_);
    const float cy = getHeight() / 2.0f;
    if (node_.isContainer()) {
      const bool empty = node_.children.empty();
      if (!empty)
        Icons::draw(g, open_ ? Icon::ChevronDown : Icon::ChevronRight,
                    juce::Rectangle<float>(static_cast<float>(x), cy - kChevron / 2.0f, kChevron, kChevron), theme::kGray);
    }
    x += kChevron + 4;
    drawGlyph(g, juce::Rectangle<float>(static_cast<float>(x), cy - kGlyph / 2.0f, kGlyph, kGlyph));
    x += kGlyph + 8;

    auto text = getLocalBounds().withLeft(x).withTrimmedRight(10 + (addHere_ ? kAddHereWidth + 4 : 0));
    const bool active = (node_.kind == LibraryNode::Kind::preset && node_.presetId == drawer_.activePresetId_) ||
                        drawer_.playing(node_);
    const bool locked = node_.kind == LibraryNode::Kind::library && !node_.mine;
    const bool keeping = node_.isContainer() && node_.path == library.keepTarget();
    if (kept()) {
      auto mark = text.removeFromRight(14).toFloat();
      Icons::draw(g, Icon::Copy, mark.withSizeKeepingCentre(12, 12), theme::kLinkBlue);
      text.removeFromRight(4);
    }
    if (node_.kind == LibraryNode::Kind::tone && node_.onDisk()) {
      // A link kept in your folders (Keep as Link, Add to Library).
      auto mark = text.removeFromRight(14).toFloat();
      Icons::draw(g, Icon::Link, mark.withSizeKeepingCentre(12, 12), theme::kBrandYellow.withAlpha(0.9f));
      text.removeFromRight(4);
    }
    if (keeping) {
      // The keep folder: KEEP on a block card lands here.
      auto mark = text.removeFromRight(14).toFloat();
      Icons::draw(g, Icon::Download, mark.withSizeKeepingCentre(12, 12), theme::kLinkBlue);
      text.removeFromRight(4);
    }
    if (active || locked || node_.linked) {
      // A dot for the loaded preset (and what the chain plays); a lock for a
      // library that isn't yours;
      // a link for a folder linked in from elsewhere.
      auto mark = text.removeFromRight(14).toFloat();
      if (active)
        paint::fill(g, mark.withSizeKeepingCentre(6, 6), 3.0f, theme::kLinkBlue);
      else
        Icons::draw(g, locked ? Icon::Lock : Icon::Link, mark.withSizeKeepingCentre(12, 12), theme::kGray);
      text.removeFromRight(4);
    }
    const bool heading = node_.kind == LibraryNode::Kind::library;
    const auto colour = node_.missing ? theme::kGray
                        : active      ? theme::kWhite
                                      : juce::Colour(theme::kWhite).withAlpha(0.92f);
    paint::text(g, node_.missing ? node_.name + " (not found)" : node_.name, text, Fonts::sans(13, heading), colour);
  }

  // Gestures
  void mouseDown(const juce::MouseEvent& e) override {
    dragging_ = false;
    // A mouse drag carries the row; a touch drag pans the list.
    setViewportIgnoreDragFlag(!e.source.isTouch());
    drawer_.services_.library.select(node_.path);
    for (auto& [index, row] : drawer_.rows_) row->repaint();
    drawer_.grabKeyboardFocus();  // the arrow keys move from here
    if (e.mods.isPopupMenu()) drawer_.rowMenu(*this, e.getPosition());
  }

  void mouseDrag(const juce::MouseEvent& e) override {
    if (dragging_ || e.source.isTouch() || e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 6) return;
    auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
    if (container == nullptr) return;
    dragging_ = true;
    juce::DynamicObject::Ptr desc = new juce::DynamicObject();
    desc->setProperty(LibraryStore::kDragKey, node_.path);
    // The drawer is the drag's source, not this row: a rebuild mid-drag (a
    // scan landing, the favorites arriving) replaces the rows, and JUCE
    // cancels a drag whose source is gone. The image is this row, as JUCE
    // would draw it, held where it was picked up.
    constexpr float kScale = 2.0f;
    auto image = createComponentSnapshot(getLocalBounds(), true, kScale).convertedToFormat(juce::Image::ARGB);
    image.multiplyAllAlphas(0.6f);
    const auto offset = -e.getMouseDownPosition();
    container->startDragging(juce::var(desc.get()), &drawer_, juce::ScaledImage(image, kScale), false, &offset);
    drawer_.startDragScroll();
  }

  void mouseUp(const juce::MouseEvent& e) override {
    if (dragging_ || e.mods.isPopupMenu() || !e.mouseWasClicked()) return;
    // A folder toggles on every click, however quick (two fast clicks are
    // open-then-close, not a double-click); an item's second click is its
    // double-click (mouseDoubleClick).
    if (e.getNumberOfClicks() > 1 && !node_.isContainer()) return;
    // Posted: a click may rebuild the rows (this one included).
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<LibraryDrawer>(&drawer_), path = node_.path] {
      if (safe == nullptr) return;
      if (const auto* node = safe->services_.library.tree().find(path)) safe->rowClicked(*node);
    });
  }

  void mouseDoubleClick(const juce::MouseEvent& e) override {
    if (e.mods.isPopupMenu()) return;
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<LibraryDrawer>(&drawer_), path = node_.path] {
      if (safe == nullptr) return;
      if (const auto* node = safe->services_.library.tree().find(path)) safe->rowDoubleClicked(*node);
    });
  }

  // Another row dropped here: into this folder (when it takes things), a
  // library onto a library (it moves to just above it), or a folder beside
  // this one (its top or bottom edge: the folder order).
  bool isInterestedInDragSource(const SourceDetails& details) override {
    const auto dragged = details.description.getProperty(LibraryStore::kDragKey, {}).toString();
    return drawer_.services_.library.canPlaceBeside(dragged, node_.path) || takesInto(dragged);
  }
  bool takesInto(const juce::String& dragged) const {
    const auto* item = drawer_.services_.library.tree().find(dragged);
    if (item != nullptr && item->kind == LibraryNode::Kind::library)
      return node_.kind == LibraryNode::Kind::library && node_.path != dragged;
    // Your library takes things into its section for them (Captures,
    // Presets), listed or not.
    if (item != nullptr && node_.kind == LibraryNode::Kind::library && node_.mine && !node_.site) {
      const auto half = drawer_.services_.library.halfFor(*item);
      return half.isNotEmpty() && half != dragged && !isWithin(half, dragged);
    }
    if (dragged.isEmpty() || !node_.isContainer() || !node_.writable) return false;
    if (dragged == node_.path || isWithin(node_.path, dragged)) return false;
    return item != nullptr && node_.accepts(*item);
  }
  void itemDragEnter(const SourceDetails& details) override { drawer_.dragOver(*this, details); }
  void itemDragMove(const SourceDetails& details) override { drawer_.dragOver(*this, details); }
  void itemDragExit(const SourceDetails&) override {
    drawer_.setDropHighlight({});
    drawer_.setDropLine({}, false);
  }
  bool isOpen() const { return open_; }
  void itemDropped(const SourceDetails& details) override {
    const bool beside = drawer_.dropLine_ == node_.path, after = drawer_.dropLineAfter_;
    drawer_.setDropHighlight({});
    drawer_.setDropLine({}, false);
    const auto dragged = details.description.getProperty(LibraryStore::kDragKey, {}).toString();
    if (beside) {
      juce::MessageManager::callAsync([safe = juce::Component::SafePointer<LibraryDrawer>(&drawer_), dragged,
                                       sibling = node_.path, after] {
        if (safe != nullptr) safe->services_.library.placeFolder(dragged, sibling, after);
      });
      return;
    }
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<LibraryDrawer>(&drawer_), dragged, folder = node_.path] {
      if (safe == nullptr) return;
      auto& library = safe->services_.library;
      const auto* node = library.tree().find(dragged);
      const auto* target = library.tree().find(folder);
      if (node != nullptr && node->kind == LibraryNode::Kind::library) library.moveLibraryBefore(dragged, folder);
      else if (node != nullptr && target != nullptr && target->kind == LibraryNode::Kind::library)
        library.moveOrCopy(dragged, library.halfFor(*node));
      else library.moveOrCopy(dragged, folder);
    });
  }

private:
  // The hint bar shows what is hovered in full: its name (often clipped in
  // the drawer) and where it sits. The fixed roots keep their explanation,
  // which is what someone hovering them for the first time needs.
  juce::String hint() const {
    if (node_.kind == LibraryNode::Kind::library || node_.mount || node_.favorites && node_.isContainer())
      return help::text(helpKey());
    const auto& tree = drawer_.services_.library.tree();
    juce::StringArray trail;
    for (const auto* up = tree.parentOf(node_.path); up != nullptr && up->kind != LibraryNode::Kind::library;
         up = tree.parentOf(up->path))
      trail.insert(0, up->name);
    const auto where = trail.joinIntoString("/");
    auto text = where.isEmpty() ? node_.name : node_.name + juce::String::fromUTF8("  \xc2\xb7  in ") + where;
    if (siteOriginal_) text << juce::String::fromUTF8("  \xc2\xb7  kept from ") << siteOriginal_->title << " on TONE3000";
    else if (kept()) text << juce::String::fromUTF8("  \xc2\xb7  kept from ") << original_.getParentDirectory().getFileName();
    return text;
  }

  bool kept() const { return original_ != juce::File() || siteOriginal_.has_value(); }
  // What the hint bar shows before the name (HintBar::paint reads it).
  juce::String hintIcon() const {
    switch (node_.kind) {
      case LibraryNode::Kind::tone: return "gear:" + node_.tone.gear;
      case LibraryNode::Kind::capture:
        return node_.gear.isNotEmpty() ? "gear:" + node_.gear : juce::String(node_.nam ? "capture" : "ir");
      case LibraryNode::Kind::preset: return "preset";
      case LibraryNode::Kind::folder: return "folder";
      case LibraryNode::Kind::library: return "library";
    }
    return {};
  }
  juce::File original_;
  std::optional<LibraryToneRef> siteOriginal_;  // kept from a TONE3000 tone

  help::Key helpKey() const {
    switch (node_.kind) {
      case LibraryNode::Kind::library:
        if (node_.site) return help::Key::librarySite;
        return node_.mine ? help::Key::libraryLibrary : help::Key::libraryOtherLibrary;
      case LibraryNode::Kind::folder:
        if (node_.favorites) return help::Key::libraryFavorites;
        if (node_.local) return help::Key::libraryLocal;
        if (node_.path == LibraryStore::kSiteCapturesPath) return help::Key::librarySiteCaptures;
        if (node_.path == LibraryStore::kSitePresetsPath) return help::Key::librarySitePresets;
        if (node_.linked) return help::Key::libraryLinked;
        if (node_.mount) return node_.isPresets() ? help::Key::libraryPresetsRoot : help::Key::libraryCapturesRoot;
        return help::Key::libraryFolder;
      case LibraryNode::Kind::preset: return help::Key::libraryPreset;
      case LibraryNode::Kind::tone: return node_.favorites ? help::Key::libraryFavorite : help::Key::libraryTone;
      case LibraryNode::Kind::capture: return help::Key::libraryCapture;
    }
    return help::Key::libraryFolder;
  }

  void drawGlyph(juce::Graphics& g, juce::Rectangle<float> box) {
    const auto colour = juce::Colour(theme::kWhite).withAlpha(0.8f);
    switch (node_.kind) {
      case LibraryNode::Kind::library:
        Icons::draw(g, Icon::LibraryBig, box, node_.mine ? theme::kWhite : theme::kGray);
        break;
      case LibraryNode::Kind::folder:
        // The fixed roots say what they hold; other folders are folders.
        if (node_.favorites)
          Icons::draw(g, Icon::Bookmark, box, colour);
        else if (node_.local)
          Icons::draw(g, Icon::Link, box, colour);
        else if (node_.mount && !node_.linked)
          Icons::draw(g, node_.isPresets() ? Icon::SlidersHorizontal : Icon::AudioLines, box, colour);
        else
          Icons::draw(g, open_ && !node_.children.empty() ? Icon::FolderOpen : Icon::FolderClosed, box, colour);
        break;
      case LibraryNode::Kind::preset:
        Icons::draw(g, Icon::SlidersHorizontal, box, colour);
        break;
      case LibraryNode::Kind::tone:
        // A TONE3000 link (all its captures, played from the site): yellow,
        // apart from your files (white) and kept copies (blue).
        Icons::draw(g, gear::svgFor(node_.tone.gear), box.expanded(2), theme::kBrandYellow.withAlpha(0.9f));
        break;
      case LibraryNode::Kind::capture: {
        // Its gear when known (its folder's TONE3000 match, or the file's
        // metadata), else what kind of file it is.
        const auto tint = kept() ? theme::kLinkBlue : colour;
        if (node_.gear.isNotEmpty()) Icons::draw(g, gear::svgFor(node_.gear), box.expanded(2), tint);
        else Icons::draw(g, node_.nam ? Icon::AudioLines : Icon::File, box, tint);
        break;
      }
    }
  }

  // "Add here": the pending tone goes into this folder.
  class AddHere : public Clickable {
  public:
    AddHere() : Clickable("Add here") {
      setMouseCursor(juce::MouseCursor::PointingHandCursor);
      setHelpText(help::text(help::Key::libraryAddHere));
    }
    void paintButton(juce::Graphics& g, bool over, bool) override {
      const auto box = getLocalBounds().toFloat();
      paint::fill(g, box, box.getHeight() / 2, theme::kLinkBlue.withAlpha(over ? 0.35f : 0.22f));
      paint::text(g, "Add here", getLocalBounds(), Fonts::sans(11, true), theme::kWhite, juce::Justification::centred);
    }
  };
  static constexpr int kAddHereWidth = 64;

  LibraryDrawer& drawer_;
  const LibraryNode& node_;
  int depth_;
  bool open_;
  bool dropTarget_ = false;
  bool dragging_ = false;
  std::unique_ptr<AddHere> addHere_;
};

// Name prompt / delete confirmation (the preset bar's save popover, reused
// in shape): a title, a field (absent when confirming) and one pill button.
class LibraryDrawer::Prompt : public Popover {
public:
  static constexpr int kWidth = 256;
  static constexpr int kPad = 16;
  static constexpr int kTitleHeight = 16;
  static constexpr int kFieldHeight = 35;
  static constexpr int kButtonHeight = 35;

  Prompt() {
    field_.onChange = [this](const juce::String&) { repaint(); };
    field_.onEnter = [this] { confirm(); };
    addAndMakeVisible(field_);
    button_.onClick = [this] { confirm(); };
    button_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible(button_);
  }

  // `prefill` void = a confirmation (no field); `danger` paints the button red.
  void show(juce::Component& anchor, const juce::String& title, std::optional<juce::String> prefill,
            const juce::String& action, bool danger, std::function<void(const juce::String&)> done) {
    title_ = title;
    action_ = action;
    danger_ = danger;
    done_ = std::move(done);
    asks_ = prefill.has_value();
    field_.setVisible(asks_);
    field_.setPlaceholder("Name");
    field_.setText(prefill.value_or(juce::String()));
    button_.setButtonText(action);
    const int fieldBlock = asks_ ? kFieldHeight + 12 : 0;
    setSize(kWidth, kBorder * 2 + kPad + kTitleHeight + 12 + fieldBlock + kButtonHeight + kPad);
    open(anchor, Align::left, 4, 8);
    if (asks_) field_.focus();
  }

  void paint(juce::Graphics& g) override {
    const auto box = getLocalBounds().toFloat();
    paint::fill(g, box, theme::kPanelCorner, theme::kPanelBg);
    paint::border(g, box, theme::kPanelCorner, theme::kBorder);
    paint::text(g, title_, contentBounds().reduced(kPad).removeFromTop(kTitleHeight), Fonts::sans(14, true),
                theme::kWhite);
    const bool enabled = !asks_ || field_.text().trim().isNotEmpty();
    const auto b = button_.getBounds().toFloat();
    if (danger_) paint::fill(g, b, b.getHeight() / 2, theme::kBrandRed.withAlpha(enabled ? 0.85f : 0.4f));
    else paint::border(g, b, b.getHeight() / 2, juce::Colour(235, 235, 245).withAlpha(0.6f));
    paint::text(g, action_, button_.getBounds(), Fonts::sans(13), enabled ? theme::kWhite : theme::kGray,
                juce::Justification::centred);
  }

  void resized() override {
    auto area = contentBounds().reduced(kPad);
    area.removeFromTop(kTitleHeight + 12);
    if (asks_) {
      field_.setBounds(area.removeFromTop(kFieldHeight));
      area.removeFromTop(12);
    }
    button_.setBounds(area.removeFromTop(kButtonHeight));
  }

private:
  // Invisible hit target; the panel paints the pill.
  class Hit : public Clickable {
  public:
    Hit() : Clickable("OK") {}
    void paintButton(juce::Graphics&, bool, bool) override {}
  };

  void confirm() {
    const auto value = field_.text().trim();
    if (asks_ && value.isEmpty()) return;
    auto done = done_;
    close();
    if (done) done(value);
  }

  TextField field_;
  Hit button_;
  juce::String title_, action_;
  bool asks_ = true, danger_ = false;
  std::function<void(const juce::String&)> done_;
};

// The "Add to Library" strip: which tone is waiting for a folder, and Cancel.
// Also the keep folder (Stop), and links to files that moved (Find / Hide).
class LibraryDrawer::Strip : public juce::Component {
public:
  explicit Strip(LibraryDrawer& drawer) : drawer_(drawer) {
    cancel_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible(cancel_);
    other_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    addChildComponent(other_);
  }
  // A tile's tone waiting for a folder (Cancel), or the keep folder (Stop).
  void showPick(const juce::String& title, LibraryStore::AddKind kind) {
    using Kind = LibraryStore::AddKind;
    const auto quoted = juce::String::fromUTF8("\xe2\x80\x9c") + title + juce::String::fromUTF8("\xe2\x80\x9d");
    text_ = kind == Kind::keep       ? "Keep " + quoted + " in..."
            : kind == Kind::keepLink ? "Keep a reference to " + quoted + " in..."
            : kind == Kind::download ? "Download " + quoted + " into..."
                                     : "Pick a folder for " + quoted;
    setHelpText(help::text(help::Key::libraryPickFolder));
    cancel_.setButtonText("Cancel");
    cancel_.setHelpText(help::text(help::Key::libraryCancelAdd));
    cancel_.onClick = [this] { drawer_.services_.library.cancelAdd(); };
    repaint();
  }
  // Links to files moved or renamed outside the plugin: Show (which, and
  // finding them) or Hide.
  void showMissing(int count) {
    text_ = count == 1 ? juce::String("1 file missing") : juce::String(count) + " files missing";
    setHelpText(help::text(help::Key::libraryMissing));
    other_.setButtonText("Show");
    other_.setHelpText(help::text(help::Key::libraryShowMissing));
    other_.onClick = [this] { drawer_.showMissingList(other_); };
    other_.setVisible(true);
    cancel_.setButtonText("Hide");
    cancel_.setHelpText(help::text(help::Key::libraryHideMissing));
    cancel_.onClick = [this] { drawer_.services_.library.hideMissingNotice(); };
    resized();
    repaint();
  }
  // An Export for Sharing asking TONE3000 about folders: how far, and Cancel.
  void showSharing(const juce::String& status) {
    text_ = status;
    setHelpText(help::text(help::Key::libraryShare));
    other_.setVisible(false);
    cancel_.setButtonText("Cancel");
    cancel_.setHelpText(help::text(help::Key::libraryShare));
    cancel_.onClick = [this] { drawer_.services_.library.cancelShare(); };
    repaint();
  }
  void showKeeping(const juce::String& folder) {
    text_ = juce::String::fromUTF8("Keeping in \xe2\x80\x9c") + folder + juce::String::fromUTF8("\xe2\x80\x9d");
    setHelpText(help::text(help::Key::libraryKeeping));
    cancel_.setButtonText("Stop");
    cancel_.setHelpText(help::text(help::Key::libraryStopKeeping));
    cancel_.onClick = [this] { drawer_.services_.library.stopKeeping(); };
    repaint();
  }
  void paint(juce::Graphics& g) override {
    const auto box = getLocalBounds().toFloat().reduced(kPadX, 4);
    paint::fill(g, box, 8.0f, theme::kLinkBlue.withAlpha(0.16f));
    auto text = getLocalBounds().reduced(kPadX + 10, 0).withTrimmedRight(
        cancel_.getWidth() + (other_.isVisible() ? other_.getWidth() : 0));
    paint::text(g, text_, text, Fonts::sans(12), theme::kWhite);
  }
  void resized() override {
    cancel_.setBounds(getWidth() - kPadX - 64, 4, 60, getHeight() - 8);
    other_.setBounds(cancel_.getX() - 60, 4, 60, getHeight() - 8);
  }

private:
  class CancelLink : public Clickable {
  public:
    CancelLink() : Clickable("Cancel") {}
    void paintButton(juce::Graphics& g, bool over, bool) override {
      paint::text(g, getButtonText(), getLocalBounds(), Fonts::sans(12, true), over ? theme::kWhite : theme::kLinkBlue,
                  juce::Justification::centred);
    }
  };
  LibraryDrawer& drawer_;
  CancelLink cancel_, other_;
  juce::String text_;
};

// Drawer

LibraryDrawer::LibraryDrawer(Services& services)
    : services_(services), strip_(std::make_unique<Strip>(*this)), missingStrip_(std::make_unique<Strip>(*this)) {
  setWantsKeyboardFocus(true);  // given by a row's click (Row::mouseDown), for the arrow keys
  services_.library.onShareStatus = [this] {
    updateStrips();
    resized();
  };
  // Which of two pedals (amps, cabs...) a double-click replaces: asked
  // where the mouse is.
  services_.library.chooseBlock = [this](const std::vector<std::string>& blocks,
                                         std::function<void(const std::string&)> pick) {
    const auto& state = services_.chain.state();
    std::vector<ContextMenu::Item> items;
    for (const auto& id : blocks) {
      const auto* block = state.findBlock(id);
      if (block == nullptr) continue;
      // Where it sits: its slot, and its lane when there are two.
      juce::String where;
      const auto slotIn = [&id](const std::vector<ChainItem>& lane) {
        for (size_t i = 0; i < lane.size(); ++i)
          if (lane[i].blockId == id) return static_cast<int>(i) + 1;
        return 0;
      };
      if (const int left = slotIn(state.chain); left > 0) where = "slot " + juce::String(left);
      else if (state.chainRight) where = "right, slot " + juce::String(slotIn(*state.chainRight));
      if (state.chainRight && where.startsWith("slot")) where = "left, " + where;
      ContextMenu::Item item{"Replace " + block->tone.title + " (" + where + ")", Icon::ArrowLeftRight,
                             help::Key::libraryChooseBlock, [pick, id] { pick(id); }};
      items.push_back(std::move(item));
    }
    items.push_back({"Add as New Block", Icon::Plus, help::Key::libraryChooseBlock, [pick] { pick({}); }});
    openMenu(std::move(items), *this, getMouseXYRelative());
  };
  setOpaque(true);
  setTitle("Library");

  newFolder_.setHelpText(help::text(help::Key::libraryNewFolder));
  newFolder_.onClick = [this] {
    const auto parent = targetFolder();
    promptName("New Folder", {}, "Create", [this, parent](const juce::String& name) {
      services_.library.createFolder(parent, name);
    });
  };
  more_.setHelpText(help::text(help::Key::libraryMenu));
  more_.onClick = [this] {
    auto& library = services_.library;
    std::vector<ContextMenu::Item> items;
    if (LibraryStore::canReveal()) {
      items.push_back({"Link Folder...", Icon::Link, help::Key::libraryLink, [&library] { library.linkFolder(); }});
      items.push_back({"Import Folder...", Icon::FolderPlus, help::Key::libraryImportFolder,
                       [&library] { library.importFolderDialog(); }});
      items.push_back({"Import File...", Icon::Download, help::Key::libraryImport, [&library] { library.importArchive(); }});
      items.push_back({"Set Folder...", Icon::FolderOpen, help::Key::libraryChooseRoot,
                       [&library] { library.chooseRoot(); }});
      items.push_back({"Reveal", Icon::ExternalLink, help::Key::libraryReveal,
                       [&library] { library.reveal(library.root().getFullPathName()); }});
    }
    items.push_back({"Refresh", Icon::RefreshCw, help::Key::libraryRefresh, [&library] { library.reload(); }});
    if (!library.missing().empty())
      items.push_back({"Missing Files...", Icon::Search, help::Key::libraryShowMissing,
                       [this] {
                         juce::MessageManager::callAsync([self = juce::Component::SafePointer<LibraryDrawer>(this)] {
                           if (self != nullptr) self->showMissingList(self->more_);
                         });
                       }});
    openMenu(std::move(items), more_, {0, more_.getHeight()});
  };
  close_.setHelpText(help::text(help::Key::libraryClose));
  close_.onClick = [this] {
    if (onClose) onClose();
  };
  addAndMakeVisible(newFolder_);
  addAndMakeVisible(more_);
  addAndMakeVisible(close_);

  search_.setPlaceholder("Search library");
  search_.setFontSize(13);
  search_.setPadding(7, 32, 30);
  search_.setLeadingIcon(Icon::Search, 14, 11, theme::kGray);
  search_.setClearButton(16, 8);
  search_.setHelpText(help::text(help::Key::librarySearch));
  search_.setText(services_.library.filter);
  gears_ = std::make_unique<GearRow>(*this);
  addAndMakeVisible(*gears_);
  // A search walks the whole tree (a linked collection: tens of thousands
  // of rows): once typing pauses, not per keystroke.
  search_.onChange = [this](const juce::String&) { searchWait_.start(120, [this] { applySearch(); }); };
  search_.onEscape = [this] { search_.setText({}, true); };
  // Down: from the search into the results, on the first capture (or tone,
  // preset) they show; the arrow keys go on from there.
  search_.onDown = [this] {
    // The text typed, searched now (its change may not even have come yet).
    auto& library = services_.library;
    if (library.filter != search_.text()) {
      searchWait_.cancel();
      applySearch();
    }
    if (library.filter.trim().isEmpty()) return false;
    for (const auto& row : model_)
      if (!row.node->isContainer()) {
        const auto path = row.node->path;  // by value: selecting may rebuild the rows
        library.select(path);
        scrollToRow(path);
        for (auto& [i, shown] : rows_) shown->repaint();
        grabKeyboardFocus();
        return true;
      }
    return false;
  };
  // Enter: the first match that loads, as a double-click would. A capture
  // or tone of yours first, then any other, then a captures folder whose own
  // name matches (not one shown only because something inside it does);
  // a preset (the whole rig) only when nothing else matches.
  search_.onEnter = [this] {
    if (searchWait_.pending()) {
      searchWait_.cancel();
      applySearch();
    }
    auto& library = services_.library;
    if (library.filter.isEmpty()) return;
    const auto& filter = library.filter;
    const auto yours = [&library](const LibraryNode& node) {
      const LibraryNode* top = &node;
      while (const auto* up = library.tree().parentOf(top->path)) top = up;
      return top->kind == LibraryNode::Kind::library && top->mine && !top->site;
    };
    const LibraryNode *mine = nullptr, *other = nullptr, *folder = nullptr, *preset = nullptr;
    for (const auto& row : model_) {
      const auto& node = *row.node;
      if (node.missing) continue;
      if (node.kind == LibraryNode::Kind::preset) {
        if (preset == nullptr) preset = &node;
      } else if (!node.isContainer()) {
        if (mine == nullptr && yours(node)) mine = &node;
        if (other == nullptr) other = &node;
      } else if (folder == nullptr && node.loadsAsBlock() && libraryNameMatches(node.name, filter)) {
        folder = &node;
      }
    }
    for (const auto* pick : {mine, other, folder, preset})
      if (pick != nullptr) return library.use(*pick);
  };
  addAndMakeVisible(search_);

  addChildComponent(*strip_);
  addChildComponent(*missingStrip_);
  scroller_.setViewedComponent(&content_, false);
  // A visible bar, unlike the plugin's other scroll areas: a linked
  // collection runs to thousands of rows, too many to wheel through.
  scroller_.setScrollBarsShown(true, false, true, false);
  scroller_.setScrollBarThickness(kScrollBarWidth);
  auto& bar = scroller_.getVerticalScrollBar();
  bar.setAutoHide(true);
  bar.setColour(juce::ScrollBar::thumbColourId, juce::Colours::white.withAlpha(0.28f));
  bar.setColour(juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
  bar.setColour(juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
  scroller_.onScroll = [this] {
    updateVisibleRows();
    if (!restoringScroll_) services_.library.setScroll(scroller_.getViewPositionY());
  };
  addAndMakeVisible(scroller_);

  services_.library.addListener(this);  // scans
  services_.chain.addListener(this);
  chainChanged(services_.chain.state());
  libraryChanged();
}

LibraryDrawer::~LibraryDrawer() {
  services_.library.onShareStatus = nullptr;
  services_.library.chooseBlock = nullptr;
  if (menu_ != nullptr) menu_->close();
  if (prompt_ != nullptr) prompt_->close();
  services_.chain.removeListener(this);
  services_.library.removeListener(this);
}

void LibraryDrawer::updateStrips() {
  auto& library = services_.library;
  const auto& pending = library.pendingAdd();
  const auto* block = pending ? services_.chain.state().findBlock(*pending) : nullptr;
  const auto keeping = library.keepTarget();
  const auto sharing = library.shareStatus();
  strip_->setVisible(block != nullptr || sharing.isNotEmpty() || keeping.isNotEmpty());
  if (block != nullptr) strip_->showPick(block->tone.title, library.pendingKind());
  else if (sharing.isNotEmpty()) strip_->showSharing(sharing);
  else if (keeping.isNotEmpty()) strip_->showKeeping(library.keepTargetName());
  missingStrip_->setVisible(library.missingNoticeShown());
  if (missingStrip_->isVisible()) missingStrip_->showMissing(library.missingNoticeCount());
}

void LibraryDrawer::libraryChanged() {
  auto& library = services_.library;
  const auto focus = library.takeFocus();
  if (focus.isNotEmpty()) search_.setText(library.filter, false);  // focus may have cleared it
  updateStrips();
  rebuild();
  resized();
  if (focus.isNotEmpty()) scrollToRow(focus);
  // Back where this instance left it, once the tree is there.
  if (scrollPending_ && !library.loading() && !model_.empty() && focus.isEmpty()) {
    scrollPending_ = false;
    const juce::ScopedValueSetter<bool> restoring(restoringScroll_, true);
    scroller_.setViewPosition(0, library.savedScroll());
    updateVisibleRows();
  }
}

void LibraryDrawer::scrollToRow(const juce::String& path) {
  for (size_t i = 0; i < model_.size(); ++i)
    if (model_[i].node->path == path) {
      // A row in view stays put; one just past an edge scrolls only as far as
      // that edge (stepping captures moves the selection, not the list); one
      // further away is centred.
      const int y = kRowTop + static_cast<int>(i) * kRowHeight;
      const int top = scroller_.getViewPositionY(), height = scroller_.getHeight();
      if (y >= top && y + kRowHeight <= top + height) return;
      const bool near = y + kRowHeight > top - height / 2 && y < top + height + height / 2;
      const int to = !near ? y - (height - kRowHeight) / 2 : y < top ? y : y + kRowHeight - height;
      scroller_.setViewPosition(0, juce::jmax(0, to));
      updateVisibleRows();
      return;
    }
}

void LibraryDrawer::chainChanged(const ChainState& state) {
  auto& library = services_.library;
  const auto id = state.preset ? state.preset->id : juce::String();
  std::set<juce::String> files;
  std::set<int> tones;
  for (const auto* block : state.toneBlocks()) {
    if (!block->tone.local) tones.insert(block->tone.id);
    else if (const auto file = library.playingFile(block->blockId); file.isNotEmpty()) files.insert(file);
  }
  if (id != activePresetId_ || files != playingFiles_ || tones != playingTones_) {
    activePresetId_ = id;
    playingFiles_ = std::move(files);
    playingTones_ = std::move(tones);
    for (auto& [index, row] : rows_) row->repaint();
  }
  // The audition block stepped to another capture: the selection follows.
  const auto auditioning = library.playingFile(library.auditionBlockId());
  if (auditioning != followed_) {
    followed_ = auditioning;
    if (auditioning.isNotEmpty()) library.follow(auditioning);
  }
}

bool LibraryDrawer::playing(const LibraryNode& node) const {
  if (node.kind == LibraryNode::Kind::capture) return playingFiles_.count(node.path) != 0;
  if (node.kind == LibraryNode::Kind::tone) return node.tone.toneId > 0 && playingTones_.count(node.tone.toneId) != 0;
  return false;
}

void LibraryDrawer::applySearch() {
  auto& library = services_.library;
  if (library.filter == search_.text()) return;
  library.filter = search_.text();
  library.saveView();
  library.closedInFilter.clear();  // a new search starts afresh
  library.openedInFilter.clear();
  rebuild();
}

void LibraryDrawer::rebuild() {
  // Rows hold references into the store's tree: rebuilt whenever it is.
  rows_.clear();
  auto& library = services_.library;
  model_ = libraryRows(library.tree(), library.openPaths(), library.filter, library.closedInFilter, library.gearFilter,
                       library.openedInFilter);
  layoutRows();
  repaint();
}

void LibraryDrawer::layoutRows() {
  const int width = scroller_.getMaximumVisibleWidth();  // beside the scroll bar when it shows
  const int height = kRowTop + static_cast<int>(model_.size()) * kRowHeight + 8;
  content_.setSize(width, juce::jmax(height, scroller_.getHeight()));
  for (auto& [index, row] : rows_) row->setBounds(0, kRowTop + index * kRowHeight, width, kRowHeight);
  updateVisibleRows();
}

void LibraryDrawer::updateVisibleRows() {
  constexpr int kOverscan = 8;  // rows past each edge, so a scroll step has them ready
  const int count = static_cast<int>(model_.size());
  const int top = scroller_.getViewPositionY();
  const int first = juce::jmax(0, (top - kRowTop) / kRowHeight - kOverscan);
  const int last = juce::jmin(count, (top + scroller_.getHeight() - kRowTop) / kRowHeight + 1 + kOverscan);
  // Out of the window: gone, unless a press is still on it (a drag's source).
  for (auto it = rows_.begin(); it != rows_.end();) {
    const bool outside = it->first < first || it->first >= last;
    it = outside && !it->second->isMouseButtonDown() ? rows_.erase(it) : std::next(it);
  }
  const int width = scroller_.getMaximumVisibleWidth();
  for (int index = first; index < last; ++index) {
    if (rows_.count(index) != 0) continue;
    auto row = std::make_unique<Row>(*this, model_[static_cast<size_t>(index)]);
    row->setDropTarget(row->node().path == dropHighlight_);
    row->setBounds(0, kRowTop + index * kRowHeight, width, kRowHeight);
    content_.addAndMakeVisible(*row);
    rows_.emplace(index, std::move(row));
  }
}

// Row gestures

void LibraryDrawer::rowClicked(const LibraryNode& node) {
  // A click on a folder always opens or closes it, whatever else is going on
  // (a pending add has its own Add here buttons; a search its own closed set).
  if (!node.isContainer()) return;
  auto& library = services_.library;
  if (library.filter.trim().isNotEmpty()) {
    // Open or closed as shown now (a matching folder past the first starts
    // closed): flipped.
    bool shownOpen = false;
    for (const auto& row : model_)
      if (row.node->path == node.path) shownOpen = row.open;
    if (shownOpen) {
      library.openedInFilter.erase(node.path);
      library.closedInFilter.insert(node.path);
    } else {
      library.closedInFilter.erase(node.path);
      library.openedInFilter.insert(node.path);
    }
    rebuild();
    return;
  }
  library.setOpen(node.path, !library.isOpen(node.path));
}

bool LibraryDrawer::keyPressed(const juce::KeyPress& key) {
  auto& library = services_.library;
  // "s": back to the search (its text selected, so typing replaces it).
  if (const auto c = key.getTextCharacter(); (c == 's' || c == 'S') && !key.getModifiers().isCommandDown() &&
                                               !key.getModifiers().isAltDown()) {
    search_.focus();
    return true;
  }
  if (model_.empty()) return false;
  int at = -1;
  for (size_t i = 0; i < model_.size(); ++i)
    if (model_[i].node->path == library.selected()) at = static_cast<int>(i);
  const auto selectRow = [this, &library](int index) {
    index = juce::jlimit(0, static_cast<int>(model_.size()) - 1, index);
    const auto path = model_[static_cast<size_t>(index)].node->path;  // by value: a rebuild replaces the rows
    library.select(path);
    scrollToRow(path);
    for (auto& [i, row] : rows_) row->repaint();
  };
  if (const auto c = key.getTextCharacter(); c >= '0' && c <= '9' && !key.getModifiers().isCommandDown() &&
                                               !key.getModifiers().isAltDown()) {
    typeNumber(static_cast<int>(c - '0'));
    return true;
  }
  // "a": A/B on the block the Library loads into (back to what it played
  // before, and again forth).
  if (const auto c = key.getTextCharacter(); (c == 'a' || c == 'A') && !key.getModifiers().isCommandDown() &&
                                               !key.getModifiers().isAltDown()) {
    if (const auto block = services_.library.abBlock(); !block.empty() && services_.library.abSwitch(block))
      keepKeyboard();
    return true;
  }
  const bool down = key.isKeyCode(juce::KeyPress::downKey), up = key.isKeyCode(juce::KeyPress::upKey);
  if (down || up) {
    selectRow(at < 0 ? 0 : at + (down ? 1 : -1));
    return true;
  }
  if (at < 0) return false;
  const auto& row = model_[static_cast<size_t>(at)];
  const auto node = *row.node;  // a copy: opening or loading may rebuild the rows
  if (key.isKeyCode(juce::KeyPress::returnKey)) {
    // A folder with captures (tones, presets) of its own: opened, and its
    // first one loaded, selected for the arrows to go on from. One holding
    // only folders opens or closes.
    const LibraryNode* first = nullptr;
    if (node.kind == LibraryNode::Kind::folder && !library.pendingAdd())
      for (const auto& child : node.children)
        if (!child.isContainer() && !child.missing) {
          first = &child;
          break;
        }
    if (first == nullptr) {
      if (node.isContainer()) rowClicked(node);
      else rowDoubleClicked(node);
    } else {
      const auto path = first->path;  // by value: opening rebuilds the rows
      if (!row.open) rowClicked(node);
      library.select(path);
      scrollToRow(path);
      for (auto& [i, shown] : rows_) shown->repaint();
      if (const auto* item = library.tree().find(path)) library.use(*item);
    }
    keepKeyboard();
    return true;
  }
  if (key.isKeyCode(juce::KeyPress::rightKey)) {
    if (!node.isContainer()) return true;
    if (!row.open) rowClicked(node);
    else if (at + 1 < static_cast<int>(model_.size()) && model_[static_cast<size_t>(at) + 1].depth > row.depth) selectRow(at + 1);
    return true;
  }
  if (key.isKeyCode(juce::KeyPress::leftKey)) {
    if (node.isContainer() && row.open) {
      rowClicked(node);
    } else {
      for (int i = at - 1; i >= 0; --i)
        if (model_[static_cast<size_t>(i)].depth < row.depth) {
          selectRow(i);
          break;
        }
    }
    return true;
  }
  return false;
}

void LibraryDrawer::keepKeyboard() {
  for (const int ms : {150, 600})
    juce::Timer::callAfterDelay(ms, [safe = juce::Component::SafePointer<LibraryDrawer>(this)] {
      if (safe != nullptr && safe->isShowing() && !safe->hasKeyboardFocus(true)) safe->grabKeyboardFocus();
    });
}

void LibraryDrawer::typeNumber(int digit) {
  auto& library = services_.library;
  // Close together: one number ("2", "5": 25). After a pause, a new one.
  const auto now = juce::Time::getMillisecondCounter();
  if (now - lastDigitMs_ > kNumberPauseMs) typed_ = 0;
  lastDigitMs_ = now;
  typed_ = typed_ * 10 + digit;
  if (typed_ > 9999) typed_ = digit;
  if (typed_ == 0) return;
  // The folder: the selected row's own when it is an open folder, else the
  // one it is in.
  const auto& tree = library.tree();
  const auto* selected = tree.find(library.selected());
  if (selected == nullptr) return;
  const auto* folder = selected->isContainer() && library.isOpen(selected->path) ? selected : tree.parentOf(selected->path);
  if (folder == nullptr) return;
  std::vector<const LibraryNode*> items;
  for (const auto& child : folder->children)
    if ((child.kind == LibraryNode::Kind::capture || child.kind == LibraryNode::Kind::tone) && !child.missing)
      items.push_back(&child);
  if (items.empty()) return;
  const int count = static_cast<int>(items.size());
  const int index = juce::jmin(typed_, count) - 1;
  const auto path = items[static_cast<size_t>(index)]->path;
  library.select(path);
  scrollToRow(path);
  for (auto& [i, row] : rows_) row->repaint();
  services_.toast.show(juce::String(index + 1) + " / " + juce::String(count), Toast::Style::quiet);
  numberLoad_.cancel();
  // No digit could make it a place in the folder: the number is whole, the
  // next digit starts another.
  const bool whole = typed_ * 10 > count;
  if (whole) typed_ = 0;
  if (!services_.prefs.getBool(UiPrefs::kLibraryNumberLoads, true)) return;
  // Loaded once the number is whole: at once when no digit could make it a
  // place in the folder, else after the pause ("2" of a 40-capture folder
  // may become 25).
  const auto load = [this, path] {
    if (const auto* node = services_.library.tree().find(path)) services_.library.use(*node);
    keepKeyboard();
  };
  if (whole) load();
  else numberLoad_.start(static_cast<int>(kNumberPauseMs), load);
}

void LibraryDrawer::rowDoubleClicked(const LibraryNode& node) {
  if (!node.isContainer() && !services_.library.pendingAdd()) services_.library.use(node);
}

void LibraryDrawer::rowMenu(Row& row, juce::Point<int> at) {
  openMenu(menuFor(row.node()), row, at);
}

void LibraryDrawer::showMissingList(juce::Component& at) {
  auto& library = services_.library;
  using Kind = LibraryStore::Missing::Kind;
  std::vector<ContextMenu::Item> items;
  constexpr size_t kMaxRows = 12;
  const auto& missing = library.missing();
  for (size_t i = 0; i < missing.size() && i < kMaxRows; ++i) {
    const auto& entry = missing[i];
    const juce::File file(entry.path);
    const auto otherName = juce::File::isAbsolutePath(entry.other) ? juce::File(entry.other).getFileName() : entry.other;
    juce::String label, role;
    Icon icon = Icon::File;
    switch (entry.kind) {
      case Kind::copy:
        label = file.getFileName() + (entry.site ? " (from TONE3000)" : " (kept copy)");
        role = entry.site ? "From " + otherName + " on TONE3000: click to download it again."
                          : "Kept from " + otherName + ". It was in " + file.getParentDirectory().getFileName() + ".";
        icon = entry.site ? Icon::Download : Icon::Copy;
        break;
      case Kind::original:
        label = file.getFileName() + " (original)";
        role = "The original of your copy " + otherName + ". It was in " + file.getParentDirectory().getFileName() + ".";
        icon = Icon::Link;
        break;
      case Kind::picture:
        label = file.getFileName() + " (folder picture)";
        role = "A folder with a picture. It was in " + file.getParentDirectory().getFileName() + ".";
        icon = Icon::Image;
        break;
      case Kind::keepFolder:
        label = file.getFileName() + " (keep folder)";
        role = "Your keep folder. It was in " + file.getParentDirectory().getFileName() + ".";
        icon = Icon::FolderOpen;
        break;
    }
    const auto path = entry.path;
    if (entry.site) {
      // Nothing to find: TONE3000 has it.
      ContextMenu::Item item{label, icon, help::Key::libraryDownloadMissing,
                             [&library, path] { library.downloadMissing({path}); }};
      item.hint = role;
      items.push_back(std::move(item));
      continue;
    }
    ContextMenu::Item item{label, icon, help::Key::libraryFindMissing, [&library, path] { library.findMissingDialog(path); }};
    item.hint = role + " Click to find it.";
    item.disabled = !LibraryStore::canReveal();
    items.push_back(std::move(item));
  }
  if (missing.size() > kMaxRows) {
    ContextMenu::Item more{"and " + juce::String(static_cast<int>(missing.size() - kMaxRows)) + " more", Icon::Ellipsis,
                           help::Key::libraryMissing, [] {}};
    more.disabled = true;
    items.push_back(std::move(more));
  }
  const auto sites = std::count_if(missing.begin(), missing.end(), [](const auto& m) { return m.site; });
  if (sites > 1)
    items.push_back({"Download All Again", Icon::Download, help::Key::libraryDownloadMissing,
                     [&library] { library.downloadMissing(); }});
  if (LibraryStore::canReveal() && static_cast<size_t>(sites) < missing.size())
    items.push_back({"Find All...", Icon::Search, help::Key::libraryFindMissing, [&library] { library.findMissingDialog(); }});
  const auto count = static_cast<int>(missing.size());
  items.push_back({count == 1 ? juce::String("Forget This Missing File") : "Forget " + juce::String(count) + " Missing Files",
                   Icon::Trash2, help::Key::libraryForgetMissing, [this, count] {
                     if (prompt_ == nullptr) prompt_ = std::make_unique<Prompt>();
                     prompt_->show(search_,
                                   count == 1 ? juce::String("Forget the missing file?")
                                              : "Forget the " + juce::String(count) + " missing files?",
                                   std::nullopt, "Forget", true,
                                   [this](const juce::String&) { services_.library.forgetMissing(); });
                   }});
  openMenu(std::move(items), at, {0, at.getHeight()});
}

void LibraryDrawer::openMenu(std::vector<ContextMenu::Item> items, juce::Component& at, juce::Point<int> point) {
  if (auto* old = menu_.release()) {
    old->close();
    juce::MessageManager::callAsync([old] { delete old; });
  }
  menu_ = std::make_unique<ContextMenu>(std::move(items));
  menu_->openAtPoint(at, point);
}

std::vector<ContextMenu::Item> LibraryDrawer::menuFor(const LibraryNode& node) {
  auto& library = services_.library;
  // Rows rebuild under the menu: everything below works from copies.
  const auto path = node.path;
  const auto name = node.name;
  const bool yourLibrary = node.kind == LibraryNode::Kind::library && node.mine && !node.site;
  const bool single = node.kind == LibraryNode::Kind::tone || node.kind == LibraryNode::Kind::capture;
  std::vector<ContextMenu::Item> items;

  if (node.kind == LibraryNode::Kind::library) {
    // Your order of libraries (kept): also by dragging one onto another.
    items.push_back({"Move Up", Icon::ArrowUp, help::Key::libraryMoveUp, [&library, path] { library.moveLibrary(path, -1); },
                     !library.canMoveLibrary(path, -1)});
    items.push_back({"Move Down", Icon::ArrowDown, help::Key::libraryMoveDown,
                     [&library, path] { library.moveLibrary(path, 1); }, !library.canMoveLibrary(path, 1)});
    if (node.site) return items;
  }
  if (yourLibrary)
    if (const auto* captures = library.capturesRoot()) {
      const auto into = captures->path;
      items.push_back({"New Folder", Icon::FolderPlus, help::Key::libraryNewFolder, [this, into] {
                         promptName("New Folder", {}, "Create",
                                    [this, into](const juce::String& n) { services_.library.createFolder(into, n); });
                       }});
    }


  if (node.missing) {
    // Nothing to do with a folder that isn't there but forget it.
    items.push_back({"Unlink", Icon::X, help::Key::libraryUnlink, [&library, path] { library.unlink(path); }});
    return items;
  }
  if (!node.isContainer()) {
    const auto copy = node;
    items.push_back({"Load", Icon::ArrowRight, single ? help::Key::libraryAudition : help::Key::libraryUse,
                     [&library, copy] { library.use(copy); }});
    if (single)
      items.push_back({"Add as Block", Icon::Plus, help::Key::libraryAddNew,
                       [&library, copy] { library.addAsNewBlock(copy); }});
  }
  if (node.kind == LibraryNode::Kind::capture) {
    // Kept links: from a kept copy to its original, from an original to
    // its copies (each shown in its folder).
    if (library.siteOriginalOf(path))
      items.push_back({"Load Source", Icon::ArrowRight, help::Key::libraryLoadOriginal,
                       [&library, path] { library.useSiteOriginal(path); }});
    if (const auto original = library.originalOf(path); original != juce::File())
      items.push_back({"Go to Source", Icon::FolderOpen, help::Key::libraryGoOriginal,
                       [&library, to = original.getFullPathName()] { library.focus(to); }, false,
                       "Go to Source: the original, in " + original.getParentDirectory().getFileName()});
    for (const auto& copy : library.copiesOf(path))
      items.push_back({"Go to Kept in " + copy.getParentDirectory().getFileName(), Icon::FolderOpen,
                       help::Key::libraryGoKept, [&library, to = copy.getFullPathName()] { library.focus(to); }, false,
                       "Go to Kept: your copy, in " + copy.getParentDirectory().getFileName()});
  }
  // A capture of yours into a folder of its own (New Folder, Rename and a
  // move in one).
  if ((node.kind == LibraryNode::Kind::capture || node.kind == LibraryNode::Kind::tone) && node.editable &&
      node.onDisk())
    items.push_back({"Put in Own Folder", Icon::FolderPlus, help::Key::libraryOwnFolder,
                     [&library, path] { library.putInOwnFolder(path); }});
  if (node.kind == LibraryNode::Kind::tone && node.tone.toneId > 0) {
    auto url = node.tone.raw["tone"]["url"].toString();
    if (url.isEmpty()) url = juce::String(config::kApiOrigin) + "/tones/" + juce::String(node.tone.toneId);
    items.push_back({"Open on TONE3000", Icon::ExternalLink, help::Key::libraryOpenSite,
                     [url] { juce::URL(url).launchInDefaultBrowser(); }});
  }
  if (node.loadsAsBlock()) {
    const auto copy = node;
    items.push_back({"Load as Block", Icon::ArrowRight, help::Key::libraryLoadFolder,
                     [&library, copy] { library.use(copy); }});
  }
  // A picture for a captures folder: what its blocks show.
  if (node.kind == LibraryNode::Kind::folder && node.isCaptures() && node.onDisk() && LibraryStore::canReveal()) {
    const bool has = library.pictureFor(path) != juce::File();
    items.push_back({has ? "Change Picture..." : "Set Picture...", Icon::Image, help::Key::librarySetPicture,
                     [&library, path] { library.choosePicture(path); }});
    if (has)
      items.push_back({"Remove Picture", Icon::X, help::Key::libraryRemovePicture,
                       [&library, path] { library.removePicture(path); }});
  }
  if (node.isContainer() && node.writable && node.isCaptures() && !node.favorites) {
    if (library.keepTarget() == path)
      items.push_back({"Stop Keeping Here", Icon::X, help::Key::libraryStopKeeping,
                       [&library] { library.stopKeeping(); }});
    else
      items.push_back({"Keep Here", Icon::Download, help::Key::libraryKeepHere,
                       [&library, path] { library.setKeepTarget(path); }});
  }
  if (node.isContainer() && node.writable && !node.favorites) {
    items.push_back({"New Folder", Icon::FolderPlus, help::Key::libraryNewFolder, [this, path] {
                       promptName("New Folder", {}, "Create",
                                  [this, path](const juce::String& n) { services_.library.createFolder(path, n); });
                     }});
    // Rigs save only where presets live.
    if (!node.isCaptures())
      items.push_back({"Save Here", Icon::Save, help::Key::librarySaveRig, [this, path, name] {
                         const auto& active = services_.chain.state().preset;
                         promptName("Save to " + name, active ? active->name : juce::String(), "Save",
                                    [this, path](const juce::String& n) { services_.library.saveRig(path, n); });
                       }});
  }
  if (node.isContainer() && node.favorites) {
    items.push_back({"Refresh", Icon::RefreshCw, help::Key::libraryRefresh, [&library] { library.reload(); }});
    return items;
  }
  if (node.editable || yourLibrary) {
    items.push_back({"Rename", Icon::Pencil, help::Key::libraryRename, [this, path, name] {
                       promptName("Rename", name, "Rename",
                                  [this, path](const juce::String& n) { services_.library.rename(path, n); });
                     }});
  }
  // Someone else's (or a favorite): copy into the matching half of yours.
  const auto* half = node.kind == LibraryNode::Kind::preset || node.isPresets() ? library.presetsRoot()
                                                                               : library.capturesRoot();
  if (!node.editable && !node.mount && !yourLibrary && half != nullptr &&
      (node.onDisk() || node.kind == LibraryNode::Kind::tone) && node.kind != LibraryNode::Kind::library) {
    const auto halfPath = half->path;
    items.push_back({"Copy to Mine", Icon::Copy, help::Key::libraryCopyToMine,
                     [&library, path, halfPath] { library.copyToMine(path, halfPath); }});
  }
  if (LibraryStore::canReveal() && node.onDisk()) {
    items.push_back({"Export Backup...", Icon::Share, help::Key::libraryExport, [&library, path] { library.exportItem(path); }});
    // A single capture of your own has nothing to share (no capture file
    // goes out); one kept from TONE3000 goes as its link.
    if (node.kind != LibraryNode::Kind::capture || library.siteOriginalOf(path))
      items.push_back({"Export for Sharing...", Icon::Share, help::Key::libraryShare, [&library, path] { library.shareItem(path); }});
    items.push_back({"Reveal", Icon::ExternalLink, help::Key::libraryReveal,
                     [&library, path] { library.reveal(path); }});
  }
  if ((yourLibrary || node.local) && LibraryStore::canReveal())
    items.push_back({"Link Folder...", Icon::Link, help::Key::libraryLink, [&library] { library.linkFolder(); }});
  // Copy a collection in instead, into this captures folder (or Captures).
  if (LibraryStore::canReveal() && (yourLibrary || (node.isContainer() && node.writable && node.isCaptures())))
    items.push_back({"Import Folder...", Icon::FolderPlus, help::Key::libraryImportFolder,
                     [&library, path, yourLibrary] { library.importFolderDialog(yourLibrary ? juce::String() : path); }});
  if (node.linked)
    items.push_back({"Unlink", Icon::X, help::Key::libraryUnlink, [&library, path] { library.unlink(path); }});
  if (node.favorites) {
    items.push_back({"Unfavorite", Icon::Bookmark, help::Key::libraryUnfavorite,
                     [&library, path] { library.remove(path); }});
  } else if (node.removable) {
    items.push_back({node.kind == LibraryNode::Kind::library ? "Remove" : "Delete", Icon::Trash2,
                     help::Key::libraryDelete, [this, path] {
                       if (const auto* n = services_.library.tree().find(path)) confirmDelete(*n);
                     }});
  }
  return items;
}

juce::String LibraryDrawer::targetFolder() const {
  const auto& tree = services_.library.tree();
  const auto& selected = services_.library.selected();
  if (const auto* node = tree.find(selected)) {
    if (node->isContainer() && node->writable && !node->favorites) return node->path;
    if (const auto* parent = tree.parentOf(selected); parent != nullptr && parent->writable && !parent->favorites)
      return parent->path;
  }
  const auto* captures = services_.library.capturesRoot();
  return captures != nullptr ? captures->path : juce::String();
}

// Prompts

void LibraryDrawer::promptName(const juce::String& title, const juce::String& prefill, const juce::String& action,
                               std::function<void(const juce::String&)> done) {
  if (prompt_ == nullptr) prompt_ = std::make_unique<Prompt>();
  prompt_->show(search_, title, prefill, action, false, std::move(done));
}

void LibraryDrawer::confirmDelete(const LibraryNode& node) {
  if (prompt_ == nullptr) prompt_ = std::make_unique<Prompt>();
  const bool library = node.kind == LibraryNode::Kind::library;
  // A USB or network drive has no Recycle Bin: there the trash is a
  // permanent delete, and the prompt says so.
  const bool permanent = node.onDisk() && !juce::File(node.path).isOnHardDisk();
  const auto title = (library ? "Remove " : "Delete ") + juce::String::fromUTF8("\xe2\x80\x9c") + node.name +
                     juce::String::fromUTF8("\xe2\x80\x9d") + (permanent ? " permanently?" : "?");
  prompt_->show(search_, title, std::nullopt,
                permanent ? "Delete Permanently" : library ? "Remove" : "Move to Trash", true,
                [this, path = node.path](const juce::String&) { services_.library.remove(path); });
}

// OS file drops

LibraryDrawer::Row* LibraryDrawer::rowAt(juce::Point<int> p) {
  if (!scroller_.getBounds().contains(p)) return nullptr;
  const auto inContent = content_.getLocalPoint(this, p);
  if (inContent.y < kRowTop) return nullptr;
  const auto it = rows_.find((inContent.y - kRowTop) / kRowHeight);
  return it != rows_.end() ? it->second.get() : nullptr;
}

void LibraryDrawer::startDragScroll() {
  dragScroll_.tick = [this] { dragScrollTick(); };
  dragScroll_.startTimerHz(60);
}

void LibraryDrawer::dragScrollTick() {
  auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
  if (container == nullptr || !container->isDragAndDropActive()) return dragScroll_.stopTimer();
  // Within the drawer's width, from a band inside the list's edge to past
  // it (a drag that overshoots the bottom keeps going).
  constexpr int kEdge = 36, kMaxStep = 24;
  const auto p = scroller_.getLocalPoint(nullptr, juce::Desktop::getMousePosition());
  if (p.x < 0 || p.x >= getWidth()) return;
  const int h = scroller_.getHeight();
  int step = 0;
  if (p.y < kEdge) step = -juce::jmin(kMaxStep, (kEdge - p.y) / 2 + 1);
  else if (p.y > h - kEdge) step = juce::jmin(kMaxStep, (p.y - (h - kEdge)) / 2 + 1);
  if (step == 0) return;
  const int maxY = juce::jmax(0, content_.getHeight() - scroller_.getViewHeight());
  const int y = juce::jlimit(0, maxY, scroller_.getViewPositionY() + step);
  if (y != scroller_.getViewPositionY()) scroller_.setViewPosition(scroller_.getViewPositionX(), y);
}

void LibraryDrawer::setDropLine(const juce::String& path, bool after) {
  if (dropLine_ == path && dropLineAfter_ == after) return;
  const auto was = dropLine_;
  dropLine_ = path;
  dropLineAfter_ = after;
  for (auto& [index, row] : rows_)
    if (row->node().path == was || row->node().path == path) row->repaint();
}

void LibraryDrawer::dragOver(Row& row, const juce::DragAndDropTarget::SourceDetails& details) {
  const auto dragged = details.description.getProperty(LibraryStore::kDragKey, {}).toString();
  const auto& path = row.node().path;
  const bool beside = services_.library.canPlaceBeside(dragged, path);
  const bool into = row.takesInto(dragged);
  // Top and bottom quarters: beside it (all of it when it takes nothing in).
  // An open folder's bottom edge leads into it (its contents follow), so
  // there it means into.
  const int y = details.localPosition.y, h = row.getHeight();
  const bool top = beside && (y < h / 4 || (!into && y < h / 2));
  const bool bottom = beside && !top && !(row.isOpen() && into) && (y >= h - h / 4 || !into);
  if (top || bottom) {
    setDropHighlight({});
    setDropLine(path, bottom);
  } else {
    setDropLine({}, false);
    setDropHighlight(into ? path : juce::String());
  }
}

void LibraryDrawer::setDropHighlight(const juce::String& path) {
  if (dropHighlight_ == path) return;
  dropHighlight_ = path;
  for (auto& [index, row] : rows_) row->setDropTarget(row->node().path == path);
}

void LibraryDrawer::fileDragMove(const juce::StringArray&, int x, int y) {
  const auto* row = rowAt({x, y});
  const bool takes = row != nullptr && row->node().isContainer() && row->node().writable;
  setDropHighlight(takes ? row->node().path : juce::String());
}

void LibraryDrawer::fileDragExit(const juce::StringArray&) { setDropHighlight({}); }

void LibraryDrawer::filesDropped(const juce::StringArray& files, int x, int y) {
  setDropHighlight({});
  // Onto a folder that takes files, else into your library.
  juce::String folder;
  if (const auto* row = rowAt({x, y});
      row != nullptr && row->node().isContainer() && row->node().writable && !row->node().favorites)
    folder = row->node().path;
  services_.library.addFiles(files, folder);  // no folder: each to its kind's half

  // Dropped folders: ask whether to link them where they are or copy them
  // in (into the captures folder under the drop, else Captures).
  juce::Array<juce::File> dirs;
  for (const auto& path : files)
    if (juce::File(path).isDirectory()) dirs.add(juce::File(path));
  if (dirs.isEmpty()) return;
  const auto* target = services_.library.tree().find(folder);
  const auto into = target != nullptr && target->isCaptures() ? folder : juce::String();
  auto& library = services_.library;
  const auto what = dirs.size() == 1 ? dirs[0].getFileName() : juce::String(dirs.size()) + " folders";
  openMenu({{"Link " + what, Icon::Link, help::Key::libraryLink,
             [&library, dirs] { for (const auto& dir : dirs) library.link(dir); }},
            {"Copy Here", Icon::FolderPlus, help::Key::libraryImportFolder,
             [&library, dirs, into] { for (const auto& dir : dirs) library.importFolder(dir, into); }}},
           *this, {x, y});
}

// Layout

void LibraryDrawer::paint(juce::Graphics& g) {
  g.fillAll(theme::kSurface);
  // The edge against the chain.
  g.setColour(theme::kBorder);
  g.fillRect(getWidth() - 1, 0, 1, getHeight());

  paint::text(g, "Library", juce::Rectangle<int>(16, 0, 120, kHeaderHeight), Fonts::sans(15, true), theme::kWhite);

  auto& library = services_.library;
  const auto footer = getLocalBounds().removeFromBottom(kFooterHeight).reduced(16, 0);
  paint::hairlineH(g, 0, static_cast<float>(getWidth() - 1), static_cast<float>(getHeight() - kFooterHeight),
                   theme::kBorder);
  const auto where = library.tree().truncated ? juce::String("Over 250,000 items: not all are shown")
                                              : shortPath(library.root().getFullPathName());
  paint::text(g, where, footer, Fonts::sans(11), theme::kGray);

  // Empty states, under the rows.
  juce::String note;
  if (library.loading())
    note = juce::String::fromUTF8("Loading\xe2\x80\xa6");
  else if (model_.empty() && library.filter.trim().isNotEmpty())
    note = juce::String::fromUTF8("Nothing matches \xe2\x80\x9c") + library.filter.trim() +
           juce::String::fromUTF8("\xe2\x80\x9d.");
  else if (const auto* mine = library.tree().mine(); mine != nullptr && mine->children.empty() && !library.loading())
    note = "Your library is empty. Use the ... menu above: Link Folder shows your captures where they are, Import "
           "Folder copies them in. Or drop a folder here.";
  if (note.isNotEmpty()) {
    const int top = scroller_.getY() + kRowTop + static_cast<int>(model_.size()) * kRowHeight + 12;
    g.setFont(Fonts::sans(12));
    g.setColour(theme::kGray);
    g.drawFittedText(note, juce::Rectangle<int>(20, top, getWidth() - 40, 40), juce::Justification::topLeft, 3);
  }
}

void LibraryDrawer::resized() {
  auto area = getLocalBounds().withTrimmedRight(1);
  auto header = area.removeFromTop(kHeaderHeight).reduced(kPadX - 4, 0);
  const int y = (kHeaderHeight - 28) / 2;
  close_.setBounds(header.getRight() - 28, y, 28, 28);
  more_.setBounds(close_.getX() - 4 - 28, y, 28, 28);
  newFolder_.setBounds(more_.getX() - 4 - 28, y, 28, 28);

  search_.setBounds(area.removeFromTop(kSearchHeight).reduced(kPadX, 0));
  area.removeFromTop(6);
  gears_->setBounds(area.removeFromTop(GearRow::kHeight).reduced(kPadX, 0));
  area.removeFromTop(4);
  if (strip_->isVisible()) strip_->setBounds(area.removeFromTop(kStripHeight));
  if (missingStrip_->isVisible()) missingStrip_->setBounds(area.removeFromTop(kStripHeight));
  area.removeFromBottom(kFooterHeight);
  scroller_.setBounds(area);
  layoutRows();
}

}  // namespace t3k::ui
