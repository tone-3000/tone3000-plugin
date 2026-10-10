// A QR code (the sign-in screen's device link): black modules on a white
// rounded card, the card's padding standing in for the quiet zone. Encoded
// with Nayuki's generator (vendor/qrcodegen); drawn from a one-pixel-per-
// module image scaled without smoothing, so the modules stay square, and at
// a whole number of pixels each so they stay even, at any window zoom.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace t3k::ui {

class QrCode : public juce::Component {
public:
  // 12px of white round the code: at least the four modules of quiet zone
  // a scanner wants, at the module sizes a 140px card gives.
  static constexpr int kPad = 12;
  static constexpr float kCorner = 8;

  QrCode();

  // Re-encodes; an empty or over-long text draws the blank card.
  void setText(const juce::String& text);

  void paint(juce::Graphics& g) override;

private:
  juce::Image modules_;  // one pixel per module, black where set
};

}  // namespace t3k::ui
