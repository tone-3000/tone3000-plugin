#include "ToneLoadFlow.h"

#include <utility>

namespace t3k::ui {

ToneLoadFlow::ToneLoadFlow(ChainStore& chain, ConnectionGate& connection, ToneSession& session, LoopFeed& loop)
    : chain_(chain), connection_(connection), session_(session), loop_(loop) {
  session_.onToneSelected = [this](const Tone& tone) { toneSelected(tone); };
  chain_.addListener(this);
}

ToneLoadFlow::~ToneLoadFlow() {
  chain_.removeListener(this);
  session_.onToneSelected = nullptr;
}

void ToneLoadFlow::chainChanged(const ChainState& state) {
  if (previewToneId_ == 0 || state.previewing) return;
  pendingSwapId_.clear();
  pendingInsertId_.clear();
  setPreviewToneId(0);
}

void ToneLoadFlow::select(ChainSide side, const std::string& targetId) {
  const auto* item = chain_.state().findBlock(targetId);
  if (item != nullptr && item->isTone()) swapBlock(targetId);
  else addModel(side, targetId);
}

// Add: remember the clicked insert slot, then open the browser. The active
// side goes to native state as the fallback for when the slot id goes stale,
// e.g. undone away mid-flow.
void ToneLoadFlow::addModel(ChainSide side, const std::string& insertBlockId) {
  connection_.requireConnection([this, side, insertBlockId] {
    pendingSwapId_.clear();
    pendingInsertId_ = insertBlockId;
    if (chain_.state().stereoEnabled) chain_.setActiveSide(side);
    if (onShowBrowser) onShowBrowser(true);
  });
}

// Swap: remember the target block, then run the same browse flow as add. The
// pending swap id is consumed when the picked tone lands.
void ToneLoadFlow::swapBlock(const std::string& blockId) {
  connection_.requireConnection([this, blockId] {
    pendingInsertId_.clear();
    pendingSwapId_ = blockId;
    if (onShowBrowser) onShowBrowser(true);
  });
}

// If a swap was pending, replace that block in place; otherwise add the tone
// at the remembered insert slot. With the loop playing, that landing is a
// preview: the target stays pending and the browser stays open.
void ToneLoadFlow::toneSelected(const Tone& tone) {
  if (tone.models.empty()) {
    DBG("Tone has no models");
    return;
  }
  const auto toneJson = tone.toJson();

  if (loop_.playing()) {
    if (!chain_.state().previewing) chain_.beginPreview();
    if (pendingSwapId_.empty() || !chain_.swapTone(pendingSwapId_, toneJson)) {
      // The first preview adds into the slot; from then on it is the block
      // the next pick replaces.
      const auto insertId = std::exchange(pendingInsertId_, {});
      const auto blockId = chain_.loadTone(toneJson, insertId);
      if (blockId.empty()) {
        DBG("Failed to load tone");
        return;
      }
      pendingSwapId_ = blockId;
    }
    setPreviewToneId(tone.id);
    return;
  }

  // Consume the pending targets up front so they can never leak into a
  // later selection. (Each flow clears the other's before starting.)
  const auto swapId = std::exchange(pendingSwapId_, {});
  const auto insertId = std::exchange(pendingInsertId_, {});
  if (onShowBrowser) onShowBrowser(false);

  bool landed = false;
  if (!swapId.empty()) {
    landed = chain_.swapTone(swapId, toneJson);
    if (!landed) DBG("Swap target no longer exists; adding tone as a new block");
  }
  if (!landed && chain_.loadTone(toneJson, insertId).empty()) DBG("Failed to load tone");
  // A session the loop opened (then stopped) closes with this landing, so
  // the auditioned block and this pick make one undo step.
  if (chain_.state().previewing) chain_.commitPreview();
  setPreviewToneId(0);
}

void ToneLoadFlow::commitPreview() {
  if (!chain_.state().previewing) return;
  chain_.commitPreview();
  setPreviewToneId(0);
  clearPendingTargets();
  if (onShowBrowser) onShowBrowser(false);
}

void ToneLoadFlow::clearPendingTargets() {
  pendingSwapId_.clear();
  pendingInsertId_.clear();
  if (chain_.state().previewing) chain_.cancelPreview();
  setPreviewToneId(0);
}

void ToneLoadFlow::setPreviewToneId(int toneId) {
  if (toneId == previewToneId_) return;
  previewToneId_ = toneId;
  if (onAuditionChanged) onAuditionChanged();
}

}  // namespace t3k::ui
