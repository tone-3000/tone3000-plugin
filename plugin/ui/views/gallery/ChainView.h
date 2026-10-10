// Chain gallery (ChainView.tsx): blocks as square image tiles in horizontal
// lanes over a ghost rail of plus circles. Mono shows one lane; stereo shows
// both L/R lanes in a single shared scroll area with the pan rail on the
// left, and a branched pair indents the branch lane past the trunk's tap
// gap with an elbow connector. This component owns the drag orchestration:
// the lane lists are mirrored into optimistic local state, so cross-lane
// drags reflow the target lane live and drops land without any snap-back
// while the native roundtrip completes. Tap/click opens the detail takeover.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "GalleryLane.h"
#include "GalleryTile.h"
#include "StereoPanRail.h"
#include "services/ChainStore.h"
#include "services/Services.h"
#include "widgets/DragScroller.h"

namespace t3k::ui {

class ChainView : public juce::Component, public TileDragHost, private ChainStore::Listener {
public:
  // Shared 24px under the header and above the faceplate (Plugin.tsx).
  static constexpr int kPadY = 24;
  static constexpr int kPadX = 24;

  explicit ChainView(Services& services);
  ~ChainView() override;

  // Preset load / reset: back to the gallery at the left edge.
  void returnToGallery();

  // Open the detail takeover for a block.
  std::function<void(const std::string& blockId)> onOpenBlock;
  // Launch the Select flow: `insertBlockId` adds, a tone block's id swaps.
  std::function<void(ChainSide side, const std::string& targetBlockId)> onSelectTone;

  // The lane: the main screen's scroller.
  DragScroller& scroller() { return *scroller_; }

  void paint(juce::Graphics& g) override;
  void paintOverChildren(juce::Graphics& g) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress& key) override;

  // TileDragHost
  void tileDragStart(GalleryTile& tile, const juce::MouseEvent& e) override;
  void tileDragMove(const juce::MouseEvent& e) override;
  void tileDragEnd(const juce::MouseEvent& e) override;
  bool tileKey(GalleryTile& tile, const juce::KeyPress& key) override;

private:
  class Ghost;
  class Column;
  struct Lanes {
    std::vector<ChainItem> left, right;
    std::vector<ChainItem>& of(ChainSide side) { return side == ChainSide::left ? left : right; }
    const std::vector<ChainItem>& of(ChainSide side) const {
      return side == ChainSide::left ? left : right;
    }
  };
  // Id of the ⌥-duplicate stand-in: the inert copy of the dragged block that
  // holds its home slot while the standard drag machinery runs untouched.
  static constexpr const char* kStandInId = "__duplicate-stand-in__";

  void chainChanged(const ChainState& state) override;
  void syncFromNative();
  void applyLanes();
  void layoutColumn();
  GalleryLane& lane(ChainSide side) { return side == ChainSide::left ? left_ : right_; }
  std::optional<ChainSide> laneOf(const Lanes& lanes, const std::string& id) const;
  const ChainItem* itemIn(const Lanes& lanes, const std::string& id) const;
  bool stereo() const { return services_.chain.state().chainRight.has_value(); }
  int tileSize() const;

  // Drag machinery (pointer and keyboard share it).
  void beginSort(GalleryTile& tile, bool pointer);
  // After a lane rebuild: hide the pointer-dragged tile's slot (the ghost
  // travels instead) or dim the keyboard-sorted tile in place.
  void markActive();
  void setDuplicateStandIn(bool on);
  void moveActiveTo(ChainSide side, int index);
  void commitSort();
  void cancelSort();
  void finishSort();
  void saveScroll();

  Services& services_;
  Lanes native_, lanes_;
  bool dragging_ = false;
  std::string activeId_;
  ChainSide originSide_ = ChainSide::left;
  bool duplicating_ = false;
  bool keyboardSort_ = false;

  StereoPanRail rail_;
  std::unique_ptr<DragScroller> scroller_;
  std::unique_ptr<Column> column_;
  bool restorePending_ = true;
  GalleryLane left_, right_;
  std::unique_ptr<Ghost> ghost_;
  juce::Point<int> grabOffset_;
};

}  // namespace t3k::ui
