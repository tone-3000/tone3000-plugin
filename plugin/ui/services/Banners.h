// Main-window banner rules (AppBanner.tsx): at most ONE banner, chosen by
// evaluating rules in priority order against the standalone audio device
// snapshot. Hosted builds never evaluate (no device state), because nagging
// users about their DAW's setup inside the plugin window would be hostile.
//
// "Ignore" hides a banner for 24h (persisted per machine), after which the
// rule may resurface. Errors and the auto-mute warning are not ignorable:
// they describe a state where the plugin can't be heard at all.
#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <optional>
#include <vector>

#include "AudioDeviceStore.h"
#include "UiPrefs.h"
#include "core/Alerts.h"
#include "core/RichText.h"
#include "model/AudioDeviceState.h"

namespace t3k::ui {

enum class BannerAction { openSettings, switchToAsio, openMicSettings };

struct BannerRule {
  juce::String id;
  AlertVariant variant;
  bool dismissable;
  std::function<bool(const AudioDeviceState&)> when;
  std::function<RichText(const AudioDeviceState&)> content;
  juce::String actionLabel;
  BannerAction action;
};

// Priority-ordered; the first that fires (and isn't ignored) wins.
const std::vector<BannerRule>& bannerRules();
// System Settings re-renders the same warning / error copy inline next to
// the relevant control, for one source of truth on wording.
const BannerRule* bannerRuleById(const juce::String& id);

struct BannerSpec {
  juce::String id;
  AlertVariant variant;
  RichText content;
  juce::String actionLabel;
  BannerAction action;
  bool dismissable;
};

class BannerStore : private AudioDeviceStore::Listener {
public:
  struct Listener {
    virtual ~Listener() = default;
    virtual void bannerChanged() = 0;
  };
  static constexpr juce::int64 kIgnoreMs = 24LL * 60 * 60 * 1000;

  BannerStore(AudioDeviceStore& devices, UiPrefs& prefs);
  ~BannerStore() override;

  const std::optional<BannerSpec>& active() const { return active_; }
  // Ignore `id` for 24h and re-pick.
  void dismiss(const juce::String& id);

  void addListener(Listener* l) { listeners_.add(l); }
  void removeListener(Listener* l) { listeners_.remove(l); }

private:
  void audioDeviceChanged() override { evaluate(); }
  void evaluate();
  bool ignored(const juce::String& id) const;

  AudioDeviceStore& devices_;
  UiPrefs& prefs_;
  std::optional<BannerSpec> active_;
  juce::ListenerList<Listener> listeners_;
};

}  // namespace t3k::ui
