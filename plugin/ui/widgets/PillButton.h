// Text pill buttons (theme.ts pillButtonStyle / filledPillButtonStyle): the
// outline pill is the house secondary action, the filled white pill the one
// primary action of a panel (Retry, Save, Try again…). Sizes itself from its
// label; callers may override the padding and font for the compact variants.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

#include "core/Icons.h"
#include "widgets/Clickable.h"

namespace t3k::ui {

class PillButton : public Clickable {
public:
  enum class Style { outline, filled };

  struct Metrics {
    int padX;
    int padY;
    float fontPx;
    int gap;  // icon ↔ label
  };
  static constexpr Metrics kOutline{16, 7, 13, 8};
  static constexpr Metrics kFilled{20, 10, 14, 8};

  PillButton(juce::String label, Style style);

  void setLabel(const juce::String& label);
  void setMetrics(Metrics metrics);
  void setLeadingIcon(Icon icon, float size);
  // The TONE3000 mark after the label (the browser's Browse CTA), `height`
  // px tall at its 3:1 aspect.
  void setTrailingMark(float height);
  // Full pill (default) or a fixed radius (the info panel's 8px link box).
  void setCornerRadius(std::optional<float> radius);
  // Fit the size to the label (default); an explicit setBounds still wins.
  void fitToContent();

  // Outline pills read muted while disabled (the web sets the label colour
  // from the form state); filled pills follow DISABLED_OPACITY.
  void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
  juce::Font font() const;

  juce::String label_;
  Style style_;
  Metrics metrics_;
  std::optional<std::pair<Icon, float>> icon_;
  float markHeight_ = 0;
  std::optional<float> radius_;
};

}  // namespace t3k::ui
