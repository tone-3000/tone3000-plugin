#include "GateDeckPanel.h"

#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {

// Normalised defaults of the real-unit parameters (50 ms, 20 ms, 80 dB).
float releaseDefault() { return static_cast<float>(scales::gateReleaseMs().fromDisplay(50)); }
float holdDefault() { return static_cast<float>(scales::gateHoldMs().fromDisplay(20)); }
float rangeDefault() { return static_cast<float>(scales::gateRangeDb().fromDisplay(80)); }

// The stereo-image deck's padding and gap, with columns sized for this
// deck's longest label ("Release") instead of "Crossover": kWidth is the
// padding plus 3 columns and 2 gaps, inside the panel's 1px border.
constexpr int kPadTop = 14, kPadSide = 16, kPadBottom = 8;
constexpr int kSectionWidth = 64;
constexpr int kSectionGap = 18;

Knob::Options deckKnob(const char* label, const KnobScale& scale, float def, help::Key help) {
  Knob::Options o;
  o.label = label;
  o.size = theme::kKnobSizeSecondary;
  o.thumb = Knob::Thumb::secondary;
  o.scale = &scale;
  o.defaultValue = def;
  o.help = help;
  return o;
}

}  // namespace

GateDeckPanel::GateDeckPanel(Services& services)
    : release_(services.backend, "gateRelease",
               deckKnob("Release", scales::gateReleaseMs(), releaseDefault(), help::Key::gateRelease)),
      hold_(services.backend, "gateHold",
            deckKnob("Hold", scales::gateHoldMs(), holdDefault(), help::Key::gateHold)),
      range_(services.backend, "gateRange",
             deckKnob("Range", scales::gateRangeDb(), rangeDefault(), help::Key::gateRange)) {
  for (auto* k : {&release_, &hold_, &range_}) addAndMakeVisible(*k);
  primaryOnly = true;          // right-click toggles the panel; don't dismiss on it
  dismissOnAnchorPress = true; // the anchor is the Gate knob, a control
  setSize(kWidth, kHeight);
}

void GateDeckPanel::resetDeck(Backend& backend) {
  ParamBinding(backend, "gateRelease").set(releaseDefault());
  ParamBinding(backend, "gateHold").set(holdDefault());
  ParamBinding(backend, "gateRange").set(rangeDefault());
}

void GateDeckPanel::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::fill(g, box, theme::kPanelCorner, theme::kPanelBg);
  paint::border(g, box, theme::kPanelCorner, theme::kBorder);
}

void GateDeckPanel::resized() {
  auto content = contentBounds();
  content.removeFromTop(kPadTop);
  content.removeFromBottom(kPadBottom);
  content.reduce(kPadSide, 0);

  // Each knob takes its column's full width so its nowrap label can
  // overflow the face symmetrically ("Release" is wider than a 36px knob).
  const int height = Knob::heightFor(theme::kKnobSizeSecondary);
  int i = 0;
  for (auto* k : {&release_, &hold_, &range_}) {
    k->setBounds(content.getX() + i * (kSectionWidth + kSectionGap),
                 content.getBottom() - height + Knob::kEditorOverflow, kSectionWidth, height);
    ++i;
  }
}

}  // namespace t3k::ui
