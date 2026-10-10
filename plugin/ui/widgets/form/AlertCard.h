// Inline alert used in settings flows (controls.tsx AlertCard): rounded
// black card with the field border, the coloured "!" glyph, white 12.5px
// copy and optional actions on the right. The main-window banner bar shares
// the glyph / copy language but lives in AppBanner (bar layout + window
// height coupling).
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "FormItem.h"
#include "core/Alerts.h"
#include "core/RichText.h"
#include "widgets/AlertActionButton.h"
#include "widgets/AlertIcon.h"

namespace t3k::ui {

class AlertCard : public FormItem {
public:
  struct Action {
    juce::String label;
    std::function<void()> onClick;
    // Secondary = the muted "Ignore" style next to a primary action.
    bool secondary = false;
  };

  static constexpr int kPadY = 11, kPadX = 13, kGap = 10;
  static constexpr float kTextPx = 12.5f, kLineHeight = 1.5f;
  // The glyph sits 1px under the first line's top (marginTop: 1rem).
  static constexpr int kIconLift = 1;

  AlertCard(AlertVariant variant, RichText content, std::vector<Action> actions = {});
  ~AlertCard() override;

  void setVariant(AlertVariant variant);
  void setContent(RichText content);

  float heightFor(float width) const override;
  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  float textWidth(float width) const;
  // The copy laid out for a card `width` wide, memoised: the stack measures
  // via heightFor and then lays out at that same width.
  const RichFlow& flowFor(float width) const;

  AlertIcon icon_;
  RichText content_;
  std::vector<std::unique_ptr<AlertActionButton>> buttons_;
  mutable std::unique_ptr<RichFlow> flow_;
  mutable float flowWidth_ = -1;
};

}  // namespace t3k::ui
