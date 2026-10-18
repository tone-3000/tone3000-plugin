// Previous / next for one tone block: ‹ position/total › on a dark pill,
// stepping the block through the result list its tone was picked from
// (ToneStepper). Shown only while the block has a browse context; the
// owner asks visibleFor() and places it (bottom-centre of a gallery tile,
// the block card's header).
//
// The buttons are the primary way to step: many Linux hosts never pass
// keys to plugin windows, so arrow keys and MIDI are extras on top.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <string>

#include "services/Services.h"
#include "widgets/ChromeIconButton.h"

namespace t3k::ui {

class StepPill : public juce::Component, private ToneStepper::Listener {
public:
  static constexpr int kHeight = 24;
  static constexpr int kLabelWidth = 56;
  static constexpr int kWidth = 2 * theme::kIconBoxSize + kLabelWidth + 8;

  StepPill(Services& services, std::string blockId);
  ~StepPill() override;

  // Whether the block has a cursor to show (the owner hides us otherwise).
  bool hasCursor() const { return cursor_.has_value(); }
  // The chain revision moved: re-read the block's cursor.
  void refresh();

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  void stepperChanged() override { refresh(); }

  Services& services_;
  std::string blockId_;
  std::optional<ToneStepper::Cursor> cursor_;
  ChromeIconButton prev_, next_;
};

}  // namespace t3k::ui
