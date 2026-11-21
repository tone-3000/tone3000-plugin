// Dot-strip level meters (port of BlockMeter.tsx DotMeter/BlockMeter and the
// dot columns in DbMeter.tsx): the full colour scale is always visible
// (dimmed) and lights up to the level. Dots sit at exact dB thresholds from
// meter::kMinDb to 0 dBFS; the last dot (top / right) is the clip LED, which
// lights only while the clip latch is set and clears on click.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "services/MeterStore.h"

namespace t3k::ui {

// Clip latches for screen readers and the keyboard, shared by DotMeter,
// DbMeter and BlockLed. Levels stay silent (they change every frame); only a
// latch speaks, once, and while latched the meter becomes a "Clear ... clip"
// button in the Tab order (never focused by a click), so the latch the
// pointer clears is clearable from the keyboard too.
namespace clipA11y {
// "<name> clipping", at most once every few seconds across all meters, so
// a hot chain doesn't talk over everything.
void announce(const juce::String& name);
// The nearest ancestor's screen-reader name (the tile's tone, a card's
// title), to say which block clipped; empty when there is none.
juce::String context(const juce::Component& component);
// Latched: accessible and focusable. Unlatched: out of the tree again,
// handing keyboard focus on first so it isn't dropped.
void setLatched(juce::Component& component, bool latched);
// The button handler: press clears; Enter and Space go through keyPress().
std::unique_ptr<juce::AccessibilityHandler> handler(juce::Component& component, std::function<void()> clear);
bool keyPress(const juce::KeyPress& key, const std::function<void()>& clear);
// Keyboard focus ring on the lit LED itself (inside its bounds).
void paintFocus(juce::Graphics& g, juce::Rectangle<float> dot);
}  // namespace clipA11y

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
  // Screen-reader name of what the strip measures ("input"), for "Clear
  // input clip" and "<block> input clipping".
  void setClipName(const juce::String& name) { clipName_ = name; }

  // Invoked by a click on the lit clip LED. Unset = the LED isn't clickable
  // (meter sits inside another control).
  std::function<void()> onClearClip;

  void paint(juce::Graphics& g) override;
  void mouseMove(const juce::MouseEvent& e) override;
  void mouseExit(const juce::MouseEvent& e) override;
  void mouseDown(const juce::MouseEvent& e) override;
  bool keyPressed(const juce::KeyPress& key) override;
  void focusGained(FocusChangeType) override { repaint(); }
  void focusLost(FocusChangeType) override { repaint(); }
  std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
  void clearClip();
  bool overClearableClip(juce::Point<int> p) const;
  void refreshAffordance(juce::Point<int> p);

  DotRail rail_;
  float db_ = MeterStore::kFloorDb;
  bool clipped_ = false;
  juce::String clipName_;
};

// DotMeter wired to a MeterStore id (BlockMeter.tsx): level and clip latch
// arrive through the store; clicking the LED clears the latch there.
class LiveDotMeter : public DotMeter, private MeterStore::Listener {
public:
  LiveDotMeter(MeterStore& meters, juce::String id, int length, bool vertical);
  ~LiveDotMeter() override;

  // Follow another meter id (a block's output: both channels or one).
  void setId(juce::String id);
  const juce::String& id() const { return id_; }

private:
  void meterChanged(const juce::String& id) override;
  void sync();

  MeterStore& meters_;
  juce::String id_;
};

}  // namespace t3k::ui
