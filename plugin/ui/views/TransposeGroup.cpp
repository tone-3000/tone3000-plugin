#include "TransposeGroup.h"

#include "core/Theme.h"

namespace t3k::ui {

namespace {

// The plate's knob-to-companion gap (Faceplate's kGroupGap).
constexpr int kGap = 10;
// Chrome boxes in a bottom-aligned row sit on the secondary-knob centreline.
constexpr int kChromeLift = theme::faceplateChromeLift(theme::kKnobSizeSecondary);

Knob::Options semitoneKnob() {
  Knob::Options o;
  o.label = "Transpose";
  o.size = theme::kKnobSizeSecondary;
  o.thumb = Knob::Thumb::secondary;
  o.variant = Knob::Variant::bipolar;  // noon = 0 st
  o.scale = &scales::semitones();
  o.defaultValue = 0.5f;
  o.steps = 25;  // -12..+12
  o.help = help::Key::transpose;
  return o;
}

}  // namespace

TransposeGroup::TransposeGroup(Services& services)
    : services_(services),
      semitones_(services.backend, "transposeSemitones", semitoneKnob()),
      power_(services.backend, "transposeEnabled", help::Key::transposePower),
      deck_(services) {
  dim_.addAndMakeVisible(semitones_);
  dim_.setOff(!power_.value(), false);
  power_.onValueChange = [this](bool on) { dim_.setOff(!on); };
  addAndMakeVisible(dim_);
  addAndMakeVisible(power_);

  // One gesture resets the whole effect, deck included.
  semitones_.onReset = [this] { TransposeDeckPanel::resetDeck(services_.backend); };
  // Touch-and-hold on the knob is the right-click of the platform.
  semitones_.onLongPress = [this] { toggleDeck(); };

  setSize(kWidth, height());
}

TransposeGroup::~TransposeGroup() { deck_.close(); }

void TransposeGroup::mouseDown(const juce::MouseEvent& e) {
  if (isSecondaryPress(e)) toggleDeck();
}

void TransposeGroup::toggleDeck() {
  if (deck_.isOpen()) {
    deck_.close();
    return;
  }
  // Anchored to the knob face, not its label column, so it lands like Gate's.
  deck_.open(semitones_, Popover::Align::left, TransposeDeckPanel::kGap, kFaceInset,
             Popover::Placement::above);
}

void TransposeGroup::resized() {
  const int baseline = getHeight() - Knob::kEditorOverflow;  // the label slot's bottom edge
  dim_.setBounds(0, 0, kColumnWidth, getHeight());
  semitones_.setBounds(0, 0, kColumnWidth, getHeight());  // face centred, label full width
  power_.setTopLeftPosition(kFaceInset + theme::kKnobSizeSecondary + kGap,
                            baseline - theme::kIconBoxSize + kChromeLift);
}

}  // namespace t3k::ui
