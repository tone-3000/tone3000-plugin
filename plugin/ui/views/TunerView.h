// The chromatic tuner takeover (TunerView.tsx): six tapered bars a side
// receding toward the note, blue → yellow → red from the centre out; the
// note letter with its accidental hung off the right, frequency and cents
// underneath, and a blue triangle above / below saying which way to tune.
// Idle keeps the grey tracks and hides the readout, so nothing shifts when
// a pitch locks. Replaces the whole meters + chain band.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "services/Services.h"
#include "services/TunerFeed.h"
#include "widgets/IconButton.h"

namespace t3k::ui {

class TunerView : public juce::Component {
public:
  // Bar panel from the idle-state SVG (50×181, inner edge running 30→151),
  // six per side at a 16 gap; 32 to the 120-wide centre column.
  static constexpr int kBarW = 50, kBarH = 181, kBarGap = 16;
  static constexpr float kTaperTop = 30.0f / 181, kTaperBottom = 151.0f / 181;
  static constexpr int kSideGap = 32, kCentreW = 120, kCentreGap = 36;
  static constexpr int kTriangleW = 61, kTriangleH = 53;
  static constexpr float kNotePx = 110;
  static constexpr int kCloseTop = 16, kCloseRight = 20, kCloseBox = 28, kCloseGlyph = 20;

  explicit TunerView(Services& services);
  ~TunerView() override;

  // The top-right ✕ (the header's tuner button also closes).
  std::function<void()> onClose;

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  class Triangle;
  enum class Side { left, right };

  void feedChanged();
  juce::Rectangle<int> readoutArea() const;
  void paintBars(juce::Graphics& g, Side side, juce::Rectangle<int> row, int litCount) const;
  void paintReadout(juce::Graphics& g, juce::Rectangle<int> box) const;

  TunerFeed feed_;
  IconButton close_{Icon::X, kCloseBox, kCloseGlyph};
  std::unique_ptr<Triangle> up_, down_;
  juce::Rectangle<int> leftRow_, rightRow_, noteBox_;
  // What is on screen, so a feed change dirties only what it moved.
  int shownLeftLit_ = 0, shownRightLit_ = 0;
  juce::String shownReadout_;
};

}  // namespace t3k::ui
