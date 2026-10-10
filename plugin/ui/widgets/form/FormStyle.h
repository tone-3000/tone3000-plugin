// Tokens of the settings form language (controls.tsx): outlined black
// fields with 1px zinc-700 borders, green pill switches, white radio/check
// indicators, 15px/600 section labels over 14px muted body copy. Every
// settings-style surface (the Settings takeover, System Settings, inline
// alerts) draws from here so the fields render identically.
#pragma once

#include <juce_graphics/juce_graphics.h>

#include "core/Fonts.h"
#include "core/Theme.h"

namespace t3k::ui::form {

// FIELD_BORDER: zinc-700.
inline const juce::Colour kFieldBorder{0xff3f3f46};
// Vertical gap between top-level settings sections.
inline constexpr int kSectionGap = 48;
// Help line → control, and control → caption.
inline constexpr int kControlGap = 16;

inline constexpr float kFieldRadius = 6;
inline constexpr float kCardRadius = 10;
// outlinedFieldStyle controls: padding 12px 16px around a 14px label.
inline constexpr int kFieldPadY = 12, kFieldPadX = 16;
inline constexpr float kFieldPx = 14;
// A one-line outlined field's border-box height.
inline int fieldHeight() { return 2 * kFieldPadY + Fonts::normalLineHeight(kFieldPx) + 2; }

// The body's inherited font-size (index.css `font-size: 16rem`): a bare
// span in a plain div shares its line box with this strut.
inline constexpr float kBodyStrutPx = 16;

// sectionLabelStyle / descriptionStyle / captionStyle.
inline constexpr float kLabelPx = 15;
inline constexpr float kBodyPx = 14;
inline constexpr float kBodyLineHeight = 1.45f;
inline constexpr float kSmallPx = 12;

// PillToggle track colours.
inline const juce::Colour kToggleOn{0xff00d13b};
inline const juce::Colour kToggleOff{0xff71717a};
// Row hover fill on selectable lists (channel rows, MIDI inputs).
inline const juce::Colour kRowHover = juce::Colours::white.withAlpha(0.06f);
// Selected / hovered option fill in a dropdown.
inline const juce::Colour kOptionFill = juce::Colours::white.withAlpha(0.10f);

}  // namespace t3k::ui::form
