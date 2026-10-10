#include "ToneQuery.h"

namespace t3k::ui {

const char* profileLabel(Profile profile) {
  switch (profile) {
    case Profile::none: return "";
    case Profile::downloaded: return "Recently used";
    case Profile::favorited: return "Favorites";
    case Profile::created: return "Created";
  }
  return "";
}

const char* profileEndpoint(Profile profile) {
  switch (profile) {
    case Profile::none: return "search";
    case Profile::downloaded: return "downloaded";
    case Profile::favorited: return "favorited";
    case Profile::created: return "created";
  }
  return "search";
}

const char* sortLabel(ToneSort sort) {
  switch (sort) {
    case ToneSort::bestMatch: return "Best match";
    case ToneSort::trending: return "Trending";
    case ToneSort::popular: return "Popular";
    case ToneSort::newest: return "Newest";
    case ToneSort::oldest: return "Oldest";
  }
  return "";
}

const char* sortId(ToneSort sort) {
  switch (sort) {
    case ToneSort::bestMatch: return "best-match";
    case ToneSort::trending: return "trending";
    case ToneSort::popular: return "downloads-all-time";
    case ToneSort::newest: return "newest";
    case ToneSort::oldest: return "oldest";
  }
  return "";
}

const std::vector<juce::String>& ToneQuery::picked(Taxonomy kind) const {
  switch (kind) {
    case Taxonomy::tags: return tags;
    case Taxonomy::makes: return makes;
    case Taxonomy::creators: return creators;
  }
  return tags;
}

std::vector<juce::String>& ToneQuery::picked(Taxonomy kind) {
  switch (kind) {
    case Taxonomy::tags: return tags;
    case Taxonomy::makes: return makes;
    case Taxonomy::creators: return creators;
  }
  return tags;
}

namespace {

// Query-string values, each escaped on its own so the API's list separator
// (`_` for tags and makes, `,` for creators, whose names may contain `_`)
// stays literal between them.
juce::String joined(const std::vector<juce::String>& values, const char* separator) {
  juce::StringArray escaped;
  for (const auto& v : values) escaped.add(juce::URL::addEscapeChars(v, true));
  return escaped.joinIntoString(separator);
}

class QueryString {
public:
  QueryString& add(const char* key, const juce::String& value) {
    if (value.isNotEmpty()) parts_.add(juce::String(key) + "=" + value);
    return *this;
  }
  QueryString& addEscaped(const char* key, const juce::String& value) {
    return add(key, juce::URL::addEscapeChars(value, true));
  }
  QueryString& addFlag(const char* key, bool on) { return add(key, on ? "true" : ""); }
  juce::String str() const { return parts_.isEmpty() ? juce::String() : "?" + parts_.joinIntoString("&"); }

private:
  juce::StringArray parts_;
};

}  // namespace

juce::String ToneQuery::requestPath(int page, int pageSize, int architecture) const {
  QueryString qs;
  qs.add("page", juce::String(page)).add("page_size", juce::String(pageSize));
  const juce::String base = "/api/v1/tones/" + juce::String(profileEndpoint(profile));
  qs.addEscaped("query", text.trim());
  if (profile != Profile::none) return base + qs.addEscaped("gear", gear).str();

  if (sort) qs.add("sort", sortId(*sort));
  qs.addEscaped("gears", gear);
  qs.add("format", format);
  qs.add("tags", joined(tags, "_"));
  qs.add("makes", joined(makes, "_"));
  qs.add("creators", joined(creators, ","));
  qs.addFlag("calibrated", calibratedInForce()).addFlag("verified", verified);
  if (architecture >= 0) qs.add("architecture", juce::String(architecture));
  return base + qs.str();
}

bool ToneQuery::operator==(const ToneQuery& o) const {
  return text == o.text && sort == o.sort && gear == o.gear && format == o.format && tags == o.tags &&
         makes == o.makes && creators == o.creators && calibrated == o.calibrated && verified == o.verified &&
         profile == o.profile;
}

}  // namespace t3k::ui
