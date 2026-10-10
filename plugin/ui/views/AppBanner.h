// The fixed-height banner bar above the plugin header (AppBanner.tsx): alert
// glyph, one ellipsised line of mixed-weight copy, an outlined primary action
// and, for ignorable rules, a muted "Ignore". The slide-in choreography is
// PluginRoot's; this just renders a spec.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "services/Banners.h"
#include "widgets/AlertActionButton.h"
#include "widgets/AlertIcon.h"

namespace t3k::ui {

class AppBanner : public juce::Component {
public:
  static constexpr int kHeight = 44;
  static constexpr int kPadX = 24, kGap = 10;
  static constexpr float kTextPx = 12.5f, kLineHeight = kTextPx * 1.4f;

  AppBanner();
  ~AppBanner() override;

  void setSpec(const BannerSpec& spec);

  std::function<void(BannerAction)> onAction;
  std::function<void(const juce::String& id)> onDismiss;

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  BannerSpec spec_;
  AlertIcon icon_;
  AlertActionButton action_{AlertActionButton::Style::primary};
  AlertActionButton ignore_{AlertActionButton::Style::secondaryBare};
  juce::Rectangle<int> textBox_;
};

}  // namespace t3k::ui
