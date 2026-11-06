// The metronome view's pendulum: given where the audio click is, in beats, one
// side swells from dark to full exactly on the click and falls away; the next
// beat swells the other side. Dark until it is fed a position.
#pragma once

#include <cmath>

namespace t3k::ui {

// Beats the audio counts before its first click. The UI can't include the
// audio class, so ProcessorBackend asserts the two agree.
inline constexpr double kMetronomeLeadInBeats = 0.5;

class BeatPulse {
public:
  void set(double beats) {
    swing_ = beats + kMetronomeLeadInBeats;  // a swing starts as the audio counts in
    active_ = true;
  }
  void reset() { active_ = false; }

  // 0 half a beat before a click, 1 on it, 0 half a beat after.
  float level() const {
    if (!active_) return 0.0f;
    const double progress = swing_ - std::floor(swing_);
    return 1.0f - std::abs(2.0f * static_cast<float>(progress) - 1.0f);
  }
  bool leftSide() const { return static_cast<long long>(std::floor(swing_)) % 2 == 0; }

private:
  double swing_ = 0;
  bool active_ = false;
};

}  // namespace t3k::ui
