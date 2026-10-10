// System Settings tab (SystemSettings.tsx): the bespoke replacement for
// JUCE's audio settings dialog, driven entirely by the native device-state
// snapshot. Every branch the stock AudioDeviceSelectorComponent handles is
// covered: a driver picker only with several backends, one device picker
// for linked-I/O drivers (ASIO) and separate input / output pickers
// otherwise (with "No device" a legitimate option), the input channel
// picker with live meters, an output pair picker for multi-out interfaces,
// buffer / rate lists from the device (single-entry lists render locked with
// a caption instead of a fake picker), the vendor control panel + reset, and
// inline (never modal) errors. Rows are persistent and re-synced on every
// device change, so open dropdowns and hover survive a refresh.
#pragma once

#include <memory>

#include "InlineBannerAlert.h"
#include "InputChannelPicker.h"
#include "MidiInputsSection.h"
#include "services/Services.h"
#include "widgets/form/AlertCard.h"
#include "widgets/form/FormRows.h"
#include "widgets/form/SelectField.h"

namespace t3k::ui {

class SystemSettingsPage : public FormStack, private AudioDeviceStore::Listener {
public:
  // Brief "playing" state on the test button.
  static constexpr int kTestPlayingMs = 1200;

  explicit SystemSettingsPage(Services& services);
  ~SystemSettingsPage() override;

private:
  class TestButton;
  class OutputRow;
  class ButtonPair;

  void audioDeviceChanged() override { sync(); }
  void sync();
  // Runs a device mutation; its error (if any) renders inline.
  template <typename Fn>
  void apply(Fn&& fn);

  Services& services_;
  juce::String error_;

  AlertCard micDenied_;
  AlertCard inlineError_;

  ToggleRow hearYourself_;
  InlineBannerAlert feedbackRisk_{"feedback-risk"};
  InlineBannerAlert inputMuted_{"input-muted"};

  SettingsGroup group_;
  FieldRow driver_;
  SelectField driverSelect_{"Audio driver"};
  InlineBannerAlert asioNudge_{"asio-nudge"};
  FieldRow inputDevice_;
  SelectField inputSelect_{"Input device"};
  FieldRow linkedDevice_;
  SelectField linkedSelect_{"Audio device"};
  InputChannelPicker channels_;
  FieldRow output_;
  std::unique_ptr<OutputRow> outputRow_;
  InlineBannerAlert noOutput_{"no-output"};
  FieldRow outputPairs_;
  SelectField pairSelect_{"Output channels"};
  FieldRow buffer_;
  SelectField bufferSelect_{"Buffer size"};
  Paragraph bufferCaption_;
  InlineBannerAlert bufferLatency_{"buffer-latency"};
  FieldRow rate_;
  SelectField rateSelect_{"Sample rate"};
  Paragraph rateCaption_;
  InlineBannerAlert bluetoothRoute_{"bluetooth-route"};
  InlineBannerAlert rateNot48k_{"rate-not-48k"};
  FieldRow driverSettings_;
  std::unique_ptr<ButtonPair> panelButtons_;

  MidiInputsSection midiInputs_;
};

}  // namespace t3k::ui
