// What the Select tone screen remembers between visits: the browser is
// mounted only while open, so its query, filter row and last page live
// here for the editor's lifetime instead. Plain data; ToneBrowser and
// FilterBar read and write it directly.
#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <optional>

#include "ToneSession.h"
#include "model/ToneQuery.h"

namespace t3k::ui {

struct BrowserState {
  ToneQuery query;
  bool filtersExpanded = false;
  int page = 1;
  // The page last shown, rendered again at once on return; it only
  // refreshes on the user's next search, filter change or page turn.
  std::optional<TonePage> result;
  // Creator avatar URLs by name, from every Creators lookup so far. A
  // picked creator pins to the top of the menu whatever the search shows;
  // this keeps its avatar once the lookup no longer returns it.
  std::map<juce::String, juce::String> creatorAvatars;
};

}  // namespace t3k::ui
