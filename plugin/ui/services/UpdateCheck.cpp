#include "UpdateCheck.h"

#include <juce_events/juce_events.h>

namespace t3k::ui {

UpdateCheck::UpdateCheck(ToneSession& session, UiPrefs& prefs, const juce::String& localVersion, bool enabled)
    : session_(session), prefs_(prefs), localVersion_(localVersion), enabled_(enabled) {
  session_.addListener(this);
  if (!enabled_ || localVersion_.isEmpty()) return;
  // Like the web's mount effect: after the editor has finished coming up.
  juce::MessageManager::callAsync(scope_.wrap([this] { check(); }));
  startTimer(kRecheckMs);
}

UpdateCheck::~UpdateCheck() {
  stopTimer();
  session_.removeListener(this);
}

int UpdateCheck::compareVersions(const juce::String& a, const juce::String& b) {
  const auto parse = [](const juce::String& v) {
    juce::StringArray segments;
    segments.addTokens(v.trim().trimCharactersAtStart("vV"), ".", {});
    juce::Array<int> out;
    for (const auto& s : segments) out.add(s.getIntValue());  // "3-beta" → 3, "x" → 0
    return out;
  };
  const auto pa = parse(a), pb = parse(b);
  for (int i = 0; i < juce::jmax(pa.size(), pb.size()); ++i) {
    const int diff = (i < pa.size() ? pa[i] : 0) - (i < pb.size() ? pb[i] : 0);
    if (diff != 0) return diff;
  }
  return 0;
}

std::optional<UpdateInfo> UpdateCheck::parsePayload(const juce::var& body) {
  if (!body.isObject()) return std::nullopt;
  const auto version = body["version"], message = body["message_html"], url = body["url"];
  if (!version.isString() || !message.isString() || !url.isString()) return std::nullopt;
  const auto link = url.toString();
  if (!link.startsWithIgnoreCase("http://") && !link.startsWithIgnoreCase("https://")) return std::nullopt;
  return UpdateInfo{version.toString(), message.toString(), link};
}

juce::int64 UpdateCheck::snoozeUntil() const {
  const auto stored = prefs_.getJson(UiPrefs::kUpdateNotice);
  const auto until = stored.isObject() ? stored["snoozeUntil"] : juce::var();
  return until.isDouble() || until.isInt() || until.isInt64() ? static_cast<juce::int64>(until) : 0;
}

void UpdateCheck::check() {
  if (!enabled_ || localVersion_.isEmpty()) return;
  scope_.reset();  // a reply to an older check must not clobber this one
  session_.fetchPluginVersion(scope_.wrap([this](Result<juce::var> result) {
    if (!result) return;  // best-effort: offline, timeout, 404 all stay silent
    auto info = parsePayload(*result);
    if (info && compareVersions(info->version, localVersion_) <= 0) info.reset();
    // A previous (likely beta-gated) payload may no longer be offered.
    auto notice = info && juce::Time::currentTimeMillis() >= snoozeUntil() ? info : std::nullopt;
    // Hourly re-checks usually return the same payload; only a real change
    // notifies, so an open notice is not torn down and reopened for nothing.
    if (update_ == info && notice_ == notice) return;
    update_ = std::move(info);
    notice_ = std::move(notice);
    listeners_.call([](Listener& l) { l.updateNoticeChanged(); });
  }));
}

void UpdateCheck::remindLater(int days) {
  auto* obj = new juce::DynamicObject();
  obj->setProperty("snoozeUntil", juce::Time::currentTimeMillis() + days * 24LL * 60 * 60 * 1000);
  prefs_.setJson(UiPrefs::kUpdateNotice, juce::var(obj));
  if (!notice_) return;
  notice_.reset();
  listeners_.call([](Listener& l) { l.updateNoticeChanged(); });
}

}  // namespace t3k::ui
