// The top bar's audition loop control: one 28px
// button that is the whole pedal. Click is the tap (arm, start, close,
// stop, play again), right-click (touch and hold) clears. The glyph says
// the state and a ring around it is the loop position, the way a looper
// pedal's LED ring does: a hollow circle while empty or armed (red once
// armed), a red dot with the ring filling as the take records, a play
// triangle with the ring sweeping while the loop plays, dimmed and frozen
// while stopped. Brand colours as the tuner uses them: red for record,
// blue for "running". Repaints only itself, on the feed's change.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "services/LoopFeed.h"
#include "widgets/Clickable.h"

namespace t3k::ui {

class LoopButton : public Clickable, private LoopFeed::Listener {
public:
  static constexpr int kBox = 28;
  static constexpr float kRingRadius = 10.0f;
  static constexpr float kRingStroke = 1.5f;
  static constexpr float kArcStroke = 2.0f;
  static constexpr float kDotRadius = 4.0f;
  static constexpr float kPlaySize = 9.0f;

  explicit LoopButton(LoopFeed& feed);
  ~LoopButton() override;

  void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

protected:
  void clicked(const juce::ModifierKeys& modifiers) override;

private:
  void loopChanged(const LoopFeed::State& state) override;
  // Help copy (and so the screen-reader name) per phase.
  void sync();

  LoopFeed& feed_;
};

}  // namespace t3k::ui
