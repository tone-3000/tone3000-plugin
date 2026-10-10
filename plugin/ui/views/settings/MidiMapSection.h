// MIDI Mapping (MidiMapSettings.tsx), inline in Plugin Settings: the list of
// learned pairings (re-learn / remove per row, a listening row while a learn
// is armed), the picker of still-unmapped controls, the presets caption and
// the channel filter. The engine owns the armed state; this renders it and
// gives up an armed learn after LEARN_TIMEOUT unless the user is typing a
// CC number into the listening row.
//
// Block-power targets are positional, so rows and the picker name each
// chain slot after the tone currently in it (from the chain state).
#pragma once

#include <memory>
#include <vector>

#include "core/DelayedCall.h"
#include "services/Services.h"
#include "widgets/form/FormRows.h"
#include "widgets/form/SelectField.h"

namespace t3k::ui {

class MidiMapSection : public FormStack, private MidiMapStore::Listener, private ChainStore::Listener {
public:
  static constexpr int kLearnTimeoutMs = 10000;
  // The list card and its rows.
  static constexpr int kRowPadY = 11, kRowPadX = 14, kRowGap = 14;
  static constexpr float kTitlePx = 14, kSubtitlePx = 11, kSourcePx = 13, kSmallPx = 12;
  static constexpr int kSubtitleGap = 3;
  static constexpr int kIconButton = 26, kIconGap = 2;
  static constexpr int kEmptyPad = 20;

  explicit MidiMapSection(Services& services);
  ~MidiMapSection() override;

private:
  class ListCard;
  class Row;
  class MappingRow;
  class LearningRow;

  void midiMapChanged() override { rebuild(); }
  void chainChanged(const ChainState&) override { rebuild(); }
  void rebuild();
  // Row / picker subtitle: group, plus lane and live tone title for block powers.
  juce::String targetContext(const juce::String& targetId) const;
  void armTimeout();
  void commitCc();
  // Run a store change from a row control once the event that asked for it
  // has unwound: the change rebuilds the list, destroying that very control.
  void defer(std::function<void()> action);

  Services& services_;
  std::unique_ptr<ListCard> list_;
  SelectField picker_{"Control to map"};
  Paragraph caption_;
  FieldRow channel_;
  SelectField channelSelect_{"MIDI channel"};
  // Digits-only draft of a typed CC number for the listening row; one learn
  // is armed at a time, so one draft suffices.
  juce::String ccDraft_;
  DelayedCall learnTimeout_;
};

}  // namespace t3k::ui
