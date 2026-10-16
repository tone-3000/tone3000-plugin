#include "UiPrefs.h"

namespace t3k::ui {

// Holds the process lock for one read-modify-write (re-entrant, so the
// PropertiesFile's own locking inside reload() / save() nests). A lock that
// can't be had within the timeout is skipped rather than waited on: the
// write still lands, just without the merge.
class UiPrefs::Guard {
public:
  explicit Guard(juce::InterProcessLock* lock) : lock_(lock), held_(lock != nullptr && lock->enter(kLockTimeoutMs)) {}
  ~Guard() {
    if (held_) lock_->exit();
  }
private:
  juce::InterProcessLock* lock_;
  bool held_;
};

UiPrefs::UiPrefs(juce::PropertiesFile* file, juce::InterProcessLock* lock) : file_(file), lock_(lock) {}

UiPrefs::~UiPrefs() {
  if (file_ != nullptr) file_->saveIfNeeded();
}

juce::String UiPrefs::get(const juce::String& key, const juce::String& fallback) const {
  if (isApart(key)) {
    const auto value = getJson(key);
    return value.isVoid() ? fallback : juce::JSON::toString(value, true);
  }
  if (file_ != nullptr)
    return file_->containsKey(key) ? file_->getValue(key) : fallback;
  auto it = memory_.find(key);
  return it == memory_.end() ? fallback : it->second;
}

bool UiPrefs::getBool(const juce::String& key, bool fallback) const {
  const auto v = get(key);
  if (v == "true") return true;
  if (v == "false") return false;
  return fallback;
}

juce::var UiPrefs::getJson(const juce::String& key) const {
  if (const auto it = apart_.find(key); it != apart_.end()) {
    auto& apart = it->second;
    if (file_ == nullptr) return apart.value;  // memory only: the value itself
    const auto file = apartFile(key);
    if (const auto stamp = stampOf(file); !apart.loaded || stamp != apart.stamp) {
      apart.value = file.existsAsFile() ? juce::JSON::parse(file.loadFileAsString()) : juce::var();
      apart.stamp = stamp;
      apart.loaded = true;
    }
    return apart.value;
  }
  const auto raw = get(key);
  return raw.isEmpty() ? juce::var() : juce::JSON::parse(raw);
}

void UiPrefs::storeApart(const juce::String& key) {
  if (isApart(key)) return;
  apart_[key] = {};
  if (file_ == nullptr) {
    // Memory only (the testbed): what is there already moves over.
    if (const auto it = memory_.find(key); it != memory_.end()) {
      apart_[key].value = juce::JSON::parse(it->second);
      memory_.erase(it);
    }
    return;
  }
  // A value still in the prefs file (an older build put it there) moves to
  // its own file, once.
  Guard guard(lock_);
  pullLocked();
  if (!file_->containsKey(key)) return;
  const auto file = apartFile(key);
  if (!file.existsAsFile()) {
    file.getParentDirectory().createDirectory();
    file.replaceWithText(file_->getValue(key));
  }
  file_->removeValue(key);
  file_->save();
}

juce::File UiPrefs::apartFile(const juce::String& key) const {
  return file_->getFile().getSiblingFile(juce::File::createLegalFileName(key) + ".json");
}

juce::int64 UiPrefs::stampOf(const juce::File& file) {
  return file.existsAsFile() ? file.getLastModificationTime().toMilliseconds() * 1000003 + file.getSize() : -1;
}

void UiPrefs::set(const juce::String& key, const juce::String& value) {
  if (isApart(key)) return setJson(key, juce::JSON::parse(value));
  if (file_ == nullptr) {
    if (auto it = memory_.find(key); it != memory_.end() && it->second == value) return;
    memory_[key] = value;
    notify({key});
    return;
  }
  Guard guard(lock_);
  auto changed = pullLocked();
  if (!file_->containsKey(key) || file_->getValue(key) != value) {
    file_->setValue(key, value);
    file_->save();
    changed.addIfNotAlreadyThere(key);
  }
  notify(changed);
}

void UiPrefs::setJson(const juce::String& key, const juce::var& value) {
  if (const auto it = apart_.find(key); it != apart_.end()) {
    auto& apart = it->second;
    if (file_ != nullptr) {
      // Through a temporary file: a reader never sees half of one.
      const auto file = apartFile(key);
      file.getParentDirectory().createDirectory();
      const auto temp = file.getSiblingFile(file.getFileName() + ".tmp");
      if (temp.replaceWithText(juce::JSON::toString(value, true)) && !temp.moveFileTo(file)) temp.deleteFile();
      apart.stamp = stampOf(file);
      apart.loaded = true;
    }
    apart.value = value;
    notify({key});
    return;
  }
  set(key, juce::JSON::toString(value, true));
}

void UiPrefs::remove(const juce::String& key) {
  if (const auto it = apart_.find(key); it != apart_.end()) {
    if (file_ != nullptr) apartFile(key).deleteFile();
    it->second = {};
    it->second.loaded = file_ != nullptr;
    notify({key});
    return;
  }
  if (file_ == nullptr) {
    if (memory_.erase(key) == 0) return;
    notify({key});
    return;
  }
  Guard guard(lock_);
  auto changed = pullLocked();
  if (file_->containsKey(key)) {
    file_->removeValue(key);
    file_->save();
    changed.addIfNotAlreadyThere(key);
  }
  notify(changed);
}

void UiPrefs::sync() {
  if (file_ == nullptr) return;
  Guard guard(lock_);
  notify(pullLocked());
}

juce::StringArray UiPrefs::pullLocked() {
  // Nothing on disk yet (first run): ours is the only copy.
  if (!file_->getFile().existsAsFile()) return {};
  const juce::StringPairArray before = file_->getAllProperties();
  file_->clear();
  if (!file_->reload()) {  // unreadable: keep what we had
    for (const auto& k : before.getAllKeys()) file_->setValue(k, before[k]);
    file_->setNeedsToBeSaved(false);
    return {};
  }
  file_->setNeedsToBeSaved(false);  // clear() + reload() only marked it dirty
  const auto& after = file_->getAllProperties();
  juce::StringArray changed;
  for (const auto& k : before.getAllKeys())
    if (!after.containsKey(k) || after[k] != before[k]) changed.add(k);
  for (const auto& k : after.getAllKeys())
    if (!before.containsKey(k)) changed.add(k);
  return changed;
}

void UiPrefs::notify(const juce::StringArray& keys) {
  for (const auto& key : keys) listeners.call([&](Listener& l) { l.prefChanged(key); });
}

}  // namespace t3k::ui
