// A CSS `transition: opacity Nms ease` for a component: tween its alpha to a
// target over a duration. Owned by the component it drives; DimGroup, the
// tile image dim and the EQ views animate through this.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Tween.h"

namespace t3k::ui {

class AlphaTween {
public:
  explicit AlphaTween(juce::Component& target)
      : tween_(target, [&target](float a) { target.setAlpha(a); }, 1.0f) {}

  float value() const { return tween_.value(); }

  void animateTo(float alpha, int durationMs, bool animate = true) {
    if (animate) tween_.animateTo(alpha, durationMs);
    else tween_.snap(alpha);
  }

  void snap(float alpha) { tween_.snap(alpha); }

private:
  Tween tween_;
};

}  // namespace t3k::ui
