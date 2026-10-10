// "← LABEL" takeover header link (ChainBlock.tsx's ← BLOCK, ToneBrowser.tsx's
// ← SELECT TONE): a 16px arrow, a 16px gap and the label in 16px mono
// upper-case on a 1.4 line box. Sizes itself from the label; with no label
// (the browser's signed-out sign-in page) it is the arrow alone.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Help.h"
#include "widgets/Clickable.h"

namespace t3k::ui {

class BackLink : public Clickable {
public:
  static constexpr int kIcon = 16;
  static constexpr int kGap = 16;
  static constexpr float kPx = 16;
  static constexpr int kHeight = 22;  // 16px mono, line-height 1.4

  BackLink(const juce::String& label, help::Key help);

  void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
  juce::String label_;
};

}  // namespace t3k::ui
