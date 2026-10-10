// Update check (useUpdateNotice.ts). Pings the version endpoint when the
// editor opens, every hour after that while it stays open, and whenever the
// session appears or disappears (a just-signed-in beta tester gets their
// payload; logout drops a beta-only notice), surfacing an "update available"
// notice when the published version is newer than the running build.
// Deliberately best-effort:
//
// - Off entirely unless the build sets T3K_UPDATE_NOTICE (forks skip it).
// - Never blocks: the reply lands whenever the session gets it.
// - Any failure (offline, 404, bad payload) is silently ignored.
//
// Dismissing the notice snoozes it for 1, 7 or 30 days (per machine). The
// snooze is UI-only: the endpoint is still polled on the same cadence, and
// only the modal is held back. There is no "skip this version": every snooze
// expires and the notice comes back (on the next check) until the user
// updates. A snoozed update still shows in Settings (`update()` ignores the
// snooze).
#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <optional>

#include "ToneSession.h"
#include "UiPrefs.h"
#include "core/AsyncScope.h"

namespace t3k::ui {

struct UpdateInfo {
  juce::String version;
  juce::String messageHtml;
  juce::String url;

  bool operator==(const UpdateInfo& o) const {
    return version == o.version && messageHtml == o.messageHtml && url == o.url;
  }
  bool operator!=(const UpdateInfo& o) const { return !(*this == o); }
};

class UpdateCheck : private ToneSession::Listener, private juce::Timer {
public:
  // Re-check cadence while the editor stays open.
  static constexpr int kRecheckMs = 60 * 60 * 1000;

  struct Listener {
    virtual ~Listener() = default;
    virtual void updateNoticeChanged() = 0;
  };

  UpdateCheck(ToneSession& session, UiPrefs& prefs, const juce::String& localVersion, bool enabled);
  ~UpdateCheck() override;

  // The update to show in the startup modal; empty once snoozed.
  const std::optional<UpdateInfo>& notice() const { return notice_; }
  // The available update regardless of snooze, for Settings.
  const std::optional<UpdateInfo>& update() const { return update_; }
  const juce::String& localVersion() const { return localVersion_; }

  void remindLater(int days);

  void addListener(Listener* l) { listeners_.add(l); }
  void removeListener(Listener* l) { listeners_.remove(l); }

  // Dot-separated numeric versions ("1.2.3"; a leading "v" and non-numeric
  // suffixes tolerated). Positive when a > b: a strictly-newer check, so dev
  // builds and forks ahead of the public release are never prompted.
  static int compareVersions(const juce::String& a, const juce::String& b);
  // The remote payload is untrusted input: exact shape and an http(s) url.
  static std::optional<UpdateInfo> parsePayload(const juce::var& body);

private:
  void sessionChanged() override { check(); }
  void timerCallback() override { check(); }
  void check();
  juce::int64 snoozeUntil() const;

  ToneSession& session_;
  UiPrefs& prefs_;
  juce::String localVersion_;
  bool enabled_;
  std::optional<UpdateInfo> notice_, update_;
  AsyncScope scope_;
  juce::ListenerList<Listener> listeners_;
};

}  // namespace t3k::ui
