// The "get a tone into the chain" flow (port of useToneLoadFlow.ts): the
// add (+) and swap entry points that open the tone browser, and the landing
// that routes a resolved tone into the remembered slot or block. The web
// parked the pending targets in sessionStorage to survive the OAuth
// redirect; the process no longer reloads, so they are fields here.
#pragma once

#include <functional>
#include <string>

#include "ChainStore.h"
#include "ConnectionGate.h"
#include "ToneSession.h"

namespace t3k::ui {

class ToneLoadFlow {
public:
  ToneLoadFlow(ChainStore& chain, ConnectionGate& connection, ToneSession& session);
  ~ToneLoadFlow();

  // Open (true) or close (false) the in-plugin tone browser.
  std::function<void(bool show)> onShowBrowser;

  // The gallery's + / swap and the detail card's ⇄: `targetId` is an insert
  // slot (add there) or a tone block (replace it); both pass the connection
  // gate first. In stereo the active side follows the tapped lane, the
  // fallback for when the slot id goes stale mid-flow.
  void select(ChainSide side, const std::string& targetId);
  // Abandon the pending target (browser closed, logout, preset load).
  void clearPendingTargets();

  // The browser's result list around the card about to be picked (a browse
  // context, BrowseContext.h); rides along with the landing tone so the
  // block can step through it afterwards. Void for picks outside a search.
  void setPickContext(juce::var context) { pickContext_ = std::move(context); }

  // The tone block the next pick replaces: the swap target, or the block the
  // last pick added (picking again from the open browser replaces it rather
  // than piling up blocks). Empty while the next pick adds.
  const std::string& targetBlockId() const { return pendingSwapId_; }

  // A pick landed on this block (the stepper focuses it for the arrow keys).
  std::function<void(const std::string& blockId)> onToneLanded;

private:
  void addModel(ChainSide side, const std::string& insertBlockId);
  void swapBlock(const std::string& blockId);
  // A resolved tone landed (a browser pick).
  void toneSelected(const Tone& tone);

  ChainStore& chain_;
  ConnectionGate& connection_;
  ToneSession& session_;
  std::string pendingSwapId_;
  std::string pendingInsertId_;
  juce::var pickContext_;
};

}  // namespace t3k::ui
