// Format tag (FormatBadge.tsx): "NAM" / "IR" in 12px mono, black on
// zinc-400, 1px×6px padding, 2px corners; NAM tones (the ones the plugin
// loads, all A2) append the 18px A2 architecture mark after a 10px gap.
// Sizes itself from its label.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace t3k::ui {

class FormatBadge : public juce::Component {
public:
  static constexpr int kA2Size = 18;
  static constexpr int kGap = 10;

  FormatBadge();

  // Empty label hides the badge (and reports a zero width).
  void setFormat(const juce::String& label, bool a2);

  void paint(juce::Graphics& g) override;

private:
  static constexpr float kFontPx = 12;
  static constexpr int kPadX = 6;

  juce::String label_;
  bool a2_ = false;
  int chipWidth_ = 0;
};

}  // namespace t3k::ui
