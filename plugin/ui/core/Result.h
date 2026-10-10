// A reply to an asynchronous call: the value, or the user-facing reason
// there is none.
#pragma once

#include <juce_core/juce_core.h>

#include <optional>

namespace t3k::ui {

template <typename T>
struct Result {
  std::optional<T> value;
  juce::String error;

  explicit operator bool() const { return value.has_value(); }
  const T& operator*() const { return *value; }
  const T* operator->() const { return &*value; }

  static Result ok(T v) { return {std::move(v), {}}; }
  static Result fail(juce::String why) { return {std::nullopt, std::move(why)}; }
};

}  // namespace t3k::ui
