// The gate's advanced panel (right-click the Gate group; touch-and-hold the
// knob): the three behaviours that shape how the gate sounds once it has
// decided to close, mirroring NoiseGate::Params:
//  - Release: the tail's time constant, 5-500 ms (log). Short is the tight
//    cutoff high-gain rhythm players ask for; long rides note decay.
//  - Hold: how long the gate waits below the threshold before releasing,
//    0-200 ms. Bridges the gaps in staccato and tremolo picking.
//  - Range: how deep the closed gate goes, 20-80 dB. 80 is a full mute;
//    less leaves the floor audible, a downward expander for clean tones.
// Plain knobs, no power switches: a gate without a release or a range is not
// a thing, and the defaults are the tuning the gate shipped with. Same
// footprint and geometry as the stereo-image deck so the two panels read as
// one family.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "services/Services.h"
#include "widgets/ParamControls.h"
#include "widgets/Popover.h"

namespace t3k::ui {

class GateDeckPanel : public Popover {
public:
  // Three bare knobs (no power switches), so the columns run narrower than
  // the stereo-image deck's; see the layout constants in the .cpp.
  static constexpr int kWidth = 262;
  static constexpr int kHeight = 85;
  // Gap between the panel's bottom edge and its anchor's top.
  static constexpr int kGap = 6;

  explicit GateDeckPanel(Services& services);

  // Restores the whole deck to its defaults (Alt/Option-click on the Gate
  // knob resets the gate, not just the threshold).
  static void resetDeck(Backend& backend);

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  ParamKnob release_, hold_, range_;
};

}  // namespace t3k::ui
