#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <string>

namespace t3k::routing {
/** Copy a complete processor state, bypassing explicitly selected,
    separable cabinets for each rendered lane. Stereo requires both lane
    assignments; mono uses only index 0. Each UUID must belong to that
    lane's effective signal path: its own blocks, plus the trunk prefix
    through the branch anchor where applicable. Shared-prefix assignments
    bypass the one physical block once. UUIDs, branch anchors, models and
    downstream FX stay intact. Failure leaves both buffers unchanged. */
juce::Result cabinetBypassState(const juce::MemoryBlock& original,
                                const std::array<std::string, 2>& laneCabinetIds,
                                juce::MemoryBlock& result);
}
