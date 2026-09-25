#include "GateGroup.h"

#include "core/Theme.h"

namespace t3k::ui {

namespace {

// The plate's knob-to-companion gap (Faceplate's kGroupGap).
constexpr int kGap = 10;
// Chrome boxes in a bottom-aligned row sit on the secondary-knob centreline.
constexpr int kChromeLift = theme::faceplateChromeLift(theme::kKnobSizeSecondary);

Knob::Options thresholdKnob() {
  Knob::Options o;
  o.label = "Gate";
  o.size = theme::kKnobSizeSecondary;
  o.thumb = Knob::Thumb::secondary;
  o.scale = &scales::gateDb();
  o.defaultValue = static_cast<float>(scales::gateDb().fromDisplay(-80));
  o.help = help::Key::gate;
  return o;
}

}  // namespace

GateGroup::GateGroup(Services& services)
    : services_(services),
      threshold_(services.backend, "gateThreshold", thresholdKnob()),
      power_(services.backend, "gateEnabled", help::Key::gatePower),
      deck_(services) {
  dim_.addAndMakeVisible(threshold_);
  dim_.setOff(!power_.value(), false);
  power_.onValueChange = [this](bool on) { dim_.setOff(!on); };
  addAndMakeVisible(dim_);
  addAndMakeVisible(power_);

  // Alt/Option-click on the knob also restores the deck defaults, so one
  // gesture resets the gate, not just the threshold.
  threshold_.onReset = [this] { GateDeckPanel::resetDeck(services_.backend); };
  // Touch-and-hold on the knob is the right-click of the platform.
  threshold_.onLongPress = [this] { toggleDeck(); };

  setSize(kWidth, height());
}

GateGroup::~GateGroup() { deck_.close(); }

void GateGroup::mouseDown(const juce::MouseEvent& e) {
  if (isSecondaryPress(e)) toggleDeck();
}

void GateGroup::toggleDeck() {
  if (deck_.isOpen()) {
    deck_.close();
    return;
  }
  // Anchored to the knob: the panel's left edge tracks the knob's.
  deck_.open(threshold_, Popover::Align::left, GateDeckPanel::kGap, 0, Popover::Placement::above);
}

void GateGroup::resized() {
  const int baseline = getHeight() - Knob::kEditorOverflow;  // the label slot's bottom edge
  dim_.setBounds(0, 0, theme::kKnobSizeSecondary, getHeight());
  threshold_.setTopLeftPosition(0, 0);
  power_.setTopLeftPosition(theme::kKnobSizeSecondary + kGap,
                            baseline - theme::kIconBoxSize + kChromeLift);
}

}  // namespace t3k::ui
