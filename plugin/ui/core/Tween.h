// One CSS `transition: <property> Nms ease` for a float: tween from the
// current value to a target over a duration, applying each frame through a
// callback. Frames come from the owner's display refresh (juce_animation's
// VBlankAnimatorUpdater), so a tween can't fire between vsyncs or twice in
// one; an owner that isn't showing has nothing to animate and snaps. Owned
// by the component it drives, so it can never outlive the target.
#pragma once

#include <juce_animation/juce_animation.h>

#include <functional>
#include <optional>

namespace t3k::ui {

class Tween {
public:
  Tween(juce::Component& owner, std::function<void(float)> apply, float initial = 0)
      : owner_(owner), frames_(&owner), apply_(std::move(apply)), value_(initial) {}

  float value() const { return value_; }
  bool running() const { return animator_ && !animator_->isComplete(); }

  // Ease to `target` (CSS `ease`); a tween already running restarts from
  // where it is.
  void animateTo(float target, int durationMs) {
    if (durationMs <= 0 || !owner_.isShowing()) {
      snap(target);
      return;
    }
    animator_ = juce::ValueAnimatorBuilder{}
                    .withDurationMs(durationMs)
                    .withEasing(juce::Easings::createEase())
                    .withValueChangedCallback([this, from = value_, target](float t) {
                      value_ = from + (target - from) * t;
                      apply_(value_);
                    })
                    .build();
    frames_.addAnimator(*animator_);
    animator_->start();
  }

  void snap(float target) {
    animator_.reset();  // the updater drops its expired reference on the next frame
    value_ = target;
    apply_(value_);
  }

private:
  juce::Component& owner_;
  juce::VBlankAnimatorUpdater frames_;
  std::optional<juce::Animator> animator_;
  std::function<void(float)> apply_;
  float value_;
};

}  // namespace t3k::ui
