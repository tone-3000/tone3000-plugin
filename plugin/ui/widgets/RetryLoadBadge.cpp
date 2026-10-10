#include "RetryLoadBadge.h"

#include <cmath>

#include "core/Fonts.h"
#include "core/Help.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

RetryLoadBadge::RetryLoadBadge() : retry_("Retry", PillButton::Style::filled) {
  retry_.setMetrics({12, 5, 12, 6});
  retry_.setLeadingIcon(Icon::RotateCcw, 12);
  retry_.setHelpText(help::text(help::Key::retryLoad));
  retry_.onClick = [this] {
    if (onRetry) onRetry();
  };
  addAndMakeVisible(retry_);
  const int textW = static_cast<int>(std::ceil(Fonts::width(Fonts::sans(11), "Download failed"))) + 2;
  setSize(std::max(textW, retry_.getWidth()), kTextHeight + kGap + retry_.getHeight());
}

void RetryLoadBadge::paint(juce::Graphics& g) {
  paint::text(g, "Download failed", getLocalBounds().removeFromTop(kTextHeight), Fonts::sans(11),
              theme::kWhite.withAlpha(0.9f), juce::Justification::centred);
}

void RetryLoadBadge::resized() {
  retry_.setTopLeftPosition((getWidth() - retry_.getWidth()) / 2, kTextHeight + kGap);
}

}  // namespace t3k::ui
