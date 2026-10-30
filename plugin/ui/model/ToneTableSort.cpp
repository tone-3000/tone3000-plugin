#include "ToneTableSort.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <numeric>
#include <string>

namespace t3k::ui {
namespace {

struct DateKey {
  std::int64_t seconds = 0;
  std::string fraction;
};

// The API supplies ISO timestamps. Parse their calendar and numeric offset,
// never the display locale/time zone. Preserve arbitrary fractional precision
// so two publications within one millisecond do not become an artificial tie.
// Extended/basic dates and times are accepted; a date-only or zone-less value
// is interpreted as UTC, consistently across Windows/macOS and DST changes.
std::optional<DateKey> dateKey(const juce::String& source) {
  const auto text = source.trim().toStdString();
  size_t position = 0;
  auto digits = [&](int count) -> std::optional<int> {
    if (position + static_cast<size_t>(count) > text.size()) return {};
    int value = 0;
    for (int i = 0; i < count; ++i) {
      const char ch = text[position++];
      if (ch < '0' || ch > '9') return {};
      value = value * 10 + (ch - '0');
    }
    return value;
  };
  auto consume = [&](char ch) {
    if (position >= text.size() || text[position] != ch) return false;
    ++position;
    return true;
  };
  const auto year = digits(4);
  if (!year) return {};
  const bool dashed = consume('-');
  const auto month = digits(2);
  if (!month || (dashed && !consume('-'))) return {};
  const auto day = digits(2);
  if (!day) return {};
  const std::chrono::year_month_day calendar{
      std::chrono::year{*year}, std::chrono::month{static_cast<unsigned>(*month)},
      std::chrono::day{static_cast<unsigned>(*day)}};
  if (!calendar.ok()) return {};

  int hour = 0, minute = 0, second = 0;
  std::string fraction;
  if (position < text.size() && (text[position] == 'T' || text[position] == 't')) {
    ++position;
    const auto parsedHour = digits(2);
    const bool colons = consume(':');
    const auto parsedMinute = digits(2);
    if (!parsedHour || !parsedMinute || (colons && !consume(':'))) return {};
    const auto parsedSecond = digits(2);
    if (!parsedSecond || *parsedHour > 23 || *parsedMinute > 59 || *parsedSecond > 59) return {};
    hour = *parsedHour; minute = *parsedMinute; second = *parsedSecond;
    if (consume('.') || consume(',')) {
      const size_t start = position;
      while (position < text.size() && text[position] >= '0' && text[position] <= '9') ++position;
      if (start == position) return {};
      fraction = text.substr(start, position - start);
      while (!fraction.empty() && fraction.back() == '0') fraction.pop_back();
    }
  }
  int offsetSeconds = 0;
  if (position < text.size()) {
    const char zone = text[position++];
    if (zone == '+' || zone == '-') {
      const auto offsetHour = digits(2);
      if (!offsetHour || *offsetHour > 23) return {};
      int offsetMinute = 0;
      const bool colon = consume(':');
      if (colon || position < text.size()) {
        const auto value = digits(2);
        if (!value || *value > 59) return {};
        offsetMinute = *value;
      }
      offsetSeconds = (*offsetHour * 3600 + offsetMinute * 60) * (zone == '+' ? 1 : -1);
    } else if (zone != 'Z' && zone != 'z') {
      return {};
    }
  }
  if (position != text.size()) return {};
  const auto days = std::chrono::sys_days{calendar}.time_since_epoch().count();
  return DateKey{static_cast<std::int64_t>(days) * 86400 + hour * 3600 + minute * 60 + second - offsetSeconds,
                 std::move(fraction)};
}

int compareDate(const DateKey& a, const DateKey& b) {
  if (a.seconds != b.seconds) return a.seconds < b.seconds ? -1 : 1;
  for (size_t i = 0; i < std::max(a.fraction.size(), b.fraction.size()); ++i) {
    const char left = i < a.fraction.size() ? a.fraction[i] : '0';
    const char right = i < b.fraction.size() ? b.fraction[i] : '0';
    if (left != right) return left < right ? -1 : 1;
  }
  return 0;
}

int compareNumber(int a, int b) {
  return a < b ? -1 : a > b ? 1 : 0;
}

} // namespace

const char* toneColumnName(ToneColumn column) {
  switch (column) {
    case ToneColumn::title: return "Title";
    case ToneColumn::creator: return "Creator";
    case ToneColumn::gear: return "Gear";
    case ToneColumn::format: return "Format";
    case ToneColumn::models: return "Models";
    case ToneColumn::downloads: return "Downloads";
    case ToneColumn::favorites: return "Favorites";
    case ToneColumn::published: return "Published";
  }
  return "Column";
}

std::vector<size_t> toneTableOrder(const std::vector<Tone>& tones,
                                  std::optional<ToneColumnSort> sort) {
  std::vector<size_t> order(tones.size());
  std::iota(order.begin(), order.end(), size_t{0});
  if (!sort) return order;

  const auto column = sort->column;
  std::vector<juce::String> strings(tones.size());
  std::vector<std::optional<DateKey>> dates;
  if (column == ToneColumn::published) dates.reserve(tones.size());
  for (size_t i = 0; i < tones.size(); ++i) {
    const auto& tone = tones[i];
    switch (column) {
      case ToneColumn::title: strings[i] = tone.title; break;
      case ToneColumn::creator: strings[i] = tone.user ? tone.user->name().trim() : juce::String(); break;
      case ToneColumn::gear: strings[i] = tone.gear; break;
      case ToneColumn::format: strings[i] = tone.format; break;
      case ToneColumn::published: dates.push_back(dateKey(tone.publishedAt)); break;
      case ToneColumn::models:
      case ToneColumn::downloads:
      case ToneColumn::favorites: break;
    }
  }
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    int comparison = 0;
    if (column == ToneColumn::published) {
      if (dates[a].has_value() != dates[b].has_value()) return dates[a].has_value();
      if (dates[a]) comparison = compareDate(*dates[a], *dates[b]);
    } else if (column == ToneColumn::models) {
      comparison = compareNumber(tones[a].catalogModelCount(), tones[b].catalogModelCount());
    } else if (column == ToneColumn::downloads) {
      comparison = compareNumber(tones[a].downloadsCount, tones[b].downloadsCount);
    } else if (column == ToneColumn::favorites) {
      comparison = compareNumber(tones[a].favoritesCount, tones[b].favoritesCount);
    } else {
      if (column == ToneColumn::creator && strings[a].isEmpty() != strings[b].isEmpty())
        return strings[a].isNotEmpty();
      comparison = strings[a].compareNatural(strings[b], false);
    }
    if (comparison != 0) return sort->ascending ? comparison < 0 : comparison > 0;
    if (tones[a].id != tones[b].id) return tones[a].id < tones[b].id;
    return a < b;
  });
  return order;
}

} // namespace t3k::ui
