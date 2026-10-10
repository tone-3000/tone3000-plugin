// Alert severities and their colours (controls.tsx ALERT_COLORS): the app
// banner, the settings alerts and the alert glyph all share them.
#pragma once

#include "Theme.h"

namespace t3k::ui {

enum class AlertVariant { error, warn, info };

inline juce::Colour alertColour(AlertVariant v) {
  return v == AlertVariant::error ? theme::kBrandRed : theme::kBrandYellow;
}

}  // namespace t3k::ui
