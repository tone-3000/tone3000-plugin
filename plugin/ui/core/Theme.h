// Shared theme tokens (port of theme.ts): black surfaces, white/gray chrome
// and three brand accents. Every colour/size literal in the UI comes from
// here, so the palette changes in one place.
#pragma once

#include <juce_graphics/juce_graphics.h>

namespace t3k::ui::theme {

// Lucide / custom glyph size inside ICON_BOX_SIZE chrome boxes.
inline constexpr int kIconSize = 14;
// Square hit-target for icon buttons beside knobs and in card headers.
inline constexpr int kIconBoxSize = 20;
// Corner radius for every icon/text chrome box.
inline constexpr float kIconBoxRadius = 2.0f;
// Height for text chrome (EQ, PRE, LITE/FULL segments).
inline constexpr int kTextBoxHeight = 20;

// The two knob footprints; every knob is one of these.
inline constexpr int kKnobSizePrimary = 48;
inline constexpr int kKnobSizeSecondary = 36;

// The one dim applied to every disabled/off control (`.ui-off`).
inline constexpr float kDisabledOpacity = 0.45f;

// Knob-to-label gap.
inline constexpr int kKnobLabelGap = 8;

// Vertical lift for a chrome icon box in a bottom-aligned faceplate row: from
// the label baseline up to the centre of a secondary knob.
inline constexpr int faceplateChromeLift(int secondaryKnobSize) {
  return -(kKnobLabelGap + 14 + secondaryKnobSize / 2 - kIconBoxSize / 2);
}

inline const juce::Colour kBrandBlue{0xff0000ff};
inline const juce::Colour kBrandYellow{0xffffff00};
inline const juce::Colour kBrandRed{0xffff0000};
// Inline doc / "Learn More" links in settings.
inline const juce::Colour kLinkBlue{0xff40a6ff};

inline const juce::Colour kWhite{0xffffffff};
inline const juce::Colour kBlack{0xff000000};

// rgba(235, 235, 245, a) family: primary muted text, secondary labels,
// pressed/active fill.
inline const juce::Colour kMuted = juce::Colour(235, 235, 245).withAlpha(0.60f);
inline const juce::Colour kSubtle = juce::Colour(235, 235, 245).withAlpha(0.40f);
inline const juce::Colour kHighlight = juce::Colour(235, 235, 245).withAlpha(0.18f);
// Disabled/idle icon gray.
inline const juce::Colour kGray{0xff8d8d93};
// Hairline used by every card/section/segment border (1px).
inline const juce::Colour kBorder = juce::Colour(84, 84, 88).withAlpha(0.65f);
// Card body background / raised chrome.
inline const juce::Colour kSurface{0xff151517};
inline const juce::Colour kSurfaceRaised{0xff1c1c1e};
// Shared fill behind LITE/FULL and the EQ view switcher.
inline const juce::Colour kSegmentedTrack = juce::Colour(120, 120, 128).withAlpha(0.36f);
// Floating panels (popovers, menus, the image deck): #141416, hairline
// border, 14px corners.
inline const juce::Colour kPanelBg{0xff141416};
inline constexpr float kPanelCorner = 14.0f;

}  // namespace t3k::ui::theme
