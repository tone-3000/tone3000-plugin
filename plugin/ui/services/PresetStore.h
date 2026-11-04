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
  const std::vector<juce::String>& categories() const { return categories_; }
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

  // Categories, stars and bulk actions. Category names are validated by the
  // store (trimmed, <= 50 chars, unique ignoring case, must exist to be filed
  // under); each returns false when refused.
  bool addCategory(const juce::String& name);
  bool deleteCategory(const juce::String& name);  // its presets fall back to the root
  bool setCategory(const juce::String& id, const juce::String& category);
  bool moveToCategory(const juce::StringArray& ids, const juce::String& category);
  bool setFavorite(const juce::String& id, bool favorite);
  bool setFavorites(const juce::StringArray& ids, bool favorite);
  std::vector<PresetInfo> duplicate(const juce::StringArray& ids);  // the new copies
  bool removeMany(const juce::StringArray& ids);

private:
  template <typename Fn>
  auto run(Fn&& fn) -> decltype(fn());

  Backend& backend_;
  ChainStore& chain_;
  std::vector<PresetInfo> presets_;
  std::vector<juce::String> categories_;
  juce::ListenerList<Listener> listeners;
};

}  // namespace t3k::ui
