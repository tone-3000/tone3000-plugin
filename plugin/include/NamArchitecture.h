#pragma once
// The plugin plays A2 captures only, so the Library leaves out folders named
// for A1 ones (and what is in them). Files aren't read for this: on a big
// collection on a slow drive that costs too much.
#include <juce_core/juce_core.h>

namespace nam_arch {

// A name marking an A1 capture or collection: "A1", "REVyHI" or "xSTD" as a
// word of its own, bare or in brackets ("(A1)", "[xSTD]").
bool namedNotA2(const juce::String& name);

}  // namespace nam_arch
