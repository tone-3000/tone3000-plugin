// The detail takeover (ChainView.tsx's detail branch + ChainBlock.tsx's
// outer column): ← BLOCK above the bordered card, centred in the chain
// area under the shared 24px pad. The tone / EQ views sit fixed; the info
// view moves the pads into a scrolling column so ← BLOCK and the card can
// reach the plugin header and faceplate. Follows the block through chain
// resyncs and falls back to the gallery when it vanishes (undo, trash).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>

#include "BlockCard.h"
#include "widgets/BackLink.h"
#include "widgets/DragScroller.h"
#include "services/ChainStore.h"
#include "services/Services.h"

namespace t3k::ui {

class BlockDetail : public juce::Component, private ChainStore::Listener, private juce::ComponentListener {
public:
  static constexpr int kPadY = 24;
  static constexpr int kBackGap = 16;

  BlockDetail(Services& services, const std::string& blockId);
  ~BlockDetail() override;


  // ← BLOCK, or the block disappeared underneath us.
  std::function<void()> onBack;
  // The card's ⇄: swap this block's tone.
  std::function<void(const std::string& blockId)> onSwap;

  // The column: the screen's main scroller while the detail is up.
  DragScroller& scroller() { return *scroller_; }

  void resized() override;

private:

  void chainChanged(const ChainState& state) override;
  // The card grows on its own (info fetch, MORE); the column follows.
  void componentMovedOrResized(juce::Component&, bool, bool wasResized) override;
  void sync();
  void layout();

  Services& services_;
  std::string blockId_;
  std::unique_ptr<DragScroller> scroller_;
  juce::Component column_;
  std::unique_ptr<BackLink> back_;
  std::unique_ptr<BlockCard> card_;
  bool infoOpen_ = false;
};

}  // namespace t3k::ui
