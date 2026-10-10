// One lane of tiles over its ghost rail (GalleryLane.tsx): plus circles
// joined by connector lines sit behind the (opaque) tiles and show through
// a slot vacated mid-drag; only the line runs inside the gaps are visible
// otherwise. In stereo, hover-revealed branch dots live on the connector
// gaps (set / re-point the branch; the active tap gap's dot clears it).
// Tiles are keyed by block id and reused across resyncs.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "AddTile.h"
#include "ToneTile.h"
#include "model/ChainState.h"
#include "services/Services.h"

namespace t3k::ui {

class GalleryLane : public juce::Component {
public:
  GalleryLane(Services& services, ChainSide side);
  ~GalleryLane() override;

  ChainSide side() const { return side_; }
  int tileSize() const { return tile_; }
  const std::vector<ChainItem>& items() const { return items_; }

  // Rebuild from a lane snapshot (native's, or the optimistic mirror).
  void setItems(const std::vector<ChainItem>& items, int tileSize);
  // Stereo: branch affordances on the gaps. `interactive` is false while a
  // drag is in flight (gap hit targets would fight drops).
  void setBranch(bool stereo, const std::optional<ChainBranch>& branch, bool interactive);
  void setCanPaste(bool canPaste);
  // The travelling tile's slot: hidden so the rail shows through.
  void setPlaceholder(const std::string& blockId);

  // Hand a tile between lanes: the leaving lane releases it (still parented
  // there until adopted), the entering lane reparents it and reuses it on
  // its next setItems. Mid-drag the dragged tile is the very component the
  // pointer's events are addressed to, so it has to survive the crossing.
  std::unique_ptr<GalleryTile> releaseTile(const std::string& blockId);
  void adoptTile(std::unique_ptr<GalleryTile> tile);

  GalleryTile* tileFor(const std::string& blockId) const;
  int indexOf(const std::string& blockId) const;

  std::function<void(const std::string& blockId)> onOpen, onSwap, onAdd;
  std::function<void(int index)> onPaste;
  std::function<void(const std::string& afterBlockId)> onSetBranch;
  std::function<void()> onClearBranch;

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  class BranchGap;

  void rebuildBranchGaps();
  // Point a tile's callbacks at this lane (re-done on adoption).
  void wire(GalleryTile& tile);

  Services& services_;
  ChainSide side_;
  int tile_ = 0;
  std::vector<ChainItem> items_;
  std::map<std::string, std::unique_ptr<GalleryTile>> tiles_;
  std::string placeholder_;

  bool stereo_ = false;
  std::optional<ChainBranch> branch_;
  bool branchInteractive_ = false;
  std::vector<std::unique_ptr<BranchGap>> gaps_;
  bool canPaste_ = false;
};

}  // namespace t3k::ui
