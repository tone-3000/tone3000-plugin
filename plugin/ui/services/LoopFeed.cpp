#include "LoopFeed.h"

namespace t3k::ui {

LoopFeed::Phase LoopFeed::parsePhase(const juce::String& name) {
  if (name == "armed") return Phase::armed;
  if (name == "recording") return Phase::recording;
  if (name == "playing") return Phase::playing;
  if (name == "stopped") return Phase::stopped;
  return Phase::empty;
}

const char* LoopFeed::phaseName(Phase phase) {
  switch (phase) {
    case Phase::armed: return "armed";
    case Phase::recording: return "recording";
    case Phase::playing: return "playing";
    case Phase::stopped: return "stopped";
    case Phase::empty: break;
  }
  return "empty";
}

LoopFeed::LoopFeed(Backend& backend, UiClock& clock) : backend_(backend), clock_(clock) {
  poll();
  clock_.addListener(this);
}

LoopFeed::~LoopFeed() { clock_.removeListener(this); }

void LoopFeed::tap() { backend_.loopTap(); poll(); }

void LoopFeed::clear() { backend_.loopClear(); poll(); }

void LoopFeed::poll() {
  const auto v = backend_.getLoopState();
  State next;
  next.phase = parsePhase(v["state"].toString());
  next.position = static_cast<double>(v["position"]);
  next.lengthSeconds = static_cast<double>(v["lengthSeconds"]);
  if (next == state_) return;
  state_ = next;
  listeners_.call([this](Listener& l) { l.loopChanged(state_); });
}

}  // namespace t3k::ui
