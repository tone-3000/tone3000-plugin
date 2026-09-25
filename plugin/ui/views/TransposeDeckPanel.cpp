#include "TransposeDeckPanel.h"

#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {

// Normalised defaults: 0 cents (centre), tonality Off (top), the 60 ms
// window (the middle detent).
constexpr float kFineDefault = 0.5f;
constexpr float kTonalityDefault = 1.0f;
constexpr float kWindowDefault = 0.5f;

// The gate deck's geometry (see GateDeckPanel.cpp).
constexpr int kPadTop = 14, kPadSide = 16, kPadBottom = 8;
constexpr int kSectionWidth = 64;
constexpr int kSectionGap = 18;

Knob::Options deckKnob(const char* label, const KnobScale& scale, float def, help::Key help,
                       Knob::Variant variant = Knob::Variant::full, std::optional<int> steps = {}) {
  Knob::Options o;
  o.label = label;
  o.size = theme::kKnobSizeSecondary;
  o.thumb = Knob::Thumb::secondary;
  o.variant = variant;
  o.scale = &scale;
  o.defaultValue = def;
  o.help = help;
  o.steps = steps;
  return o;
}

}  // namespace

TransposeDeckPanel::TransposeDeckPanel(Services& services)
    : fine_(services.backend, "transposeFine",
            deckKnob("Fine", scales::cents(), kFineDefault, help::Key::transposeFine, Knob::Variant::bipolar)),
      tonality_(services.backend, "transposeTonality",
                deckKnob("Tonality", scales::tonalityHz(), kTonalityDefault, help::Key::transposeTonality)),
      window_(services.backend, "transposeWindow",
              deckKnob("Latency", scales::windowMs(), kWindowDefault, help::Key::transposeWindow,
                       Knob::Variant::full, 3)) {
  for (auto* k : {&fine_, &tonality_, &window_}) addAndMakeVisible(*k);
  primaryOnly = true;          // right-click toggles the panel; don't dismiss on it
  dismissOnAnchorPress = true; // the anchor is the Transpose knob, a control
  setSize(kWidth, kHeight);
}

void TransposeDeckPanel::resetDeck(Backend& backend) {
  ParamBinding(backend, "transposeFine").set(kFineDefault);
  ParamBinding(backend, "transposeTonality").set(kTonalityDefault);
  ParamBinding(backend, "transposeWindow").set(kWindowDefault);
}

void TransposeDeckPanel::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::fill(g, box, theme::kPanelCorner, theme::kPanelBg);
  paint::border(g, box, theme::kPanelCorner, theme::kBorder);
}

void TransposeDeckPanel::resized() {
  auto content = contentBounds();
  content.removeFromTop(kPadTop);
  content.removeFromBottom(kPadBottom);
  content.reduce(kPadSide, 0);

  const int height = Knob::heightFor(theme::kKnobSizeSecondary);
  int i = 0;
  for (auto* k : {&fine_, &tonality_, &window_}) {
    k->setBounds(content.getX() + i * (kSectionWidth + kSectionGap),
                 content.getBottom() - height + Knob::kEditorOverflow, kSectionWidth, height);
    ++i;
  }
}

}  // namespace t3k::ui
