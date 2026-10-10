#include "Labels.h"

#include <cmath>
#include <map>

namespace t3k::ui::labels {

namespace {

// Mirrors the web's GEAR_CAPTURE_SMALL_MAP (compact card labels).
const std::map<juce::String, juce::String> kGear = {
    {"amp", "Amp Head"},     {"amp-cab", "Amp + Cab"}, {"full-rig", "Amp + Cab"},
    {"pedal", "Pedal"},      {"outboard", "Outboard"}, {"cab", "Cabinet"},
    {"space", "Space"},      {"experimental", "Experimental"}, {"ir", "IR"},
};

const std::map<juce::String, juce::String> kFormat = {
    {"nam", "NAM"}, {"ir", "IR"}, {"aida-x", "AIDA-X"}, {"aa-snapshot", "Snapshot"}, {"proteus", "Proteus"},
};

// Up to `significant` significant digits, trailing zeros dropped ("12.3",
// "1.234", "10").
juce::String significantDigits(double v, int significant) {
  const int intDigits = v >= 1 ? static_cast<int>(std::floor(std::log10(v))) + 1 : 1;
  const int decimals = std::max(0, significant - intDigits);
  auto s = juce::String(v, decimals);
  if (s.containsChar('.')) s = s.trimCharactersAtEnd("0").trimCharactersAtEnd(".");
  return s;
}

}  // namespace

const std::vector<GearFilter>& gearFilters() {
  static const std::vector<GearFilter> filters = {
      {"amp-cab", "Amp + Cab"}, {"amp", "Amp Head"},       {"cab", "Cabinet"},
      {"pedal", "Pedal"},       {"outboard", "Outboard"},  {"space", "Spaces"},
      {"experimental", "Experimental"},
  };
  return filters;
}

juce::String gear(const juce::String& g) {
  if (g.isEmpty()) return {};
  const auto it = kGear.find(g.toLowerCase());
  return it == kGear.end() ? g : it->second;
}

juce::String format(const juce::String& f) {
  if (f.isEmpty()) return {};
  const auto it = kFormat.find(f.toLowerCase());
  return it == kFormat.end() ? f.toUpperCase() : it->second;
}

juce::String count(double value) {
  if (!std::isfinite(value) || value < 0) value = 0;
  const auto n = static_cast<juce::int64>(std::llround(value));
  if (n < 10000) {
    // en-US grouping.
    auto digits = juce::String(n);
    juce::String out;
    for (int i = 0; i < digits.length(); ++i) {
      if (i > 0 && (digits.length() - i) % 3 == 0) out << ',';
      out << digits[i];
    }
    return out;
  }
  const char* units[] = {"K", "M", "B", "T"};
  double scaled = static_cast<double>(n) / 1000.0;
  int unit = 0;
  while (scaled >= 1000.0 && unit < 3) {
    scaled /= 1000.0;
    ++unit;
  }
  auto text = significantDigits(scaled, 4);
  // Rounding to 4 significant digits can carry into the next unit.
  if (text.getDoubleValue() >= 1000.0 && unit < 3) {
    ++unit;
    text = significantDigits(scaled / 1000.0, 4);
  }
  return text + units[unit];
}

juce::String toFixed(double value, int decimals) {
  if (decimals > 0) return juce::String(value, decimals);
  return juce::String(static_cast<juce::int64>(std::llround(value)));
}

juce::String timeAgoShort(const juce::String& iso8601, juce::Time now) {
  if (iso8601.length() < 4) return {};  // fromISO8601 asserts on an empty string
  const auto then = juce::Time::fromISO8601(iso8601);
  if (then == juce::Time()) return {};
  const auto seconds = (now.toMilliseconds() - then.toMilliseconds()) / 1000;
  constexpr juce::int64 kMinute = 60, kHour = kMinute * 60, kDay = kHour * 24, kWeek = kDay * 7,
                        kMonth = kWeek * 4, kYear = kMonth * 12;
  if (seconds < 10) return "now";
  if (seconds < kMinute) return juce::String(seconds) + "s";
  if (seconds < kHour) return juce::String(seconds / kMinute) + "m";
  if (seconds < kDay) return juce::String(seconds / kHour) + "h";
  if (seconds < kWeek) return juce::String(seconds / kDay) + "d";
  if (seconds < kMonth) return juce::String(seconds / kWeek) + "w";
  if (seconds < kYear) return juce::String(seconds / kMonth) + "mo";
  return juce::String(seconds / kYear) + "y";
}

}  // namespace t3k::ui::labels
