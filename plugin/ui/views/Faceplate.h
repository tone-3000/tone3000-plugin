// Bottom faceplate (port of Faceplate.tsx): main input/output gain, the gate
// group (GateGroup, with its advanced deck) and the global 3-band tone
// stack, the stereo-image slot (Spread in mono chain mode, Align in stereo)
// and, when they apply, the input-mode button, the output balance knob and
// auto balance. Gate + tone stack carry power switches (APVTS bools, so they
// automate and persist like everything else).
//
// Five peer groups share the plate width (CSS space-between); every group
// has a fixed footprint with inactive companions hidden in place, so toggling
// stereo / spread never shifts the plate.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "GateGroup.h"
#include "StereoImageGroup.h"
#include "core/Design.h"
#include "services/Services.h"
#include "widgets/ChromeIconButton.h"
#include "widgets/DimGroup.h"
#include "widgets/ParamControls.h"

namespace t3k::ui {

class Faceplate : public juce::Component,
                  private ChainStore::Listener,
                  private AutoMeasure::Listener {
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
  void syncFlags();

  Services& services_;

  ParamKnob input_;
  std::unique_ptr<InputModeButton> inputMode_;

  GateGroup gate_;

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
