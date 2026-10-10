// Input channel selection (SystemSettings.tsx InputChannelPicker) with
// JUCE's min-1 / max-2 window surfaced as an explicit Mono / Stereo control:
// radio behaviour in mono, pick-any-two (oldest swapped out) in stereo, each
// row with a live peak meter. Selection *order* lives here (native only
// knows the mask) so stereo's third pick can swap out the oldest choice.
#pragma once

#include <memory>
#include <vector>

#include "InlineBannerAlert.h"
#include "services/Services.h"
#include "widgets/form/FormRows.h"

namespace t3k::ui {

class InputChannelPicker : public FieldRow {
public:
  static constexpr int kListMaxHeight = 296;
  // Compact horizontal strip (~8 dots); same DotMeter as the block rails.
  static constexpr int kMeterLength = 100;
  // Channel rows: padding 8px 4px, 12px gaps, 8px hover radius.
  static constexpr int kRowPadY = 8, kRowPadX = 4, kRowGap = 12, kRowRadius = 8;
  static constexpr int kIndexMinWidth = 16;
  static constexpr float kNamePx = 11;

  explicit InputChannelPicker(Services& services);
  ~InputChannelPicker() override;

  void update(const AudioDeviceState& state);

  void visibilityChanged() override { syncMetering(); }
  void parentHierarchyChanged() override { syncMetering(); }

private:
  class ChannelRow;
  class ChannelList;

  bool stereo() const { return active_.size() >= 2; }
  void selectChannel(int index);
  void switchMode(bool stereo);
  // Meters run only while this picker is on screen (native registers a raw
  // device tap, so signal shows even while Hear Yourself is muted).
  void syncMetering();
  void levelsChanged();

  Services& services_;
  std::vector<AudioInputChannel> channels_;
  std::vector<int> active_;
  // Oldest → newest selection order, reconciled against native truth.
  std::vector<int> order_;
  bool deviceOpen_ = false;
  juce::String inputDevice_;

  SegmentedControl mode_;
  std::unique_ptr<ChannelList> list_;
  Paragraph stereoCaption_;
  Paragraph emptyCaption_;
  InlineBannerAlert noInput_{"no-input"};
  std::unique_ptr<AudioInputLevels> levels_;
};

}  // namespace t3k::ui
