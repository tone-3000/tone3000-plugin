// Per-machine UI preferences and per-editor session values (port of
// uiPreferences.ts and the sessionStorage uses in helpText.ts /
// useToneSession.ts): `persistent` is a PropertiesFile in the plugin's
// app-data folder, `session` is plain memory that lives as long as the
// editor. Keys keep the names the web UI used, but that is lineage only:
// its values lived in the browser engine's localStorage, which nothing
// reads, so the first run after upgrading from a web-UI build starts with an
// empty file (one sign-in, default toggles).
//
// The file is one per machine user and every host process (each DAW, the
// standalone) holds its own copy of it, so a write is a merge: under the
// process lock, pull in what the others wrote, apply the one key, save at
// once. Nothing of another process's ever gets overwritten with a stale
// copy, which is what keeps one sign-in valid everywhere.
#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <functional>
#include <map>

namespace t3k::ui {

class UiPrefs {
public:
  struct Listener {
    virtual ~Listener() = default;
    virtual void prefChanged(const juce::String& key) = 0;
  };

  // Persistent values live in `file`, which the caller owns and may share
  // between editors (nullptr = memory only, as the testbed uses). `lock` is
  // the file's Options::processLock, held across each read-modify-write.
  explicit UiPrefs(juce::PropertiesFile* file = nullptr, juce::InterProcessLock* lock = nullptr);
  ~UiPrefs();

  // Pull in what other processes wrote since the last read or write;
  // listeners hear every key that changed. Writes do this on their own;
  // call it before acting on a value another host may have moved on
  // (the tokens).
  void sync();

  juce::String get(const juce::String& key, const juce::String& fallback = {}) const;
  bool getBool(const juce::String& key, bool fallback) const;
  juce::var getJson(const juce::String& key) const;
  void set(const juce::String& key, const juce::String& value);
  void setBool(const juce::String& key, bool value) { set(key, value ? "true" : "false"); }
  void setJson(const juce::String& key, const juce::var& value);
  void remove(const juce::String& key);

  // Editor-lifetime values.
  std::map<juce::String, juce::String> session;

  void addListener(Listener* l) { listeners.add(l); }
  void removeListener(Listener* l) { listeners.remove(l); }

  // Keys shared by more than one component.
  static constexpr const char* kShowHints = "t3k.showHints";
  static constexpr const char* kShowBlockNormalizeControl = "t3k.showBlockNormalizeControl";
  static constexpr const char* kShowBlockSizeControl = "t3k.showBlockSizeControl";
  // Faceplate effect groups (view only; a powered effect shows regardless).
  static constexpr const char* kShowGateControl = "t3k.showGateControl";
  static constexpr const char* kShowLooperControl = "t3k.showLooperControl";
  static constexpr const char* kShowPitchControl = "t3k.showPitchControl";
  static constexpr const char* kShowPresetPcNumbers = "t3k.showPresetPcNumbers";
  static constexpr const char* kTokens = "t3k_tokens";
  static constexpr const char* kCachedUser = "t3k.cachedUser";
  static constexpr const char* kUpdateNotice = "t3k.updateNotice";
  static constexpr const char* kDismissedBanners = "t3k.dismissedBanners";
  // Session keys.
  static constexpr const char* kDetailBlockId = "t3k.detailBlockId";
  static constexpr const char* kChainScroll = "t3k.chainScroll";

private:
  static constexpr int kLockTimeoutMs = 500;

  class Guard;
  // Reload the file, dropping our copy first (reload() merges, so a key
  // another process removed would otherwise linger); returns the keys whose
  // values changed. Caller holds the lock.
  juce::StringArray pullLocked();
  void notify(const juce::StringArray& keys);

  juce::PropertiesFile* file_;
  juce::InterProcessLock* lock_;
  std::map<juce::String, juce::String> memory_;
  juce::ListenerList<Listener> listeners;
};

}  // namespace t3k::ui
