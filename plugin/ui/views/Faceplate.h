// Bottom faceplate (port of Faceplate.tsx): main input/output gain, the gate
// and pitch groups (GateGroup / PitchGroup, each with an advanced deck) and
// the global 3-band tone stack, the stereo-image slot (Spread in mono chain
// mode, Align in stereo) and, when they apply, the input-mode button, the
// output balance knob and auto balance. Gate, pitch and tone stack carry
// power switches (APVTS bools, so they automate and persist like everything
// else).
//
// Five peer groups share the plate width (CSS space-between): input, the
// effects cluster (gate + pitch, spaced like the tone stack's knobs so they
// read as one), tone stack, image slot, output. Every group has a fixed
// footprint with inactive companions hidden in place, so toggling stereo /
// spread never shifts the plate. The effects are the exception, by design:
// Plugin Settings → Show Gate / Show Pitch Shift picks which of them the
// plate shows (gate by default, pitch hidden), a powered effect always shows
// so a preset's sound is never controlled from a hidden knob, and the plate
// re-spreads when the cluster shrinks or goes away.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "GateGroup.h"
#include "PitchGroup.h"
#include "StereoImageGroup.h"
#include "core/Design.h"
#include "services/Services.h"
#include "widgets/ChromeIconButton.h"
#include "widgets/DimGroup.h"
#include "widgets/ParamControls.h"

namespace t3k::ui {

class Faceplate : public juce::Component,
                  private ChainStore::Listener,
                  private AutoMeasure::Listener,
                  private UiPrefs::Listener {
public:
  static constexpr int kHeight = design::kPlateHeight;

  explicit Faceplate(Services& services);
  ~Faceplate() override;

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  class InputModeButton;

  void chainChanged(const ChainState&) override { syncFlags(); }
  void autoMeasureChanged() override;
  void prefChanged(const juce::String& key) override;
  void syncFlags();
  // Show / hide an effect group; a change re-spreads the plate.
  void showEffect(juce::Component& group, bool show);

  Services& services_;

  ParamKnob input_;
  std::unique_ptr<InputModeButton> inputMode_;

  // Effects cluster: each group shows while its view setting is on or its
  // power is (the bindings watch the power switches for the latter).
  GateGroup gate_;
  PitchGroup pitch_;
  ParamBinding gateEnabled_, pitchEnabled_;

  DimGroup toneDim_;
  ParamKnob bass_, middle_, treble_;
  ParamPowerButton tonePower_;

  // Stereo-image slot: Spread in mono, Align in stereo. On a mono rig only
  // Spread dims and goes inert as a whole (the hover hint says why).
  DimGroup imageDim_;
  StereoImageGroup spread_, align_;

  // [=][Bal][Output]: inactive companions stay laid out but hidden.
  ChromeIconButton autoBalance_;
  ParamKnob balance_, output_;

  // Mono-mode spread makes the balance trim audible on a stereo rig.
  ParamBinding spreadEnabled_;
};

}  // namespace t3k::ui
