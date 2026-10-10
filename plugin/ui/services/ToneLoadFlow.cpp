#include "ToneLoadFlow.h"

#include <utility>

namespace t3k::ui {

ToneLoadFlow::ToneLoadFlow(ChainStore& chain, ConnectionGate& connection, ToneSession& session)
    : chain_(chain), connection_(connection), session_(session) {
  session_.onToneSelected = [this](const Tone& tone) { toneSelected(tone); };
}

ToneLoadFlow::~ToneLoadFlow() { session_.onToneSelected = nullptr; }

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
// at the remembered insert slot.
void ToneLoadFlow::toneSelected(const Tone& tone) {
  if (tone.models.empty()) {
    DBG("Tone has no models");
    return;
  }
  // Consume the pending targets up front so they can never leak into a
  // later selection. (Each flow clears the other's before starting.)
  const auto swapId = std::exchange(pendingSwapId_, {});
  const auto insertId = std::exchange(pendingInsertId_, {});
  const auto toneJson = tone.toJson();
  if (onShowBrowser) onShowBrowser(false);

  if (!swapId.empty()) {
    if (chain_.swapTone(swapId, toneJson)) return;
    DBG("Swap target no longer exists; adding tone as a new block");
  }
  if (chain_.loadTone(toneJson, insertId).empty()) DBG("Failed to load tone");
}

void ToneLoadFlow::clearPendingTargets() {
  pendingSwapId_.clear();
  pendingInsertId_.clear();
}

}  // namespace t3k::ui
