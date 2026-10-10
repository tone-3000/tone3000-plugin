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
  const auto raw = get(key);
  return raw.isEmpty() ? juce::var() : juce::JSON::parse(raw);
}

void UiPrefs::set(const juce::String& key, const juce::String& value) {
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
  set(key, juce::JSON::toString(value, true));
}

void UiPrefs::remove(const juce::String& key) {
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
