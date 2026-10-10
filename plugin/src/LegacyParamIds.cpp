#include "LegacyParamIds.h"

#include <array>

namespace t3k::legacy_ids {

namespace {

struct Rename {
  const char* legacy;
  const char* current;
};

// The pitch shifter shipped to beta testers as "Transpose".
constexpr std::array<Rename, 5> kRenames{{
    {"transposeEnabled", "pitchEnabled"},
    {"transposeSemitones", "pitchSemitones"},
    {"transposeStep", "pitchStep"},
    {"transposeTonality", "pitchTonality"},
    {"transposeWindow", "pitchWindow"},
}};

}  // namespace

juce::String currentParamId(const juce::String& id) {
  for (const auto& r : kRenames)
    if (id == r.legacy)
      return r.current;
  return id;
}

int migrateParamIds(juce::ValueTree tree, const juce::Identifier& property) {
  int renamed = 0;
  // Index-based: removing a stale duplicate shifts the children after it.
  for (int i = 0; i < tree.getNumChildren(); ++i) {
    juce::ValueTree child = tree.getChild(i);
    const juce::String legacy = child.getProperty(property).toString();
    const juce::String current = currentParamId(legacy);
    if (current == legacy)
      continue;
    const juce::ValueTree stale = tree.getChildWithProperty(property, current);
    if (stale.isValid()) {
      if (tree.indexOf(stale) < i)
        --i;
      tree.removeChild(stale, nullptr);
    }
    child.setProperty(property, current, nullptr);
    ++renamed;
  }
  return renamed;
}

}  // namespace t3k::legacy_ids
