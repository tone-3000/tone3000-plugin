// The pitch group's advanced panel (right-click the group; touch-and-hold
// the knob): the settings behind the semitone knob.
//  - STEP: on (the default), the knob detents to whole semitones and the
//    processor rounds the shift, so automation snaps too (a transpose);
//    off, the knob sweeps smoothly (Shift-drag for fine control) like a
//    whammy pedal. Same chrome as the EQ card's PRE toggle.
//  - Tonality: the frequency above which the input bypasses the shifter
//    (1-20 kHz, log), so pick attack and fret noise keep their brightness
//    while the notes move. Off (the top) is a pure shift.
//  - Buffer: the engine's delay buffer, four detents (20/30/40/60 ms). The
//    lowest note it holds a full period of, and how often it splices; the
//    latency it reports to the host is half of it plus 1 ms. Attacks always
//    pass in a few ms whatever the buffer.
//  - Mix: the dry/shifted blend, 0-100% shifted (the default is 100). Lower
//    it to mix the dry signal back in; the dry is held by the floor, the
//    same alignment the tonality band uses, so it lands with the attacks.
// Plain controls, no power switches, same footprint as the gate deck plus
// Mix's column so the two read as one family.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "services/Services.h"
#include "widgets/ParamControls.h"
#include "widgets/Popover.h"

namespace t3k::ui {

class PitchDeckPanel : public Popover {
public:
  // Four control columns (STEP, Tonality, Buffer, Mix): kWidth is the
  // padding plus the columns and gaps, inside the panel's 1px border.
  static constexpr int kWidth = 344;
  static constexpr int kHeight = 85;
  // Gap between the panel's bottom edge and its anchor's top.
  static constexpr int kGap = 6;

  explicit PitchDeckPanel(Services& services);

  // Restores the whole deck to its defaults (Alt/Option-click on the
  // Pitch knob resets the effect, not just the semitones).
  static void resetDeck(Backend& backend);

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  ParamTextToggle step_;
  ParamKnob tonality_, window_, mix_;
};

}  // namespace t3k::ui
