// `UiTestbed --selftest`: juce::UnitTest cases for the UI's pure logic.
#pragma once

#include <juce_core/juce_core.h>

namespace t3k::ui::testbed {

// `--selftest [--fail-fast] [name ...]`: every "ui" test (0 when all pass), or only the groups
// whose name contains one of the names, in the order given (so what you just
// wrote runs first); --fail-fast stops after the first group with a failure;
// --no-pointer skips the groups that drive real pointer input (they fail when
// someone clicks elsewhere on the machine while they run).
int runSelfTests(const juce::StringArray& args = {});

}  // namespace t3k::ui::testbed
