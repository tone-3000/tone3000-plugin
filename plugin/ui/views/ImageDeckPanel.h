// The shared advanced panel for the stereo-image slot (port of
// ImageDeckPanel.tsx): Spread (mono chain mode) and Align (stereo chain
// mode) mount the same deck sections (native ImageDeck.h), so one panel
// serves both, keyed on the feature (which selects the parameter ids and
// help strings):
//  - Wobble knob + power: humanising drift of the delay.
//  - Crossover knob + power: lows below the cutoff skip the deck.
//  - Diffuse power: the phase-decorrelation cascade on the delayed side.
//  - Mono-safety LED: live L/R output correlation.
// Spread's sections default on (the deck is its core sound); Align's all
// default off (purely corrective until the deck is asked for).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "services/Services.h"
#include "widgets/DimGroup.h"
#include "widgets/ParamControls.h"
#include "widgets/Popover.h"

namespace t3k::ui {

enum class ImageFeature { spread, align };
// APVTS id prefix: "spread" / "align".
juce::String imageFeaturePrefix(ImageFeature feature);

class ImageDeckPanel : public Popover {
public:
  static constexpr int kWidth = 316;
  static constexpr int kHeight = 85;
  // Gap between the panel's bottom edge and its anchor's top.
  static constexpr int kGap = 6;

  ImageDeckPanel(Services& services, ImageFeature feature);
  ~ImageDeckPanel() override;

  // Restores a feature's whole deck to its defaults (Alt/Option-click on the
  // feature's Offset knob resets the feature, not just the knob).
  static void resetDeck(Backend& backend, ImageFeature feature);

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  // A deck section: a labelled secondary knob centred in its column, its
  // power button floated beside it. The knob + label dim while off; the
  // power stays bright.
  struct Section {
    Section(Services& services, const juce::String& param, Knob::Options options,
            help::Key powerHelp);
    void layout(juce::Rectangle<int> column);
    DimGroup dim;
    ParamKnob knob;
    ParamPowerButton power;
  };
  class Led;

  Section wobble_, crossover_;
  ParamPowerButton diffusePower_;
  std::unique_ptr<Led> led_;
};

}  // namespace t3k::ui
