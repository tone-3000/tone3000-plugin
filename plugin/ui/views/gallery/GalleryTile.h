// Gesture base for the gallery's tiles (the useTileMenu / useSortable /
// file-drop wiring of GalleryBlock.tsx): a primary click opens, a
// right-click (or a held touch on coarse-pointer devices) opens the tile's
// action sheet, travel past the drag distance hands the tile to the lane's
// drag host, and an OS file drag arms the tile as a drop target.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
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
};

class GalleryTile : public juce::Component, public juce::FileDragAndDropTarget {
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
  void fileDragEnter(const juce::StringArray&, int, int) override { setDropArmed(true); }
  void fileDragExit(const juce::StringArray&) override { setDropArmed(false); }
  void filesDropped(const juce::StringArray& files, int, int) override;

protected:
  // Primary click (not a drag, not a swallowed post-menu click).
  virtual void open() = 0;
  virtual std::vector<ContextMenu::Item> menuItems() = 0;
  // An OS file drag is hovering (upload glyph + green dashed border).
  virtual void dropArmedChanged(bool /*armed*/) {}
  virtual void travellingChanged(bool /*travelling*/) {}

  bool dropArmed() const { return dropArmed_; }
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
  void setDropArmed(bool armed);
  TileDragHost* host();

  Services& services_;
  std::string blockId_;
  int size_;
  bool travelling_ = false;
  bool dropArmed_ = false;
  bool dragging_ = false;
  juce::Point<int> pressAt_;
  juce::int64 suppressClickUntilMs_ = 0;
  juce::int64 menuDismissedMs_ = 0;
  DelayedCall hold_;
  std::unique_ptr<ContextMenu> menu_;
};

}  // namespace t3k::ui
