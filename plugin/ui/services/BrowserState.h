// What the Select tone screen remembers between visits: the browser is
// mounted only while open, so its query, filter row and last page live
// here for the editor's lifetime instead. ToneBrowser and
// FilterBar read and write it directly.
#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <optional>
#include <cstdint>
#include <functional>

#include "ToneSession.h"
#include "core/AsyncScope.h"
#include "model/ToneQuery.h"
#include "model/ToneTableSort.h"

namespace t3k::ui {

struct BrowserState {
  struct FavoriteOperation {
    int toneId = 0;
    bool previous = false, next = false;
    int previousCount = 0, nextCount = 0;
    juce::String account;
    std::uint64_t generation = 0;
  };
  ToneQuery query;
  bool filtersExpanded = false;
  // Exchange is an alternate presentation of the same server page. Its
  // column sort applies only to that page; it never changes query.sort.
  bool exchangeView = false;
  std::optional<ToneColumnSort> tableSort;
  std::optional<int> selectedToneId;
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
  // A write belongs to the editor/session, not the transient browser.
  // Only the mounted view receives completion; a closed view leaves no
  // callable pointing at its components. The scope guards editor teardown.
  std::uint64_t requestGeneration = 0;
  std::optional<FavoriteOperation> pendingFavorite;
  std::function<void(const FavoriteOperation&, const juce::String&, bool sameAccount)> onFavoriteCompleted;
  AsyncScope favoriteRequestScope;
};

}  // namespace t3k::ui
