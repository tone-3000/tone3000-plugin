// Plugin Settings (Settings.tsx pluginTab): one scrollable page of options
// that work in every build: Show Info Bar, Show Gate / Show Pitch Shift
// (which of the two the faceplate shows), NAM A2 size default, the per-block
// size / normalization controls, calibration (with its dBu field),
// oversampling (with its rate), multi-core, MIDI Mapping, Presets (open the
// user presets folder), diagnostics, and the version / update footer.
#pragma once

#include <memory>

#include "MidiMapSection.h"
#include "core/DelayedCall.h"
#include "services/ParamBinding.h"
#include "services/Services.h"
#include "widgets/form/FormRows.h"
#include "widgets/form/SelectField.h"

namespace t3k::ui {

class PluginSettingsPage : public FormStack,
                           private UiPrefs::Listener,
                           private ChainStore::Listener,
                           private UpdateCheck::Listener {
public:
  static constexpr int kLogStatusMs = 3000;
  // Input calibration range, dBu (the parameter is normalised 0..1).
  static constexpr float kDbuMin = -60, kDbuMax = 60;

  explicit PluginSettingsPage(Services& services);
  ~PluginSettingsPage() override;

private:
  class DbuField;

  void prefChanged(const juce::String& key) override;
  void chainChanged(const ChainState& state) override;
  void updateNoticeChanged() override { syncFooter(); }
  void syncPrefs();
  void syncParams();
  void syncFooter();
  // Settings-page parameter edits also become the machine-wide default.
  void rememberMachineDefault(const ParamBinding& param);
  void setAndRemember(ParamBinding& param, float normalised);
  void showLogStatus(const juce::String& status);

  Services& services_;

  ToggleRow infoBar_;
  // View-only toggles for the faceplate's gate / pitch groups; the
  // Faceplate shows a powered effect regardless.
  ToggleRow showGate_, showPitch_;
  FieldRow namSize_;
  RadioOption lite_, full_;
  ToggleRow blockSize_;
  TipRow blockSizeTip_;
  ToggleRow normalize_;
  TipRow normalizeTip_;

  ParamBinding calibrateParam_, dbuParam_, osEnabledParam_, osFactorParam_;
  ToggleRow calibration_;
  std::unique_ptr<DbuField> dbu_;
  Paragraph calibrationHelp_;
  TipRow calibrationTip_;
  Paragraph calibrationHandoff_;
  ToggleRow oversampling_;
  FormLabel rateLabel_;
  SelectField osRate_{"Oversampling rate"};
  ToggleRow multiCore_;

  FieldRow midi_;
  MidiMapSection midiSection_;

  FieldRow presets_;
  FormButton openPresets_;
  FormBox openPresetsBox_;

  FieldRow diagnostics_;
  FormButton copyLogs_, revealLogs_;
  FormBox copyLogsBox_, revealLogsBox_;
  Paragraph logStatus_;
  DelayedCall logStatusClear_;

  FormStack footer_;
  FormButton update_;
  FormBox updateBox_;
  Paragraph version_;
};

}  // namespace t3k::ui
