// Text chrome button (EQ, PRE), port of ChromeTextButton.tsx. A
// TEXT_BOX_HEIGHT box with ICON_BOX_RADIUS corners, 12px mono cap-trimmed
// label, 4px side pads. Priority: open (panel showing) > armed (feature
// shaping the sound) > idle.
//   open:  WHITE fill + BLACK label
//   armed: BRAND_YELLOW fill + BLACK label
//   idle:  BORDER hairline + MUTED label
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Help.h"
#include "widgets/Clickable.h"

namespace t3k::ui {

class ChromeTextButton : public Clickable {
public:
  ChromeTextButton(juce::String label, help::Key help);

  void setArmed(bool armed);
  void setOpen(bool open);
  bool isArmed() const { return armed_; }
  bool isOpen() const { return open_; }

  void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
  static constexpr float kFontPx = 12;
  static constexpr int kPadX = 4;

  juce::String label_;
  bool armed_ = false, open_ = false;
};

}  // namespace t3k::ui
