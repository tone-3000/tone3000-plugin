// Display strings for TONE3000 catalog data (ports of labels.ts,
// formatCount.ts and timeAgoShort.ts): gear and format names, compact
// social counts, and the creator line's relative time.
#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace t3k::ui::labels {

// JavaScript's Number.toFixed: `decimals` places, trailing zeros kept, and
// none at 0 ("38", where juce::String(38.4, 0) would print "38.4").
juce::String toFixed(double value, int decimals);

// The browser's gear filter chips (labels.ts GEAR_FILTERS), in order.
struct GearFilter {
  const char* id;
  const char* label;
};
const std::vector<GearFilter>& gearFilters();

// "amp" → "Amp Head", unknown gear → the id as is, "" → "".
juce::String gear(const juce::String& gear);
// "nam" → "NAM", unknown → upper-cased, "" → "".
juce::String format(const juce::String& format);

// Compact social-style counts: exact with grouping under 10,000 ("1,234"),
// then K / M / B with at most 4 significant digits ("12.3K", "1.234M").
juce::String count(double value);

// Compact relative time ("now", "45s", "13h", "3d", "2w", "5mo", "1y") for
// an ISO-8601 timestamp; "" when it doesn't parse.
juce::String timeAgoShort(const juce::String& iso8601, juce::Time now = juce::Time::getCurrentTime());

}  // namespace t3k::ui::labels
