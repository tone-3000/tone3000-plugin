// The chain area between the meters: the gallery, or the detail takeover
// for one block (ChainView.tsx's two branches). Owns the detail block id,
// remembered for the editor's lifetime so a swap from the detail view
// (which the tone browser takes over the screen for, and may bounce
// through OAuth) reopens the same card on return; cleared when the user
// backs out, so gallery-initiated swaps land on the gallery.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>

#include "block/BlockDetail.h"
#include "gallery/ChainView.h"
#include "services/Services.h"

namespace t3k::ui {

class ChainScreen : public juce::Component {
public:
  explicit ChainScreen(Services& services);
  ~ChainScreen() override;

  // Launch the Select flow (add at an insert slot, or swap a tone block).
  std::function<void(ChainSide side, const std::string& targetBlockId)> onSelectTone;

  // Preset load / reset: close the detail view and rewind the gallery.
  void returnToGallery();

  ChainView& gallery() { return gallery_; }
  // The scroller in front: the detail's column while one is open, else
  // the gallery's lane.
  DragScroller& scroller() { return detail_ != nullptr ? detail_->scroller() : gallery_.scroller(); }

  void resized() override;

private:
  void openDetail(const std::string& blockId);
  void closeDetail();

  Services& services_;
  ChainView gallery_;
  std::unique_ptr<BlockDetail> detail_;
};

}  // namespace t3k::ui
