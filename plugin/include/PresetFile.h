// The .t3kpreset on-disk format and the filename rules, shared by
// PresetManager (the plugin's store) and tools/preset_tool.cpp (the
// maintainer tool that regenerates the shipped factory presets). Depends on
// juce_core + juce_data_structures only, so the tool needs neither the
// processor nor NAM.
//
// Two framings are read, one is written:
//
//   v2 (written):  "T3KH" | int32 LE headerBytes | header ValueTree | body ValueTree
//   v1 (legacy):   "T3KB" | body ValueTree
//
// The header is a tiny ValueTree ("T3KPresetHeader": id, name) so listing a
// preset folder reads a few hundred bytes per file instead of deserializing
// the megabytes of model bytes each body embeds. The body is the complete
// preset (it repeats id and name), so a reader that only knows the body
// still has everything, and load() is unchanged apart from skipping the
// header. Legacy files fall back to a full parse for their header fields;
// PresetManager rewrites them as v2 on their next save/rename.
#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

namespace t3k::presetfile {

constexpr const char* kExtension = ".t3kpreset";
constexpr const char* kTag = "T3KPreset";
constexpr const char* kHeaderTag = "T3KPresetHeader";

struct Header {
  bool valid{false};
  bool legacy{false};  // v1 framing: fields came from a full parse
  juce::String id;     // may be empty (legacy file, or written without one)
  juce::String name;   // may be empty
};

/** Identity fields only. v2: header bytes only. v1: full parse. */
Header readHeader(const juce::File& file);

/** The full preset tree, or an invalid tree when missing/corrupt. */
juce::ValueTree read(const juce::File& file);

/** Write `preset` in the v2 framing (header from its "id" / "name"
    properties), write-then-rename so a failure never clobbers the target. */
bool write(const juce::File& file, const juce::ValueTree& preset);

/** The filename stem a display name maps to, valid on every platform we
    ship to (the union of their rules, so a preset saved on one is still
    valid when copied to another): the characters any of them forbids
    (`/ \ : * ? " < > |`, controls) become "-" (runs collapsed), leading and
    trailing dots/spaces go (Windows strips them itself, which would desync
    us), a leading "." or "~" goes (hidden / lock-file conventions), Windows'
    reserved device names (CON, NUL, COM1…) get a "_" prefix, and the result
    is capped at kMaxStemBytes of UTF-8 on a code-point boundary (filesystems
    limit names in bytes). Never empty: "Preset" as the floor. Lossy by
    design; the true name lives inside the file. */
juce::String sanitizeStem(const juce::String& name);
constexpr int kMaxStemBytes = 100;

/** `dir/<stem>.t3kpreset`, or with " 2", " 3", … appended until the name is
    free. `self` (a file being renamed/rewritten) never counts as taken, so a
    case-only rename resolves to itself. Existence is asked of the
    filesystem, so case-(in)sensitivity follows the volume. */
juce::File uniqueFile(const juce::File& dir, const juce::String& stem,
                      const juce::File& self = {});

}  // namespace t3k::presetfile
