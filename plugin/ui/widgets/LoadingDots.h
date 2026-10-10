// The three blinking dots from tone3000.com (LoadingDots.tsx): 8px discs,
// 4px apart, each fading 0.2 → 1 → 0.2 over 1.4 s, staggered by 0.2 s.
// Animates only while showing.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace t3k::ui {

class LoadingDots : public juce::Component, private juce::Timer {
public:
  static constexpr int kDot = 8;
  static constexpr int kMargin = 2;  // each side of every dot
  static constexpr int kWidth = 3 * (kDot + 2 * kMargin);

  LoadingDots();

  void paint(juce::Graphics& g) override;
  void visibilityChanged() override;
  void parentHierarchyChanged() override;

private:
  void timerCallback() override { repaint(); }
  void syncTimer();
  // Opacity of dot `i` at `t` seconds into the loop.
  static float opacityAt(int i, double t);

  double startSeconds_ = 0;
};

}  // namespace t3k::ui
