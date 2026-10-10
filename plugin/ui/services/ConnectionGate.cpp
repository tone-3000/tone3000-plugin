#include "ConnectionGate.h"

namespace t3k::ui {

ConnectionGate::ConnectionGate(ToneSession& session) : session_(session) {}

ConnectionGate::~ConnectionGate() = default;

void ConnectionGate::requireConnection(std::function<void()> action) {
  if (!session_.online()) {
    pendingAction_ = std::move(action);
    setProblem(Problem::offline);
    return;
  }
  pendingAction_ = nullptr;
  if (action) action();
  verifyInBackground(false);
}

void ConnectionGate::retry() {
  if (pendingAction_) {
    if (!session_.online()) return;
    auto action = std::move(pendingAction_);
    pendingAction_ = nullptr;
    setProblem(std::nullopt);
    action();
    verifyInBackground(false);
    return;
  }
  verifyInBackground(true);
}

void ConnectionGate::dismiss() {
  pendingAction_ = nullptr;
  setProblem(std::nullopt);
}

void ConnectionGate::verifyInBackground(bool force) {
  if (probing_) return;
  if (!force && juce::Time::currentTimeMillis() - lastProbeAtMs_ < kProbeTtlMs) return;
  probing_ = true;
  probe(false);
}

void ConnectionGate::probe(bool confirming) {
  session_.probeSecureConnection(scope_.wrap([this, confirming](ToneSession::Probe result) {
    if (result == ToneSession::Probe::insecure && !confirming) {
      // A first failure is re-checked before anything surfaces.
      confirmDelay_.start(kConfirmDelayMs, [this] { probe(true); });
      return;
    }
    settle(result);
  }));
}

void ConnectionGate::settle(ToneSession::Probe result) {
  probing_ = false;
  lastProbeAtMs_ = juce::Time::currentTimeMillis();
  if (result == ToneSession::Probe::ok) {
    // Auto-heal: never leave a stale warning up once TLS works again.
    if (problem_ == Problem::insecure) setProblem(std::nullopt);
  } else if (result == ToneSession::Probe::insecure && session_.online()) {
    // Don't replace an open offline modal (and its queued action).
    if (!problem_) setProblem(Problem::insecure);
  }
  // Inconclusive results stay silent: no evidence, no alarm.
}

void ConnectionGate::setProblem(std::optional<Problem> problem) {
  if (problem == problem_) return;
  problem_ = problem;
  listeners_.call([](Listener& l) { l.connectionProblemChanged(); });
}

}  // namespace t3k::ui
