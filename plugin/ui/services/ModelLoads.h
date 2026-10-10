// Model downloads that need a fresh TONE3000 token first (Plugin.tsx
// handleSwitchModel / handleRetryLoad). Native fetches model files itself
// with a Bearer header, so before it starts a download the session refreshes
// the token when it is near expiry.
#pragma once

#include <string>

#include "ChainStore.h"
#include "ToneSession.h"
#include "model/Tone.h"

namespace t3k::ui {

class ModelLoads {
public:
  ModelLoads(ChainStore& chain, ToneSession& session) : chain_(chain), session_(session) {}

  // Retry a failed download. Signed in, the token is refreshed first (the
  // block may have waited long enough for the last pushed token to expire);
  // signed out we retry anyway, since public model URLs work anonymously.
  void retry(const std::string& blockId);

  // Switch to another model of the block's tone. Catalog models need a live
  // session (a rejected refresh clears the tokens and the picker disables
  // itself); local `file:` models load straight from the stash. `done`
  // reports whether native accepted the switch.
  void switchModel(const std::string& blockId, const Model& model, std::function<void(bool)> done);

private:
  ChainStore& chain_;
  ToneSession& session_;
};

}  // namespace t3k::ui
