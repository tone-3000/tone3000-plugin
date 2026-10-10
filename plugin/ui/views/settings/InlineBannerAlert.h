// Re-renders a main-window banner's copy inline, next to the control that
// fixes it (SystemSettings.tsx InlineBannerAlert): same words as the banner,
// but no action / ignore buttons (you're already in the form). Placement
// gating is the caller's; this just draws.
#pragma once

#include <optional>

#include "services/Banners.h"
#include "widgets/form/AlertCard.h"

namespace t3k::ui {

class InlineBannerAlert : public AlertCard {
public:
  explicit InlineBannerAlert(const juce::String& ruleId)
      : AlertCard(AlertVariant::info, {}), rule_(bannerRuleById(ruleId)) {
    jassert(rule_ != nullptr);
    if (rule_ != nullptr) setVariant(rule_->variant);
    setVisible(false);
  }

  // Whether the rule itself fires for `state` (the default gating).
  bool firesFor(const AudioDeviceState& state) const { return rule_ != nullptr && rule_->when(state); }

  // Re-evaluate against `state`. Defaults to the banner's own trigger so the
  // two never drift; pass `show` only where the inline placement wants
  // different gating than the main-window banner. Returns whether it shows.
  bool update(const AudioDeviceState& state, std::optional<bool> show = {}) {
    const bool visible = rule_ != nullptr && show.value_or(firesFor(state));
    if (visible) setContent(rule_->content(state));
    return visible;
  }

private:
  const BannerRule* rule_;
};

}  // namespace t3k::ui
