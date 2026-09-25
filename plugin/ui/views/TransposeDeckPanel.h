// The transpose group's advanced panel (right-click the group; touch-and-hold
// the knob): the three settings behind the semitone knob, mirroring
// Transpose::Params:
//  - Fine: trims the shift by ±50 cents, for songs tuned between semitones.
//  - Tonality: the frequency above which the shift is an offset instead of
//    a ratio (1-20 kHz, log), so pick attack and fret noise keep their
//    brightness while the notes move. Off (the top) is a pure shift.
//  - Latency: the pitch analysis window, three detents (30/60/100 ms). It is
//    all the latency the effect adds; longer keeps chords steadier.
// Plain knobs, no power switches, same footprint as the gate deck so the two
// read as one family.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "services/Services.h"
#include "widgets/ParamControls.h"
#include "widgets/Popover.h"

namespace t3k::ui {

class TransposeDeckPanel : public Popover {
public:
  static constexpr int kWidth = 262;
  static constexpr int kHeight = 85;
  // Gap between the panel's bottom edge and its anchor's top.
  static constexpr int kGap = 6;

  explicit TransposeDeckPanel(Services& services);

  // Restores the whole deck to its defaults (Alt/Option-click on the
  // Transpose knob resets the effect, not just the semitones).
  static void resetDeck(Backend& backend);

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  ParamKnob fine_, tonality_, window_;
};

}  // namespace t3k::ui
