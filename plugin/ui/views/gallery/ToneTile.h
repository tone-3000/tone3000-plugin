// Gallery view of a chain block (GalleryBlock.tsx): a square tone image
// with quick actions (power / swap / trash) revealed along the top edge on
// hover, the clip latch bottom-right and the inset energy glow. Loading
// dots while the model downloads, a Retry badge if the download failed.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "GalleryTile.h"
#include "core/AlphaTween.h"
#include "core/BusyGrace.h"
#include "model/ChainState.h"
#include "widgets/BlockLed.h"
#include "widgets/ChromeIconButton.h"
#include "widgets/EnergyGlow.h"
#include "widgets/LoadingDots.h"
#include "widgets/RetryLoadBadge.h"
#include "widgets/ToneImage.h"

namespace t3k::ui {

class ToneTile : public GalleryTile, private Pointer::Listener {
public:
  static constexpr int kChromeHeight = 32;
  static constexpr int kChromePad = 4;
  static constexpr int kChromeGap = 16;
  static constexpr int kLedInset = 8;
  static constexpr int kGlyphSize = 64;

  ToneTile(Services& services, const ChainItem& block, int size);
  ~ToneTile() override;

  // A fresh snapshot of the same block (resync).
  void setBlock(const ChainItem& block);
  const ChainItem& block() const { return block_; }

  std::function<void(const std::string& blockId)> onOpen;
  // Swap: launch the Select flow to replace this block's tone in place.
  std::function<void(const std::string& blockId)> onSwap;
  // Add Before / After: the Select flow for a new slot beside this block
  // (target is slotBefore / slotAfter of it).
  std::function<void(const std::string& target)> onAddBeside;

  void paint(juce::Graphics& g) override;
  void paintOverChildren(juce::Graphics& g) override;
  void resized() override;

protected:
  void open() override;
  std::vector<ContextMenu::Item> menuItems() override;
  void dropArmedChanged(bool armed) override;
  bool takesEdgeDrops() const override { return true; }
  void travellingChanged(bool travelling) override;

private:
  // Reveals the action strip while the pointer is anywhere over the tile
  // (CSS :hover on the face; pinned while travelling, and whenever the
  // pointer is a finger, which can't hover: Services::pointer).
  // Hover is read off the events themselves, not the OS pointer, so the
  // children's enter/exit (which JUCE reports as an exit from the tile) and
  // the testbed's synthesized pointer both resolve the same way.
  class HoverWatcher : public juce::MouseListener {
  public:
    explicit HoverWatcher(ToneTile& tile) : tile_(tile) {}
    void mouseEnter(const juce::MouseEvent& e) override { tile_.pointerMoved(e, false); }
    void mouseMove(const juce::MouseEvent& e) override { tile_.pointerMoved(e, false); }
    void mouseExit(const juce::MouseEvent& e) override { tile_.pointerMoved(e, true); }

  private:
    ToneTile& tile_;
  };

  void pointerMoved(const juce::MouseEvent& e, bool leaving);
  void setHovered(bool hovered);
  void pointerChanged() override { setHovered(hovered_); }
  void syncState();
  void togglePower();

  ChainItem block_;
  bool enabled_ = true;  // optimistic; native converges via the resync
  bool hovered_ = false;

  ToneImage image_;
  AlphaTween imageFade_{image_};
  BusyGrace busyGrace_;
  LoadingDots dots_;
  RetryLoadBadge retry_;
  // The translucent strip under the quick actions so they read on any art;
  // strip + buttons fade together (opacity only, never a layout change).
  class Strip : public juce::Component {
  public:
    Strip() { setInterceptsMouseClicks(false, true); }
    void paint(juce::Graphics& g) override;
  };
  Strip chrome_;
  ChromeIconButton power_, swap_, remove_;
  EnergyGlow glow_;
  // The LED shows itself while the clip is latched; the slot hides while
  // the tile is a drop target.
  juce::Component ledSlot_;
  BlockLed led_;
  HoverWatcher hover_{*this};
};

}  // namespace t3k::ui
