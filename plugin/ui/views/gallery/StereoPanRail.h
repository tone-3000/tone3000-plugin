// Left rail for stereo (GalleryLane.tsx StereoPanRail): per-lane pan knobs,
// each centred on its lane with a solo/polarity [S|Ø] group under the label,
// and the pan-link + whole-chain-swap pill on the seam between them, wired
// together by hairline connectors. Linked (default) mirrors the knobs so
// width changes stay symmetric. On a rig that can't reproduce stereo
// (`monoSum`) the pans dim and go inert and the pill shows a MONO chip.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "services/Services.h"
#include "widgets/ChromeIconButton.h"
#include "widgets/DimGroup.h"
#include "widgets/ParamControls.h"
#include "widgets/SegmentedText.h"

namespace t3k::ui {

class StereoPanRail : public juce::Component {
public:
  // The pill's outer footprint fixes the rail width; +1 for the left
  // edge-fade's outer overhang so it doesn't sit on the pill.
  static constexpr int kPillWidth = theme::kIconBoxSize * 2 + 4 + 2 * 5 + 2;
  static constexpr int kPillHeight = theme::kIconBoxSize + 2 * 3 + 2;
  static constexpr int kWidth = kPillWidth + 1;
  static constexpr int kConnectorMargin = 10;
  static constexpr int kChipGap = 8;

  explicit StereoPanRail(Services& services);
  ~StereoPanRail() override;

  void setMonoSum(bool monoSum);

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  struct Lane {
    Lane(Services& services, bool left);
    DimGroup panOff;
    ParamKnob pan;
    SegmentedText chips;
    ParamBinding solo, invert;
    // Bounds within the rail (the knob wrap + chips), for the connectors.
    juce::Rectangle<int> wrap;
  };

  class MonoChip : public juce::Component {
  public:
    void paint(juce::Graphics& g) override;
  };

  void syncChips();
  void toggleSolo(bool left);

  Services& services_;
  Lane left_, right_;
  ParamBinding linked_;
  ChromeIconButton link_, swap_;
  MonoChip mono_;
  bool monoSum_ = false;
  juce::Rectangle<int> pill_;
};

}  // namespace t3k::ui
