#include "MidiMapState.h"

#include "VarReader.h"

namespace t3k::ui {

using namespace var_reader;

MidiMapState MidiMapState::parse(const juce::var& v) {
  MidiMapState s;
  s.channel = integer(v, "channel");
  s.learnTargetId = str(v, "learnTargetId");
  s.mappings = list<MidiMapping>(v, "mappings", [](const juce::var& m) {
    return MidiMapping{str(m, "targetId"),
                       str(m, "source", "cc") == "note" ? MidiSource::note : MidiSource::cc,
                       integer(m, "number")};
  });
  return s;
}

const MidiMapping* MidiMapState::mappingFor(const juce::String& targetId) const {
  for (const auto& m : mappings)
    if (m.targetId == targetId)
      return &m;
  return nullptr;
}

}  // namespace t3k::ui
