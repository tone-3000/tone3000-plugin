// Dim scrim over in-place content while it reloads (LoadingDots.tsx
// BusyOverlay): the content stays underneath, visibly disabled behind
// `rgba(0,0,0,.5)` and a `backdrop-filter: blur(4px)`, and the blinking dots
// sit on top, pinned near the top (list reloads) or centred.
//
// The blur is a half-scale snapshot of the parent's area beneath the overlay
// (taken with the overlay hidden), box-blurred and refreshed on a slow timer
// so artwork landing underneath shows through, the same scheme as
// ModalLayer's scrim.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LoadingDots.h"

namespace t3k::ui {

class BusyOverlay : public juce::Component, private juce::Timer {
public:
  enum class Align { top, centre };
  static constexpr int kTopPad = 64;
  static constexpr float kBlurPx = 4;
  static constexpr float kSnapshotScale = 0.5f;
  static constexpr int kRefreshMs = 125;

  explicit BusyOverlay(Align align = Align::top);
  ~BusyOverlay() override;

  // Follow the host's rounded corners (the web's overflow: hidden parent).
  void setCornerRadius(float radius) { radius_ = radius; }

  void paint(juce::Graphics& g) override;
  void resized() override;
  void visibilityChanged() override;
  void parentHierarchyChanged() override;

private:
  void timerCallback() override { refresh(); }
  void refresh();

  Align align_;
  float radius_ = 0;
  bool snapshotting_ = false;
  juce::Image blurred_;
  LoadingDots dots_;
};

}  // namespace t3k::ui
