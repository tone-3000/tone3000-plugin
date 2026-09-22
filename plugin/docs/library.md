# The tone library: keeping the tones you actually use

`<app-data>/TONE3000/Library/` is the user's own folder tree of local
tones — `.nam` captures and IR `.wav` files, in whatever folders they make
— and the tone browser's first tab is a view of it. The point is that the
tones a player uses every day stop living on tone3000.com: file a tone
once and every later session loads it from disk, instantly, signed out,
offline.

It is deliberately a *store*, not a second kind of tone. A picked entry's
path goes to `loadLocalTonePath`, the same call a Load File pick makes, so
what lands in the chain is an ordinary local block (background load, model
cache, undo, duplication, presets, DAW state — all unchanged, see
[`local-models.md`](local-models.md)). Nothing downstream knows the tone
came from the library.

## The pieces

- `ToneLibrary` (`plugin/include/ToneLibrary.h`) is the file layer: list,
  create, rename, move, copy, remove, and the path resolution everything goes
  through. Pure files, like `PresetManager`; it knows nothing about tones,
  blocks or validation. Its root is swappable (`setLibraryRoot`), which is
  how the tests stay out of the real library and where a
  settings-configurable location would plug in.
- `ProcessorLibrary.cpp` is the half that needs the chain or the
  local-load pipeline: loading an entry, filing a live block, importing
  dropped bytes.
- `EditorWebViewSetup.cpp` exposes both over the bridge; the UI side is
  `useLibrary` + `LibraryBrowser`, rendered as the browser's Library tab.

## Paths

Every path that crosses the bridge is root-relative, `/`-separated, with
`""` for the root. `ToneLibrary::resolve` refuses absolute paths, `..` and
empty segments and re-checks the result is inside the root, so a bug (or a
crafted call) in the webview can't reach the rest of the disk; the
`PathsThatEscapeTheLibraryAreRefused` test pins it. Names the user types
go through `sanitizeName`, which is why a folder called `Marshall/JCM`
becomes `MarshallJCM` instead of a nested folder.

## A folder is a unit

Loading a folder loads its files as **one multi-model block**, by the same
rules as dropping a folder on a tile (majority extension decides NAM vs
IR, natural name order, 300 files / 50 MB caps). So `Fender Twin/` with
eight captures becomes one tile with eight switchable models, and the
folder tree is both organization and a way to build multi-model tones by
hand. The listing's `models` count per folder is exactly what loading it
would add.

## Getting tones in

Four ways, and the library shows whatever is in the folder regardless of
which was used:

- **Save to Library** on a tile menu files the block's *active model*,
  bytes and all, from its model cache. This is the one that matters for
  catalog tones: the bytes are already in memory from the download, so
  filing them costs nothing, needs no account, and the copy outlives the
  session. Named `<tone> - <model>` (collapsed to one name when the model
  adds nothing), uniqued with ` (2)` rather than overwriting.
- **Drop** files or a folder on the browser. Bytes ride the bridge as
  base64 (the webview never exposes paths) and are validated exactly like
  a drop on a tile, so nothing unloadable lands in the library. A dropped
  folder keeps its name and becomes a library folder.
- **Add ▸ Add Files / Add Folder** opens the OS picker and copies the pick
  in. This is the route that works everywhere: Linux never delivers OS
  file drags to the embedded webview ([issue #22](https://github.com/tone-3000/tone3000-plugin/issues/22)),
  so there it is the only way in besides Save to Library.
- **The OS.** *Show Folder* opens the library in Finder/Explorer; files
  put there by hand show up on the next listing. Files copied from disk
  are only checked by extension — the loader validates them on load, the
  same as anything a user files in themselves.

Removals go to the OS trash where there is one. Blocks already in the
chain keep playing after a removal: they load from the content-addressed
local stash, and presets and DAW state embed their model bytes.

## Organising

Any row can be dragged: drop a tone or a folder on a **folder row** to file it
there, or on a **breadcrumb** to move it up to that level (the `Library` crumb
is the root). It is a move on disk (`ToneLibrary::move`), not a copy. A name
already taken at the destination is uniqued with ` (2)` rather than
overwritten, a file keeps its extension, and a folder is uniqued by its whole
name (`Twin v1.2` becomes `Twin v1.2 (2)`). Dropping on the folder an item is
already in does nothing, and a folder can't go into itself or its own subtree
(compared by real ancestry, so `Pack` may move into `Pack 2`).

Order is not part of this: a folder lists in natural name order and loads its
models in that order, and there is no stored order to reorder. Blocks already
in the chain are unaffected, for the same reason removal doesn't touch them.

The drag is pointer-based `@dnd-kit`, like the chain and the preset bar, not
HTML5 drag events. HTML5 dragging is where old WebKit has needed fixes and
where Linux webviews behave differently, and this keeps clear of the OS-file
drop handlers, which only react to a drag that carries `Files`. Only the drop
target under the pointer counts (`pointerIntersection`); the default detector
falls back to the dragged row's overlap and would light up a folder beside
the tone row the pointer is on.

## Offline

The library is the reason the **+** button still does something useful
with the internet down: when the OS reports no network, `useToneLoadFlow`
opens the browser on the Library tab instead of raising the offline modal
(the catalog tabs are still there, and still say what's wrong if the user
switches to them).
