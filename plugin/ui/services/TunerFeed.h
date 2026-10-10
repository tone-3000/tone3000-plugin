// The tuner's pitch readout (TunerView.tsx's polling half). Constructing a
// feed switches the native pitch detector on (it costs audio-thread work, so
// it only runs while the tuner screen is up); destroying it switches it off.
// Polls on the UiClock tick, smooths the cents lightly so the display
// doesn't jitter, and holds the last note on screen briefly after the
// signal decays.
#pragma once

#include <functional>

#include "UiClock.h"
#include "backend/Backend.h"

namespace t3k::ui {

class TunerFeed : private UiClock::Listener {
public:
  static constexpr int kHoldMs = 900;
  static constexpr float kMinConfidence = 0.5f;
  // Per-tick weight of the new cents reading: a ~100 ms time constant at
  // UiClock::kHz, enough to hide detector jitter without lagging a bend.
  static constexpr float kCentsSmoothing = 0.3f;

  struct State {
    bool hasSignal = false;
    juce::String note;  // last detected, kept while the readout fades
    float cents = 0;    // smoothed
    float frequency = 0;
    bool operator==(const State& o) const {
      return hasSignal == o.hasSignal && note == o.note && juce::exactlyEqual(cents, o.cents) &&
             juce::exactlyEqual(frequency, o.frequency);
    }
  };

  TunerFeed(Backend& backend, UiClock& clock);
  ~TunerFeed() override;

  const State& state() const { return state_; }
  std::function<void()> onChange;

private:
  void tick() override;

  Backend& backend_;
  UiClock& clock_;
  State state_;
  juce::int64 holdUntilMs_ = 0;
};

}  // namespace t3k::ui
