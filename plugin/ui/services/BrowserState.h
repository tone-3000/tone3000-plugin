// What the Select tone screen remembers between visits: the browser is
// mounted only while open, so its query, filter row and last page live
// here for the editor's lifetime instead. Plain data; ToneBrowser and
// FilterBar read and write it directly.
#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <optional>

#include "ToneSession.h"
#include "UiPrefs.h"
#include "model/ToneQuery.h"

namespace t3k::ui {

struct BrowserState {
  ToneQuery query;
  bool filtersExpanded = false;
  int page = 1;
  // The page last shown, rendered again at once on return; it only
  // refreshes on the user's next search, filter change or page turn.
  std::optional<TonePage> result;
  // Whether `result` is the signed-out preview's trending feed (one page,
  // narrowed by `query.gear` alone) rather than a page of the signed-in
  // search: a return reuses it only while the session state still matches.
  bool resultIsTrending = false;
  // Creator avatar URLs by name, from every Creators lookup so far. A
  // picked creator pins to the top of the menu whatever the search shows;
  // this keeps its avatar once the lookup no longer returns it.
  std::map<juce::String, juce::String> creatorAvatars;
  // The filter chips have been set from the ones remembered (the browser's
  // first opening in this editor; restoreFilters).
  bool filtersRestored = false;
};

// Remember Browser Filters (UiPrefs::kRememberBrowserFilters, off by
// default). The state lives as long as the editor, and a host closes and
// reopens the plugin's window often, so the browser came back on the default
// filters every time. With the setting on, the filter chips (sort, gear,
// format, Calibrated, Verified) are kept in the prefs (kBrowserFilters) and a
// new editor's browser opens on them. Not the search text, nor the tag /
// make / creator picks: one of those left over would quietly hide results.

// Once per editor, before the browser first reads the state.
inline void restoreFilters(BrowserState& state, const UiPrefs& prefs) {
  if (state.filtersRestored) return;
  state.filtersRestored = true;
  if (!prefs.getBool(UiPrefs::kRememberBrowserFilters, false)) return;
  const auto saved = prefs.getJson(UiPrefs::kBrowserFilters);
  if (!saved.isObject()) return;
  const auto sort = saved["sort"].toString();
  for (const auto s : kToneSorts)
    if (s != ToneSort::bestMatch && sort == sortId(s)) state.query.sort = s;
  state.query.gear = saved["gear"].toString();
  state.query.format = saved["format"].toString();
  state.query.calibrated = static_cast<bool>(saved.getProperty("calibrated", false));
  state.query.verified = static_cast<bool>(saved.getProperty("verified", false));
}

// After every filter change (and when the setting is turned on).
inline void rememberFilters(const BrowserState& state, UiPrefs& prefs) {
  if (!prefs.getBool(UiPrefs::kRememberBrowserFilters, false)) return;
  const auto& query = state.query;
  auto* saved = new juce::DynamicObject();
  // Best match is only ever the default for a search: never remembered.
  saved->setProperty("sort", query.sort && *query.sort != ToneSort::bestMatch ? juce::String(sortId(*query.sort))
                                                                              : juce::String());
  saved->setProperty("gear", query.gear);
  saved->setProperty("format", query.format);
  saved->setProperty("calibrated", query.calibrated);
  saved->setProperty("verified", query.verified);
  prefs.setJson(UiPrefs::kBrowserFilters, juce::var(saved));
}

}  // namespace t3k::ui
