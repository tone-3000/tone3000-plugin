// Dot-strip level meters (port of BlockMeter.tsx DotMeter/BlockMeter and the
// dot columns in DbMeter.tsx): the full colour scale is always visible
// (dimmed) and lights up to the level. Dots sit at exact dB thresholds from
// meter::kMinDb to 0 dBFS; the last dot (top / right) is the clip LED, which
// lights only while the clip latch is set and clears on click.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "services/MeterStore.h"

namespace t3k::ui {

// Geometry + painter for one strip, shared by DotMeter (a component of its
// own) and DbMeter (which paints two strips beside a label rail in float
// coordinates, since its stereo overflow lands on half pixels).
struct DotRail {
  float dotSize;
  float gap;
  int count;
  bool vertical;

  // Extent along the meter axis.
  float length() const { return count * dotSize + (count - 1) * gap; }
  // Bounds of the strip when its low end sits at `origin` (bottom-left for a
  // vertical rail, left for a horizontal one), i.e. the box the dots fill.
  juce::Rectangle<float> boxAt(juce::Point<float> origin) const;
  // Centre of dot `i` (0 = quietest) inside `box`.
  juce::Point<float> centre(int i, juce::Rectangle<float> box) const;
  juce::Rectangle<float> dot(int i, juce::Rectangle<float> box) const {
    return juce::Rectangle<float>(dotSize, dotSize).withCentre(centre(i, box));
  }
  juce::Rectangle<float> clipDot(juce::Rectangle<float> box) const { return dot(count - 1, box); }

  void paint(juce::Graphics& g, juce::Rectangle<float> box, float db, bool clipped) const;

  // Dot counts the two web meters derive from their pixel length.
  static int countForColumn(int height, float dotSize, float gap);   // DbMeter: floor(h / pitch)
  static int countForStrip(int length, float dotSize, float gap);    // DotMeter: max(2, floor((l + gap) / pitch))
};

// Presentational strip: the caller feeds it a level and clip state.
class DotMeter : public juce::Component {
public:
  static constexpr float kDotSize = 4;
  // Matches DbMeter's gap so the rails read as one family.
  static constexpr float kGap = 10;

  DotMeter(int length, bool vertical);

  void setLevel(float db);
  void setClipped(bool clipped);
  float level() const { return db_; }
  bool clipped() const { return clipped_; }

  // Invoked by a click on the lit clip LED. Unset = the LED isn't clickable
  // (meter sits inside another control).
  std::function<void()> onClearClip;

  void paint(juce::Graphics& g) override;
  void mouseMove(const juce::MouseEvent& e) override;
  void mouseExit(const juce::MouseEvent& e) override;
  void mouseDown(const juce::MouseEvent& e) override;

private:
  bool overClearableClip(juce::Point<int> p) const;
  void refreshAffordance(juce::Point<int> p);

  DotRail rail_;
  float db_ = MeterStore::kFloorDb;
  bool clipped_ = false;
};

// DotMeter wired to a MeterStore id (BlockMeter.tsx): level and clip latch
// arrive through the store; clicking the LED clears the latch there.
class LiveDotMeter : public DotMeter, private MeterStore::Listener {
public:
  LiveDotMeter(MeterStore& meters, juce::String id, int length, bool vertical);
  ~LiveDotMeter() override;

private:
  void meterChanged(const juce::String& id) override;
  void sync();

  MeterStore& meters_;
  juce::String id_;
};

}  // namespace t3k::ui
