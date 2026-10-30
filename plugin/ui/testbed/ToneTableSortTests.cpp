#include "model/ToneTableSort.h"

#include <juce_core/juce_core.h>
#include <algorithm>
#include <climits>
#include <optional>
#include <vector>

namespace t3k::ui::testbed {
namespace {

Tone row(int id, const juce::String& title, const juce::String& creator = {}) {
  Tone tone;
  tone.id = id; tone.title = title; tone.format = "nam";
  if (creator.isNotEmpty()) {
    User user; user.id = "creator-" + juce::String(id); user.username = creator;
    tone.user = std::move(user);
  }
  auto* raw = new juce::DynamicObject;
  raw->setProperty("id", id); raw->setProperty("title", title);
  raw->setProperty("extraUnmappedField", "preserve this payload verbatim");
  tone.raw = juce::var(raw);
  return tone;
}

struct ToneTableSortTests final : juce::UnitTest {
  ToneTableSortTests() : juce::UnitTest("ToneTableSort", "ui") {}

  void expectOrder(const std::vector<Tone>& tones, ToneColumn column, bool ascending,
                   const std::vector<size_t>& expected) {
    const auto actual = toneTableOrder(tones, ToneColumnSort{column, ascending});
    expect(actual == expected, juce::String(toneColumnName(column)) + (ascending ? " ascending" : " descending"));
    auto permutation = actual;
    std::sort(permutation.begin(), permutation.end());
    expectEquals(permutation.size(), tones.size());
    for (size_t i = 0; i < permutation.size(); ++i) expectEquals(permutation[i], i);
  }

  void runTest() override {
    beginTest("titles use case-insensitive natural numbers in both directions");
    std::vector<Tone> tones{row(30,"Amp 10"), row(20,"amp 2"), row(10,"AMP 1")};
    expectOrder(tones, ToneColumn::title, true, {2,1,0});
    expectOrder(tones, ToneColumn::title, false, {0,1,2});

    beginTest("creator uses visible display name or username fallback, with natural order");
    tones = {row(30,"a","username z"), row(20,"b","Creator 10"), row(10,"c","creator 1")};
    tones[0].user->displayName = "Creator 2";
    tones[0].user->isVerified = true;
    expectOrder(tones, ToneColumn::creator, true, {2,0,1});
    expectOrder(tones, ToneColumn::creator, false, {1,0,2});

    beginTest("gear and format strings ignore capitalization and retain ID tie breaks");
    tones = {row(30,"a"),row(20,"b"),row(10,"c")};
    tones[0].gear = "Cab 10"; tones[1].gear = "cab 2"; tones[2].gear = "CAB 1";
    expectOrder(tones, ToneColumn::gear, true, {2,1,0});
    expectOrder(tones, ToneColumn::gear, false, {0,1,2});
    tones[0].format = "NAM"; tones[1].format = "ir"; tones[2].format = "nam";
    expectOrder(tones, ToneColumn::format, true, {1,2,0});
    expectOrder(tones, ToneColumn::format, false, {2,0,1});

    beginTest("model counts sort numerically using the playable catalog count");
    tones = {row(30,"a"),row(20,"b"),row(10,"c")};
    tones[0].modelsCount = 100; tones[0].a2ModelsCount = 2;
    tones[1].format = "ir"; tones[1].modelsCount = 10; tones[1].a2ModelsCount = 0;
    tones[2].modelsCount = 200; tones[2].a2ModelsCount = 1;
    expectOrder(tones, ToneColumn::models, true, {2,0,1});
    expectOrder(tones, ToneColumn::models, false, {1,0,2});

    beginTest("download and favorite counts are numeric without overflow-prone subtraction");
    tones[0].downloadsCount = 20; tones[1].downloadsCount = 3; tones[2].downloadsCount = 100;
    expectOrder(tones, ToneColumn::downloads, true, {1,0,2});
    expectOrder(tones, ToneColumn::downloads, false, {2,0,1});
    tones[0].favoritesCount = INT_MAX; tones[1].favoritesCount = 2; tones[2].favoritesCount = 1000;
    expectOrder(tones, ToneColumn::favorites, true, {1,2,0});
    expectOrder(tones, ToneColumn::favorites, false, {0,2,1});

    beginTest("publication order compares actual instants, not timestamp text");
    tones = {row(30,"a"),row(20,"b"),row(10,"c")};
    tones[0].publishedAt = "2026-10-30T09:00:00+02:00";
    tones[1].publishedAt = "2026-10-30T07:30:00Z";
    tones[2].publishedAt = "2026-10-30T08:00:00-01:00";
    expectOrder(tones, ToneColumn::published, true, {0,1,2});
    expectOrder(tones, ToneColumn::published, false, {2,1,0});

    beginTest("offsets crossing a month/day boundary compare chronologically");
    tones[0].publishedAt = "2026-11-01T00:15:00+01:00";
    tones[1].publishedAt = "2026-10-31T23:30:00Z";
    tones[2].publishedAt = "2026-10-31T23:00:00-01:00";
    expectOrder(tones, ToneColumn::published, true, {0,1,2});
    expectOrder(tones, ToneColumn::published, false, {2,1,0});

    beginTest("equal timezone-normalized instants tie by ID in either direction");
    tones = {row(20,"a"),row(10,"b")};
    tones[0].publishedAt = "2026-10-30T07:00:00Z";
    tones[1].publishedAt = "2026-10-30T09:00:00+02:00";
    expectOrder(tones, ToneColumn::published, true, {1,0});
    expectOrder(tones, ToneColumn::published, false, {1,0});

    beginTest("submillisecond precision is retained and equivalent trailing zeros tie");
    tones = {row(1,"a"),row(20,"b"),row(10,"c")};
    tones[0].publishedAt = "2026-10-30T07:00:00.0000002Z";
    tones[1].publishedAt = "2026-10-30T07:00:00.0000001Z";
    tones[2].publishedAt = "2026-10-30T07:00:00.00000010Z";
    expectOrder(tones, ToneColumn::published, true, {2,1,0});
    expectOrder(tones, ToneColumn::published, false, {0,2,1});

    beginTest("missing creators stay last, including an empty user object");
    tones = {row(30,"a"),row(4,"b"),row(10,"c","Zed"),row(20,"d","Alpha")};
    tones[1].user = User{};
    expectOrder(tones, ToneColumn::creator, true, {3,2,1,0});
    expectOrder(tones, ToneColumn::creator, false, {2,3,1,0});

    beginTest("missing/malformed dates are last while the Unix epoch remains a valid date");
    tones = {row(9,"a"),row(4,"b"),row(3,"c"),row(2,"d"),row(1,"e")};
    tones[1].publishedAt = "not a date";
    tones[2].publishedAt = "2026-02-29T00:00:00Z";
    tones[3].publishedAt = "2026-10-30T00:00:00Z";
    tones[4].publishedAt = "1970-01-01T00:00:00Z";
    expectOrder(tones, ToneColumn::published, true, {4,3,2,1,0});
    expectOrder(tones, ToneColumn::published, false, {3,4,2,1,0});

    beginTest("date-only/basic ISO and zone-less values have deterministic UTC semantics");
    tones = {row(3,"a"),row(1,"b"),row(2,"c"),row(4,"d")};
    tones[0].publishedAt = "2026-10-30";
    tones[1].publishedAt = "2026-10-29T23:30:00Z";
    tones[2].publishedAt = "20261030T003000+0100";
    tones[3].publishedAt = "2026-10-30T00:00:00";
    expectOrder(tones, ToneColumn::published, true, {1,2,0,3});

    beginTest("invalid calendar/time/offset values never become plausible timestamps");
    tones = {row(1,"valid"),row(2,"month"),row(3,"time"),row(4,"offset"),row(5,"junk")};
    tones[0].publishedAt = "2024-02-29T00:00:00Z";
    tones[1].publishedAt = "2026-13-01T00:00:00Z";
    tones[2].publishedAt = "2026-10-30T25:00:00Z";
    tones[3].publishedAt = "2026-10-30T12:00:00+24:00";
    tones[4].publishedAt = "2026-10-30T12:00:00Zgarbage";
    expectOrder(tones, ToneColumn::published, true, {0,1,2,3,4});
    expectOrder(tones, ToneColumn::published, false, {0,1,2,3,4});

    beginTest("equal keys use ascending ID and then original index, not unstable reverse ties");
    tones = {row(5,"Amp 2"),row(2,"amp 2"),row(2,"AMP 2"),row(9,"Amp 2")};
    expectOrder(tones, ToneColumn::title, true, {1,2,0,3});
    expectOrder(tones, ToneColumn::title, false, {1,2,0,3});

    beginTest("turning sorting off restores exact API order without mutating Tone or raw payloads");
    tones = {row(10,"Bravo"),row(20,"Alpha"),row(30,"Charlie")};
    std::vector<juce::String> rawBefore;
    for (const auto& tone : tones) rawBefore.push_back(tone.toJson());
    const auto sorted = toneTableOrder(tones, ToneColumnSort{ToneColumn::title,true});
    expect(sorted == std::vector<size_t>{1,0,2});
    const int selectedId = 20;
    const auto selected = std::find_if(sorted.begin(), sorted.end(), [&](size_t index) { return tones[index].id == selectedId; });
    expect(selected != sorted.end());
    if (selected != sorted.end()) expect(&tones[*selected] == &tones[1]);
    expect(toneTableOrder(tones, std::nullopt) == std::vector<size_t>{0,1,2});
    for (size_t i = 0; i < tones.size(); ++i) expectEquals(tones[i].toJson(), rawBefore[i]);
    expectEquals(tones[0].title, juce::String("Bravo"));
    expectEquals(tones[1].id, selectedId);

    beginTest("empty/singleton catalogs and all visible column names");
    expect(toneTableOrder({}, std::nullopt).empty());
    expect(toneTableOrder({}, ToneColumnSort{}).empty());
    expect(toneTableOrder({row(50,"only")}, ToneColumnSort{ToneColumn::published,false}) == std::vector<size_t>{0});
    juce::StringArray names;
    for (const auto column : {ToneColumn::title,ToneColumn::creator,ToneColumn::gear,ToneColumn::format,
                              ToneColumn::models,ToneColumn::downloads,ToneColumn::favorites,ToneColumn::published}) {
      const juce::String name = toneColumnName(column);
      expect(name.isNotEmpty()); expect(!names.contains(name)); names.add(name);
    }
  }
};
static ToneTableSortTests toneTableSortTests;

} // namespace
} // namespace t3k::ui::testbed
