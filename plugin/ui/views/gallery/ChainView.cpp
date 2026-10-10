#include "ChainView.h"

#include <algorithm>
#include <cmath>

#include "GalleryGeometry.h"
#include "core/Fonts.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "widgets/Popover.h"

namespace t3k::ui {

namespace {
constexpr int kTitleSize = 16;

template <typename T>
void arrayMove(std::vector<T>& v, int from, int to) {
  if (from == to || from < 0 || to < 0 || from >= static_cast<int>(v.size()) ||
      to >= static_cast<int>(v.size()))
    return;
  T item = std::move(v[static_cast<size_t>(from)]);
  v.erase(v.begin() + from);
  v.insert(v.begin() + to, std::move(item));
}

std::vector<std::string> idsOf(const std::vector<ChainItem>& items) {
  std::vector<std::string> ids;
  ids.reserve(items.size());
  for (const auto& item : items) ids.push_back(item.blockId);
  return ids;
}
}  // namespace

// The tile travelling with the pointer: a snapshot of the tile at the drag
// ghost opacity, on the overlay layer so it rides above both lanes.
class ChainView::Ghost : public juce::Component {
public:
  explicit Ghost(juce::Image image) : image_(std::move(image)) {
    setInterceptsMouseClicks(false, false);
    setAlpha(gallery::kDragGhostOpacity);
    setSize(image_.getWidth(), image_.getHeight());
  }
  void paint(juce::Graphics& g) override { g.drawImageAt(image_, 0, 0); }

private:
  juce::Image image_;
};

// The lanes column inside the scroller: both lanes (the branch lane indented
// past the trunk prefix) and the two-lane elbow of an active branch, drawn
// with the same lines as the ghost rail.
class ChainView::Column : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          public juce::DragAndDropTarget {
public:
  explicit Column(ChainView& view) : view_(view) {}

  // Gap drops (see ChainView::gapAt): OS files and Library rows.
  bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
  void fileDragEnter(const juce::StringArray&, int x, int y) override { hover({x, y}); }
  void fileDragMove(const juce::StringArray&, int x, int y) override { hover({x, y}); }
  void fileDragExit(const juce::StringArray&) override { view_.clearDropMark(); }
  void filesDropped(const juce::StringArray& files, int x, int y) override {
    view_.clearDropMark();
    if (const auto gap = view_.gapAt({x, y})) view_.services_.localFiles.drop(gap->target, files);
  }
  bool isInterestedInDragSource(const SourceDetails& details) override {
    const auto* node = view_.services_.library.tree().find(libraryPath(details));
    // An item or a captures folder; a preset replaces the chain, no gap for it.
    return node != nullptr && node->kind != LibraryNode::Kind::preset && (!node->isContainer() || node->loadsAsBlock());
  }
  void itemDragEnter(const SourceDetails& details) override { hover(details.localPosition); }
  void itemDragMove(const SourceDetails& details) override { hover(details.localPosition); }
  void itemDragExit(const SourceDetails&) override { view_.clearDropMark(); }
  void itemDropped(const SourceDetails& details) override {
    view_.clearDropMark();
    const auto gap = view_.gapAt(details.localPosition);
    if (!gap) return;
    // Posted: the load rebuilds the lanes.
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<Column>(this), path = libraryPath(details),
                                     target = gap->target] {
      if (safe == nullptr) return;
      auto& library = safe->view_.services_.library;
      if (const auto* node = library.tree().find(path)) library.use(*node, target);
    });
  }

  // The insertion bar, in the gap where the new block goes.
  void setMark(std::optional<juce::Rectangle<float>> mark) {
    if (mark == mark_) return;
    mark_ = mark;
    repaint();
  }
  void paintOverChildren(juce::Graphics& g) override {
    if (!mark_) return;
    const auto bar = *mark_;
    g.setColour(gallery::kFileDropBorder.withAlpha(1.0f));
    g.fillRoundedRectangle(bar, bar.getWidth() / 2);
    const auto glyph = juce::Rectangle<float>(kMarkGlyph, kMarkGlyph).withCentre(bar.getCentre());
    g.setColour(theme::kBlack);
    g.fillEllipse(glyph.expanded(4));
    Icons::draw(g, Icon::Plus, glyph, gallery::kFileDropBorder.withAlpha(1.0f));
  }

  struct BranchLayout {
    ChainSide trunkSide;
    int indent;
    float tapGapX;
  };
  std::optional<BranchLayout> branch;
  int tile = gallery::kTileSize;

  void paint(juce::Graphics& g) override {
    if (!branch) return;
    const float x = gallery::kEdgeFadeWidth + branch->tapGapX;
    const float topCentre = tile / 2.0f;
    const float bottomCentre = tile + gallery::kLaneGap + tile / 2.0f;
    const float stubY = branch->trunkSide == ChainSide::left ? bottomCentre : topCentre;
    const float w = gallery::kLineWidth;
    g.setColour(theme::kWhite);
    g.fillRect(juce::Rectangle<float>(x - w / 2, lanesTop_ + topCentre, w, bottomCentre - topCentre));
    g.fillRect(juce::Rectangle<float>(x, lanesTop_ + stubY - w / 2, gallery::kTileGap / 2.0f, w));
  }
  void setLanesTop(int y) { lanesTop_ = static_cast<float>(y); }

private:
  static constexpr float kMarkGlyph = 16.0f;
  static juce::String libraryPath(const SourceDetails& details) {
    return details.description.getProperty(LibraryStore::kDragKey, {}).toString();
  }
  void hover(juce::Point<int> p) {
    if (const auto gap = view_.gapAt(p)) view_.showDropMark(gap->side, gap->boundary);
    else view_.clearDropMark();
  }

  ChainView& view_;
  float lanesTop_ = 0;
  std::optional<juce::Rectangle<float>> mark_;
};

ChainView::ChainView(Services& services)
    : services_(services),
      rail_(services),
      scroller_(std::make_unique<DragScroller>(DragScroller::Axis::horizontal)),
      column_(std::make_unique<Column>(*this)),
      left_(services, ChainSide::left),
      right_(services, ChainSide::right) {
  addChildComponent(rail_);

  scroller_->setViewedComponent(column_.get(), false);
  scroller_->setFocusMargin(gallery::kEdgeFadeWidth);  // a focused tile clears the edge fade
  scroller_->onScroll = [this] { saveScroll(); };
  addAndMakeVisible(*scroller_);
  column_->addAndMakeVisible(left_);
  column_->addChildComponent(right_);

  for (auto* l : {&left_, &right_}) {
    const auto side = l->side();
    l->onOpen = [this](const std::string& id) { if (onOpenBlock) onOpenBlock(id); };
    l->onSwap = [this, side](const std::string& id) { if (onSelectTone) onSelectTone(side, id); };
    l->onAdd = [this, side](const std::string& id) { if (onSelectTone) onSelectTone(side, id); };
    l->onPaste = [this, side](int index) { services_.chain.pasteBlock(side, index); };
    l->onSetBranch = [this, side](const std::string& after) { services_.chain.setBranch(side, after); };
    l->onClearBranch = [this] { services_.chain.clearBranch(); };
  }

  services_.chain.addListener(this);
  syncFromNative();
}

ChainView::~ChainView() { services_.chain.removeListener(this); }

int ChainView::tileSize() const { return gallery::tileSize(stereo()); }

// State
void ChainView::chainChanged(const ChainState&) { syncFromNative(); }

// Resync the optimistic lanes only when native reports new state and no
// drag is in flight.
void ChainView::syncFromNative() {
  const auto& state = services_.chain.state();
  native_.left = state.chain;
  native_.right = state.chainRight.value_or(std::vector<ChainItem>{});
  if (rail_.isVisible() != stereo()) {
    // The rail takes (or frees) its column, so the scroller has to move too.
    rail_.setVisible(stereo());
    resized();
  }
  rail_.setMonoSum(state.stereoEnabled && !state.stereoOutput);
  if (!dragging_) {
    lanes_ = native_;
    applyLanes();
  }
}

void ChainView::applyLanes() {
  const auto& state = services_.chain.state();
  const int tile = tileSize();
  // A block that changed lanes keeps its tile. Mid-drag this is what keeps
  // the gesture alive: the tile under the pointer is the component JUCE
  // delivers the drag to, and rebuilding the lane it left would destroy it
  // (the pointer's events would then go nowhere, the ghost stuck where it
  // crossed). Off-drag it just saves rebuilding a tile the resync moved.
  const auto handOver = [](GalleryLane& from, GalleryLane& to, const std::vector<ChainItem>& toItems) {
    for (const auto& item : toItems)
      if (from.tileFor(item.blockId) != nullptr && to.tileFor(item.blockId) == nullptr)
        to.adoptTile(from.releaseTile(item.blockId));
  };
  handOver(left_, right_, lanes_.right);
  handOver(right_, left_, lanes_.left);
  left_.setItems(lanes_.left, tile);
  right_.setItems(lanes_.right, tile);
  right_.setVisible(stereo());
  for (auto* l : {&left_, &right_}) {
    l->setBranch(stereo(), stereo() ? state.branch : std::nullopt, stereo() && !dragging_);
    l->setCanPaste(state.canPasteBlock);
  }
  layoutColumn();
}

std::optional<ChainSide> ChainView::laneOf(const Lanes& lanes, const std::string& id) const {
  for (const auto& item : lanes.left)
    if (item.blockId == id) return ChainSide::left;
  for (const auto& item : lanes.right)
    if (item.blockId == id) return ChainSide::right;
  return std::nullopt;
}

const ChainItem* ChainView::itemIn(const Lanes& lanes, const std::string& id) const {
  for (const auto* v : {&lanes.left, &lanes.right})
    for (const auto& item : *v)
      if (item.blockId == id) return &item;
  return nullptr;
}

// Layout
void ChainView::resized() {
  auto area = getLocalBounds().reduced(kPadX, kPadY);
  if (stereo()) rail_.setBounds(area.removeFromLeft(StereoPanRail::kWidth).withSizeKeepingCentre(
      StereoPanRail::kWidth, rail_.getHeight()));
  scroller_->setBounds(area);
  layoutColumn();
}

// Branched layout: the branch lane starts at the trunk's tap gap, so its row
// is indented past the whole trunk prefix (its input *is* that prefix's
// output). Resolved against the optimistic lane state; a stale tap id
// renders as independent lanes until native's cleared state arrives.
void ChainView::layoutColumn() {
  const auto& state = services_.chain.state();
  const int tile = tileSize();
  std::optional<Column::BranchLayout> branch;
  if (stereo() && state.branch) {
    const int tapIndex = lane(state.branch->side).indexOf(state.branch->afterBlockId);
    if (tapIndex != -1)
      branch = Column::BranchLayout{state.branch->side, (tapIndex + 1) * (tile + gallery::kTileGap),
                                    gallery::gapCentreX(tapIndex + 1, tile)};
  }
  column_->branch = branch;
  column_->tile = tile;

  auto indentFor = [&](ChainSide side) {
    return branch && side != branch->trunkSide ? branch->indent : 0;
  };
  int content = left_.getWidth() + indentFor(ChainSide::left);
  if (stereo()) content = std::max(content, right_.getWidth() + indentFor(ChainSide::right));
  const int width = std::max(scroller_->getWidth(), content + 2 * gallery::kEdgeFadeWidth);
  const int lanesHeight = stereo() ? tile * 2 + gallery::kLaneGap : tile;
  const int height = scroller_->getHeight();
  const int lanesTop = (height - lanesHeight) / 2;
  column_->setSize(width, height);
  column_->setLanesTop(lanesTop);
  left_.setTopLeftPosition(gallery::kEdgeFadeWidth + indentFor(ChainSide::left), lanesTop);
  right_.setTopLeftPosition(gallery::kEdgeFadeWidth + indentFor(ChainSide::right),
                            lanesTop + tile + gallery::kLaneGap);
  column_->repaint();

  // Restore the persisted offset once the scroller has its size (an offset
  // past the end, the chain shrank, clamps on assignment).
  if (restorePending_ && scroller_->getWidth() > 0 && !lanes_.left.empty()) {
    restorePending_ = false;
    const int saved = services_.prefs.session[UiPrefs::kChainScroll].getIntValue();
    if (saved > 0) scroller_->setViewPosition(saved, 0);
  }
}

// Gap drops (see the header)
std::optional<ChainView::GapDrop> ChainView::gapAt(juce::Point<int> p) {
  const int tile = tileSize();
  const int step = tile + gallery::kTileGap;
  for (const auto side : {ChainSide::left, ChainSide::right}) {
    if (side == ChainSide::right && !stereo()) continue;
    const auto& lane = this->lane(side);
    const auto& items = lane.items();
    const int n = static_cast<int>(items.size());
    const auto row = lane.getBounds();
    if (n == 0 || p.y < row.getY() - gallery::kLaneGap / 2 || p.y > row.getBottom() + gallery::kLaneGap / 2) continue;
    const int k = juce::jlimit(0, n, juce::roundToInt((p.x - row.getX() + gallery::kTileGap / 2.0f) / step));
    const ChainItem* left = k > 0 ? &items[static_cast<size_t>(k - 1)] : nullptr;
    const ChainItem* right = k < n ? &items[static_cast<size_t>(k)] : nullptr;
    std::string target;
    if (right != nullptr && !right->isInsert) target = slotBefore(right->blockId);
    else if (left != nullptr && !left->isInsert) target = slotAfter(left->blockId);
    else target = (right != nullptr ? right : left)->blockId;  // between empty slots: fill one
    return GapDrop{side, k, target};
  }
  return std::nullopt;
}

void ChainView::showDropMark(ChainSide side, int boundary) {
  const auto& lane = this->lane(side);
  constexpr float kBarWidth = 8.0f;
  const float x = lane.getX() + boundary * static_cast<float>(tileSize() + gallery::kTileGap) - gallery::kTileGap / 2.0f;
  column_->setMark(juce::Rectangle<float>(x - kBarWidth / 2, static_cast<float>(lane.getY()), kBarWidth,
                                          static_cast<float>(lane.getHeight())));
}

void ChainView::clearDropMark() { column_->setMark(std::nullopt); }

void ChainView::tileDropEdge(GalleryTile& tile, std::optional<bool> after) {
  if (!after) return clearDropMark();
  for (const auto side : {ChainSide::left, ChainSide::right}) {
    const int index = lane(side).indexOf(tile.blockId());
    if (index != -1) return showDropMark(side, index + (*after ? 1 : 0));
  }
}

void ChainView::saveScroll() {
  if (restorePending_) return;  // layout clamps before the restore, not the user
  services_.prefs.session[UiPrefs::kChainScroll] = juce::String(scroller_->getViewPositionX());
}

void ChainView::returnToGallery() {
  services_.prefs.session.erase(UiPrefs::kChainScroll);
  scroller_->setViewPosition(0, 0);
}

void ChainView::paint(juce::Graphics& g) {
  if (stereo()) return;
  // Mono-only section title in the top-left dead space, aligned with the
  // first tile (the lane's edge-fade inset).
  const auto font = Fonts::mono(kTitleSize);
  const int x = scroller_->getX() + gallery::kEdgeFadeWidth;
  paint::text(g, "SIGNAL CHAIN", {x, kPadY, 300, juce::roundToInt(kTitleSize * 1.2f)}, font,
              theme::kWhite);
}

// Fade the lanes out under the gutters as they scroll.
void ChainView::paintOverChildren(juce::Graphics& g) {
  paint::edgeFades(g, scroller_->getBounds().toFloat(), gallery::kEdgeFadeWidth, juce::Colours::black);
}

// Sorting: pointer + keyboard
void ChainView::beginSort(GalleryTile& tile, bool pointer) {
  dragging_ = true;
  keyboardSort_ = !pointer;
  activeId_ = tile.blockId();
  originSide_ = laneOf(native_, activeId_).value_or(ChainSide::left);
  lanes_ = native_;
  duplicating_ = false;
  for (auto* l : {&left_, &right_})
    l->setBranch(stereo(), stereo() ? services_.chain.state().branch : std::nullopt, false);
}

void ChainView::tileDragStart(GalleryTile& tile, const juce::MouseEvent& e) {
  if (dragging_) cancelSort();
  beginSort(tile, /*pointer=*/true);

  // The tile itself travels with the pointer; a hidden placeholder holds its
  // slot, which reveals the ghost rail behind it.
  auto* host = findParentComponentOfClass<OverlayHost>();
  if (host != nullptr) {
    auto& overlay = host->overlayLayer();
    ghost_ = std::make_unique<Ghost>(tile.createComponentSnapshot(tile.getLocalBounds()));
    const auto tileInOverlay = overlay.getLocalArea(&tile, tile.getLocalBounds());
    grabOffset_ = overlay.getLocalPoint(e.eventComponent, e.getPosition()) - tileInOverlay.getPosition();
    ghost_->setTopLeftPosition(tileInOverlay.getPosition());
    overlay.addAndMakeVisible(*ghost_);
  }
  markActive();
  if (tile.isShowing()) tile.grabKeyboardFocus();  // Escape cancels
  if (e.mods.isAltDown()) setDuplicateStandIn(true);
}

void ChainView::markActive() {
  const auto side = laneOf(lanes_, activeId_);
  if (!side) return;
  if (keyboardSort_) {
    if (auto* tile = lane(*side).tileFor(activeId_)) {
      tile->setTravelling(true);
      if (tile->isShowing()) tile->grabKeyboardFocus();
    }
  } else {
    lane(*side).setPlaceholder(activeId_);
  }
}

// Insert (or remove) the ⌥-duplicate stand-in: an inert copy of the dragged
// block pinned at its home slot. With the home slot visibly occupied the
// same gesture reads as pulling a *copy* out. Rebuilt from native state so
// toggling ⌥ mid-drag also undoes any optimistic cross-lane reflow.
void ChainView::setDuplicateStandIn(bool on) {
  const auto* item = itemIn(native_, activeId_);
  if (item == nullptr || item->isInsert) return;  // inserts have nothing to duplicate
  duplicating_ = on;
  lanes_ = native_;
  if (on) {
    auto& home = lanes_.of(originSide_);
    const int index = static_cast<int>(std::find_if(home.begin(), home.end(), [&](const ChainItem& i) {
                                         return i.blockId == activeId_;
                                       }) - home.begin());
    ChainItem standIn = *item;
    standIn.blockId = kStandInId;
    home.insert(home.begin() + index, standIn);
  }
  applyLanes();
  markActive();
}

// Move the active item to `index` in `side` (same-lane sort, or the live
// cross-lane reflow: the target lane parts to make room). Insert slots are
// lane anchors and never change lanes.
void ChainView::moveActiveTo(ChainSide side, int index) {
  const auto from = laneOf(lanes_, activeId_);
  if (!from) return;
  auto& source = lanes_.of(*from);
  const int oldIndex = static_cast<int>(std::find_if(source.begin(), source.end(), [&](const ChainItem& i) {
                                          return i.blockId == activeId_;
                                        }) - source.begin());
  if (*from == side) {
    const int clamped = juce::jlimit(0, static_cast<int>(source.size()) - 1, index);
    if (clamped == oldIndex) return;
    arrayMove(source, oldIndex, clamped);
  } else {
    if (!stereo() || source[static_cast<size_t>(oldIndex)].isInsert) return;
    ChainItem item = source[static_cast<size_t>(oldIndex)];
    source.erase(source.begin() + oldIndex);
    auto& target = lanes_.of(side);
    const int clamped = juce::jlimit(0, static_cast<int>(target.size()), index);
    target.insert(target.begin() + clamped, item);
    lane(*from).setPlaceholder({});
  }
  applyLanes();
  markActive();
}

void ChainView::tileDragMove(const juce::MouseEvent& e) {
  if (!dragging_ || ghost_ == nullptr) return;
  if (e.mods.isAltDown() != duplicating_) setDuplicateStandIn(e.mods.isAltDown());

  auto& overlay = *ghost_->getParentComponent();
  const auto pointer = overlay.getLocalPoint(e.eventComponent, e.getPosition());
  ghost_->setTopLeftPosition(pointer - grabOffset_);

  // Target by the dragged tile's centre: which lane band it is in, and the
  // slot under it. Landing after the hovered tile once the centre has
  // passed the hovered tile's centre.
  const auto centre = column_->getLocalPoint(&overlay, ghost_->getBounds().getCentre());
  const int tile = tileSize();
  ChainSide side = ChainSide::left;
  if (stereo()) {
    const int seam = left_.getBottom() + gallery::kLaneGap / 2;
    side = centre.y < seam ? ChainSide::left : ChainSide::right;
  }
  auto& target = lane(side);
  const int count = static_cast<int>(target.items().size());
  if (count == 0) return;
  const int localX = centre.x - target.getX();
  const int slot = juce::jlimit(0, count - 1, juce::roundToInt(std::floor(localX / static_cast<float>(tile + gallery::kTileGap))));
  const auto from = laneOf(lanes_, activeId_);
  if (from && *from == side) {
    moveActiveTo(side, slot);
  } else {
    const int hoveredCentre = target.getX() + gallery::tileX(slot, tile) + tile / 2;
    const bool landAfter = centre.x > hoveredCentre;
    moveActiveTo(side, localX >= gallery::laneWidth(count, tile) ? count : slot + (landAfter ? 1 : 0));
  }
}

void ChainView::tileDragEnd(const juce::MouseEvent&) {
  if (!dragging_) return;
  commitSort();
}

// Commit to native: ⌥-drop clones instead of moving; a lane change is one
// moveBlock (exact final index); a same-lane shuffle is one reorder. The
// chainChanged resync converges the optimistic state.
void ChainView::commitSort() {
  const auto side = laneOf(lanes_, activeId_);
  const std::string id = activeId_;
  const bool duplicating = duplicating_;
  const auto origin = originSide_;
  Lanes lanes = lanes_;
  finishSort();
  if (!side) return;

  const auto& items = lanes.of(*side);
  const int finalIndex = static_cast<int>(std::find_if(items.begin(), items.end(), [&](const ChainItem& i) {
                                            return i.blockId == id;
                                          }) - items.begin());
  if (finalIndex >= static_cast<int>(items.size())) return;

  if (duplicating && !items[static_cast<size_t>(finalIndex)].isInsert) {
    services_.chain.duplicateBlock(id, *side, finalIndex);
    return;
  }
  if (origin != *side) {
    services_.chain.moveBlockToChain(id, *side, finalIndex);
    return;
  }
  if (idsOf(native_.of(*side)) != idsOf(items)) services_.chain.reorderBlocks(idsOf(items));
}

void ChainView::cancelSort() {
  finishSort();
}

void ChainView::finishSort() {
  dragging_ = false;
  keyboardSort_ = false;
  duplicating_ = false;
  ghost_.reset();
  for (auto* l : {&left_, &right_}) {
    l->setPlaceholder({});
    // The tile follows the item across lanes, so look for it in both.
    if (auto* tile = l->tileFor(activeId_)) tile->setTravelling(false);
  }
  activeId_.clear();
  lanes_ = native_;
  applyLanes();
}

// Stock keyboard sorting: Space or Enter on a focused tile picks it up,
// arrows snap it one slot per press (up/down cross lanes in stereo),
// Space/Enter drops, Escape cancels.
bool ChainView::tileKey(GalleryTile& tile, const juce::KeyPress& key) {
  const bool pick = key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey;
  if (dragging_ && key == juce::KeyPress::escapeKey) {
    cancelSort();
    return true;
  }
  if (!dragging_) {
    if (!pick) return false;
    beginSort(tile, /*pointer=*/false);
    markActive();
    return true;
  }
  if (!keyboardSort_ || tile.blockId() != activeId_) return false;
  if (pick) {
    commitSort();
    return true;
  }
  const auto side = laneOf(lanes_, activeId_);
  if (!side) return false;
  const int index = lane(*side).indexOf(activeId_);
  if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey) {
    moveActiveTo(*side, index + (key == juce::KeyPress::leftKey ? -1 : 1));
  } else if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey) {
    if (!stereo()) return false;
    const auto other = *side == ChainSide::left ? ChainSide::right : ChainSide::left;
    if ((key == juce::KeyPress::upKey) != (other == ChainSide::left)) return true;
    moveActiveTo(other, index);
  } else {
    return false;
  }
  // The travelling tile stays in view, as it would under a finger.
  if (const auto now = laneOf(lanes_, activeId_))
    if (auto* moved = lane(*now).tileFor(activeId_)) scroller_->reveal(*moved, gallery::kEdgeFadeWidth);
  return true;
}

bool ChainView::keyPressed(const juce::KeyPress& key) {
  if (dragging_ && key == juce::KeyPress::escapeKey) {
    cancelSort();
    return true;
  }
  return false;
}

}  // namespace t3k::ui
