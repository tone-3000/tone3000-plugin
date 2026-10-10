#include "PitchDeckPanel.h"

#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {

// Defaults: STEP on, tonality Off (top, normalised), the 30 ms buffer (the
// second of four detents).
constexpr bool kStepDefault = true;
constexpr float kTonalityDefault = 1.0f;
constexpr float kWindowDefault = 1.0f / 3.0f;

// The gate deck's geometry (see GateDeckPanel.cpp).
constexpr int kPadTop = 14, kPadSide = 16, kPadBottom = 8;
constexpr int kSectionWidth = 64;
constexpr int kSectionGap = 18;

Knob::Options deckKnob(const char* label, const KnobScale& scale, float def, help::Key help,
                       std::optional<int> steps = {}) {
  Knob::Options o;
  o.label = label;
  o.size = theme::kKnobSizeSecondary;
  o.thumb = Knob::Thumb::secondary;
  o.scale = &scale;
  o.defaultValue = def;
  o.help = help;
  o.steps = steps;
  return o;
}

}  // namespace

PitchDeckPanel::PitchDeckPanel(Services& services)
    : step_(services.backend, "pitchStep", "STEP", help::Key::pitchStep),
      tonality_(services.backend, "pitchTonality",
                deckKnob("Tonality", scales::tonalityHz(), kTonalityDefault, help::Key::pitchTonality)),
      window_(services.backend, "pitchWindow",
              deckKnob("Buffer", scales::bufferMs(), kWindowDefault, help::Key::pitchWindow, 4)) {
  addAndMakeVisible(step_);
  for (auto* k : {&tonality_, &window_}) addAndMakeVisible(*k);
  primaryOnly = true;          // right-click toggles the panel; don't dismiss on it
  dismissOnAnchorPress = true; // the anchor is the Pitch knob, a control
  setSize(kWidth, kHeight);
}

void PitchDeckPanel::resetDeck(Backend& backend) {
  ParamBinding(backend, "pitchStep").set(kStepDefault);
  ParamBinding(backend, "pitchTonality").set(kTonalityDefault);
  ParamBinding(backend, "pitchWindow").set(kWindowDefault);
}

namespace {
juce::Rectangle<int> column(juce::Rectangle<int> content, int i) {
  return {content.getX() + i * (kSectionWidth + kSectionGap), content.getY(), kSectionWidth,
          content.getHeight()};
}
}  // namespace

void PitchDeckPanel::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::fill(g, box, theme::kPanelCorner, theme::kPanelBg);
  paint::border(g, box, theme::kPanelCorner, theme::kBorder);
}

void PitchDeckPanel::resized() {
  auto content = contentBounds();
  content.removeFromTop(kPadTop);
  content.removeFromBottom(kPadBottom);
  content.reduce(kPadSide, 0);

  // STEP takes a knob column, its toggle centred in the panel; the text is
  // its own label (as the EQ card's PRE has none).
  const auto step = column(content, 0);
  step_.setTopLeftPosition(step.getCentreX() - step_.getWidth() / 2,
                           getLocalBounds().getCentreY() - step_.getHeight() / 2);
  const int height = Knob::heightFor(theme::kKnobSizeSecondary);
  int i = 1;
  for (auto* k : {&tonality_, &window_}) {
    const auto c = column(content, i++);
    k->setBounds(c.getX(), content.getBottom() - height + Knob::kEditorOverflow, kSectionWidth, height);
  }
}

}  // namespace t3k::ui
