// The column the scrim modals share (ConnectionModal.tsx, OAuthOverlay.tsx):
// a glyph or the loading dots on top, centred body copy under it, and a row
// of pill buttons, all 16px apart with no card behind them. Sizes itself
// from its parts; ModalLayer keeps it centred.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>
#include <vector>

#include "LoadingDots.h"
#include "PillButton.h"
#include "core/Icons.h"
#include "core/TextFlow.h"

namespace t3k::ui {

class ScrimMessage : public juce::Component {
public:
  static constexpr int kGap = 16, kButtonGap = 12, kIconSize = 28;
  static constexpr float kBodyPx = 14, kIconOpacity = 0.9f, kBodyOpacity = 0.95f;

  ScrimMessage();
  ~ScrimMessage() override;

  // Top element: a 28px Lucide glyph, the loading dots, or nothing.
  void setIcon(std::optional<Icon> icon);
  void setBusy(bool busy);
  // Body copy wrapped at `maxWidth` (empty hides the row).
  void setCopy(const juce::String& copy, int maxWidth);
  // Pill buttons, left to right; a hidden button leaves the row (call
  // layout() after toggling one).
  PillButton& addButton(const juce::String& label, PillButton::Style style);
  // Re-derive the size from the parts.
  void layout();

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  int buttonsWidth() const;

  std::optional<Icon> icon_;
  std::unique_ptr<LoadingDots> dots_;
  juce::String copy_;
  int copyMaxW_ = 0;
  std::unique_ptr<TextFlow> flow_;
  int copyH_ = 0;
  std::vector<std::unique_ptr<PillButton>> buttons_;
  juce::Rectangle<int> topBox_, copyBox_;
};

}  // namespace t3k::ui
