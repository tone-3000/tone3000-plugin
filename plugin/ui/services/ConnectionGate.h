// First line of defence for network-dependent actions (add / swap / login),
// the port of useConnectionGate.ts:
//
//   services.connection.requireConnection([&] { openToneBrowser(); });
//
// requireConnection never blocks. With no network interface up the offline
// modal opens and the action is queued for "Try again" (instant, no probe).
// Otherwise the action runs at once and a throttled background probe checks
// HTTPS to TONE3000 on the side; nothing ever waits on a probe.
//
// The insecure modal is diagnostic, not a gate: it appears only after two
// consecutive network-layer failures while the OS still reports a
// connection, by which point the triggering action has already failed on
// its own recovery paths and the modal explains why (usually a wrong system
// clock). Timeouts are inconclusive and never alarm; a later successful
// probe (or retry) closes the modal, so a blip can't leave a stale warning.
#pragma once

#include <juce_events/juce_events.h>

#include <functional>
#include <optional>

#include "ToneSession.h"
#include "core/AsyncScope.h"
#include "core/DelayedCall.h"

namespace t3k::ui {

class ConnectionGate {
public:
  enum class Problem { offline, insecure };

  struct Listener {
    virtual ~Listener() = default;
    virtual void connectionProblemChanged() = 0;
  };

  // Minimum gap between background probes; retry bypasses it.
  static constexpr juce::int64 kProbeTtlMs = 30'000;
  // Gap before the confirmation probe: one transient failure (DAW startup
  // contention, Wi-Fi renegotiating after wake) must not raise the alarm.
  static constexpr int kConfirmDelayMs = 2000;

  explicit ConnectionGate(ToneSession& session);
  ~ConnectionGate();

  void requireConnection(std::function<void()> action);
  const std::optional<Problem>& problem() const { return problem_; }
  // Offline: re-check the OS flag and release the queued action. Insecure:
  // force a fresh probe; auto-heal closes the modal on success.
  void retry();
  void dismiss();

  void addListener(Listener* l) { listeners_.add(l); }
  void removeListener(Listener* l) { listeners_.remove(l); }

private:
  void verifyInBackground(bool force);
  void probe(bool confirming);
  void settle(ToneSession::Probe result);
  void setProblem(std::optional<Problem> problem);

  ToneSession& session_;
  std::optional<Problem> problem_;
  std::function<void()> pendingAction_;  // offline modal only
  bool probing_ = false;
  juce::int64 lastProbeAtMs_ = 0;
  DelayedCall confirmDelay_;
  AsyncScope scope_;  // the editor can close with a probe in flight
  juce::ListenerList<Listener> listeners_;
};

}  // namespace t3k::ui
