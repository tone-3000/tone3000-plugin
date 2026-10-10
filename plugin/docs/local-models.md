# Local models: drop / picker loading

A local `.nam` file, an IR `.wav`, or a folder of them loads as a local
block, no browser or account involved: dropped on a tile, or picked via the
tile context menus' **Load File / Load Folder** (a native OS file dialog).
The design goal is that local files are a second-class *entry point*, not a
second-class block: once in, they ride the exact catalog pipeline
(background load, in-memory model cache, undo, duplication, presets, DAW
state), and the code branches on "local" in only a handful of places.

Entry points: both the drop and the picker land in
`plugin/ui/services/LocalFiles` (`drop` / `pick`), which hands a path to
`TONE3000Processor::loadLocalTonePath` in
`plugin/src/ProcessorModelLoader.cpp` (on iOS, security-scoped URLs to
`loadLocalToneUrls`; see `docs/ios.md`). Behavior is pinned by
`test/src/local_load_tests.cpp`, which drives the byte-array sibling
`loadLocalTone` ({ name, base64 data } entries: the shape the UI used before
it had file paths, kept because the tests feed files that way).

## Drop and picker

Dropping a file or folder onto a tile (`juce::FileDragAndDropTarget` on the
gallery tiles) and right-clicking a tile for **Load File** / **Load Folder**
(a native `juce::FileChooser`) are the same route with two front doors: an
insert slot adds, a tone tile swaps in place.

`loadLocalTonePath` reads the bytes straight from disk. A folder is walked
recursively; the majority extension decides NAM vs IR, the folder name
becomes the tone title, and each file becomes one model named after it
(300 files / 50 MB each max, matching the catalog's per-tone model limit,
in natural name order). A single file must be `.nam` or `.wav` and is titled
from its name.

Each file is validated at load time (`.nam` must parse and pass the A2
shape check, `.wav` must open as real audio) so a bad file is a toast, never
a retry badge. Survivors are wrapped in a synthetic tone JSON: `id: 0`,
`local: true`, and each model's `model_url` a `file://` URL. On a desktop a
file read from disk (a drop, the file picker, the Library) is played **in
place**: its URL is the file itself, and nothing is copied. Bytes that arrive
without a file to come back to (the base64 route, the iOS document picker,
whose files are only readable once) are stashed (below) and the URL points
at the stash copy. From there `loadTone` takes over, and
`fetchModelFromUrl` resolves `file://` URLs from disk instead of the
network.

While a file is open for validation, its gear is inferred best-effort so a
lane of local tiles isn't a row of identical file glyphs: a `.nam`'s
`metadata.gear_type` (free text written by the trainer; only the common
spellings map, e.g. `amp` / `pedal_amp` / `preamp` → `amp`, `amp_cab` →
`amp-cab`, `studio` → `outboard`) and, for an IR, cab length (at or under
the short/long cutoff → `cab`). The result is the same catalog `gear` id a
TONE3000 tone carries, set on the tone from its first file; when nothing
matches, `gear` is simply absent and the tile keeps the generic file glyph
(`ToneImage`).

The picker isn't sugar: it covers users who never think to drag-drop, works
signed out, and is the only route on iOS (the Files app has no drag into
the plugin). Drops work on every desktop platform, Linux included; the old
web UI never received them there because XDnD died at the embedded
WebKitGTK view
([issue #22](https://github.com/tone-3000/tone3000-plugin/issues/22)), one
of the reasons the UI is native.

## One stored model list, one exception

Catalog tones store only the *active* model natively; the picker pages the
full catalog from the API. Local tones have no API, so their model list is
the dropped files and stays whole in the stored tone JSON:
`parseToneForLoading` and `switchModel` skip their pruning for local tones,
and the tone summary ships each local model's `model_url` so the picker can
drive switches (which also work signed out; nothing downloads).

## In place, and the stash

A file played in place is read from where it is for every load and switch;
the user's collection isn't duplicated into app data (a folder of 300
captures, or a linked collection in the Library, used to leave a copy of
every file it played). Its dates are never touched. Presets and DAW state
embed the model bytes as always, so a project still reopens when the file
has moved. A rename or move made in the Library takes the blocks along: a
block playing a file from under it is re-pointed (its `model_url`,
`source_path` and a picture under it, `relinkLocalFiles`), and the move is
remembered for the process, so a cache-lost reload of the old path (undo
after a remove, retry) finds the file where it went
(`noteLocalFilesMoved`, followed by `resolveLocalModelFile`). A file that
has gone otherwise (moved outside the plugin, a project saved before a
rename) is looked for in the Library by its name, and a file found there
counts only when its bytes give the model's id (`localModelId`, the hash
every local model's id comes from); the block is re-pointed to it
(`LibraryStore::findMovedFiles`). Anything not found that way (deleted, or
outside the Library and its linked folders) is reported missing, like any
missing file.

`<app-data>/TONE3000/LocalModels/<content-hash>-<size>.<ext>` holds the rest:
the local equivalent of "the server", the copy that cache-lost reloads
re-fetch from. Content-addressed names dedupe re-drops.

Its lifecycle is self-maintaining:

- **Liveness is mtime.** Every use re-stamps the file: stash writes, reads
  in `fetchModelFromUrl`, and cache-hit loads via `refreshLocalStashCopy`.
- **GC.** `cleanLocalModelStash` (once per process, off-thread) deletes
  stash files unused for a week. Clearing on startup would be wrong:
  instances in other processes may still hold undo history that references
  the files by path.
- **The name is the address.** A block's `model_url` persists the stash
  path absolutely, in presets, DAW/app state and undo snapshots. Reads go
  back through `resolveLocalModelFile`, which falls back to the same file
  name under the *current* stash root when the stored path is gone (a stash
  copy's name only, `isStashFileName`: a file played in place that has gone
  is never swapped for a same-named stash file). A path
  this machine wrote always still exists, so desktop loads are untouched; the
  one desktop case it changes is a preset or state carrying a stash URL from
  another machine, which now re-stashes locally from the embedded bytes
  instead of reading the embedded cache alone. It is what keeps iOS working:
  the app data container's UUID rotates on every reinstall or app update.
- **Self-healing.** Presets and DAW state embed the model bytes
  (`ModelCache`), so they reopen without the stash, on any machine. When
  such a load hits the embedded cache and the stash copy is missing (GC'd,
  or a different machine), `refreshLocalStashCopy` writes it back, so undo
  and retry keep working there too.
- **A writable root, even after damage.** The app-data folder can exist
  without being writable: a sudo'd run of an older `install-plugin.sh`
  (it wrote the user Factory folder, and macOS sudo keeps `$HOME`) or a
  restored backup leaves it root-owned, and every stash write then failed as
  "Couldn't store the dropped file" while reads kept working
  ([issue #76](https://github.com/tone-3000/tone3000-plugin/issues/76)).
  `ensureWritableDir` (constructor, once per process, plus the stash and
  preset write paths) puts the write bits back in place when the user still
  owns the folder; otherwise it renames the folder aside to an `.unwritable`
  sibling (the parent belongs to the user even when the folder doesn't) and
  recreates it fresh. Nothing is deleted, and the log names what it did.
