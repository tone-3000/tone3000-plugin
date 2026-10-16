// Preset list access (port of usePresets.ts). The list is fetched on demand
// (construction + after every mutation), never polled: native rescans the
// shared presets folder on each call so other instances' saves show up too.
// The *active* preset rides the chain state; every mutation here resyncs
// the ChainStore so the UI converges immediately.
#pragma once

#include "ChainStore.h"

namespace t3k::ui {

class PresetStore {
public:
  struct Listener {
    virtual ~Listener() = default;
    virtual void presetsChanged(const std::vector<PresetInfo>& presets) = 0;
  };

  PresetStore(Backend& backend, ChainStore& chain);

  const std::vector<PresetInfo>& presets() const { return presets_; }
  void refresh();

  void addListener(Listener* l) { listeners.add(l); }
  void removeListener(Listener* l) { listeners.remove(l); }

  // Save current state under `name` (same-name user preset is overwritten);
  // the new preset, or nullopt.
  std::optional<PresetInfo> save(const juce::String& name);
  bool load(const juce::String& id);
  bool rename(const juce::String& id, const juce::String& name);
  bool remove(const juce::String& id);
  // N steps within the preset's section (negative = earlier).
  bool move(const juce::String& id, int delta);
  // Load the next (+1) / previous (-1) preset: through the active preset's
  // Library folder when it came from one, else the list (native decides, the
  // same walk the MIDI preset steps take).
  bool step(int delta);

private:
  template <typename Fn>
  auto run(Fn&& fn) -> decltype(fn());

  Backend& backend_;
  ChainStore& chain_;
  std::vector<PresetInfo> presets_;
  juce::ListenerList<Listener> listeners;
};

}  // namespace t3k::ui
