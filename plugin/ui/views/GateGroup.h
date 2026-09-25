// The faceplate's gate group: the threshold knob and its power switch, plus
// the advanced deck (GateDeckPanel). The knob dims and goes inert while the
// gate is off; the power stays bright. Right-click anywhere on the group
// (Ctrl-click on macOS, touch-and-hold on the knob) opens the deck above the
// plate, the same gesture as the stereo-image slot; Alt/Option-click on the
// knob resets the threshold and the deck together.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "GateDeckPanel.h"
#include "services/Services.h"
#include "widgets/DimGroup.h"
#include "widgets/ParamControls.h"
#include "widgets/SecondaryPress.h"

namespace t3k::ui {

class GateGroup : public juce::Component, public SecondaryPressTarget {
public:
  // Knob + the plate's knob-to-companion gap + the power box.
  static constexpr int kWidth = theme::kKnobSizeSecondary + 10 + theme::kIconBoxSize;
  // As tall as a secondary knob column (the plate bottom-aligns it).
  static int height() { return Knob::heightFor(theme::kKnobSizeSecondary); }

  explicit GateGroup(Services& services);
  ~GateGroup() override;

  void resized() override;
  // Right-click on the group's own space toggles the deck; the knob and the
  // power button forward theirs here.
  void mouseDown(const juce::MouseEvent& e) override;
  void secondaryPress(const juce::MouseEvent&) override { toggleDeck(); }

private:
  void toggleDeck();

  Services& services_;
  DimGroup dim_;
  ParamKnob threshold_;
  ParamPowerButton power_;
  GateDeckPanel deck_;
};

}  // namespace t3k::ui
