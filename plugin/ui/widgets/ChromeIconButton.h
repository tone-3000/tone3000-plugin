// Shared chrome icon button for the faceplate, card headers, tiles and pan
// rail (port of ChromeIconButton.tsx). The box is always ICON_BOX_SIZE with
// a 1px (mostly transparent) border so the content box never changes
// between tones; the glyph is ICON_SIZE and centred.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Help.h"
#include "core/Icons.h"
#include "widgets/Clickable.h"

namespace t3k::ui {

class ChromeIconButton : public Clickable {
public:
  // plain: white icon (optional HIGHLIGHT fill via setFilled).
  // power: on = white/clear; off = GRAY + HIGHLIGHT (section power, normalise).
  // armed: on = BRAND_YELLOW + BLACK (listening / engaged); off = BORDER + GRAY.
  // link:  on = white; off = GRAY, never a fill (pan link).
  enum class Tone { plain, power, armed, link };

  ChromeIconButton(Icon icon, Tone tone, help::Key help);
  ChromeIconButton(const char* svg, Tone tone, help::Key help);

  void setIcon(Icon icon);
  // power / armed / link: feature on / listening / linked. Defaults on.
  void setOn(bool on);
  bool isOn() const { return on_; }
  // plain only: HIGHLIGHT fill while true (e.g. a non-default input mode).
  void setFilled(bool filled);
  // Panel showing: WHITE fill + BLACK icon; wins over tone / on / filled.
  void setOpen(bool open);

  void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
  // Primary presses only (a right-click belongs to the enclosing group's
  // context gesture, see SecondaryPress).
  void mouseDown(const juce::MouseEvent& e) override;

private:
  ChromeIconButton(Tone tone, help::Key help);

  std::optional<Icon> icon_;
  const char* svg_ = nullptr;
  Tone tone_;
  bool on_ = true, filled_ = false, open_ = false;
};

}  // namespace t3k::ui
