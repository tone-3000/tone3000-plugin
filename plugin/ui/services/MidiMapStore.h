// MIDI mapping state (port of useMidiMap.ts): native is the source of truth
// and pushes `midiMapChanged` on every map change (learn commits happen when
// the user moves a hardware control, not from any UI action); we re-pull on
// the event and after every mutation. Works in hosted builds too.
#pragma once

#include <optional>

#include "backend/Backend.h"
#include "model/MidiMapState.h"

namespace t3k::ui {

class MidiMapStore : private Backend::Listener {
public:
  struct Listener {
    virtual ~Listener() = default;
    virtual void midiMapChanged() = 0;
  };

  explicit MidiMapStore(Backend& backend);
  ~MidiMapStore() override;

  // nullopt until the first successful pull.
  const std::optional<MidiMapState>& state() const { return state_; }
  void refresh();

  void addListener(Listener* l) { listeners.add(l); }
  void removeListener(Listener* l) { listeners.remove(l); }

  void setChannel(int channel);
  void startLearn(const juce::String& targetId);
  void cancelLearn();
  void removeMapping(const juce::String& targetId);
  // Assign a CC number directly, the typed alternative to learn.
  void setCcMapping(const juce::String& targetId, int number);

private:
  void midiMapChanged() override { refresh(); }

  Backend& backend_;
  std::optional<MidiMapState> state_;
  juce::ListenerList<Listener> listeners;
};

}  // namespace t3k::ui
