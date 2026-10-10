// Compact alert action (AppBanner.tsx actionButtonStyle / controls.tsx
// alertActionStyle): 11.5px/600 text in 4px vertical padding. The primary
// carries a 1px white border (radius 7) and 11px side pads; the secondary
// ("Ignore") reads muted, either borderless with 4px pads (the banner bar)
// or zinc-bordered like a field (inline alert cards).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "widgets/Clickable.h"

namespace t3k::ui {

class AlertActionButton : public Clickable {
public:
  enum class Style { primary, secondaryBare, secondaryOutlined };
  static constexpr float kTextPx = 11.5f;
  static constexpr int kPadY = 4, kPrimaryPadX = 11, kBarePadX = 4;
  static constexpr float kRadius = 7;

  explicit AlertActionButton(Style style);

  void setLabel(const juce::String& label);
  void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
  static juce::Font font();
  int border() const { return style_ == Style::secondaryBare ? 0 : 1; }
  int padX() const { return style_ == Style::secondaryBare ? kBarePadX : kPrimaryPadX; }

  Style style_;
  juce::String label_;
};

}  // namespace t3k::ui
