#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <map>
#include <set>
#include <vector>

#include "PresetFile.h"

/**
 * On-disk internal preset store. Pure file layer: one preset per file (the
 * framing lives in PresetFile.h), no knowledge of what a preset contains
 * (the processor builds/consumes the payloads). Message-thread only for
 * writes.
 *
 * Layout:
 *   <user data dir>/TONE3000/Presets/<Name>.t3kpreset    (user presets)
 *   <user data dir>/TONE3000/Presets/Factory/…           (read-only factory)
 *   <system data dir>/TONE3000/Presets/Factory/…         (installer-shipped)
 *
 * The system Factory folder is where installers drop shipped presets
 * (macOS /Library/Application Support, Windows ProgramData); on iOS there is
 * no installer, so it is FactoryPresets inside the app bundle. Both Factory
 * dirs are scanned; a user-Factory file with the same id wins so local
 * overrides of a shipped preset are possible.
 *
 * Identity vs. filename. A preset's identity is the "id" property inside
 * the file (a uuid minted on first save); the filename is a *view* of its
 * display name, sanitized so the same file is valid on macOS, Windows,
 * Linux and iOS (presetfile::sanitizeStem), with " 2", " 3", … appended on
 * a collision. Ids are exposed as "user:<id>" / "factory:<id>" so the two
 * namespaces can never collide and the UI can tell them apart without extra
 * lookups. Because identity lives inside, renames (which rename the file)
 * and users shuffling files in Finder/Explorer never break the ids stored in
 * order.json or as activePresetId inside DAW projects.
 *
 * Legacy files (pre-readable-names) are named <uuid>.t3kpreset and carry no
 * "id"; their id is the filename stem, which is the same uuid, so every id
 * ever handed out keeps resolving. Migration is lazy: the next save-over or
 * rename of such a preset writes the stem in as its "id", rewrites it in
 * the v2 framing and renames the file to its display name. Files with no
 * "id" and a readable stem (a user renamed one by hand) work the same way.
 *
 * Cost model. list() rescans the directories on every call so multiple
 * plugin instances sharing the folder stay coherent for free. A rescan is
 * a directory listing plus one stat() per file: id and name are cached per
 * file, keyed on (mtime, size), and a changed/new file costs one *header*
 * read (a few hundred bytes; PresetFile.h) rather than deserializing the
 * megabytes of model bytes it embeds. Only legacy v1 files need a full
 * parse, once. This matters because hosts drive list() at editor open
 * (Reaper asks for all 128 program names every time, GitHub issue #169),
 * and the cache is per plugin instance, so every instance's first call in
 * a session is cold.
 *
 * Ordering: user presets always come before factory presets (the browser's
 * two sections; a player's own presets own the low MIDI program-change
 * numbers). Within each section a custom order can be set via move() and
 * persists in order.json beside the preset files; presets not in the order
 * file (new saves, first run) fall back to name order after the ordered
 * ones. List order is user-facing truth: the browser, prev/next stepping
 * and MIDI program-change numbers all follow it.
 */
class PresetManager {
public:
  struct Info {
    juce::String id;
    juce::String name;
    juce::String category;  // user category, "" = root; always an existing category
    bool favorite{false};
    bool factory{false};
  };

  static constexpr const char* kFileExtension = t3k::presetfile::kExtension;
  static constexpr const char* kPresetTag = t3k::presetfile::kTag;
  static constexpr int kMaxCategoryNameLength = 50;

  PresetManager();

  /** Store presets under an explicit base directory (tests use a temp dir).
      `systemFactory` stands in for the installer-shipped Factory dir; the
      default keeps temp stores isolated from presets installed on the
      machine. */
  explicit PresetManager(const juce::File& baseDir, const juce::File& systemFactory = {});

  /** Copies re-root at the same directories with an empty cache (the lock is
      per instance). Exists for setPresetStoreForTesting. */
  PresetManager(const PresetManager& other);
  PresetManager& operator=(const PresetManager& other);

  /** The folder user presets are saved to (may not exist yet on a fresh
      install; created on first save). Factory presets live elsewhere. */
  juce::File userPresetsDir() const { return userDir; }

  /** All presets, user first then factory, each section sorted by name. */
  std::vector<Info> list() const;

  /** Full preset tree for an id, or an invalid tree when missing/corrupt. */
  juce::ValueTree load(const juce::String& id) const;

  /** Store a preset under `name`. A user preset with the same name is
      overwritten (same id); that's the "update" path, since the save
      popover is the only write UI. Returns the resulting Info, or an
      empty-id Info on IO failure. */
  Info save(const juce::String& name, juce::ValueTree preset) const;

  /** Rename a user preset: rewrites the name inside the file and renames
      the file to match. The id is unchanged. */
  bool rename(const juce::String& id, const juce::String& newName) const;

  /** Delete a user preset. Factory presets are refused. */
  bool remove(const juce::String& id) const;

  /** Move a preset by `delta` steps within its section (negative = earlier).
      Clamped to the factory/user boundary so the browser's sections and the
      global order can't disagree. Persists the whole current order. */
  bool move(const juce::String& id, int delta) const;

  /** User categories, sorted alphabetically (case-insensitive). */
  juce::StringArray listCategories() const;

  /** Add a category (trimmed, non-empty, <= kMaxCategoryNameLength chars,
      case-insensitively unique). */
  bool addCategory(const juce::String& name) const;

  /** Delete a category. Its presets fall back to the root; none are deleted. */
  bool deleteCategory(const juce::String& name) const;

  /** File user presets under an existing category ("" = root). Refuses
      factory presets, unknown ids and categories that don't exist. */
  bool setPresetCategory(const juce::String& id, const juce::String& category) const;
  bool movePresetsToCategory(const juce::StringArray& ids, const juce::String& category) const;

  /** Star/unstar presets (factory presets included). All-or-nothing: false
      when any id is unknown or the write fails. */
  bool setPresetFavorite(const juce::String& id, bool isFavorite) const;
  bool setPresetsFavorite(const juce::StringArray& ids, bool isFavorite) const;

  /** Copy a user preset as "Copy-<name>" (" 2", " 3", … on a clash) with a
      fresh id, in the same category, unstarred. Empty-id Info on failure. */
  Info duplicatePreset(const juce::String& id) const;
  std::vector<Info> duplicatePresets(const juce::StringArray& ids) const;

  /** Delete user presets; returns the ids actually removed. */
  juce::StringArray removePresets(const juce::StringArray& ids) const;

  /** presetfile::sanitizeStem, kept here for callers/tests of the store. */
  static juce::String sanitizeFileStem(const juce::String& name) {
    return t3k::presetfile::sanitizeStem(name);
  }
  static constexpr int kMaxStemBytes = t3k::presetfile::kMaxStemBytes;

private:
  // One scanned file: its public Info plus where it lives.
  struct Entry {
    Info info;
    juce::File file;
  };

  static juce::File defaultSystemFactoryDir();

  // Bookkeeping across the per-directory scans of one list() call: which
  // files exist (to prune the cache) and how many were actually read.
  struct ScanState {
    std::set<juce::String> seen;
    int parsed{0};
  };
  /** One directory's presets, name-sorted. Ids/names come from the cache
      when the file is unchanged. Caller holds cacheLock. */
  std::vector<Entry> scanDir(const juce::File& dir, const char* prefix, bool factory,
                             ScanState& state) const;
  /** The merged, section-ordered list with file paths (list() minus paths). */
  std::vector<Entry> entries() const;
  /** The file behind an id, or an invalid File. User Factory beats system
      Factory for the same factory id. */
  juce::File fileForId(const juce::String& id) const;
  /** Write `preset` for a user preset whose canonical filename is
      sanitizeStem(name): in place when `current` already has that stem,
      otherwise to a fresh unique file with `current` removed after the
      write succeeds (the lazy legacy migration). */
  static juce::File writeUserPreset(const juce::File& dir, const juce::File& current,
                                    const juce::String& name, const juce::ValueTree& preset);

  juce::File orderFile() const;
  juce::StringArray readOrder() const;
  bool writeOrder(const juce::StringArray& ids) const;

  /** Category/favourite bookkeeping, one side file (presets-meta.json) beside
      the preset files. Keyed by preset id, so it survives renames and never
      touches the preset files (which keeps list()'s header-only scan cheap). */
  struct Meta {
    juce::StringArray categories;                // sorted, case-insensitive
    std::map<juce::String, juce::String> filed;  // user preset id -> category
    juce::StringArray favorites;                 // preset ids
  };
  juce::File metaFile() const;
  Meta readMeta() const;
  bool writeMeta(const Meta& meta) const;
  static int indexOfCategory(const juce::StringArray& categories, const juce::String& name);

  // Per-file cache behind list(); see the class comment. `valid` is false
  // for files that failed to parse, so a corrupt file costs one read, not
  // one per call. Entries for files that vanished are dropped on the next
  // scan. Locked because hosts may call the program API off the message
  // thread.
  struct Cached {
    juce::int64 modificationMs{0};
    juce::int64 size{0};
    bool valid{false};
    juce::String rawId;  // "id" from the file, or the stem when it has none
    juce::String name;
  };
  mutable juce::CriticalSection cacheLock;
  mutable std::map<juce::String, Cached> cache;  // full path -> entry

  juce::File userDir;
  juce::File factoryDir;        // user-local Factory/ (user overrides; the Linux tarball installs here)
  juce::File systemFactoryDir;  // installer-shipped Factory/ (invalid when absent)
};
