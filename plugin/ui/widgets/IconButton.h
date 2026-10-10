// Small square icon button (port of IconButton.tsx): a Lucide or custom glyph
// centred in a box, white when lit, GRAY when inactive, dimmed when
// disabled, with an optional HIGHLIGHT fill while active. Used across the
// top bar and miscellaneous chrome; faceplate / card / tile controls use
// ChromeIconButton (tones + ICON_SIZE).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

#include "core/Icons.h"
#include "core/Theme.h"
#include "widgets/Clickable.h"

namespace t3k::ui {

class IconButton : public Clickable {
public:
  // Glyph size follows the box: ICON_SIZE at ICON_BOX_SIZE, 18 in a 28 box.
  static int glyphSizeFor(int box) {
    return box <= theme::kIconBoxSize ? theme::kIconSize : juce::roundToInt(box * 18.0 / 28.0);
  }

  explicit IconButton(Icon icon, int boxSize = theme::kIconBoxSize);
  IconButton(Icon icon, int boxSize, int glyphSize);
  IconButton(const char* svg, int boxSize, int glyphSize);

  void setIcon(Icon icon);
  // Lit (white) vs muted; defaults lit.
  void setActive(bool active);
  // Show the HIGHLIGHT fill behind the icon while active.
  void setFillWhenActive(bool fill);
  // Glyph colour override (nullopt = white / GRAY by state).
  void setColour(std::optional<juce::Colour> colour);
  void setBackgroundColour(std::optional<juce::Colour> colour);
  void setCornerRadius(float radius) { radius_ = radius; }

  void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
  IconButton(std::optional<Icon> icon, const char* svg, int boxSize, int glyphSize);

  std::optional<Icon> icon_;
  const char* svg_ = nullptr;
  int glyphSize_;
  bool active_ = true;
  bool fillWhenActive_ = false;
  float radius_ = theme::kIconBoxRadius;
  std::optional<juce::Colour> colour_, background_;
};

}  // namespace t3k::ui
