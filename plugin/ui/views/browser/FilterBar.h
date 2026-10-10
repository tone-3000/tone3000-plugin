// The browser's filter row: one line of chips scrolling sideways under the
// gallery's edge fades, editing the BrowserState's ToneQuery.
//
//   [filters] | [verified] [Profile ▾] [Amp + Cab] [Amp Head] …
//
// The filters button expands the row in place, pushing the rest right:
//
//   [‹] | [Trending ▾] [Format ▾] [Tags ▾] [Makes ▾] [Creators ▾] [Calibrated] [verified] [Profile ▾] …
//
// Menu chips open a FilterMenu below themselves; a chip holding a value
// shows it with an × that clears it. The taxonomy menus (Tags, Makes,
// Creators) look their options up as the user types. While a profile filter
// is set the catalog-only controls dim: the profile streams list by gear
// alone, so the rest of the query is parked until the profile clears.
//
// Signed out (the browser's trending preview) the row is the gear chips
// alone: the trending feed takes one gear filter and nothing else. The gear
// pick is the same query field, so it carries over into the search once
// the user signs in.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "FilterChip.h"
#include "FilterMenu.h"
#include "core/AsyncScope.h"
#include "core/DelayedCall.h"
#include "core/Help.h"
#include "services/Services.h"
#include "widgets/DragScroller.h"
#include "views/gallery/GalleryGeometry.h"

namespace t3k::ui {

class FilterBar : public juce::Component {
public:
  static constexpr int kHeight = FilterChip::kHeight;
  static constexpr int kBleed = gallery::kEdgeFadeWidth;
  static constexpr int kGap = 10;

  FilterBar(Services& services, BrowserState& state);
  ~FilterBar() override;

  // The bounds of the content column: the row extends kBleed past it.
  void setColumn(juce::Rectangle<int> column) { setBounds(column.expanded(kBleed, 0).withHeight(kHeight)); }

  // The query changed outside the row (the search text was submitted): the
  // Sort chip's default label follows it.
  void refresh() { refreshChips(); }
  // Whether the catalog-only controls are parked behind a profile filter.
  bool profileLocked() const { return query_.profile != Profile::none; }
  // Gear chips alone (signed out), or the full row.
  void setGearOnly(bool gearOnly);

  // Any filter changed: the browser fetches page 1.
  std::function<void()> onChange;

  void resized() override;
  void paintOverChildren(juce::Graphics& g) override;

private:

  void buildChips();
  void layoutChips();
  void refreshChips();
  // The profile chip's avatar, once a signed-in user with one is known (the
  // row may be built signed out and live through the sign-in).
  void loadAvatar();
  // A filter changed: refresh the row, close any open menu (a press on a
  // chip's × while its menu is up), tell the owner.
  void changed();
  void closeMenu();
  void setExpanded(bool expanded);

  // Chip factories (each wires its own handlers).
  std::unique_ptr<FilterChip> makeMenuChip(help::Key hint, std::function<void(FilterChip&)> open);
  std::unique_ptr<FilterChip> makeTaxonomyChip(help::Key hint, Taxonomy kind);
  // One menu at a time: `options` with `picked` current, `pick` applied to
  // the query and the row refetched.
  void openMenu(FilterChip& chip, FilterMenu::Picks picks, std::vector<FilterMenu::Option> options,
                const std::vector<juce::String>& picked, std::function<void(const juce::String& id)> pick,
                const char* searchPlaceholder = nullptr);
  void openSortMenu(FilterChip& chip);
  void openFormatMenu(FilterChip& chip);
  void openProfileMenu(FilterChip& chip);
  void openTaxonomyMenu(FilterChip& chip, Taxonomy kind);
  void lookupTaxonomy(Taxonomy kind, const juce::String& text);

  Services& services_;
  BrowserState& state_;
  ToneQuery& query_;  // state_.query
  bool gearOnly_ = false;

  std::unique_ptr<DragScroller> scroller_;
  juce::Component row_;
  std::unique_ptr<FilterChip> toggle_;  // the filters button; ‹ while expanded
  // Behind the filters button
  std::unique_ptr<FilterChip> sort_, format_, tags_, makes_, creators_, calibrated_;
  // Always on the row
  std::unique_ptr<FilterChip> verified_, profile_;
  std::vector<std::unique_ptr<FilterChip>> gear_;
  juce::String avatarUrl_;  // the one requested, if any
  juce::Rectangle<int> divider_;

  // The open menu and, for a taxonomy menu, its debounced lookup.
  std::unique_ptr<FilterMenu> menu_;
  Taxonomy menuKind_ = Taxonomy::tags;
  DelayedCall lookupDebounce_;
  AsyncScope lookupScope_;
};

}  // namespace t3k::ui
