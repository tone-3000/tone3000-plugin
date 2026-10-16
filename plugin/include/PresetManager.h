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
 *
 * Library folders (Library.h). Presets can also live in folders outside the
 * flat list: subfolders of the user folder and the Library's own folders.
 * They have no place in the list (program changes and the preset browser
 * never see them) and are addressed by path, "file:<absolute path>", so a
 * preset loaded from a Library folder is still an ordinary active preset.
 * listFolder() lists one such folder for the Library and for folder-scoped
 * prev/next stepping.
 */
class PresetManager {
public:
  struct Info {
    juce::String id;
    juce::String name;
    bool factory{false};
  };

  // One preset in a folder listing: its Info plus the file behind it.
  struct FolderEntry {
    Info info;
    juce::File file;
  };

  static constexpr const char* kFileExtension = t3k::presetfile::kExtension;
  static constexpr const char* kPresetTag = t3k::presetfile::kTag;
  static constexpr const char* kFilePrefix = "file:";

  /** The path-addressed id of a preset file outside the list. */
  static juce::String fileId(const juce::File& file) { return kFilePrefix + file.getFullPathName(); }
  static bool isFileId(const juce::String& id) { return id.startsWith(kFilePrefix); }

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

  /** The presets directly inside `dir`. The user folder itself lists as
      list()'s user section (same "user:" ids, same custom order), so the
      Library's Presets folder and the preset browser agree on what is
      active; any other folder lists in natural name order ("2" before
      "10", so a numbered setlist reads in order) with "file:" ids. */
  std::vector<FolderEntry> listFolder(const juce::File& dir) const;

  /** The file behind any id ("user:", "factory:" or "file:"), or an
      invalid File when it is gone. */
  juce::File fileForId(const juce::String& id) const;

  /** Save into a Library folder, with the same rules as save(): a preset
      of the same name in `dir` is overwritten in place (keeping its id),
      otherwise a new file named after the preset. Returns the preset's
      "file:" id (or save()'s result when `dir` is the user folder). */
  Info saveInFolder(const juce::File& dir, const juce::String& name, juce::ValueTree preset) const;

  /** Rename a preset file outside the list: the name inside and the file
      name both change. Returns the moved file, or an invalid File. */
  juce::File renameFile(const juce::File& file, const juce::String& newName) const;

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
  struct Cached;
  /** One directory's presets, name-sorted. Ids/names come from `fileCache`
      when the file is unchanged. Caller holds cacheLock. */
  std::vector<Entry> scanDir(const juce::File& dir, const char* prefix, bool factory,
                             ScanState& state, std::map<juce::String, Cached>& fileCache) const;
  /** The merged, section-ordered list with file paths (list() minus paths). */
  std::vector<Entry> entries() const;
  /** Write `preset` for a user preset whose canonical filename is
      sanitizeStem(name): in place when `current` already has that stem,
      otherwise to a fresh unique file with `current` removed after the
      write succeeds (the lazy legacy migration). */
  static juce::File writeUserPreset(const juce::File& dir, const juce::File& current,
                                    const juce::String& name, const juce::ValueTree& preset);

  juce::File orderFile() const;
  juce::StringArray readOrder() const;
  bool writeOrder(const juce::StringArray& ids) const;

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
  // listFolder()'s files, kept apart so the list scan's pruning never drops
  // them (and the other way round). Pruned per folder as it is rescanned.
  mutable std::map<juce::String, Cached> folderCache;

  juce::File userDir;
  juce::File factoryDir;        // user-local Factory/ (user overrides; the Linux tarball installs here)
  juce::File systemFactoryDir;  // installer-shipped Factory/ (invalid when absent)
};
