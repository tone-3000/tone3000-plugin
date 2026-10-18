#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>

// A tone block's browse context: the result list its tone was picked from,
// so the block can step to the previous / next result without reopening the
// browser (the gallery's ‹ › and the MIDI next / prev targets).
//
//   { query:      the browser's filters (opaque here; the UI's ToneQuery),
//     page:       which page of that query `tones` holds (1-based),
//     pageSize:   the page size it was fetched with,
//     totalPages: how many pages the query had then,
//     index:      the block's position in `tones`,
//     tones:      [{ id, title, gear, format, a2 }] }
//
// The context arrives from three untrusted places (the UI, project state,
// presets and shared rigs), so every entry point runs it through sanitize:
// anything that is not the shape above, or is out of range, is dropped or
// clamped, and a context that keeps no tones becomes void (no cursor).
namespace browsecontext {

inline constexpr int kMaxTones = 100;
inline constexpr int kMaxPage = 10000;
inline constexpr int kMaxPageSize = 100;
inline constexpr int kMaxTextLength = 200;
// The query is opaque to the processor; cap its serialized size so a
// hostile file can't park megabytes in every undo snapshot.
inline constexpr int kMaxQueryBytes = 4096;

inline int clampInt(const juce::var& v, int lo, int hi, int fallback) {
  if (!(v.isInt() || v.isInt64() || v.isDouble()))
    return fallback;
  const double d = static_cast<double>(v);
  if (!std::isfinite(d))
    return fallback;
  return static_cast<int>(juce::jlimit(static_cast<double>(lo), static_cast<double>(hi), d));
}

inline juce::String cleanText(const juce::var& v, int maxLength) {
  return v.isString() ? v.toString().substring(0, maxLength) : juce::String();
}

// Plain JSON values only (objects, arrays, strings, numbers, bools); no
// methods, binary blobs or other var kinds survive.
inline bool isPlainJson(const juce::var& v, int depth = 0) {
  if (depth > 8)
    return false;
  if (v.isVoid() || v.isUndefined() || v.isBool() || v.isInt() || v.isInt64() || v.isDouble() ||
      v.isString())
    return true;
  if (const auto* arr = v.getArray()) {
    for (const auto& item : *arr)
      if (!isPlainJson(item, depth + 1))
        return false;
    return true;
  }
  if (auto* obj = v.getDynamicObject()) {
    for (const auto& prop : obj->getProperties())
      if (!isPlainJson(prop.value, depth + 1))
        return false;
    return true;
  }
  return false;
}

inline juce::var sanitize(const juce::var& in) {
  if (in.getDynamicObject() == nullptr)
    return {};
  const auto* rows = in["tones"].getArray();
  if (rows == nullptr)
    return {};

  juce::Array<juce::var> tones;
  for (const auto& row : *rows) {
    if (tones.size() >= kMaxTones)
      break;
    const int id = clampInt(row["id"], 0, std::numeric_limits<int>::max(), 0);
    if (row.getDynamicObject() == nullptr || id <= 0)
      continue;
    juce::DynamicObject::Ptr tone = new juce::DynamicObject();
    tone->setProperty("id", id);
    tone->setProperty("title", cleanText(row["title"], kMaxTextLength));
    tone->setProperty("gear", cleanText(row["gear"], 32));
    tone->setProperty("format", cleanText(row["format"], 16));
    tone->setProperty("a2", static_cast<bool>(row.getProperty("a2", true)));
    tones.add(juce::var(tone.get()));
  }
  if (tones.isEmpty())
    return {};

  juce::DynamicObject::Ptr out = new juce::DynamicObject();
  const juce::var query = in["query"];
  if (query.getDynamicObject() != nullptr && isPlainJson(query) &&
      juce::JSON::toString(query, true).getNumBytesAsUTF8() <= static_cast<size_t>(kMaxQueryBytes))
    out->setProperty("query", query.clone());
  const int totalPages = clampInt(in["totalPages"], 1, kMaxPage, 1);
  out->setProperty("page", clampInt(in["page"], 1, totalPages, 1));
  out->setProperty("pageSize", clampInt(in["pageSize"], 1, kMaxPageSize, tones.size()));
  out->setProperty("totalPages", totalPages);
  out->setProperty("index", clampInt(in["index"], 0, tones.size() - 1, 0));
  out->setProperty("tones", tones);
  return juce::var(out.get());
}

inline juce::var fromString(const juce::String& json) {
  if (json.isEmpty() || json.getNumBytesAsUTF8() > 64 * 1024)
    return {};
  return sanitize(juce::JSON::parse(json));
}

inline juce::String toString(const juce::var& context) {
  return context.getDynamicObject() != nullptr ? juce::JSON::toString(context, true) : juce::String();
}

}  // namespace browsecontext
