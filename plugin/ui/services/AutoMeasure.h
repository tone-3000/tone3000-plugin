// Drives a one-shot native measurement (port of useAutoMeasure.ts): auto
// balance listens to the player for ~2 s and matches the two chains' levels;
// auto align runs an internal probe (output muted for under half a second)
// and measures the inter-chain lag. toggle() arms or cancels; while armed we
// poll until the native state machine leaves 'listening' (done, timeout or
// cancel). The toast follows the flow: a pinned "Listening"/"Measuring"
// while armed, then the formatted result on success; cancel and timeout just
// take it down.
#pragma once

#include <juce_events/juce_events.h>

#include "Toast.h"
#include "backend/Backend.h"
#include "model/ChainState.h"

namespace t3k::ui {

class AutoMeasure : private juce::Timer {
public:
  enum class Kind { balance, align };

  struct Listener {
    virtual ~Listener() = default;
    virtual void autoMeasureChanged() = 0;
  };

  AutoMeasure(Backend& backend, Toast& toast, Kind kind);
  ~AutoMeasure() override;

  bool listening() const { return listening_; }
  void toggle();

  void addListener(Listener* l) { listeners.add(l); }
  void removeListener(Listener* l) { listeners.remove(l); }

private:
  // The result line for a finished measurement.
  static juce::String doneMessage(Kind kind, const AutoMeasureResult& result);
  void timerCallback() override;
  void setListening(bool listening);

  Backend& backend_;
  Toast& toast_;
  Kind kind_;
  bool listening_ = false;
  juce::ListenerList<Listener> listeners;
};

}  // namespace t3k::ui
