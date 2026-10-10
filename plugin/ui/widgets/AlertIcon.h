// Circled "!" in the variant colour (controls.tsx AlertIcon): a 16px ring
// with a 1.6px stroke and a 10px bold glyph, the shared alert glyph.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Alerts.h"

namespace t3k::ui {

class AlertIcon : public juce::Component {
public:
  static constexpr int kSize = 16;
  static constexpr float kStroke = 1.6f;
  static constexpr float kGlyphPx = 10;

  explicit AlertIcon(AlertVariant variant = AlertVariant::info);

  void setVariant(AlertVariant variant);
  void paint(juce::Graphics& g) override;

private:
  AlertVariant variant_;
};

}  // namespace t3k::ui
