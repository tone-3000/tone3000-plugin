// The faceplate's stereo-image slot (port of SpreadControls.tsx and
// AlignControls.tsx, which differ only in copy, defaults and the auto
// button). Spread doubles a mono chain into an ADT-style stereo image
// (native Spread.h); Align is a corrective alignment delay between two
// chains (native StereoOffset.h).
//
// Face: a "SPREAD"/"ALIGN" advert pill while off; clicking it powers the
// feature on and reveals the bipolar Offset knob (+ auto-align for Align)
// and a power button that collapses back to the advert. Both states share
// one footprint so the toggle never shifts the plate. Right-click anywhere
// on the group (Ctrl-click on macOS, touch-and-hold on the knob) opens the
// shared deck panel.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "ImageDeckPanel.h"
#include "services/AutoMeasure.h"
#include "services/Services.h"
#include "widgets/ChromeIconButton.h"
#include "widgets/ParamControls.h"
#include "widgets/SecondaryPress.h"

namespace t3k::ui {

class StereoImageGroup : public juce::Component,
                         public SecondaryPressTarget,
                         private AutoMeasure::Listener {
public:
  // Fixed slot width shared by the advert pill, the expanded row and the
  // other feature's group: every state occupies the same footprint. Sized
  // for the advert, the widest face.
  static constexpr int kWidth = 148;
  // The slot is as tall as a primary knob column (the plate bottom-aligns it).
  static int height() { return Knob::heightFor(theme::kKnobSizePrimary); }

  StereoImageGroup(Services& services, ImageFeature feature);
  ~StereoImageGroup() override;

  void resized() override;
  // Right-click anywhere on the group toggles the deck.
  void mouseDown(const juce::MouseEvent& e) override;
  void secondaryPress(const juce::MouseEvent&) override { toggleDeck(); }

private:
  class Advert;
  void autoMeasureChanged() override;
  void syncEnabled();
  void toggleDeck();

  Services& services_;
  ImageFeature feature_;
  ParamBinding enabled_;
  std::unique_ptr<Advert> advert_;
  std::unique_ptr<ChromeIconButton> auto_;  // align only
  ParamKnob offset_;
  ChromeIconButton power_;
  ImageDeckPanel deck_;
};

}  // namespace t3k::ui
