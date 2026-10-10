// Connection gate modal (ConnectionModal.tsx), two variants:
//
// - offline: a network-dependent action was attempted with no connection
//   at all. The action is queued and "Try again" re-runs it.
// - insecure: a background probe confirmed (twice) that HTTPS to TONE3000
//   fails while the OS reports a connection (usually a wrong system clock),
//   so we offer a jump to the OS date & time settings where the platform
//   has one. Purely diagnostic: the triggering action already ran and
//   failed on its own recovery paths.
//
// Stacks above the OAuth overlay and the update notice so the diagnosis
// always shows.
#pragma once

#include <functional>

#include "services/ConnectionGate.h"
#include "widgets/ModalLayer.h"
#include "widgets/ScrimMessage.h"

namespace t3k::ui {

class ConnectionModal : public ModalLayer {
public:
  static constexpr int kBodyMaxW = 400;

  ConnectionModal(Backdrop backdrop, ConnectionGate::Problem problem, bool canOpenDateTimeSettings);

  std::function<void()> onRetry, onDismiss, onOpenDateTimeSettings;

private:
  ScrimMessage body_;
};

}  // namespace t3k::ui
