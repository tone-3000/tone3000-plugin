// MIDI device enablement for the System Settings tab (MidiInputsSection.tsx;
// standalone only: hosts route MIDI to the processor themselves). Decides
// which hardware feeds the plugin; what each knob or pedal does is MIDI
// Mapping on the Plugin Settings page. The OS doesn't push MIDI device
// arrivals, so the device state is re-pulled every couple of seconds while
// this section is on screen.
#pragma once

#include <memory>
#include <vector>

#include "services/Services.h"
#include "widgets/form/FormControls.h"
#include "widgets/form/FormRows.h"

namespace t3k::ui {

class MidiInputsSection : public FieldRow, private juce::Timer {
public:
  static constexpr int kHotplugPollMs = 2000;
  static constexpr int kRowPadY = 11, kRowPadX = 13, kRowGap = 12;
  static constexpr float kEnabledPx = 11;
  static constexpr int kEmptyPad = 20;

  explicit MidiInputsSection(Services& services);
  ~MidiInputsSection() override;

  void update(const AudioDeviceState& state);

  void visibilityChanged() override { syncPolling(); }
  void parentHierarchyChanged() override { syncPolling(); }

private:
  class InputRow;
  class Card;

  void timerCallback() override { services_.audioDevice.refresh(); }
  void syncPolling();

  Services& services_;
  std::unique_ptr<Card> card_;
  FormButton bluetooth_;
  FormBox bluetoothBox_;
};

}  // namespace t3k::ui
