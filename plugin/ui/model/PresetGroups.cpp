#include "model/PresetGroups.h"

#include <algorithm>

namespace t3k::ui {

namespace {

const PresetInfo* find(const std::vector<PresetInfo>& list, const juce::String& id) {
  const auto it = std::find_if(list.begin(), list.end(), [&id](const PresetInfo& p) { return p.id == id; });
  return it == list.end() ? nullptr : &*it;
}

bool sameCategory(const juce::String& a, const juce::String& b) { return a.trim().compareIgnoreCase(b.trim()) == 0; }

juce::String destinationName(const juce::String& category) {
  return category.isEmpty() ? juce::String("All Uncategorized") : category;
}

juce::String movedToast(int count, const juce::String& category) {
  const auto dest = destinationName(category);
  return count > 1 ? "Moved " + juce::String(count) + " presets to " + dest : "Moved to " + dest;
}

bool allUserPresets(const std::vector<PresetInfo>& list, const juce::StringArray& ids) {
  return std::all_of(ids.begin(), ids.end(), [&list](const juce::String& id) {
    const auto* preset = find(list, id);
    return preset != nullptr && !preset->factory;
  });
}

}  // namespace

PresetGroups groupPresets(const std::vector<PresetInfo>& list, const std::vector<juce::String>& categories,
                          const juce::String& query) {
  PresetGroups groups;
  const auto needle = query.trim().toLowerCase();

  std::vector<juce::String> sorted(categories);
  std::sort(sorted.begin(), sorted.end(),
            [](const juce::String& a, const juce::String& b) { return a.compareIgnoreCase(b) < 0; });
  for (const auto& name : sorted) groups.categories.emplace_back(name, std::vector<PresetInfo>{});

  for (const auto& preset : list) {
    if (needle.isNotEmpty() && !preset.name.toLowerCase().contains(needle)) continue;
    ++groups.matchCount;
    if (preset.favorite) groups.favourites.push_back(preset);
    if (preset.factory) {
      groups.factory.push_back(preset);
      continue;
    }
    ++groups.userCount;
    if (preset.favorite) continue;
    const auto it = std::find_if(groups.categories.begin(), groups.categories.end(),
                                 [&preset](const auto& entry) { return sameCategory(entry.first, preset.category); });
    if (preset.category.trim().isEmpty() || it == groups.categories.end())
      groups.root.push_back(preset);  // unfiled, or filed under a category that is gone
    else
      it->second.push_back(preset);
  }

  if (needle.isNotEmpty())
    groups.categories.erase(std::remove_if(groups.categories.begin(), groups.categories.end(),
                                           [](const auto& entry) { return entry.second.empty(); }),
                            groups.categories.end());
  return groups;
}

DropOutcome decideDrop(const std::vector<PresetInfo>& list, const juce::String& sourceId,
                       const juce::StringArray& selection, bool reordering, const DropTarget& target) {
  DropOutcome none;
  const auto* source = find(list, sourceId);
  if (source == nullptr || target.kind == DropTarget::Kind::none) return none;
  const auto* targetPreset = target.kind == DropTarget::Kind::preset ? find(list, target.name) : nullptr;
  if (target.kind == DropTarget::Kind::preset && targetPreset == nullptr) return none;

  const juce::StringArray ids = selection.contains(sourceId) ? selection : juce::StringArray{sourceId};
  const int count = ids.size();
  const bool fromFavourites = source->favorite;
  const bool intoFavourites =
      target.kind == DropTarget::Kind::favourites || (targetPreset != nullptr && targetPreset->favorite);

  // Reorder mode: a drop on a preset moves the dragged one to its place in the
  // global order, whatever group either sits in (a starred row too).
  if (reordering && targetPreset != nullptr) {
    // Reordering never crosses the user/factory boundary.
    if (source->factory != targetPreset->factory) return none;
    int from = -1, to = -1, index = 0;
    for (const auto& preset : list) {
      if (preset.factory != source->factory) continue;
      if (preset.id == source->id) from = index;
      if (preset.id == targetPreset->id) to = index;
      ++index;
    }
    if (from < 0 || to < 0 || from == to) return none;
    DropOutcome out;
    out.kind = DropOutcome::Kind::reorder;
    out.reorderId = source->id;
    out.delta = to - from;
    return out;
  }

  // Into Favourites from outside: starring is the whole effect.
  if (intoFavourites && !fromFavourites) {
    DropOutcome out;
    out.kind = DropOutcome::Kind::star;
    out.ids = ids;
    out.toast = count > 1 ? "Starred & added " + juce::String(count) + " presets to Favourites"
                          : juce::String("Starred & added to Favourites");
    return out;
  }

  // Out of Favourites: unstar, and file it where it landed (a category, the
  // root, or the category of the preset it landed on). Factory presets can be
  // unstarred but never filed.
  if (fromFavourites && !intoFavourites) {
    DropOutcome out;
    out.ids = ids;
    if (!allUserPresets(list, ids)) {
      out.kind = DropOutcome::Kind::unstar;
      out.toast = count > 1 ? "Unstarred " + juce::String(count) + " presets" : juce::String("Unstarred");
      return out;
    }
    juce::String destination;
    if (target.kind == DropTarget::Kind::category) destination = target.name;
    else if (targetPreset != nullptr) destination = targetPreset->category;
    out.kind = DropOutcome::Kind::unstarAndMove;
    out.category = destination;
    const auto dest = destinationName(destination);
    out.toast = count > 1 ? "Unstarred & moved " + juce::String(count) + " presets to " + dest
                          : "Unstarred & moved to " + dest;
    return out;
  }

  // Onto a category header or its empty zone.
  if (target.kind == DropTarget::Kind::category) {
    if (!allUserPresets(list, ids)) return none;
    const bool allThere = std::all_of(ids.begin(), ids.end(), [&](const juce::String& id) {
      const auto* p = find(list, id);
      return p != nullptr && !p->favorite && sameCategory(p->category, target.name);
    });
    if (allThere) return none;  // already filed there
    DropOutcome out;
    out.kind = DropOutcome::Kind::move;
    out.ids = ids;
    out.category = target.name;
    out.toast = movedToast(count, target.name);
    return out;
  }

  // Onto another preset row.
  if (targetPreset != nullptr) {
    if (!source->factory && !targetPreset->factory && !sameCategory(source->category, targetPreset->category) &&
        allUserPresets(list, ids)) {
      DropOutcome out;
      out.kind = DropOutcome::Kind::move;
      out.ids = ids;
      out.category = targetPreset->category;
      out.toast = movedToast(count, targetPreset->category);
      return out;
    }
  }
  return none;
}

}  // namespace t3k::ui
