#pragma once
#include <juce_core/juce_core.h>

/**
 * On-disk library of local tones: the user's own folder tree of `.nam`
 * captures and IR `.wav` files, kept next to the presets so favourites live
 * somewhere stable instead of being re-fetched from tone3000.com every
 * session.
 *
 * Layout (the user owns it; the plugin never reorganizes it):
 *   <user data dir>/TONE3000/Library/<their folders>/<file>.nam|.wav
 *
 * Pure file layer, like PresetManager: it lists, creates, renames, copies
 * and removes, and knows nothing about tones, blocks or model validation
 * (the processor validates bytes before asking for a write; see
 * TONE3000Processor::validateLocalModelBytes). Loading is not its job
 * either: a picked entry's *path* goes to loadLocalTonePath, so library
 * files ride the exact same pipeline as a dropped file. Message-thread
 * only.
 *
 * Paths in and out of this class are `/`-separated and relative to the
 * library root ("" is the root itself), never absolute: they cross the
 * webview bridge, so `resolve` refuses anything that would escape the root
 * (absolute paths, `..`, symlink-free string trickery) and every entry
 * point goes through it.
 *
 * A folder is a first-class item, not just grouping: loading one loads its
 * files as a single multi-model block (the same rule as dropping a folder),
 * so "Fender Twin/" with eight captures becomes one tile with eight
 * switchable models.
 */
class ToneLibrary {
public:
  /** The default root under the shared TONE3000 app-data folder. */
  ToneLibrary();

  /** Store the library under an explicit root (tests use a temp dir). */
  explicit ToneLibrary(const juce::File& root);

  /** The library root; created on demand by the mutating calls. */
  juce::File rootDir() const { return root; }

  /** Absolute file for a root-relative path, or an invalid File when the
      path escapes the library (absolute, `..`, empty segment). */
  juce::File resolve(const juce::String& relativePath) const;

  /** Root-relative path for a file inside the library ("" for the root
      itself), or an empty string when it isn't in the library. */
  juce::String relativePathOf(const juce::File& file) const;

  /** One folder's contents, as the UI's browser renders it:
        { path, name, parent, loadable,
          folders: [{ name, path, models }],
          models:  [{ name, path, kind, size, modified }] }
      `kind` is "nam" or "ir"; a folder's `models` is every loadable file
      under it (recursively), and the listing's own `loadable` is what
      loading *this* folder as one block would add — its tree's majority
      extension, which is the number the menu's "Load all" row shows. Both
      counts stop at 1000. Everything else in the folder is ignored, not
      hidden: the user's own README or sample audio just doesn't show up.
      Returns { error } when the path escapes the library or isn't a
      folder. */
  juce::var list(const juce::String& relativePath) const;

  /** Create a subfolder. `unique` suffixes " (2)" rather than failing on a
      name that is taken, for the importer (which doesn't get to pick).
      Returns { path } or { error }. */
  juce::var createFolder(const juce::String& parentPath, const juce::String& name,
                         bool unique = false) const;

  /** Rename a file or folder in place, keeping a file's extension. Returns
      { path } or { error }. */
  juce::var rename(const juce::String& relativePath, const juce::String& newName) const;

  /** Move a file or folder into `destFolderPath` ("" is the root), keeping
      its name and uniquing it with " (2)" when the destination already has
      one. Moving to where the item already is changes nothing. Refuses the
      root, a missing destination, a destination that isn't a folder, and a
      folder into itself or its own subtree. Returns { path } or { error }. */
  juce::var move(const juce::String& relativePath, const juce::String& destFolderPath) const;

  /** Remove a file or a whole folder. Goes to the OS trash when it can
      (this is the user's own data), deletes otherwise. */
  bool remove(const juce::String& relativePath) const;

  /** Copy a file, or a folder of files, into `folderPath`. Only `.nam` and
      `.wav` files are copied; folder structure is preserved. Returns
      { path, copied } or { error }. */
  juce::var importFrom(const juce::String& folderPath, const juce::File& source) const;

  /** Write already-validated bytes into `folderPath` under `name`,
      uniquing the name against what's there. `name` may carry a relative
      subpath ("Marshall/JCM800.nam"), whose folders are created as needed,
      so a dropped folder keeps its shape. Returns { path, name } or
      { error }. */
  juce::var write(const juce::String& folderPath, const juce::String& name, const void* data,
                  size_t size) const;

  /** True for the two extensions the plugin can load. */
  static bool isModelFile(const juce::File& file);

  /** "nam" for a `.nam` capture, "ir" for a `.wav`. */
  static juce::String kindOf(const juce::File& file);

  /** `name` reduced to something safe to put on disk, or "" when nothing
      usable is left. */
  static juce::String sanitizeName(const juce::String& name);

  /** `<base><extension>` inside `dir`, suffixed " (2)", " (3)", … until it
      names something that doesn't exist yet. An invalid File when the
      suffixes run out: callers overwrite whatever they are handed, so a
      taken name must never be returned. */
  static juce::File uniqueChild(const juce::File& dir, const juce::String& base,
                                const juce::String& extension);

private:
  juce::File root;
};
