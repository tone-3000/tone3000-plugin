// The faceplate's transpose group: the semitone knob and its power switch,
// plus the advanced deck (TransposeDeckPanel). Same shape and gestures as
// the gate group to its left: the knob dims and goes inert while the effect
// is off, the power stays bright; right-click anywhere on the group
// (Ctrl-click on macOS, touch-and-hold on the knob) opens the deck above the
// plate; Alt/Option-click on the knob resets the semitones and the deck
// together. Off is the default: the power is what adds latency.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "TransposeDeckPanel.h"
#include "services/Services.h"
#include "widgets/DimGroup.h"
#include "widgets/ParamControls.h"
#include "widgets/SecondaryPress.h"

namespace t3k::ui {

class TransposeGroup : public juce::Component, public SecondaryPressTarget {
public:
  // "Transpose" is wider than a 36px knob, so the knob takes a deck-style
  // column and its label overflows the face symmetrically; the power sits
  // the plate's knob-to-companion gap off the face, as it does for Gate.
  static constexpr int kColumnWidth = 64;
  static constexpr int kFaceInset = (kColumnWidth - theme::kKnobSizeSecondary) / 2;
  static constexpr int kWidth = kFaceInset + theme::kKnobSizeSecondary + 10 + theme::kIconBoxSize;
  static int height() { return Knob::heightFor(theme::kKnobSizeSecondary); }

  explicit TransposeGroup(Services& services);
  ~TransposeGroup() override;

  void resized() override;
  void mouseDown(const juce::MouseEvent& e) override;
  void secondaryPress(const juce::MouseEvent&) override { toggleDeck(); }

private:
  void toggleDeck();

  Services& services_;
  DimGroup dim_;
  ParamKnob semitones_;
  ParamPowerButton power_;
  TransposeDeckPanel deck_;
};

}  // namespace t3k::ui
