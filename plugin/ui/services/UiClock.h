// One 30 Hz heartbeat for everything the UI polls from the backend: meter
// levels, the EQ spectrum, the tuner reading, the chain revision, raw input
// levels. One tick puts every poll, and every repaint it causes, in the same
// frame; separate timers would spread them over staggered frames and wake
// the message thread once per poller.
//
// Runs only while something is subscribed, and idles while the editor is
// not on screen (`visible` says so: minimised standalone window, hidden
// editor): then it just re-checks a few times a second and no poll, parse
// or repaint happens until the window is back.
#pragma once

#include <juce_events/juce_events.h>

#include <functional>

namespace t3k::ui {

class UiClock : private juce::Timer {
public:
  static constexpr int kHz = 30;
  static constexpr int kPeriodMs = 1000 / kHz;
  // Re-check cadence while hidden: the first tick after the window is back
  // lands within this.
  static constexpr int kHiddenPeriodMs = 250;

  struct Listener {
    virtual ~Listener() = default;
    virtual void tick() = 0;
  };

  UiClock() = default;
  ~UiClock() override { stopTimer(); }

  // Whether the UI is on screen; unset means always. Cheap: it is asked
  // every tick.
  std::function<bool()> visible;

  void addListener(Listener* l) {
    listeners_.add(l);
    if (!isTimerRunning()) startTimer(kPeriodMs);
  }
  void removeListener(Listener* l) {
    listeners_.remove(l);
    if (listeners_.isEmpty()) stopTimer();
  }

  // Call when the UI may have just come back on screen so the next tick is
  // immediate rather than up to kHiddenPeriodMs away.
  void wake() {
    if (isTimerRunning() && getTimerInterval() != kPeriodMs) startTimer(kPeriodMs);
  }

private:
  void timerCallback() override {
    const bool onScreen = !visible || visible();
    const int period = onScreen ? kPeriodMs : kHiddenPeriodMs;
    if (getTimerInterval() != period) startTimer(period);
    if (!onScreen) return;
    listeners_.call([](Listener& l) { l.tick(); });
  }

  juce::ListenerList<Listener> listeners_;
};

}  // namespace t3k::ui
