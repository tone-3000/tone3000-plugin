#include "PresetStore.h"

namespace t3k::ui {

PresetStore::PresetStore(Backend& backend, ChainStore& chain) : backend_(backend), chain_(chain) {
  refresh();
}

void PresetStore::refresh() {
  presets_ = parsePresetList(backend_.getPresetList());
  listeners.call([this](Listener& l) { l.presetsChanged(presets_); });
}

template <typename Fn>
auto PresetStore::run(Fn&& fn) -> decltype(fn()) {
  auto result = fn();
  refresh();
  chain_.refresh();
  return result;
}

std::optional<PresetInfo> PresetStore::save(const juce::String& name) {
  const auto res = run([&] { return backend_.savePreset(name); });
  if (!res.isObject()) return std::nullopt;
  PresetInfo info;
  info.id = res.getProperty("id", "").toString();
  info.name = res.getProperty("name", "").toString();
  info.factory = false;
  return info.id.isEmpty() ? std::nullopt : std::optional<PresetInfo>(info);
}

bool PresetStore::load(const juce::String& id) {
  return run([&] { return backend_.loadPreset(id); });
}

bool PresetStore::rename(const juce::String& id, const juce::String& name) {
  return run([&] { return backend_.renamePreset(id, name); });
}

bool PresetStore::remove(const juce::String& id) {
  return run([&] { return backend_.deletePreset(id); });
}

bool PresetStore::move(const juce::String& id, int delta) {
  return run([&] { return backend_.movePreset(id, delta); });
}

bool PresetStore::step(int delta) {
  return run([&] { return backend_.stepPreset(delta); });
}

}  // namespace t3k::ui
