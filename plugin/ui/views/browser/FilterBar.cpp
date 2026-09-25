#include "FilterBar.h"

#include <algorithm>

#include "core/GearGlyphs.h"
#include "core/Help.h"
#include "core/Labels.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
constexpr int kDividerHeight = 24;
constexpr int kLookupDebounceMs = 250;

// The active profile chip's short label ("Recent ×" fits where "Recently
// used" wouldn't).
const char* profileChipLabel(Profile profile) {
  return profile == Profile::downloaded ? "Recent" : profileLabel(profile);
}

juce::String joined(const std::vector<juce::String>& names) {
  juce::StringArray arr;
  for (const auto& n : names) arr.add(n);
  return arr.joinIntoString(", ");
}

// A chip holding a value shows it with an ×; empty, its name with a ▾.
void showValue(FilterChip& chip, const juce::String& value, const char* name, help::Key hint) {
  const bool has = value.isNotEmpty();
  chip.setLabel(has ? value : juce::String(name));
  chip.setTrailing(has ? FilterChip::Trailing::clear : FilterChip::Trailing::chevron);
  chip.setActive(has);
  chip.setHelpText(help::text(has ? help::Key::browserClearFilter : hint));
}

// Catalog-only controls park behind a profile filter; the hint says why.
void lockFor(FilterChip& chip, bool locked, help::Key hint) {
  chip.setLocked(locked);
  chip.setHelpText(help::text(locked ? help::Key::browserProfileLocked : hint));
}
}  // namespace

FilterBar::FilterBar(Services& services, BrowserState& state)
    : services_(services),
      state_(state),
      query_(state.query),
      scroller_(std::make_unique<DragScroller>(DragScroller::Axis::horizontal)) {
  scroller_->setViewedComponent(&row_, false);
  scroller_->onScroll = [this] { repaint(); };
  addAndMakeVisible(*scroller_);
  buildChips();
  refreshChips();
}

FilterBar::~FilterBar() = default;

// Chips
std::unique_ptr<FilterChip> FilterBar::makeMenuChip(help::Key hint, std::function<void(FilterChip&)> open) {
  auto chip = std::make_unique<FilterChip>();
  chip->setHelpText(help::text(hint));
  chip->setTrailing(FilterChip::Trailing::chevron);
  auto* raw = chip.get();
  chip->onPress = [this, raw, openMenu = std::move(open)] {
    if (menu_) {
      closeMenu();  // pressing the chip of the open menu closes it
      return;
    }
    openMenu(*raw);
  };
  row_.addChildComponent(*chip);
  return chip;
}

std::unique_ptr<FilterChip> FilterBar::makeTaxonomyChip(help::Key hint, Taxonomy kind) {
  auto chip = makeMenuChip(hint, [this, kind](FilterChip& c) { openTaxonomyMenu(c, kind); });
  chip->onClear = [this, kind] {
    query_.picked(kind).clear();
    changed();
  };
  return chip;
}

void FilterBar::buildChips() {
  toggle_ = std::make_unique<FilterChip>();
  toggle_->onPress = [this] { setExpanded(!state_.filtersExpanded); };
  row_.addChildComponent(*toggle_);

  sort_ = makeMenuChip(help::Key::browserSort, [this](FilterChip& chip) { openSortMenu(chip); });
  sort_->onClear = [this] {
    query_.sort.reset();
    changed();
  };
  format_ = makeMenuChip(help::Key::browserFormat, [this](FilterChip& chip) { openFormatMenu(chip); });
  format_->onClear = [this] {
    query_.format.clear();
    changed();
  };
  tags_ = makeTaxonomyChip(help::Key::browserTags, Taxonomy::tags);
  makes_ = makeTaxonomyChip(help::Key::browserMakes, Taxonomy::makes);
  creators_ = makeTaxonomyChip(help::Key::browserCreators, Taxonomy::creators);
  calibrated_ = std::make_unique<FilterChip>("Calibrated");
  calibrated_->setHelpText(help::text(help::Key::browserCalibrated));
  calibrated_->onPress = [this] {
    query_.calibrated = !query_.calibrated;
    changed();
  };
  row_.addChildComponent(*calibrated_);

  verified_ = std::make_unique<FilterChip>();
  verified_->setLeadingBadge();
  verified_->onPress = [this] {
    query_.verified = !query_.verified;
    changed();
  };
  row_.addChildComponent(*verified_);

  profile_ = makeMenuChip(help::Key::browserProfile, [this](FilterChip& chip) { openProfileMenu(chip); });
  profile_->onClear = [this] {
    query_.profile = Profile::none;
    changed();
  };
  if (const auto user = services_.session.user(); user && user->avatarUrl.isNotEmpty())
    services_.images.load(user->avatarUrl, avatarRequest_,
                          [this](const juce::Image& img) { profile_->leadingAvatar().setImage(img); });
  profile_->leadingAvatar();

  for (const auto& filter : labels::gearFilters()) {
    auto chip = std::make_unique<FilterChip>(filter.label);
    chip->setLeadingSvg(gear::svgFor(filter.id));
    chip->setHelpText(help::text(help::Key::browserGear));
    chip->onPress = [this, id = juce::String(filter.id)] {
      query_.gear = query_.gear == id ? juce::String() : id;
      changed();
    };
    row_.addChildComponent(*chip);
    gear_.push_back(std::move(chip));
  }
}

// State
void FilterBar::changed() {
  closeMenu();
  refreshChips();
  if (onChange) onChange();
}

void FilterBar::closeMenu() {
  lookupDebounce_.cancel();
  lookupScope_.reset();
  menu_.reset();
}

void FilterBar::setExpanded(bool expanded) {
  state_.filtersExpanded = expanded;
  closeMenu();
  refreshChips();
  scroller_->setViewPosition(0, 0);
}

// Every chip's label / active state from the query, then the row.
void FilterBar::refreshChips() {
  const bool expanded = state_.filtersExpanded;
  const bool locked = profileLocked();

  toggle_->setLeadingIcon(expanded ? Icon::ChevronLeft : Icon::ListFilter);
  toggle_->setDot(!expanded && query_.hasAdvancedFilters());
  toggle_->setActive(!expanded && query_.hasAdvancedFilters());
  // Parked filters can still be folded away, just not opened or changed.
  lockFor(*toggle_, locked && !expanded, expanded ? help::Key::browserFewerFilters : help::Key::browserMoreFilters);

  // The sort chip always names the order in force; only a non-default one
  // is a filter, with an × back to the default.
  const auto* sortName = sortLabel(query_.effectiveSort());
  showValue(*sort_, query_.sortIsExplicit() ? sortName : "", sortName, help::Key::browserSort);
  juce::String formatChip;
  for (const auto& option : kFormatOptions)
    if (query_.format == option.id) formatChip = option.chip;
  showValue(*format_, formatChip, "Format", help::Key::browserFormat);
  showValue(*tags_, joined(query_.tags), "Tags", help::Key::browserTags);
  showValue(*makes_, joined(query_.makes), "Makes", help::Key::browserMakes);
  showValue(*creators_, joined(query_.creators), "Creators", help::Key::browserCreators);
  calibrated_->setActive(query_.calibratedInForce());
  for (auto* chip : {sort_.get(), format_.get(), tags_.get(), makes_.get(), creators_.get(), calibrated_.get()}) {
    chip->setVisible(expanded);
    chip->setLocked(locked);
    if (locked) chip->setHelpText(help::text(help::Key::browserProfileLocked));
  }
  // Impulse responses carry no calibration data: the chip parks under IR gear.
  if (!locked) {
    const bool ir = !query_.calibratedApplies();
    calibrated_->setLocked(ir);
    calibrated_->setHelpText(help::text(ir ? help::Key::browserCalibratedIr : help::Key::browserCalibrated));
  }

  verified_->setActive(query_.verified);
  lockFor(*verified_, locked, help::Key::browserVerified);
  // The avatar is the chip's name: no label until a profile is picked.
  showValue(*profile_, profileChipLabel(query_.profile), "", help::Key::browserProfile);
  const auto& gearFilters = labels::gearFilters();
  for (size_t i = 0; i < gear_.size(); ++i) gear_[i]->setActive(query_.gear == gearFilters[i].id);

  layoutChips();
}

// Layout
void FilterBar::layoutChips() {
  std::vector<FilterChip*> order = {toggle_.get()};
  if (state_.filtersExpanded)
    for (auto* chip : {sort_.get(), format_.get(), tags_.get(), makes_.get(), creators_.get(), calibrated_.get()})
      order.push_back(chip);
  order.insert(order.end(), {verified_.get(), profile_.get()});
  for (auto& chip : gear_) order.push_back(chip.get());

  // The divider parts the extra filters (or, folded, the button holding
  // them) from the ones always on show.
  auto* lastExtra = state_.filtersExpanded ? calibrated_.get() : toggle_.get();
  int x = kBleed;
  for (auto* chip : order) {
    chip->setVisible(true);
    chip->setTopLeftPosition(x, 0);
    x += chip->getWidth() + kGap;
    if (chip == lastExtra) {
      divider_ = {x, (kHeight - kDividerHeight) / 2, 1, kDividerHeight};
      x += 1 + kGap;
    }
  }
  row_.setSize(std::max(x - kGap + kBleed, getWidth()), kHeight);
  row_.repaint();
  repaint();
}

void FilterBar::resized() {
  scroller_->setBounds(getLocalBounds());
  // Shorter rows never scroll: the content is at least the viewport.
  row_.setSize(std::max(row_.getWidth(), getWidth()), kHeight);
}

void FilterBar::paintOverChildren(juce::Graphics& g) {
  g.setColour(theme::kBorder);
  g.fillRect(divider_.translated(-scroller_->getViewPositionX(), 0));
  paint::edgeFades(g, getLocalBounds().toFloat(), kBleed, juce::Colours::black);
}

// Menus
void FilterBar::openMenu(FilterChip& chip, FilterMenu::Picks picks, std::vector<FilterMenu::Option> options,
                         const std::vector<juce::String>& picked, std::function<void(const juce::String& id)> pick,
                         const char* searchPlaceholder) {
  menu_ = std::make_unique<FilterMenu>(services_.images, picks, searchPlaceholder ? searchPlaceholder : "");
  menu_->setOptions(std::move(options), picked);
  menu_->onPick = [this, apply = std::move(pick)](const juce::String& id) {
    apply(id);
    changed();
  };
  menu_->onDismiss = [this] { closeMenu(); };
  menu_->openBelow(chip);
}

void FilterBar::openSortMenu(FilterChip& chip) {
  std::vector<FilterMenu::Option> options;
  for (const auto sort : kToneSorts)
    if (sort != ToneSort::bestMatch || query_.text.isNotEmpty())
      options.push_back({sortId(sort), sortLabel(sort), {}, {}});
  openMenu(chip, FilterMenu::Picks::single, std::move(options), {sortId(query_.effectiveSort())},
           [this](const juce::String& id) {
             for (const auto sort : kToneSorts)
               if (id == sortId(sort)) query_.setSort(sort);
           });
}

void FilterBar::openFormatMenu(FilterChip& chip) {
  std::vector<FilterMenu::Option> options;
  for (const auto& option : kFormatOptions) options.push_back({option.id, option.label, {}, {}});
  openMenu(chip, FilterMenu::Picks::single, std::move(options), {query_.format},
           [this](const juce::String& id) { query_.format = id; });
}

void FilterBar::openProfileMenu(FilterChip& chip) {
  std::vector<FilterMenu::Option> options;
  for (const auto profile : kProfiles) {
    // Favorites carries the bookmark it is kept with (the old tabs' icon).
    const auto icon = profile == Profile::favorited ? std::optional(Icon::Bookmark) : std::nullopt;
    options.push_back({profileEndpoint(profile), profileLabel(profile), {}, icon});
  }
  openMenu(chip, FilterMenu::Picks::single, std::move(options), {profileEndpoint(query_.profile)},
           [this](const juce::String& id) {
             for (const auto profile : kProfiles)
               if (id == profileEndpoint(profile)) query_.profile = profile;
           });
}

// Opens on a lookup of the most-used names; typing narrows it. A pick
// toggles the name.
void FilterBar::openTaxonomyMenu(FilterChip& chip, Taxonomy kind) {
  const char* placeholder = kind == Taxonomy::tags    ? "Search tags"
                            : kind == Taxonomy::makes ? "Search makes & models"
                                                      : "Search creators";
  menuKind_ = kind;
  openMenu(
      chip, FilterMenu::Picks::multi, {}, {},
      [this, kind](const juce::String& name) {
        auto& picked = query_.picked(kind);
        const auto it = std::find(picked.begin(), picked.end(), name);
        if (it == picked.end()) picked.push_back(name);
        else picked.erase(it);
      },
      placeholder);
  menu_->onSearch = [this, kind](const juce::String& text) {
    lookupDebounce_.start(kLookupDebounceMs, [this, kind, text] { lookupTaxonomy(kind, text); });
  };
  lookupTaxonomy(kind, {});
}

// The picked names lead, ticked, then the lookup's remaining names.
void FilterBar::lookupTaxonomy(Taxonomy kind, const juce::String& text) {
  lookupScope_.reset();  // a newer lookup supersedes anything in flight
  menu_->setStatus(juce::String::fromUTF8("Loading\xe2\x80\xa6"));
  services_.session.listTaxonomy(kind, text, lookupScope_.wrap([this, kind](Result<std::vector<TaxonomyEntry>> r) {
    if (!menu_ || menuKind_ != kind) return;
    if (!r) {
      menu_->setStatus(juce::String::fromUTF8("Couldn\xe2\x80\x99t load. Try again."));
      return;
    }
    const bool creators = kind == Taxonomy::creators;
    // Remember every creator avatar seen: a picked creator stays pinned at
    // the top after the search moves on and no longer returns them.
    if (creators)
      for (const auto& entry : *r)
        if (entry.avatarUrl.isNotEmpty()) state_.creatorAvatars[entry.name] = entry.avatarUrl;
    const auto& picked = query_.picked(kind);
    std::vector<FilterMenu::Option> options;
    auto add = [&](const juce::String& name, const juce::String& avatarUrl) {
      options.push_back({name, name, creators ? std::optional(avatarUrl) : std::nullopt, {}});
    };
    for (const auto& name : picked) {
      const auto found = std::find_if(r->begin(), r->end(), [&](const auto& e) { return e.name == name; });
      juce::String avatarUrl = found != r->end() ? found->avatarUrl : juce::String();
      if (avatarUrl.isEmpty() && creators)
        if (const auto cached = state_.creatorAvatars.find(name); cached != state_.creatorAvatars.end())
          avatarUrl = cached->second;
      add(name, avatarUrl);
    }
    for (const auto& entry : *r)
      if (std::find(picked.begin(), picked.end(), entry.name) == picked.end()) add(entry.name, entry.avatarUrl);
    if (options.empty()) menu_->setStatus("No matches");
    else menu_->setOptions(std::move(options), picked);
  }));
}

}  // namespace t3k::ui
