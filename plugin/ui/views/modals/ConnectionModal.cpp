#include "ConnectionModal.h"

namespace t3k::ui {

namespace {
const char* const kOfflineCopy = "No internet connection. Connect to browse and load tones from TONE3000.";
const char* const kInsecureCopy =
    "Couldn't make a secure connection to TONE3000. If other sites work on this computer, the usual cause is a "
    "wrong date & time; a firewall, VPN, or security software can also block the plugin.";
}  // namespace

ConnectionModal::ConnectionModal(Backdrop backdrop, ConnectionGate::Problem problem, bool canOpenDateTimeSettings)
    : ModalLayer(std::move(backdrop)) {
  const bool offline = problem == ConnectionGate::Problem::offline;
  setName(offline ? "No internet connection" : "Secure connection failed");
  setTitle(getName());
  body_.setIcon(offline ? Icon::WifiOff : Icon::ShieldAlert);
  body_.setCopy(offline ? kOfflineCopy : kInsecureCopy, kBodyMaxW);
  body_.addButton("Try again", PillButton::Style::filled).onClick = [this] {
    if (onRetry) onRetry();
  };
  if (!offline && canOpenDateTimeSettings)
    body_.addButton("Date & time settings", PillButton::Style::outline).onClick = [this] {
      if (onOpenDateTimeSettings) onOpenDateTimeSettings();
    };
  body_.addButton("Dismiss", PillButton::Style::outline).onClick = [this] {
    if (onDismiss) onDismiss();
  };
  setContent(body_);
}

}  // namespace t3k::ui
