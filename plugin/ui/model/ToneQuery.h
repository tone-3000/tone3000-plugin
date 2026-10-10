// What the tone browser asks the catalog for: the search text plus every
// filter the filter bar exposes, and how that becomes a request.
//
// Two shapes of request hide behind one query. The catalog search
// (GET /tones/search) takes all of it. A profile filter (Recently used,
// Favorites, Created) instead pages one of the signed-in user's own streams
// (GET /tones/{downloaded|favorited|created}), which filter by gear and a
// title search only: the browser parks the other filters while one is set.
#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace t3k::ui {

// The Profile chip's options, in menu order.
enum class Profile { none, downloaded, favorited, created };
inline constexpr Profile kProfiles[] = {Profile::downloaded, Profile::favorited, Profile::created};
// "Recently used", "Favorites", "Created"; "" for none.
const char* profileLabel(Profile profile);
// The gated endpoint's path segment ("downloaded", …).
const char* profileEndpoint(Profile profile);

// The Sort chip's options, in menu order. Best match is only offered (and
// only the API default) while there is search text.
enum class ToneSort { bestMatch, trending, popular, newest, oldest };
inline constexpr ToneSort kToneSorts[] = {ToneSort::bestMatch, ToneSort::trending, ToneSort::popular,
                                          ToneSort::newest, ToneSort::oldest};
// "Best match", "Trending", "Popular", "Newest", "Oldest".
const char* sortLabel(ToneSort sort);
// The API's TonesSort value.
const char* sortId(ToneSort sort);

// The Format chip's options.
struct FormatOption {
  const char* id;     // "nam"
  const char* label;  // the menu row: "Neural Amp Modeler (NAM)"
  const char* chip;   // the selected chip: "NAM"
};
inline constexpr FormatOption kFormatOptions[] = {
    {"nam", "Neural Amp Modeler (NAM)", "NAM"},
    {"ir", "Impulse Response (IR)", "IR"},
};

// The searchable taxonomy filters (Tags, Makes, Creators): each looks its
// options up from its own endpoint and holds the picked names.
enum class Taxonomy { tags, makes, creators };

struct ToneQuery {
  juce::String text;
  std::optional<ToneSort> sort;  // nullopt: the API default for the text
  juce::String gear;             // "" = any (single, like the chip row)
  juce::String format;           // "" = any
  std::vector<juce::String> tags, makes, creators;
  bool calibrated = false;  // see calibratedInForce()
  bool verified = false;
  Profile profile = Profile::none;

  // Calibration is a property of amp and pedal captures. Cabinets and
  // spaces are impulse responses, as is the IR format, so under those the
  // Calibrated filter is parked: kept, but neither shown as set nor sent.
  bool calibratedApplies() const { return gear != "cab" && gear != "space" && format != "ir"; }
  bool calibratedInForce() const { return calibrated && calibratedApplies(); }

  // The API's default sort for this text, and the sort in force.
  ToneSort defaultSort() const { return text.isNotEmpty() ? ToneSort::bestMatch : ToneSort::trending; }
  ToneSort effectiveSort() const { return sort.value_or(defaultSort()); }
  // Store a pick; the default is stored as "no pick" so it never reads as a
  // filter (and the request leaves `sort` to the API).
  void setSort(ToneSort pick) { sort = pick == defaultSort() ? std::nullopt : std::optional(pick); }
  // A sort other than the default: the one case the Sort chip lights up.
  bool sortIsExplicit() const { return sort.has_value() && *sort != defaultSort(); }
  // Anything set behind the filter button (the dot on it).
  bool hasAdvancedFilters() const {
    return sortIsExplicit() || format.isNotEmpty() || !tags.empty() || !makes.empty() || !creators.empty() ||
           calibratedInForce();
  }
  const std::vector<juce::String>& picked(Taxonomy kind) const;
  std::vector<juce::String>& picked(Taxonomy kind);

  // The API path with its query string for one page. `architecture` is the
  // NAM model architecture the plugin loads (< 0 omits the filter). It is
  // always sent: omitting it falls back to the API's legacy A1 + Custom
  // default, and the API ignores it for formats without one (IR).
  juce::String requestPath(int page, int pageSize, int architecture) const;

  bool operator==(const ToneQuery& o) const;
  bool operator!=(const ToneQuery& o) const { return !(*this == o); }
};

}  // namespace t3k::ui
