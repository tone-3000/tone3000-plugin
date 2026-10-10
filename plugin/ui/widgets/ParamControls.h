// Controls bound to host parameters: the useParameter.ts + control pairings
// the faceplate, deck and block views are built from. Each owns its
// ParamBinding; writes go out as gestures, echoes come back as setValue.
#pragma once

#include "ChromeIconButton.h"
#include "ChromeTextButton.h"
#include "Knob.h"
#include "services/ParamBinding.h"

namespace t3k::ui {

// A knob driving a continuous (0..1 normalised) parameter.
class ParamKnob : public Knob {
public:
  ParamKnob(Backend& backend, const juce::String& paramId, Options options);
  ParamBinding& binding() { return binding_; }

  // Every value the knob wrote to its parameter (a linked pan mirrors it).
  std::function<void(float normalised)> onValueChange;

private:
  ParamBinding binding_;
};

// A power-tone chrome button toggling a bool parameter. `on` follows the
// parameter; a click flips it.
class ParamPowerButton : public ChromeIconButton {
public:
  ParamPowerButton(Backend& backend, const juce::String& paramId, help::Key help);
  ParamBinding& binding() { return binding_; }
  bool value() const { return binding_.boolValue(); }

  // The parameter changed (click or echo); owners dim their section here.
  std::function<void(bool on)> onValueChange;

private:
  void sync();
  ParamBinding binding_;
};

// A text chrome button (the EQ card's PRE) toggling a bool parameter: armed
// while the parameter is on; a click flips it.
class ParamTextToggle : public ChromeTextButton {
public:
  ParamTextToggle(Backend& backend, const juce::String& paramId, juce::String label, help::Key help);
  ParamBinding& binding() { return binding_; }
  bool value() const { return binding_.boolValue(); }

private:
  ParamBinding binding_;
};

}  // namespace t3k::ui
