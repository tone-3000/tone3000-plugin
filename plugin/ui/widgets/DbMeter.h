// Main input/output meter (port of DbMeter.tsx): a dB label rail beside one
// dot column, or two (L/R) sharing the scale in stereo. The meter keeps a
// fixed mono-width footprint; in stereo the labels+dots row widens by one
// column and overflows the footprint symmetrically, so the dot pair's centre
// lands where the mono column's was (over the gain knob) and the label gap
// stays constant. That overflow is 5.5px, so the whole meter is painted in
// float coordinates instead of positioned as child components.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "DotMeter.h"
#include "services/MeterStore.h"

namespace t3k::ui {

class DbMeter : public juce::Component, private MeterStore::Listener {
public:
  enum class Labels { left, right };

  static constexpr float kDotSize = 6;
  static constexpr float kDotGap = 10;
  static constexpr float kColumnGap = 5;
  static constexpr float kLabelGap = 10;
  // Tighter when labels sit right of the dots: right-aligned digits are
  // ragged on the side facing the dots, so the gap already reads larger.
  static constexpr float kLabelGapRight = 6;
  static constexpr float kLabelWidth = 18;
  // The mono footprint (labels + gap + one column), independent of side.
  static constexpr int kFootprint = static_cast<int>(kLabelWidth + kLabelGap + kDotSize);
  // Component width: the footprint plus room for the stereo overflow on both
  // sides. Place it at slotX - kInset to put the footprint on the slot.
  static constexpr int kInset = 6;
  static constexpr int kWidth = kFootprint + 2 * kInset;

  // `height` is the requested column extent; the meter rounds down to whole
  // dots and sizes itself to the actual column height.
  DbMeter(MeterStore& meters, bool input, int height, Labels labels);
  ~DbMeter() override;

  void setStereo(bool stereo);
  bool stereo() const { return columns_.size() == 2; }
  // Local centre of column `i`'s clip LED (testbed drives hover it).
  juce::Point<int> clipDotCentre(int column) const;

  void paint(juce::Graphics& g) override;
  void mouseMove(const juce::MouseEvent& e) override;
  void mouseExit(const juce::MouseEvent& e) override;
  void mouseDown(const juce::MouseEvent& e) override;

private:
  void meterChanged(const juce::String& id) override;
  // The dots' box for column `i` in local float coordinates.
  juce::Rectangle<float> columnBox(int i) const;
  // Index of the column whose lit clip LED is under `p`, or -1.
  int clipColumnAt(juce::Point<int> p) const;
  void refreshAffordance(juce::Point<int> p);

  MeterStore& meters_;
  bool input_;
  Labels labels_;
  DotRail rail_;
  std::vector<juce::String> columns_;  // meter ids, left to right
};

}  // namespace t3k::ui
