// A2 only in the Library (NamArchitecture.h): folders named for A1 captures
// ("(A1)", "xSTD", "REVyHI") and folders left empty by them stay out of the
// listing. Files aren't read for it.
#include "Library.h"
#include "NamArchitecture.h"
#include "test_helpers.h"

#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

namespace {

juce::StringArray names(const juce::var& node) {
  juce::StringArray out;
  if (const auto* kids = node["children"].getArray())
    for (const auto& kid : *kids)
      out.add(kid["name"].toString());
  return out;
}

juce::var child(const juce::var& node, const juce::String& name) {
  if (const auto* kids = node["children"].getArray())
    for (const auto& kid : *kids)
      if (kid["name"].toString() == name)
        return kid;
  return {};
}

}  // namespace

TEST(NamArchitectureTest, NamesMarkingA1) {
  for (const char* name : {"Amp A1", "Amp (A1)", "[A1] Amp", "Amp - xSTD", "REVyHI captures", "amp (xstd)"})
    EXPECT_TRUE(nam_arch::namedNotA2(name)) << name;
  for (const char* name : {"A2", "Amp A12", "BA1", "Amp-A1", "xSTDs", "Clean"})
    EXPECT_FALSE(nam_arch::namedNotA2(name)) << name;
}

TEST(NamArchitectureTest, TheLibraryLeavesOutA1Folders) {
  const auto base = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("t3k-nam-arch-tests-" + juce::Uuid().toString());
  base.createDirectory();
  {
    PresetManager presets(base.getChildFile("Presets"));
    LocalLibrary library(presets);
    library.setLocation(base.getChildFile("Library"), "me");
    const auto captures = library.capturesDir();
    const auto add = [](const juce::File& dir) {
      dir.createDirectory();
      testFile("a2-amp-test.nam").copyFileTo(dir.getChildFile("Capture.nam"));
    };
    add(captures.getChildFile("Amp"));
    add(captures.getChildFile("Amp (A1)"));                      // named for A1: out, whatever is in it
    add(captures.getChildFile("Stack").getChildFile("xSTD"));    // all Stack holds is an A1 folder: out too
    add(captures.getChildFile("Mixed").getChildFile("[REVyHI]"));
    add(captures.getChildFile("Mixed"));
    captures.getChildFile("Empty").createDirectory();
    add(captures.getChildFile("__MACOSX"));  // a Mac zip's leftovers

    const auto mine = child(library.scan()["libraries"][0], "Captures");
    ASSERT_TRUE(mine.isObject());
    EXPECT_EQ(names(mine), juce::StringArray({"Amp", "Empty", "Mixed"}))
        << "A1-named folders out, and one holding only those; an empty one stays";
    EXPECT_EQ(names(child(mine, "Mixed")), juce::StringArray({"Capture"}));
  }
  base.deleteRecursively();
}
