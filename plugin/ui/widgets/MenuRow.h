// Dropdown row: optional icon + 14px label, rounded hover fill (AccountMenu.tsx
// and TileMenu.tsx share this look).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

#include "core/Icons.h"
#include "widgets/Clickable.h"

namespace t3k::ui {

class MenuRow : public Clickable {
public:
  struct Metrics {
    int height;
    int padX;
    int gap;  // icon ↔ label
    int icon;
    float fontPx;
  };
  // Account dropdown: padding 10px 12px, gap 12px, icon 18, font 14 → 38px.
  static constexpr Metrics kDropdown{38, 12, 12, 18, 14};
  // Tile / lane context menus: padding 9px 12px, icon 16, font 13 → 34px.
  static constexpr Metrics kContext{34, 12, 12, 16, 13};
  static constexpr int kHeight = kDropdown.height;

  MenuRow(const juce::String& label, std::optional<Icon> icon, Metrics metrics = kDropdown);

  void setLabelColour(juce::Colour colour);
  void setDisabledLook(bool disabled);
  // Width the row wants for its content (icon + gap + label + padding).
  int preferredWidth() const;

  void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
  juce::String label_;
  std::optional<Icon> icon_;
  Metrics metrics_;
  juce::Colour labelColour_;
  bool disabledLook_ = false;
};

}  // namespace t3k::ui
