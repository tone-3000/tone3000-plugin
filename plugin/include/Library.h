#pragma once
#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <set>

#include "PresetManager.h"

/**
 * The local Library (plugin/docs/library.md): a base folder holding one
 * folder per owner, yours and the libraries you imported from other
 * players. A library has two typed halves: Captures (single models: TONE3000
 * tone references, .t3ktone, and local .nam / .wav captures) and Presets
 * (whole rigs, .t3kpreset). Every folder takes its type from the half it is
 * in, lists only that kind, and refuses the other.
 *
 *   <root>/
 *     tonehound/            your library (the owner name is a UI pref)
 *       Captures/
 *         Clean/
 *           Plexi - Crunch.t3ktone
 *           my-amp.nam
 *       Local/
 *         NAM Captures ->   a linked folder (see below)
 *       Presets/  ->        the user presets folder, mounted (see below)
 *         Setlist/
 *           01. Best song.t3kpreset
 *     awesomeuser/          an imported library: read-only, replaced
 *                           wholesale when re-imported (a local "follow")
 *       Captures/  Presets/
 *
 * The user presets folder (PresetManager) is not moved or copied: your
 * library shows it as its "Presets" folder, so every preset you ever saved
 * is in the Library without a migration, and a subfolder made under it is
 * an ordinary Library folder. Its Factory/ subfolder (read-only shipped
 * presets) and order.json stay out of the tree.
 *
 * Linked folders are mounted the same way, into Local: any folder
 * elsewhere (a NAM capture collection the NAM app also uses) shows there
 * under its own name, nothing copied. Their contents are yours to edit like the
 * rest; the link itself is a UI pref, so unlinking never touches a file.
 *
 * Everything is plain files: the tree is the folder structure, names are
 * file names (a preset's display name lives inside it, as everywhere).
 * A scan caches each folder's listing by its modification time (and the
 * processor saves the whole listing for the next session), so files moved
 * in Finder/Explorer show up on the next scan of their folder. The class is
 * a pure file layer (no processor, no UI); scans run on a worker thread,
 * edits on the message thread.
 */
class LocalLibrary {
public:
  static constexpr const char* kToneExtension = ".t3ktone";
  static constexpr const char* kArchiveExtension = ".t3klibrary";
  static constexpr const char* kManifestName = "t3klibrary.json";
  static constexpr const char* kPresetsFolderName = "Presets";
  static constexpr const char* kCapturesFolderName = "Captures";
  // Your library's third fixed folder: local files that aren't your own
  // captures (a keep folder of others' captures, a collection you copied
  // in), organized however you like, with the linked folders listed in it.
  static constexpr const char* kLocalFolderName = "Local";
  // Folder types (folderType): a library's two halves, and "" for a folder
  // in neither (one an older layout or a hand-made import left at the top).
  static constexpr const char* kCapturesType = "captures";
  static constexpr const char* kPresetsType = "presets";
  static constexpr const char* kDefaultOwner = "My Library";
  // Scan guards for a root pointed somewhere huge (a whole drive). Real
  // collections run to tens of thousands of captures (a linked NAM archive
  // of 46k files was the case that set this), so the node cap is well past
  // that; the drawer only builds rows for what is on screen.
  static constexpr int kMaxDepth = 16;
  static constexpr int kMaxNodes = 250000;

  explicit LocalLibrary(const PresetManager& presetStore) : presets(presetStore) {}

  /** Where the Library lives: the base folder, your library's folder name
      in it (sanitized; empty falls back to kDefaultOwner), and the folders
      linked into your library (those failing canLink are dropped; missing
      ones stay listed, flagged, so they can be unlinked). */
  void setLocation(const juce::File& rootDir, const juce::String& ownerName,
                   const juce::Array<juce::File>& linkedDirs = {});
  juce::File rootDir() const { return root; }
  juce::File ownDir() const { return root.getChildFile(owner); }
  // Your library's id (made on first use, kept in a hidden file in it):
  // exports carry it, and an import is your own backup only when it
  // matches. "" when there is none and `create` is false.
  juce::String libraryId(bool create) const;
  juce::File presetsDir() const { return presets.userPresetsDir(); }
  juce::File capturesDir() const { return ownDir().getChildFile(kCapturesFolderName); }
  juce::File localDir() const { return ownDir().getChildFile(kLocalFolderName); }
  const juce::Array<juce::File>& linkedDirs() const { return links; }
  /** kCapturesType, kPresetsType or "": your Captures folder and links are
      captures, the presets folder presets; in another library, its
      Captures/ and Presets/ folders. */
  juce::String folderType(const juce::File& dir) const;
  /** `item` may go into `folder` by kind: captures and references into
      captures folders, presets into presets folders, a folder into one of
      its own type (or an untyped one anywhere). */
  bool accepts(const juce::File& folder, const juce::File& item) const;
  /** canWriteInto, and a presets (or untyped) folder: where a rig saves. */
  bool takesPresets(const juce::File& folder) const;
  /** Why `dir` can't be linked ("" = it can): it must be a folder outside
      the Library and the presets folder, holding neither, and not linked
      already (nested links would list the same files twice). */
  juce::String linkProblem(const juce::File& dir) const;
  /** Documents/TONE3000/Library (the app's Documents on iOS). */
  static juce::File defaultRoot();

  /** The whole tree, re-read from disk:
        { root, owner, libraries: [node] }
      node: { kind: "library" | "folder" | "preset" | "tone" | "capture",
              name, path, editable, removable,
              mine (library), mount (a fixed root: Captures, Presets, a link),
              type (folders: "captures" | "presets" | ""),
              linked, missing (a linked folder; missing = not found),
              id (preset: its preset id), ref (tone: the .t3ktone object),
              format (capture: "nam" | "ir"), children (library / folder) }
      Your library always comes first (even before its folder exists): its
      Captures, its Local folder (links last inside it), its Presets mount,
      then any untyped
      folders at its top. The others follow in natural name order.

      `useCache`: reuse the listing of every folder whose modification time
      hasn't moved since the last scan, so a rescan of a big linked
      collection (46k captures took ~2 s to list) costs a stat per folder.
      A folder's time moves when an entry in it is added, removed or
      renamed, and every write here goes through a rename (presets,
      references, order.json), so a listing is never stale; a file edited
      in place elsewhere is the one thing missed, which a full scan
      (useCache false: the drawer's Refresh) picks up. The cache is one per
      process, shared by every instance (and by the copies the processor
      scans off the message thread), and scans run one at a time: five
      instances in a project cost one scan of a big collection, not five
      racing over the same disk. A scan stops early once cancelScans() is
      called (the plugin going away mid-scan). */
  // `stop`: the caller's own way to cut it short (an editor closing), as
  // cancelScans does for the instance.
  juce::var scan(bool useCache = true, const std::atomic<bool>* stop = nullptr) const;
  /** The location a listing was made under (root, owner, presets folder,
      links): a listing for one key says nothing about another. The scan's
      result also carries `rescanned`, how many folders it had to list
      again (0: nothing changed since the last scan this process made). */
  juce::String locationKey() const;

  /** Contents of your library (and of the Presets mount, minus Factory).
      Your library's own folder is renamable but not editable here. */
  bool canEdit(const juce::File& item) const;
  /** A folder new items may be put into: your Captures, the Presets mount,
      a linked folder, or an editable folder. (Which items: accepts.) */
  bool canWriteInto(const juce::File& folder) const;
  /** canEdit, or a whole imported library (removing it = "unfollow"). */
  bool canRemove(const juce::File& item) const;

  // Edits. Each returns the resulting file (invalid on failure or refusal).
  juce::File createFolder(const juce::File& parent, const juce::String& name) const;
  /** Folders and non-preset items rename on disk; a preset also takes the
      new display name inside (PresetManager::renameFile). Your library's
      own folder renames too; the caller then moves the owner pref. */
  juce::File rename(const juce::File& item, const juce::String& newName) const;
  /** To the OS trash where there is one, deleted where there is none. */
  bool remove(const juce::File& item) const;
  /** Tests delete outright instead of filling the machine's trash. */
  void setUseTrash(bool use) { useTrash = use; }
  /** Stop a scan of this object (or a copy) in flight, and any later one. */
  void cancelScans() { cancelled->store(true); }
  juce::File move(const juce::File& item, const juce::File& folder) const;
  /** Copy into one of your folders (from anywhere in the Library: this is
      how items from an imported library become yours). A copied preset
      gets a fresh id, and its new name when the copy had to be renamed. */
  juce::File copy(const juce::File& item, const juce::File& folder) const;
  /** Write a .t3ktone for a catalog tone: `ref` is { tone: { id, title, …
      }, model: { id, name } }. An identical reference already in the
      folder is returned instead of duplicated. */
  juce::File addToneRef(const juce::File& folder, const juce::var& ref) const;
  /** Copy a local capture (.nam / .wav) in as `name` + its extension. */
  juce::File addCapture(const juce::File& folder, const juce::File& source,
                        const juce::String& name) const;

  /** Zip a folder, an item, or a whole library into a .t3klibrary archive
      (manifest + content; see library.md). The manifest records a folder's
      or item's type so an import lands it in the matching half. */
  bool exportArchive(const juce::File& item, const juce::File& archive) const;
  /** A .t3klibrary to share (library.md "Sharing"): no capture file goes
      out, only TONE3000 links. `siteRefs` maps a capture's path to the
      TONE3000 tone it is from ({ tone, model }); those become one .t3ktone
      per tone per folder, every other capture stays home. Presets go with
      each block that plays a capture not in `siteRefs` emptied (its model
      bytes left out). References go as they are; your whole library brings
      its linked folders along (as links) under Local. No library id, no
      kept links. Returns { links, leftOut, presets, emptied } (counts), or
      void when it couldn't be written. */
  juce::var shareArchive(const juce::File& item, const juce::File& archive, const juce::var& siteRefs) const;
  /** A preset made fit to share, in place: a block of a TONE3000 tone keeps
      the bytes of its tone's models only; a block of local captures keeps
      its bytes only when every one of them is a capture downloaded from
      TONE3000 (its content hash in `trusted`), and goes without the paths
      it was loaded from; every other block becomes an empty slot, its model
      bytes gone. How many were emptied. */
  static int emptyUnsharedBlocks(juce::ValueTree& preset, const std::set<juce::String>& trusted);
  /** Copy a folder from anywhere (an old capture collection) into one of
      your captures folders, subfolders kept, as a new folder named after it.
      Only captures and references come along; trainer leftovers, notes and
      everything else stay behind, as do folders left empty by that. The
      copy-instead-of-link alternative to linkedDirs. Returns the new folder,
      invalid when nothing was copied (or `into` lies inside `source`). */
  juce::File importFolder(const juce::File& source, const juce::File& into) const;
  /** Unpack an archive under its owner's name. A whole library from
      someone else replaces their folder (re-importing is how a followed
      library updates); your own library merges back in (restoring a
      backup); a folder or item lands beside what is there. Returns the
      imported folder or item. */
  juce::File importArchive(const juce::File& archive) const;

  /** The parsed .t3ktone object, or a void var when the file isn't one. */
  static juce::var readToneRef(const juce::File& file);
  /** "folder", "preset", "tone", "capture", or "" for anything else. */
  static juce::String kindOf(const juce::File& file);

private:
  struct ScanBudget {
    int nodes{0};
    int rescanned{0};  // folders listed from disk (not the cache), or gone
    bool useCache{true};
    const std::atomic<bool>* cancelled{nullptr};
    const std::atomic<bool>* stop{nullptr};  // the caller's (scan's `stop`)
    bool stopped() const {
      return (cancelled != nullptr && cancelled->load()) || (stop != nullptr && stop->load());
    }
    std::set<juce::String> seen;  // folders listed this scan (the rest are pruned)
  };
  // One folder's listing: subfolders and item nodes, in display order.
  struct ScanCache {
    struct Dir {
      juce::int64 modified{0};
      juce::Array<juce::File> folders;
      juce::Array<juce::var> items;
      int notA2{0};  // folders left out as named for A1 (nam_arch)
    };
    juce::CriticalSection lock;
    juce::String key;  // the location the listings were made under
    std::map<juce::String, Dir> dirs;
  };
  static std::shared_ptr<ScanCache> processScanCache();
  std::shared_ptr<ScanCache> scanCache = processScanCache();
  std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);
  juce::var libraryNode(const juce::File& dir, bool mine, ScanBudget& budget) const;
  juce::var folderNode(const juce::File& dir, const juce::String& name, bool mount, int depth,
                       ScanBudget& budget) const;
  // `notA2`: how many folders it left out as named for A1 (or emptied by
  // that), so a folder emptied that way goes too.
  juce::Array<juce::var> children(const juce::File& dir, int depth, ScanBudget& budget, int* notA2 = nullptr) const;
  bool hidden(const juce::File& file) const;
  bool isLinkRoot(const juce::File& dir) const;
  bool insideLink(const juce::File& item) const;
  /** An unused `dir/<stem><ext>`, " 2", " 3", … appended on a clash. */
  static juce::File uniqueChild(const juce::File& dir, const juce::String& stem,
                                const juce::String& ext, const juce::File& self = {});
  /** Move `src`'s contents into `dst`: folders merge, identical files are
      skipped, clashing ones are renamed. */
  static bool mergeInto(const juce::File& src, const juce::File& dst);
  static bool moveAcross(const juce::File& from, const juce::File& to);
  /** The library (a folder in `root`) `file` is or is in; none outside. */
  juce::File libraryOf(const juce::File& file) const;
  /** The library state (LibraryState.h) of what an archive holds, keyed
      relative to its content, and the pictures it names; `whole`: a whole
      library (yours also brings its keep folder, order and links). */
  void exportState(juce::ZipFile::Builder& zip, const juce::File& item, bool whole) const;
  /** Merged into `library`'s state, each key where `landed` says its file
      went; what the library already says about a file stays. */
  void importState(juce::ZipFile& zip, const juce::var& state, const juce::File& library,
                   const std::function<juce::File(const juce::String&)>& landed, bool whole) const;

  const PresetManager& presets;
  juce::File root;
  juce::String owner{kDefaultOwner};
  juce::Array<juce::File> links;
  bool useTrash{true};
};
