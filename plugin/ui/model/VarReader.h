// Tolerant readers for the `juce::var` payloads the backend ships. The web
// UI got the same leniency for free from JavaScript (missing keys → undefined,
// numbers of any width); these keep the native parsers equally forgiving so
// an older stored tone or a partial mock never throws a view off.
#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <optional>
#include <vector>

namespace t3k::ui::var_reader {

inline juce::String str(const juce::var& obj, const char* key, const juce::String& fallback = {}) {
  const auto& v = obj[key];
  return v.isVoid() ? fallback : v.toString();
}

inline double num(const juce::var& obj, const char* key, double fallback = 0.0) {
  const auto& v = obj[key];
  if (v.isDouble() || v.isInt() || v.isInt64())
    return static_cast<double>(v);
  if (v.isBool())
    return static_cast<bool>(v) ? 1.0 : 0.0;
  if (v.isString())
    return v.toString().getDoubleValue();
  return fallback;
}

inline std::optional<double> optNum(const juce::var& obj, const char* key) {
  const auto& v = obj[key];
  if (v.isDouble() || v.isInt() || v.isInt64())
    return static_cast<double>(v);
  return std::nullopt;
}

inline int integer(const juce::var& obj, const char* key, int fallback = 0) {
  return static_cast<int>(num(obj, key, fallback));
}

inline bool boolean(const juce::var& obj, const char* key, bool fallback = false) {
  const auto& v = obj[key];
  if (v.isBool())
    return static_cast<bool>(v);
  if (v.isDouble() || v.isInt() || v.isInt64())
    return static_cast<double>(v) > 0.5;
  if (v.isString())
    return v.toString() == "true";
  return fallback;
}

inline std::optional<bool> optBool(const juce::var& obj, const char* key) {
  const auto& v = obj[key];
  return v.isBool() ? std::optional<bool>(static_cast<bool>(v)) : std::nullopt;
}

template <typename T>
std::vector<T> list(const juce::var& obj, const char* key,
                    const std::function<T(const juce::var&)>& parse) {
  std::vector<T> out;
  if (const auto* arr = obj[key].getArray())
    for (const auto& item : *arr)
      out.push_back(parse(item));
  return out;
}

inline std::vector<juce::String> strings(const juce::var& obj, const char* key) {
  return list<juce::String>(obj, key, [](const juce::var& v) { return v.toString(); });
}

inline std::vector<double> numbers(const juce::var& obj, const char* key) {
  return list<double>(obj, key, [](const juce::var& v) { return static_cast<double>(v); });
}

}  // namespace t3k::ui::var_reader
