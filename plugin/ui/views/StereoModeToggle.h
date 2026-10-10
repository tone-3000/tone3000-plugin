// Mono/stereo pill switch in the top bar (port of StereoModeToggle.tsx):
// an 80x36 SURFACE_RAISED pill with a 36x28 HIGHLIGHT that snaps to the
// selected side, and one icon segment per mode.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace t3k::ui {

class StereoModeToggle : public juce::Component {
public:
  static constexpr int kWidth = 80;
  static constexpr int kHeight = 36;

  StereoModeToggle();
  ~StereoModeToggle() override;

  void setStereoEnabled(bool enabled);
  std::function<void(bool stereo)> onToggle;

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  class Segment;
  std::unique_ptr<Segment> mono_, stereo_;
  bool stereoEnabled_ = false;
};

}  // namespace t3k::ui
