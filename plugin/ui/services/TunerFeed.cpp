#include "TunerFeed.h"

#include "core/Pitch.h"
#include "model/ChainState.h"

namespace t3k::ui {

TunerFeed::TunerFeed(Backend& backend, UiClock& clock) : backend_(backend), clock_(clock) {
  backend_.setTunerEnabled(true);
  tick();
  clock_.addListener(this);
}

TunerFeed::~TunerFeed() {
  clock_.removeListener(this);
  backend_.setTunerEnabled(false);
}

void TunerFeed::tick() {
  const auto reading = TunerReading::parse(backend_.getTunerReading());
  const auto hz = static_cast<float>(reading.frequency);
  const auto confidence = static_cast<float>(reading.confidence);
  const auto now = juce::Time::currentTimeMillis();

  State next = state_;
  if (hz > 0 && confidence > kMinConfidence) {
    const auto note = pitch::fromFrequency(hz);
    next.cents = state_.cents * (1.0f - kCentsSmoothing) + note.cents * kCentsSmoothing;
    next.note = note.name;
    next.frequency = hz;
    next.hasSignal = true;
    holdUntilMs_ = now + kHoldMs;
  } else if (state_.hasSignal && now >= holdUntilMs_) {
    next.hasSignal = false;
  }
  if (next == state_) return;
  state_ = next;
  if (onChange) onChange();
}

}  // namespace t3k::ui
