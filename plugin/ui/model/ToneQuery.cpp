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

namespace {

juce::var stringList(const std::vector<juce::String>& values) {
  juce::Array<juce::var> out;
  for (const auto& v : values) out.add(v);
  return out;
}

std::vector<juce::String> readList(const juce::var& v) {
  std::vector<juce::String> out;
  if (const auto* arr = v.getArray())
    for (const auto& item : *arr)
      if (item.isString() && out.size() < 32) out.push_back(item.toString().substring(0, 100));
  return out;
}

}  // namespace

juce::var ToneQuery::toVar() const {
  juce::DynamicObject::Ptr o = new juce::DynamicObject();
  o->setProperty("text", text);
  if (sort) o->setProperty("sort", sortId(*sort));
  o->setProperty("gear", gear);
  o->setProperty("format", format);
  o->setProperty("tags", stringList(tags));
  o->setProperty("makes", stringList(makes));
  o->setProperty("creators", stringList(creators));
  o->setProperty("calibrated", calibrated);
  o->setProperty("verified", verified);
  o->setProperty("profile", profileEndpoint(profile));
  return juce::var(o.get());
}

ToneQuery ToneQuery::fromVar(const juce::var& v) {
  ToneQuery q;
  if (v.getDynamicObject() == nullptr) return q;
  const auto str = [&v](const char* key, int maxLength) {
    const auto& value = v[key];
    return value.isString() ? value.toString().substring(0, maxLength) : juce::String();
  };
  q.text = str("text", 200);
  const auto sort = str("sort", 32);
  for (const auto option : kToneSorts)
    if (sort == sortId(option)) q.sort = option;
  // Only the catalog's own gear and format ids; anything else would reach
  // the request path.
  const auto gear = str("gear", 32);
  if (gear.containsOnly("abcdefghijklmnopqrstuvwxyz-")) q.gear = gear;
  const auto format = str("format", 16);
  for (const auto& option : kFormatOptions)
    if (format == option.id) q.format = format;
  q.tags = readList(v["tags"]);
  q.makes = readList(v["makes"]);
  q.creators = readList(v["creators"]);
  q.calibrated = static_cast<bool>(v.getProperty("calibrated", false));
  q.verified = static_cast<bool>(v.getProperty("verified", false));
  const auto profile = str("profile", 32);
  for (const auto option : kProfiles)
    if (profile == profileEndpoint(option)) q.profile = option;
  return q;
}

bool ToneQuery::operator==(const ToneQuery& o) const {
  return text == o.text && sort == o.sort && gear == o.gear && format == o.format && tags == o.tags &&
         makes == o.makes && creators == o.creators && calibrated == o.calibrated && verified == o.verified &&
         profile == o.profile;
}

}  // namespace t3k::ui
