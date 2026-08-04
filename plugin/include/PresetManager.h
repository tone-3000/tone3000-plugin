#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

/**
 * On-disk internal preset store. Pure file layer: one XML file per preset,
 * no knowledge of what a preset contains (the processor builds/consumes the
 * ValueTree payloads). Message-thread only.
 *
 * Layout:
 *   <user data dir>/TONE3000/Presets/<uuid>.t3kpreset   (user presets)
 *   <user data dir>/TONE3000/Presets/Factory/…          (read-only factory)
 *
 * Ids are "user:<stem>" / "factory:<stem>" so the two namespaces can never
 * collide and the UI can tell them apart without extra lookups. Display
 * names live *inside* the file (filenames are uuids), so any characters are
 * fine and renames never touch the filesystem name.
 *
 * The list is rescanned on every call; it's a handful of stat()s, and it
 * keeps multiple plugin instances sharing the folder coherent for free.
 *
 * Ordering: factory presets always come before user presets (the browser's
 * two sections). Within each section a custom order can be set via move()
 * and persists in order.json beside the preset files; presets not in the
 * order file (new saves, first run) fall back to name order after the
 * ordered ones. List order is user-facing truth: the browser, prev/next
 * stepping and MIDI program-change numbers all follow it.
 */
class PresetManager {
public:
  struct Info {
    juce::String id;
    juce::String name;
    bool factory{false};
  };

  static constexpr const char* kFileExtension = ".t3kpreset";
  static constexpr const char* kPresetTag = "T3KPreset";

  PresetManager();

  /** Store presets under an explicit base directory (tests use a temp dir). */
  explicit PresetManager(const juce::File& baseDir);

  /** All presets, factory first, each section sorted by name. */
  std::vector<Info> list() const;

  /** Full preset tree for an id, or an invalid tree when missing/corrupt. */
  juce::ValueTree load(const juce::String& id) const;

  /** Store a preset under `name`. A user preset with the same name is
      overwritten in place (same id); that's the "update" path, since the
      save popover is the only write UI. Returns the resulting Info, or an
      empty-id Info on IO failure. */
  Info save(const juce::String& name, juce::ValueTree preset) const;

  /** Rename a user preset (rewrites the name inside the file). */
  bool rename(const juce::String& id, const juce::String& newName) const;

  /** Delete a user preset. Factory presets are refused. */
  bool remove(const juce::String& id) const;

  /** Move a preset one step up (delta < 0) or down (delta > 0) within its
      section; factory stays before user, so the browser's sections and the
      global order can't disagree. Persists the whole current order. */
  bool move(const juce::String& id, int delta) const;

private:
  juce::File fileForId(const juce::String& id) const;
  static juce::ValueTree readPresetFile(const juce::File& file);
  static bool writePresetFile(const juce::File& file, const juce::ValueTree& preset);

  juce::File orderFile() const;
  juce::StringArray readOrder() const;
  bool writeOrder(const juce::StringArray& ids) const;

  juce::File userDir;
  juce::File factoryDir;
};
