#include "GalleryLane.h"

#include "GalleryGeometry.h"
#include "core/Design.h"
#include "core/Help.h"
#include "core/Icons.h"
#include "core/Theme.h"
#include "widgets/Clickable.h"

namespace t3k::ui {

namespace {
// Diameter of the branch dots: half the power-button chrome footprint.
constexpr int kBranchDot = theme::kIconBoxSize / 2;
}  // namespace

// Full-gap hover zone wrapping a branch dot: the whole 24px connector run is
// the hit/hover area; the filled white disc stays hidden until then (always
// shown while the pointer is a finger, which can't hover a 24px gap).
class GalleryLane::BranchGap : public Clickable, private Pointer::Listener {
public:
  BranchGap(Pointer& pointer, help::Key helpKey, std::function<void()> action)
      : Clickable({}), pointer_(pointer) {
    setHelpText(help::text(helpKey));
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    onClick = std::move(action);
    pointer_.addListener(this);
    pointerChanged();
  }
  ~BranchGap() override { pointer_.removeListener(this); }

  void mouseEnter(const juce::MouseEvent&) override { setAlpha(1.0f); }
  void mouseExit(const juce::MouseEvent&) override {
    if (!pointer_.coarse()) setAlpha(0.0f);
  }
  void pointerChanged() override {
    if (!isMouseOver()) setAlpha(pointer_.coarse() ? 1.0f : 0.0f);
  }

  void paintButton(juce::Graphics& g, bool, bool) override {
    g.setColour(theme::kWhite);
    g.fillEllipse(getLocalBounds().toFloat().withSizeKeepingCentre(kBranchDot, kBranchDot));
  }

private:
  Pointer& pointer_;
};

GalleryLane::GalleryLane(Services& services, ChainSide side) : services_(services), side_(side) {
  setInterceptsMouseClicks(false, true);
}

GalleryLane::~GalleryLane() = default;

GalleryTile* GalleryLane::tileFor(const std::string& blockId) const {
  const auto it = tiles_.find(blockId);
  return it == tiles_.end() ? nullptr : it->second.get();
}

int GalleryLane::indexOf(const std::string& blockId) const {
  for (size_t i = 0; i < items_.size(); ++i)
    if (items_[i].blockId == blockId) return static_cast<int>(i);
  return -1;
}

void GalleryLane::setItems(const std::vector<ChainItem>& items, int tileSize) {
  items_ = items;
  tile_ = tileSize;

  // Reuse tiles of the same kind; anything else is rebuilt.
  std::map<std::string, std::unique_ptr<GalleryTile>> next;
  for (size_t i = 0; i < items_.size(); ++i) {
    const auto& item = items_[i];
    auto existing = tiles_.find(item.blockId);
    std::unique_ptr<GalleryTile> tile;
    if (existing != tiles_.end()) {
      const bool sameKind = item.isInsert ? dynamic_cast<AddTile*>(existing->second.get()) != nullptr
                                          : dynamic_cast<ToneTile*>(existing->second.get()) != nullptr;
      if (sameKind && existing->second->tileSize() == tile_) tile = std::move(existing->second);
      tiles_.erase(existing);
    }
    if (tile == nullptr) {
      if (item.isInsert)
        tile = std::make_unique<AddTile>(services_, item.blockId, tile_);
      else
        tile = std::make_unique<ToneTile>(services_, item, tile_);
      wire(*tile);
      addAndMakeVisible(*tile);
    }
    if (auto* add = dynamic_cast<AddTile*>(tile.get())) {
      add->setRouting(AddTile::routingFor(static_cast<int>(i), static_cast<int>(items_.size())));
      add->setCanPaste(canPaste_);
    } else if (auto* tone = dynamic_cast<ToneTile*>(tile.get())) {
      tone->setBlock(item);
    }
    tile->setVisible(item.blockId != placeholder_);
    next[item.blockId] = std::move(tile);
  }
  tiles_ = std::move(next);  // dropped tiles (and their menus) go here

  setSize(gallery::laneWidth(static_cast<int>(items_.size()), tile_), tile_);
  rebuildBranchGaps();
  resized();
  repaint();
}

void GalleryLane::setBranch(bool stereo, const std::optional<ChainBranch>& branch,
                            bool interactive) {
  stereo_ = stereo;
  branch_ = branch;
  branchInteractive_ = interactive;
  rebuildBranchGaps();
  resized();
}

void GalleryLane::setCanPaste(bool canPaste) {
  canPaste_ = canPaste;
  for (auto& [id, tile] : tiles_)
    if (auto* add = dynamic_cast<AddTile*>(tile.get())) add->setCanPaste(canPaste);
}

void GalleryLane::setPlaceholder(const std::string& blockId) {
  placeholder_ = blockId;
  for (auto& [id, tile] : tiles_) tile->setVisible(id != placeholder_);
  repaint();
}

std::unique_ptr<GalleryTile> GalleryLane::releaseTile(const std::string& blockId) {
  const auto it = tiles_.find(blockId);
  if (it == tiles_.end()) return nullptr;
  auto tile = std::move(it->second);
  tiles_.erase(it);
  return tile;
}

void GalleryLane::adoptTile(std::unique_ptr<GalleryTile> tile) {
  if (tile == nullptr) return;
  addChildComponent(*tile);  // reparents; the next setItems places and shows it
  wire(*tile);
  tiles_[tile->blockId()] = std::move(tile);
}

void GalleryLane::wire(GalleryTile& tile) {
  if (auto* add = dynamic_cast<AddTile*>(&tile)) {
    add->onAdd = [this](const std::string& id) { if (onAdd) onAdd(id); };
    add->onPaste = [this](const std::string& id) {
      if (onPaste) onPaste(indexOf(id));
    };
  } else if (auto* tone = dynamic_cast<ToneTile*>(&tile)) {
    tone->onOpen = [this](const std::string& id) { if (onOpen) onOpen(id); };
    tone->onSwap = [this](const std::string& id) { if (onSwap) onSwap(id); };
    tone->onAddBeside = [this](const std::string& target) { if (onAdd) onAdd(target); };
  }
}

// Every gap following a tone block carries a set-branch dot, except the
// active tap gap on the trunk lane, whose dot clears the branch instead.
void GalleryLane::rebuildBranchGaps() {
  gaps_.clear();
  if (!stereo_) return;
  const bool trunk = branch_.has_value() && branch_->side == side_;
  const int tapIndex = trunk ? indexOf(branch_->afterBlockId) : -1;
  const int count = static_cast<int>(items_.size());
  for (int i = 0; i < count - 1; ++i) {
    const auto& item = items_[static_cast<size_t>(i)];
    if (item.isInsert) continue;
    const bool tap = trunk && i == tapIndex;
    if (!tap && !branchInteractive_) continue;
    auto gap = tap ? std::make_unique<BranchGap>(services_.pointer, help::Key::branchJunction,
                                                 [this] { if (onClearBranch) onClearBranch(); })
                   : std::make_unique<BranchGap>(services_.pointer, help::Key::branchGap,
                                                 [this, id = item.blockId] {
                                                   if (onSetBranch) onSetBranch(id);
                                                 });
    gap->setBounds(design::snap(gallery::gapCentreX(i + 1, tile_) - gallery::kTileGap / 2.0f), 0,
                   gallery::kTileGap, tile_);
    addAndMakeVisible(*gap);
    gap->toFront(false);
    gaps_.push_back(std::move(gap));
  }
}

void GalleryLane::resized() {
  for (size_t i = 0; i < items_.size(); ++i)
    if (auto* tile = tileFor(items_[i].blockId))
      tile->setBounds(gallery::tileX(static_cast<int>(i), tile_), 0, tile_, tile_);
}

// The ghost rail: one plus circle per slot, connector lines between them,
// each line extended past the icon boxes by plusCircleInset so it meets the
// drawn ring (stopping at the box edge reads as a hairline gap).
void GalleryLane::paint(juce::Graphics& g) {
  const int count = static_cast<int>(items_.size());
  if (count == 0) return;
  const int icon = gallery::plusIconSize(tile_);
  const float inset = gallery::plusCircleInset(icon);
  const float cy = tile_ / 2.0f;
  // Tiles repaint on every meter tick; only draw the slots that actually
  // fall in the dirty area rather than re-rendering the SVG for every slot.
  const auto clip = g.getClipBounds().toFloat();
  g.setColour(theme::kWhite);
  for (int i = 0; i < count; ++i) {
    const float cx = gallery::tileX(i, tile_) + tile_ / 2.0f;
    if (i > 0) {
      const float x0 = cx - (tile_ + gallery::kTileGap) + icon / 2.0f - inset;
      const float x1 = cx - icon / 2.0f + inset;
      const juce::Rectangle<float> line(x0, cy - gallery::kLineWidth / 2, x1 - x0, gallery::kLineWidth);
      if (line.intersects(clip)) g.fillRect(line);
    }
    const auto circle = juce::Rectangle<float>(icon, icon).withCentre({cx, cy});
    if (circle.intersects(clip))
      Icons::draw(g, Icon::PlusCircle, circle, theme::kWhite, /*strokeWidth=*/1.0f);
  }
}

}  // namespace t3k::ui
