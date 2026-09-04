# Local models: drop / picker loading

A local `.nam` file, an IR `.wav`, or a folder of them loads as a local
block, no browser or account involved: dropped on a tile, or picked via the
tile context menus' **Load File / Load Folder** (a native OS file dialog).
The design goal is that local files are a second-class *entry point*, not a
second-class block: once in, they ride the exact catalog pipeline
(background load, in-memory model cache, undo, duplication, presets, DAW
state), and the code branches on "local" in only a handful of places.

Entry points: `handleDropFile` in `ui/src/hooks/useToneLoadFlow.ts` →
`loadLocalTone` in `plugin/src/ProcessorModelLoader.cpp` (drops), and the
tiles' menus → `pickLocalToneFile` on the editor → `loadLocalTonePath`
(picker). Behavior is pinned by `test/src/local_load_tests.cpp`.

## The drop

The stock OS webviews never expose file paths to the DOM (no Electron-style
`webUtils.getPathForFile`), so the UI reads the dropped bytes and ships them
over the bridge as base64, one `{ name, data }` entry per file. Folders are
walked recursively; the majority extension decides NAM vs IR, the folder
name becomes the tone title, and each file becomes one model named after it
(300 max, matching the catalog's per-tone model limit).

Native validates each file at drop time (`.nam` must parse and pass the A2
shape check, `.wav` must open as real audio) so a bad file is a toast, never
a retry badge. Survivors are stashed (below) and wrapped in a synthetic tone
JSON: `id: 0`, `local: true`, and each model's `model_url` pointing at its
stash copy with a `file://` URL. From there `loadTone` takes over, and
`fetchModelFromUrl` resolves `file://` URLs from disk instead of the
network.

## The picker

Right-clicking a tile offers **Load File** / **Load Folder**: a native
`juce::FileChooser` on the editor (`pickLocalToneFile`), whose pick feeds
`loadLocalTonePath`, the path-based sibling of `loadLocalTone` that reads
bytes straight from disk (no base64 bridge trip) and then converges on the
same validate/stash/load pipeline. The folder rules the UI implements for
drops (majority extension, 300-file / 50 MB caps, natural name order, title
from the folder name) live natively in `loadLocalTonePath` for this flow.
An insert slot adds and a tone tile swaps in place, the same targeting as a
drop.

The picker isn't sugar: it's the local-load route that works everywhere.
Linux never delivers OS file drags to the embedded WebKitGTK view (XDnD
dies at the embedded `GtkPlug`, below the DOM, so no drop event ever fires;
[issue #22](https://github.com/tone-3000/tone3000-plugin/issues/22)), so on
that platform the menu is how local files get in at all. It also covers
users who never think to drag-drop, and works signed out.

## One stored model list, one exception

Catalog tones store only the *active* model natively; the picker pages the
full catalog from the API. Local tones have no API, so their model list is
the dropped files and stays whole in the stored tone JSON:
`parseToneForLoading` and `switchModel` skip their pruning for local tones,
and the tone summary ships each local model's `model_url` so the picker can
drive switches (which also work signed out; nothing downloads).

## The stash

`<app-data>/TONE3000/LocalModels/<content-hash>-<size>.<ext>` is the local
equivalent of "the server": the copy that cache-lost reloads (undo after a
remove, retry) re-fetch from, stable even after the user's original file
moves. Content-addressed names dedupe re-drops.

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
  name under the *current* stash root when the stored path is gone. A path
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
