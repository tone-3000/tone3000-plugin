// A cancellable one-shot timer (the setTimeout/clearTimeout pair the web UI
// leaned on for debounces, hold gestures and entrance animations). Owned by
// a component so it can never outlive what it calls into.
#pragma once

#include <juce_events/juce_events.h>

#include <functional>

namespace t3k::ui {

class DelayedCall : private juce::Timer {
public:
  ~DelayedCall() override { stopTimer(); }

  // (Re)arms: a pending call is replaced.
  void start(int ms, std::function<void()> fn) {
    fn_ = std::move(fn);
    startTimer(juce::jmax(1, ms));
  }
  void cancel() {
    stopTimer();
    fn_ = nullptr;
  }
  bool pending() const { return isTimerRunning(); }

private:
  void timerCallback() override {
    stopTimer();
    auto fn = std::move(fn_);
    fn_ = nullptr;
    if (fn) fn();
  }

  std::function<void()> fn_;
};

}  // namespace t3k::ui
