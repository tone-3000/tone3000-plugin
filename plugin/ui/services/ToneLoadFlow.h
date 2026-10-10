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
  // Abandon the pending target (browser closed without picking, logout,
  // preset load).
  void clearPendingTargets();

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
};

}  // namespace t3k::ui
