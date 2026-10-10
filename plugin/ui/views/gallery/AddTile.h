// The insert slot as a dashed add tile (GalleryBlock.tsx AddTile), sortable
// so the insert point can be repositioned within its lane like any block.
// Routing lines continue the lane's connector line through to the plus
// circle. Also the drop zone for local .nam / IR .wav files.
#pragma once

#include "GalleryTile.h"

namespace t3k::ui {

class AddTile : public GalleryTile {
public:
  // Which tile edges get a routing line into the plus circle (signal-flow
  // continuation of the lane's connector lines).
  enum class Routing { none, left, right, both };
  static Routing routingFor(int index, int count);

  AddTile(Services& services, std::string blockId, int size);

  void setRouting(Routing routing);
  // Paste the copied block into this slot; unset while there's nothing valid
  // to paste (the action sheet shows Paste disabled).
  void setCanPaste(bool canPaste);

  std::function<void(const std::string& insertBlockId)> onAdd;
  std::function<void(const std::string& insertBlockId)> onPaste;

  void paint(juce::Graphics& g) override;

protected:
  void open() override;
  std::vector<ContextMenu::Item> menuItems() override;

private:
  Routing routing_ = Routing::none;
  bool canPaste_ = false;
};

}  // namespace t3k::ui
