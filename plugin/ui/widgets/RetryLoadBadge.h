// Failed-download state for a chain block (RetryLoadBadge.tsx): "Download
// failed" over a filled Retry pill, shown over the dimmed artwork on the
// gallery tile and the detail card.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PillButton.h"

namespace t3k::ui {

class RetryLoadBadge : public juce::Component {
public:
  RetryLoadBadge();

  std::function<void()> onRetry;

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  static constexpr int kTextHeight = 13;  // 11px line
  static constexpr int kGap = 8;

  PillButton retry_;
};

}  // namespace t3k::ui
