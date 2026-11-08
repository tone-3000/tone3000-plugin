// The audition loop's state, polled from the backend on the UiClock tick.
// The header's loop button paints from it and
// sends its taps through it; the browser will read it to know whether a
// card click previews or commits. The backend consumes a tap at its next
// audio callback, so the button follows the click one tick later; the
// testbed's mock answers at once.
#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "UiClock.h"
#include "backend/Backend.h"

namespace t3k::ui {

class LoopFeed : private UiClock::Listener {
public:
  // The LoopRecorder's states, by the names the backend ships.
  enum class Phase { empty, armed, recording, playing, stopped };
  static Phase parsePhase(const juce::String& name);
  static const char* phaseName(Phase phase);

  struct State {
    Phase phase = Phase::empty;
    double position = 0;       // 0..1 through the loop (recording: the take's fill)
    double lengthSeconds = 0;  // once the take has closed
    bool operator==(const State& o) const {
      return phase == o.phase && juce::exactlyEqual(position, o.position) &&
             juce::exactlyEqual(lengthSeconds, o.lengthSeconds);
    }
    bool operator!=(const State& o) const { return !(*this == o); }
  };

  struct Listener {
    virtual ~Listener() = default;
    virtual void loopChanged(const State& state) = 0;
  };

  LoopFeed(Backend& backend, UiClock& clock);
  ~LoopFeed() override;

  const State& state() const { return state_; }
  // Whether a loop is standing in for the player right now: the browser's
  // cue that a card click previews rather than commits.
  bool playing() const { return state_.phase == Phase::playing; }

  // The one footswitch, and its hold.
  void tap();
  void clear();

  // Read the backend now (the clock tick does this; tests call it directly).
  void poll();

  void addListener(Listener* l) { listeners_.add(l); }
  void removeListener(Listener* l) { listeners_.remove(l); }

private:
  void tick() override { poll(); }

  Backend& backend_;
  UiClock& clock_;
  State state_;
  juce::ListenerList<Listener> listeners_;
};

}  // namespace t3k::ui
