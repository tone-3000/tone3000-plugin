// The "get a tone into the chain" flow (port of useToneLoadFlow.ts): the
// add (+) and swap entry points that open the tone browser, and the landing
// that routes a resolved tone into the remembered slot or block. The web
// parked the pending targets in sessionStorage to survive the OAuth
// redirect; the process no longer reloads, so they are fields here.
//
// Audition: while the loop plays, the player's
// hands are free, so a pick previews instead of landing for good. The
// first preview opens a chain preview session (no history until it ends);
// the tone goes into the target through the ordinary load / swap, the
// target stays pending so the next pick replaces the same block, and the
// browser stays open. Picking the previewed tone again keeps it (one undo
// step for the whole session); ← / Escape / closing the browser put the
// chain back as it was. With the loop stopped, a pick lands as it always
// did, and closes any open session as part of that landing.
#pragma once

#include <functional>
#include <string>

#include "ChainStore.h"
#include "ConnectionGate.h"
#include "LoopFeed.h"
#include "ToneSession.h"

namespace t3k::ui {

class ToneLoadFlow : private ChainStore::Listener {
public:
  ToneLoadFlow(ChainStore& chain, ConnectionGate& connection, ToneSession& session, LoopFeed& loop);
  ~ToneLoadFlow() override;

  // Open (true) or close (false) the in-plugin tone browser.
  std::function<void(bool show)> onShowBrowser;

  // The gallery's + / swap and the detail card's ⇄: `targetId` is an insert
  // slot (add there) or a tone block (replace it); both pass the connection
  // gate first. In stereo the active side follows the tapped lane, the
  // fallback for when the slot id goes stale mid-flow.
  void select(ChainSide side, const std::string& targetId);
  // Abandon the pending target (browser closed without picking, logout,
  // preset load). An open audition is cancelled: the chain goes back.
  void clearPendingTargets();

  // The audition: the tone previewed in the target (0 = none), keep it,
  // and the hook the browser uses to mark the card and pin the hint.
  int previewToneId() const { return previewToneId_; }
  bool auditioning() const { return previewToneId_ != 0; }
  void commitPreview();
  std::function<void()> onAuditionChanged;

private:
  void addModel(ChainSide side, const std::string& insertBlockId);
  void swapBlock(const std::string& blockId);
  // A resolved tone landed (a browser pick).
  void toneSelected(const Tone& tone);

  void setPreviewToneId(int toneId);
  // The backend can end a session on its own (a host state load replaces
  // the chain); the audition's pending target and card go with it.
  void chainChanged(const ChainState& state) override;

  ChainStore& chain_;
  ConnectionGate& connection_;
  ToneSession& session_;
  LoopFeed& loop_;
  std::string pendingSwapId_;
  std::string pendingInsertId_;
  int previewToneId_ = 0;
};

}  // namespace t3k::ui
