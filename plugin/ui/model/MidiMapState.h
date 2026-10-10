// MIDI mapping engine snapshot (port of midiMap.ts).
#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace t3k::ui {

enum class MidiSource { cc, note };

struct MidiMapping {
  juce::String targetId;
  MidiSource source = MidiSource::cc;
  int number = 0;
};

struct MidiMapState {
  // 0 = omni, 1-16 = that channel only.
  int channel = 0;
  // Target armed for learn ("" when idle).
  juce::String learnTargetId;
  std::vector<MidiMapping> mappings;

  static MidiMapState parse(const juce::var& v);
  const MidiMapping* mappingFor(const juce::String& targetId) const;
};

}  // namespace t3k::ui
