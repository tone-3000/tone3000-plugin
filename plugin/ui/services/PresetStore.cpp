#include "PresetStore.h"

namespace t3k::ui {

PresetStore::PresetStore(Backend& backend, ChainStore& chain) : backend_(backend), chain_(chain) {
  refresh();
}

void PresetStore::refresh() {
  const auto payload = backend_.getPresetList();
  presets_ = parsePresetList(payload);
  categories_ = parsePresetCategories(payload);
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

bool PresetStore::addCategory(const juce::String& name) {
  return run([&] { return backend_.addPresetCategory(name); });
}

bool PresetStore::deleteCategory(const juce::String& name) {
  return run([&] { return backend_.deletePresetCategory(name); });
}

bool PresetStore::setCategory(const juce::String& id, const juce::String& category) {
  return run([&] { return backend_.setPresetCategory(id, category); });
}

bool PresetStore::moveToCategory(const juce::StringArray& ids, const juce::String& category) {
  return run([&] { return backend_.movePresetsToCategory(ids, category); });
}

bool PresetStore::setFavorite(const juce::String& id, bool favorite) {
  return run([&] { return backend_.setPresetFavorite(id, favorite); });
}

bool PresetStore::setFavorites(const juce::StringArray& ids, bool favorite) {
  return run([&] { return backend_.setPresetsFavorite(ids, favorite); });
}

std::vector<PresetInfo> PresetStore::duplicate(const juce::StringArray& ids) {
  const auto res = run([&] { return backend_.duplicatePresets(ids); });
  std::vector<PresetInfo> out;
  if (const auto* arr = res.getArray())
    for (const auto& p : *arr)
      out.push_back(PresetInfo{p["id"].toString(), p["name"].toString(), false,
                               p["category"].toString(), static_cast<bool>(p["favorite"])});
  return out;
}

bool PresetStore::removeMany(const juce::StringArray& ids) {
  return run([&] { return backend_.deletePresets(ids); });
}

}  // namespace t3k::ui
