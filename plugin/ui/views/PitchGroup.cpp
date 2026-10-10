#include "PitchGroup.h"

#include <cmath>
#include <optional>

#include "core/Theme.h"

namespace t3k::ui {

namespace {

// The plate's knob-to-companion gap (Faceplate's kGroupGap).
constexpr int kGap = 10;
// Chrome boxes in a bottom-aligned row sit on the secondary-knob centreline.
constexpr int kChromeLift = theme::faceplateChromeLift(theme::kKnobSizeSecondary);

// One detent per semitone across ±24.
constexpr int kSemitoneSteps = 2 * 24 + 1;

Knob::Options semitoneKnob() {
  Knob::Options o;
  o.label = "Pitch";
  o.size = theme::kKnobSizeSecondary;
  o.thumb = Knob::Thumb::secondary;
  o.variant = Knob::Variant::bipolar;  // noon = 0 st
  o.scale = &scales::semitones();
  o.defaultValue = 0.5f;
  o.steps = kSemitoneSteps;  // STEP's default; syncStep() follows the parameter
  o.help = help::Key::pitch;
  return o;
}

}  // namespace

PitchGroup::PitchGroup(Services& services)
    : services_(services),
      semitones_(services.backend, "pitchSemitones", semitoneKnob()),
      power_(services.backend, "pitchEnabled", help::Key::pitchPower),
      step_(services.backend, "pitchStep"),
      deck_(services) {
  dim_.addAndMakeVisible(semitones_);
  dim_.setOff(!power_.value(), false);
  power_.onValueChange = [this](bool on) { dim_.setOff(!on); };
  addAndMakeVisible(dim_);
  addAndMakeVisible(power_);
  step_.onChange = [this] { syncStep(); };
  syncStep();

  // One gesture resets the whole effect, deck included.
  semitones_.onReset = [this] { PitchDeckPanel::resetDeck(services_.backend); };
  // Touch-and-hold on the knob is the right-click of the platform.
  semitones_.onLongPress = [this] { toggleDeck(); };

  setSize(kWidth, height());
}

PitchGroup::~PitchGroup() { deck_.close(); }

// STEP on: the knob detents to whole semitones, and a shift left between
// them by a smooth sweep snaps to the nearest so the knob shows what the
// processor (which rounds under STEP) is playing. Off: the knob sweeps.
void PitchGroup::syncStep() {
  const bool stepped = step_.boolValue();
  semitones_.setSteps(stepped ? std::optional<int>(kSemitoneSteps) : std::nullopt);
  if (!stepped) return;
  const float unit = 1.0f / static_cast<float>(kSemitoneSteps - 1);
  const float snapped = std::round(semitones_.value() / unit) * unit;
  if (!juce::approximatelyEqual(snapped, semitones_.value())) semitones_.binding().set(snapped);
}

void PitchGroup::visibilityChanged() {
  if (!isVisible()) deck_.close();
}

void PitchGroup::mouseDown(const juce::MouseEvent& e) {
  if (isSecondaryPress(e)) toggleDeck();
}

void PitchGroup::toggleDeck() {
  if (deck_.isOpen()) {
    deck_.close();
    return;
  }
  // Anchored to the knob: the panel's left edge tracks the knob's.
  deck_.open(semitones_, Popover::Align::left, PitchDeckPanel::kGap, 0, Popover::Placement::above);
}

void PitchGroup::resized() {
  const int baseline = getHeight() - Knob::kEditorOverflow;  // the label slot's bottom edge
  dim_.setBounds(0, 0, theme::kKnobSizeSecondary, getHeight());
  semitones_.setTopLeftPosition(0, 0);
  power_.setTopLeftPosition(theme::kKnobSizeSecondary + kGap, baseline - theme::kIconBoxSize + kChromeLift);
}

}  // namespace t3k::ui
