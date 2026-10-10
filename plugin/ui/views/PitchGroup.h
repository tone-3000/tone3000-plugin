// The faceplate's pitch group: the semitone knob and its power switch, plus
// the advanced deck (PitchDeckPanel). Same shape and gestures as the gate
// group to its left: the knob dims and goes inert while the effect is off,
// the power stays bright; right-click anywhere on the group (Ctrl-click on
// macOS, touch-and-hold on the knob) opens the deck above the plate;
// Alt/Option-click on the knob resets the semitones and the deck together.
// Off is the default: the power is what adds latency. The knob covers ±24
// semitones and detents to whole ones while the deck's STEP is on (a
// transpose); with STEP off it sweeps (Shift-drag: fine) like a whammy
// pedal, which is what a MIDI expression pedal mapped to it gets.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PitchDeckPanel.h"
#include "services/Services.h"
#include "widgets/DimGroup.h"
#include "widgets/ParamControls.h"
#include "widgets/SecondaryPress.h"

namespace t3k::ui {

class PitchGroup : public juce::Component, public SecondaryPressTarget {
public:
  // Knob + the plate's knob-to-companion gap + the power box, like Gate.
  static constexpr int kWidth = theme::kKnobSizeSecondary + 10 + theme::kIconBoxSize;
  static int height() { return Knob::heightFor(theme::kKnobSizeSecondary); }

  explicit PitchGroup(Services& services);
  ~PitchGroup() override;

  void resized() override;
  // The plate hides the group (Show Pitch Shift view setting): its deck goes too.
  void visibilityChanged() override;
  void mouseDown(const juce::MouseEvent& e) override;
  void secondaryPress(const juce::MouseEvent&) override { toggleDeck(); }

private:
  void toggleDeck();
  void syncStep();

  Services& services_;
  DimGroup dim_;
  ParamKnob semitones_;
  ParamPowerButton power_;
  // The deck's STEP toggle decides whether the knob detents (see syncStep).
  ParamBinding step_;
  PitchDeckPanel deck_;
};

}  // namespace t3k::ui
