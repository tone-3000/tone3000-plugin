// How the preset browser lays the store's list out, and what a drop means.
// Pure functions over PresetInfo, so the grouping and the drag-and-drop rules
// are testable without a window (see PresetGroupsTests in SelfTests.cpp).
//
// Groups, top to bottom: Favourites (any starred preset, factory included),
// "Your Presets" (user presets that are neither starred nor filed: the root,
// shown as "All Uncategorized"), one group per user category (alphabetical,
// starred presets live in Favourites instead), then the TONE3000 factory
// section. A starred preset therefore shows in Favourites and, if it is a
// factory preset, again under TONE3000.
#pragma once

#include <juce_core/juce_core.h>

#include <utility>
#include <vector>

#include "model/ChainState.h"

namespace t3k::ui {

inline constexpr int kMaxCategoryNameLength = 50;

struct PresetGroups {
  std::vector<PresetInfo> favourites;
  std::vector<PresetInfo> root;
  // Every known category, alphabetical (a search drops the empty ones).
  std::vector<std::pair<juce::String, std::vector<PresetInfo>>> categories;
  std::vector<PresetInfo> factory;
  int userCount = 0;     // user presets matching the filter, starred or not
  int matchCount = 0;    // everything matching the filter
};

// `query` filters by name, case-insensitively; empty keeps everything.
PresetGroups groupPresets(const std::vector<PresetInfo>& list, const std::vector<juce::String>& categories,
                          const juce::String& query);

// Where a dragged preset was released.
struct DropTarget {
  enum class Kind { none, favourites, category, preset };
  Kind kind = Kind::none;
  // category: the category name ("" = the root); preset: the preset id.
  juce::String name;
};

struct DropOutcome {
  enum class Kind {
    none,
    star,            // ids -> starred
    unstar,          // ids -> unstarred (factory presets: nothing to file)
    unstarAndMove,   // ids -> unstarred, then filed under `category`
    move,            // ids -> filed under `category`
    reorder,         // reorderId shifted by `delta` within its section
  };
  Kind kind = Kind::none;
  juce::StringArray ids;
  juce::String category;
  juce::String reorderId;
  int delta = 0;
  juce::String toast;
};

// The rule for one drop. `list` is the store's list in its shown order,
// `selection` the bulk-selected ids (a drag of a selected row carries all of
// them), `reordering` whether the browser is in reorder mode (a drop on a row
// then reorders instead of filing).
DropOutcome decideDrop(const std::vector<PresetInfo>& list, const juce::String& sourceId,
                       const juce::StringArray& selection, bool reordering, const DropTarget& target);

}  // namespace t3k::ui
