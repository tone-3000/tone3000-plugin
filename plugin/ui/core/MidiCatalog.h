// Display catalog for the MIDI mapping UI (port of midiCatalog.ts): which
// targets are mappable and how to present them. The native engine accepts
// any APVTS parameter id (or block-power id); this list is the UI's curation
// (setup-domain params like calibration stay out; they describe the rig, not
// something you perform with).
#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

#include "model/MidiMapState.h"

namespace t3k::ui::midi {

// Drives the behaviour display: continuous knobs are absolute via CC,
// toggles flip on/off, triggers fire an action once per press.
enum class TargetKind { continuous, toggle, trigger };

struct MappableTarget {
  // APVTS parameter id, positional block power ("block1Power"), or a
  // virtual action id ("presetNext").
  juce::String id;
  juce::String name;
  // Section subtitle, mirroring the faceplate's layout.
  juce::String group;
  TargetKind kind;
};

// Block-power targets are positional ("Block 1" is a lane's first tone block
// whatever it currently holds), so a mapping survives tone swaps and preset
// loads, like switches on a pedalboard. Display-only cap (the engine takes
// up to 64): enough for any realistic pedalboard without burying the picker.
inline constexpr int kBlockPowerTargets = 12;

const std::vector<MappableTarget>& mappableTargets();
const MappableTarget* targetById(const juce::String& id);

// "block3Power" → {2, left}, "rightBlock3Power" → {2, right}; nullopt for
// anything else (mirrors the native parse).
struct BlockPowerTarget {
  int index;
  bool right;
};
std::optional<BlockPowerTarget> blockPowerTarget(const juce::String& targetId);

// 60 → "C4" (scientific pitch, middle C = C4).
juce::String noteName(int note);
// "CC 64" / "Note C2": the mapping row's source column.
juce::String sourceLabel(const MidiMapping& mapping);
// How the pairing behaves, mirroring the native derivation: trigger targets
// fire per press, toggle targets (and any note source) flip on/off,
// everything else tracks the CC value absolutely.
juce::String behaviorLabel(const MidiMapping& mapping);

}  // namespace t3k::ui::midi
