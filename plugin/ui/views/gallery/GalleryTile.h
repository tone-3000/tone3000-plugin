// Gesture base for the gallery's tiles (the useTileMenu / useSortable /
// file-drop wiring of GalleryBlock.tsx): a primary click opens, a
// right-click (or a held touch on coarse-pointer devices) opens the tile's
// action sheet, travel past the drag distance hands the tile to the lane's
// drag host, and an OS file drag or a Library row arms the tile as a drop
// target (a Library preset loads; a tone or capture lands here like a pick).
// A tile that takes edge drops (a tone tile) splits into three zones: the
// middle swaps, a strip along each side as wide as the gap between tiles
// adds a new block before / after it (an insertion bar in the gap marks
// which), so nothing has to be moved out of the way. The gaps themselves
// take drops too (ChainView): gap plus strip is the target.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/DelayedCall.h"
#include "services/Services.h"
#include "widgets/ContextMenu.h"

namespace t3k::ui {

class GalleryTile;

// The lane's owner (ChainView) runs the drag: the tile only reports
// pointer travel. A keyboard grab sorts one slot per arrow press.
class TileDragHost {
public:
  virtual ~TileDragHost() = default;
  virtual void tileDragStart(GalleryTile& tile, const juce::MouseEvent& e) = 0;
  virtual void tileDragMove(const juce::MouseEvent& e) = 0;
  virtual void tileDragEnd(const juce::MouseEvent& e) = 0;
  // Space/Enter picks up or drops; arrows move; Escape cancels. Returns
  // whether the key was consumed.
  virtual bool tileKey(GalleryTile& tile, const juce::KeyPress& key) = 0;
  // A drop hovering one of the tile's edges (before / after), or neither:
  // the host marks the gap beside it, where the new block will go.
  virtual void tileDropEdge(GalleryTile& /*tile*/, std::optional<bool> /*after*/) {}
};

class GalleryTile : public juce::Component, public juce::FileDragAndDropTarget, public juce::DragAndDropTarget {
public:
  GalleryTile(Services& services, std::string blockId, int size);
  ~GalleryTile() override;

  const std::string& blockId() const { return blockId_; }
  int tileSize() const { return size_; }
  // The web dims a travelling tile (dnd-kit Feedback) to 0.75.
  void setTravelling(bool travelling);
  bool travelling() const { return travelling_; }

  // Tone tile: swap in place; insert slot: add. Shared menu rows.
  std::vector<ContextMenu::Item> localLoadItems();

  void mouseDown(const juce::MouseEvent& e) override;
  void mouseDrag(const juce::MouseEvent& e) override;
  void mouseUp(const juce::MouseEvent& e) override;
  bool keyPressed(const juce::KeyPress& key) override;
  // A button to screen readers: press opens, show-menu opens the action
  // sheet. The subclass names it with setTitle().
  std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

  bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
  void fileDragEnter(const juce::StringArray&, int x, int) override { setDrop(true, edgeAt(x)); }
  void fileDragMove(const juce::StringArray&, int x, int) override { setDrop(true, edgeAt(x)); }
  void fileDragExit(const juce::StringArray&) override { setDrop(false, DropEdge::none); }
  void filesDropped(const juce::StringArray& files, int x, int) override;

  // Library rows (LibraryDrawer): items, and captures folders (one block
  // switching between their files).
  bool isInterestedInDragSource(const SourceDetails& details) override;
  void itemDragEnter(const SourceDetails& details) override { setDrop(true, edgeFor(details)); }
  void itemDragMove(const SourceDetails& details) override { setDrop(true, edgeFor(details)); }
  void itemDragExit(const SourceDetails&) override { setDrop(false, DropEdge::none); }
  void itemDropped(const SourceDetails& details) override;

  // Which part of the tile a drop at local x lands on (edge drops above).
  enum class DropEdge { none, before, after };

  DropEdge edgeAt(int x) const;
  // The load target for a drop on `edge`: this block, or a new slot beside it.
  std::string dropTarget(DropEdge edge) const;

protected:
  // Primary click (not a drag, not a swallowed post-menu click).
  virtual void open() = 0;
  virtual std::vector<ContextMenu::Item> menuItems() = 0;
  // An OS file drag is hovering (upload glyph + green dashed border).
  virtual void dropArmedChanged(bool /*armed*/) {}
  // Tone tiles: drops near an edge add beside the block instead of swapping.
  virtual bool takesEdgeDrops() const { return false; }
  virtual void travellingChanged(bool /*travelling*/) {}

  bool dropArmed() const { return dropArmed_; }
  // Where an armed drop would land (none: on the tile itself).
  DropEdge dropEdge() const { return dropEdge_; }
  bool menuOpen() const { return menu_ != nullptr && menu_->isOpen(); }
  Services& services() { return services_; }

private:
  static constexpr int kLongPressMs = 500;
  static constexpr int kLongPressSlop = 5;
  // A touch that covers the drag distance within this is a swipe that pans
  // the lane, not a sort. In practice a dwell threshold: 6 px in 150 ms is
  // only 40 px/s, so what separates the two is whether the finger paused
  // before it moved.
  static constexpr int kFlickMs = 150;
  // Real px the menu drops below the touch point (the release must land
  // outside it, and the sheet stays readable past the fingertip).
  static constexpr int kLongPressMenuDrop = 24;
  // How long a set click suppression stays valid.
  static constexpr int kSuppressClickMs = 700;

  void openMenu(juce::Point<int> at);
  void closeMenu();
  void setDrop(bool armed, DropEdge edge);
  DropEdge edgeFor(const SourceDetails& details) const;
  TileDragHost* host();

  Services& services_;
  std::string blockId_;
  int size_;
  bool travelling_ = false;
  bool dropArmed_ = false;
  DropEdge dropEdge_ = DropEdge::none;
  bool dragging_ = false;
  juce::Point<int> pressAt_;
  juce::int64 suppressClickUntilMs_ = 0;
  juce::int64 menuDismissedMs_ = 0;
  DelayedCall hold_;
  std::unique_ptr<ContextMenu> menu_;
};

}  // namespace t3k::ui
