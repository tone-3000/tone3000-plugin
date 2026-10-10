# The Library change set: what changed and why

This page is for reviewing the Library branch. It walks through the commits
in order, says what each one changes and why it was needed, explains the
main design decisions, lists the known gaps, and collects ideas for later.

- How the Library is used: [`library-guide.md`](library-guide.md).
- How it works in detail (formats, file layout, edge cases):
  [`library.md`](library.md).
- Adding beside a block: [`chain-slots.md`](chain-slots.md).
- Playing local files in place: [`local-models.md`](local-models.md).

## Size and shape

The branch is nineteen commits on top of upstream v0.0.12 (`ab5e5ae`): the
ten it was opened with, and nine follow-ups (11 to 19). It touches 98 files
with about 23,000 added lines. About 6,200 of those are screenshot
fixtures (`scenarios.json`), about 4,100 are tests and test support, and
about 1,300 are docs, which leaves about 11,000 lines of product code.

The commits are ordered so that each one builds and its tests pass on its
own. They go from small and independent to large:

| # | Commit | Kind | Files |
| --- | --- | --- | --- |
| 1 | Sort a folder's models by name without the extension | bug fix | 2 |
| 2 | Show Normalize on a freshly opened card, labelled NORM | bug fix | 8 |
| 3 | Wait 300 ms before showing a block's loading look | small feature | 6 |
| 4 | Play captures from disk in place instead of copying them | behaviour change | 4 |
| 5 | Run chosen self-test groups, `--fail-fast`, `--no-pointer` | tooling | 4 |
| 6 | Add a block beside another (edge and gap drops) | feature | 21 |
| 7 | The Library's native side | feature | 19 |
| 8 | The Library drawer | feature | 64 |
| 9 | Number keys and A/B on the block card | feature | 7 |
| 10 | Docs: this page and the user's guide | docs | 2 |
| 11 | Folder pictures follow a renamed library | bug fix | 3 |
| 12 | Blocks follow their files through Library renames and moves | bug fix | 7 |
| 13 | Docs: this page, for 11 and 12 | docs | 1 |
| 14 | Blocks find their moved files by name and bytes | bug fix | 13 |
| 15 | Name the block card and model picker for screen readers | bug fix | 2 |
| 16 | REFRESH: add a folder's new captures to its block | feature | 9 |
| 17 | SOURCE prefers your own copy of a TONE3000 tone | feature | 5 |
| 18 | Scroll the drawer while dragging near its edges | feature | 3 |
| 19 | Folders in your order: drop between folders | feature | 12 |

Commits 1 to 5 stand alone and could be merged separately. Commit 6 is
useful on its own for OS file drops, and the Library's drag and drop needs
it. Commits 7 to 9 are the Library itself, 11, 12 and 14 fix what
renaming a library showed, and 15 to 19 came from using it (below).

## Commit by commit

### 1. Folder sort ignores the extension

**What:** `localNameLess` compares file names without their extension first.

**Why:** a folder loaded "Gain 4.5.nam" before "Gain 4.nam".
`compareNatural` on the full names compares `.nam` against `5.nam`, and
`'5' < 'n'`. This is an upstream bug. The Library made it visible because
its folder loads step through packs in file order.

### 2. NORM shows on a fresh card

**What:** `BlockCard` runs `setBodyView()` at the end of its constructor and
when the Per-Block Normalization setting changes. The `=` glyph becomes a
NORM text button, and the settings tip shows the same chip
(`inline_chrome::textButton`).

**Why:** the per-block Normalize control appeared only after an EQ or info
toggle, and turning the setting on left an open card unchanged (upstream
bug). The bare `=` read as decoration and went unnoticed. The Library adds
KEEP to the same body view, which made the bug visible.

### 3. Loading look after 300 ms (`BusyGrace`)

**What:** the card's dimmed artwork with a spinner, and the tile's dots, show
only once a load has run for 300 ms.

**Why:** switching between local captures takes a few milliseconds. With the
Library, users step through folders constantly, and every step flashed the
loading look.

### 4. Local files play in place

**What:** on a desktop, a capture read from disk keeps its own path as its
`model_url`. Nothing is copied into `LocalModels`, and the file's dates are
never touched. Byte loads (the base64 route) and iOS still use the stash.
Only stash file names (`<hash>-<size>.<ext>`, `isStashFileName`) are
re-rooted by name.

**Why:** every capture that played was copied into app data, and the copy's
date was refreshed to keep the stash cleanup away. With a linked collection
of thousands of files, the stash would have duplicated whatever the user
auditioned. It also edited the dates of the user's files. Presets and DAW
state already embed the model bytes, so a project still opens when the file
has moved. This is point 1 of upstream issue #210.

**Review note:** a file played in place that has since gone is reported
missing. It is never swapped for a stash file with the same name (the test
`OnlyStashNamesReRoot`).

### 5. Targeted self-tests

**What:** `UiTestbed --selftest [--fail-fast] [--no-pointer] [name ...]`
runs only the named groups (substring match, in the order given). Each test
announces itself on stderr as it starts.

**Why:** the full UI suite takes about 110 s and grew by 19 groups in this
branch. Running only what changed keeps the loop short, and a crash now
names the test it happened in. `--no-pointer` skips the groups that drive
real pointer input, which fail when someone uses the machine during the run.

**Review note:** running a group first exposed a latent crash. Several
groups, upstream's included, call `expect` during setup, before their
first `beginTest`. In a full run, those checks were silently counted under
the previous group; run first, JUCE's runner dereferences a null result.
Those groups now start with `beginTest("setup")`.

### 6. Adding beside a block

**What:** a drop on a tone tile's left or right edge (a strip as wide as the
gap between tiles), or in the gap itself, adds a new block there. An
insertion bar marks where. Tone tiles get **Add Before... / Add After...**.
`loadTone` accepts `before:<blockId>` and `after:<blockId>` targets
(`kSlotBeforePrefix`, `slotBefore()` / `slotAfter()` on the UI side). The
tone splices in, and only the tail re-pads.

**Why:** dragging a capture from the Library between two blocks otherwise
meant moving blocks out of the way first. Every load path that takes an
insert id (browser picks, OS drops, Library loads) takes these targets too,
so nothing else had to learn about them.

**Review note:** an earlier version also kept gaps when a block was removed
and added Remove Slot. That was dropped. The lane invariant
(`normalizeLaneInserts`) and removal are upstream's, unchanged.

### 7. The Library, native side

**What:**

- **`LocalLibrary`** (`Library.h/.cpp`) is the file layer: a folder per
  library, Captures / Presets / Local sections, linked folders, the listing,
  file operations, `.t3klibrary` export and import, Export for Sharing, and
  the library state file (`LibraryState.h`).
- **`ProcessorLibrary.cpp`** is the processor's API for the editor: Keep (the
  playing model written as a capture), TONE3000 downloads (one at a time on a
  pool), and async moves, copies and deletes.
- **Folder loads:** `loadLocalToneInFolder` loads a capture with its folder
  as one block, starting on the capture picked.
- **`setLocalToneArt`** dresses a local block as a TONE3000 tone (image,
  creator, link, title, gear).
- **Presets:** save into a Library folder (`savePresetToFolder`), step through
  the active preset's folder (`stepPreset`), and follow the active preset
  through moves and renames (`relinkActivePreset`).
- **A1 folders and `__MACOSX`** are left out of listings and folder loads
  (`NamArchitecture`).
- **HTTP error pages:** `fetchModelFromUrl` checks the status code.
- **Drawer view in the state:** the drawer's view is saved with the plugin
  state (`libraryView` in `ProcessorState`).

**Why each part is native:**

- **The file layer** lives in the processor because the processor owns the
  preset manager and the stash, and the DSP tests can then cover it without
  a UI (37 tests in `library_tests.cpp`).
- **Scans** run off the message thread. `getLibrary` is the one backend call
  that is safe there: it scans a copy of the location taken under
  `libraryLock`. A collection of 30,000 files takes seconds to walk cold.
  The listing is cached in `library-listing.cache` and prewarmed on a thread
  when the plugin loads, so the drawer usually opens at once.
- **Folder loads are split** into `prepare` (any thread, reads and checks the
  files) and `finish` (message thread, builds the block). Reading and
  validating 231 `.nam` files (each a JSON parse) blocked the UI for seconds.
  `prepare` now checks files on up to 8 threads and caches each result by
  path, size and modification time, so loading the same pack again is
  instant. iOS keeps the old single-threaded, uncached path, because its
  stash copies need their dates refreshed.
- **`localModelProblem`** factors the existing A2 / WAV validation out of
  `stashLocalBytes`, so Keep and downloads check bytes before writing
  anything into the user's Library.
- **The HTTP status check:** Keep and Download write fetched bytes into the
  user's Library. Without the check, an error page (401, 404, 500) would be
  parsed as a model, and could be written there as a capture.
- **The A1 filter** goes by folder name only. Reading every file to find A1
  captures was tried and was too slow on large collections. The plugin plays
  only A2, and many packs ship A1 folders beside the A2 ones.

### 8. The Library drawer

**What:** everything the user sees. Here is where each part lives:

| Area | Code |
| --- | --- |
| Drawer, rows, menus, keys, strips | `views/library/LibraryDrawer` |
| State and actions | `services/LibraryStore` (+ `LibraryStoreState.cpp`) |
| Tree, filtering, search rows | `model/Library` |
| TONE3000 artwork for local folders | `services/ToneArt` |
| WebP pictures | `services/PictureFile` (libwebp) |
| KEEP, SOURCE / KEPT, picture button, card drops | `views/block/BlockCard` |
| Library rows dropped on tiles and gaps | `GalleryTile`, `ChainView` |
| Backend calls | `Backend.h`, `ProcessorBackend` |
| Block played before (A/B), folder loads | `ChainStore` |
| Large caches kept in their own files | `UiPrefs::storeApart` |
| Quiet toasts, hint icons, row hints | `Toast`, `HintBus` / `HintBar`, `ContextMenu` |
| Mock library, scenarios, 19 self-test groups | `testbed/` |

**Why it is built this way:**

- **Audition by part, not "replace the open block".** Users try five amps in
  a row in one block, the way the NAM app swaps its single model, so loads go
  to the block playing the same part (`auditionTarget`). Loading into
  whichever card was open would let a pedal replace the amp. With several
  blocks of one part, a menu asks which, once.
- **Kept links.** A curated folder loses track of where each pick came
  from, and the next step is often "try the same pack one setting up". A
  kept copy records its original, so SOURCE and KEPT can go both ways. Copies kept before links existed are matched by
  name and bytes (`learnKept`).
- **The library state file.** The UI prefs are per machine and get wiped, but
  the Library folder moves, syncs and is backed up. So what the Library knows
  (kept links, pictures, order, the keep folder) is mirrored into a hidden
  `.t3klibrary.json` per library and merged back with a three-way merge.
  Two machines sharing a synced folder then don't undo each other's
  removals.
- **Missing files.** Folders get moved in Explorer by mistake. Matching by
  the longest path ending under a folder the user picks reconnects whole
  folders at once. A bare file-name match counts only for the file asked
  about, because names like "DI.nam" are everywhere.
- **Export for Sharing never ships capture files.** The plugin can't tell a
  bought capture from a free one, so no local capture goes. Captures that
  came from TONE3000 go as links to their tones. Preset blocks keep their
  bytes only when every model's hash matches a capture downloaded from
  TONE3000; other blocks are emptied.
- **ToneArt is polite to the API.** It sends one request at a time, at least
  a second apart, only while signed in, and only once per folder: every
  answer, a miss included, is cached. A folder renamed by its owner is still
  matched when its files name a creator: that creator's tones are searched,
  and a tone matches only when its model names are the folder's files.
- **No UI freezes.** Moves, copies, deletes, drops, imports, exports and
  folder loads run on workers, with the work under way shown in a pinned
  toast.
- **Large caches in their own files** (`storeApart`). The kept links and the
  artwork cache grow to thousands of entries. In the shared prefs file, every
  write re-merged all of them.

**New dependency:** libwebp (BSD-3-Clause, decoder only), fetched with CPM in
`NativeUi.cmake` like the other dependencies, and credited in the README.
It decodes WebP pictures once; they are stored as PNG.

### 9. Number keys and A/B on the card

**What:** with a block's card open, digits pick a model (digits typed close
together make one number, with a "25 / 40" toast), Left / Right step, and
"a" switches between the current and previous model, folder or tone. The
card takes the keyboard when it opens and when clicked. `PluginRoot` passes
unclaimed keys (after a click in empty space or the faceplate) to the open
card.

**Why:** auditioning by ear is fastest with the hands on the keyboard, and
comparing two captures is the core of choosing one. The routing exists
because hosts give the plugin window the keyboard only when something in it
asks: without it, numbers worked only after clicking the model picker.

### 10. Docs

[`library-guide.md`](library-guide.md) for users and this page.

### 11. Folder pictures follow a renamed library

**What:** when a path is renamed or moved in the Library (`remapPaths`),
the paths of the picture files under it move too, not only the folders the
pictures belong to. A block whose capture (or its original) sits under a
folder whose picture moved is dressed again at once.

**Why:** a library signed out on first use is named "My Library", and takes
the TONE3000 username on the first sign-in (`adoptUsername`). Its
`.t3kpictures` folder goes with it. Before this, the stored picture paths
still named "My Library", and a block wearing one showed nothing until the
project was reopened. The same happened on renaming your library in the
drawer.

### 12. Blocks follow their files through Library renames and moves

**What:** where the processor already re-points the active preset after a
rename or move (`libraryRename`, `libraryMove`, `libraryMoveAsync`),
`relinkLocalFiles` re-points the blocks too. Each local block's model URLs,
source paths and a picture under the moved path are rewritten. The move is
also remembered for the process (`noteLocalFilesMoved`, up to 500 moves),
and `resolveLocalModelFile` follows those moves when a stored path is gone.

**Why:** captures play in place since commit 4, so a block names the user's
file, not a stash copy. After a folder was renamed or moved in the drawer,
playing and saving still worked (the session stores the bytes), but
anything that reads the file again failed:
- an undo bringing a removed block back;
- a retry;
- switching to another model of the folder;
- the UI's kept links and pictures, which go by the block's source path.

Following moves inside `resolveLocalModelFile` also covers undo snapshots,
which keep their own copy of the old path, and other instances in the same
host.

### 14. Blocks find their moved files by name and bytes

**What:** once the Library is listed, and whenever the chain changes,
`LibraryStore::findMovedFiles` checks each local block's files. For one
that isn't at its path, it looks in the Library and linked folders for a
file of that name whose bytes give the block's model id, and re-points the
block there through `relinkLocalFiles` (now public, and on the backend).
- **One formula for the id:** a local model's id has always been a hash of
  its bytes. It now lives in one place, `library_state::localModelId`, used
  by the processor and the Library alike. It keeps the processor's own
  FNV-1a starting value (`1469598103934665603`, not FNV's standard one),
  since every saved session and preset holds ids made with it; a DSP test
  pins the id of a test capture.
- **The plugin log** says what the search does (`[Library]` lines): the
  files it is looking for, candidates whose bytes don't match, and where it
  found them.
- **A whole folder gone:** once one file is confirmed in a new folder, the
  rest are taken from there by name, and the folder is re-pointed in one go.
  A 300-file pack isn't re-read.
- **The same bytes in several places** (a kept copy and its original): the
  folder holding the most of the block's missing files wins.
- **Files on a drive that isn't plugged in** are left alone; they're away,
  not moved. Each missing path is checked once per listing.

**Why:** commit 12 only follows moves the drawer makes while the plugin is
loaded. A project saved before a rename (here: a library renamed on sign-in
by a build without commit 12) came back with dead paths. A capture never
played had no stored bytes and showed "Download failed", and kept copies
lost their names and pictures. Renames made in Explorer or Finder had the
same effect.

### 15. Names for the block card and the model picker

**What:** the card is titled "Block" and the picker "Model".

**Why:** commit 9 made both take keyboard focus (for numbers and A/B), which
made them Tab stops without a screen-reader name. `UiTestbed --capture`
fails on unnamed Tab stops.

### 16. REFRESH

**What:** a REFRESH button above SOURCE / KEPT, shown when the block's
folder holds files of its kind that the block doesn't list
(`LibraryStore::newInFolder`, cached by the folder's date). It loads the
folder into the block again on the capture it plays (`refreshBlock`,
`loadCapture(..., again)`, which skips the "already in the block: just
switch" shortcut).

**Why:** a block's captures are fixed when it loads its folder, and saved
with the session, so a session reopens exactly as it was. That's right for
a project, but a capture kept into the folder the block plays didn't join
it, and the only way to get it was to load something from the folder again.
Making it automatic would change saved sessions behind the user's back, so
it's a button.

### 17. SOURCE prefers your own copy

**What:** for a copy kept from a TONE3000 tone, SOURCE (and the drawer's
Load Source) first looks for the same model as a file of yours
(`localSiteOriginal`):
- a file linked to the same tone and model, or with the same recorded
  bytes (Download All Captures, an earlier KEEP), the folder holding the
  most of that tone winning;
- else a file with those bytes in a folder the artwork lookup matched to
  the tone (downloaded from the website into a linked folder), the size
  checked before any file is read.

Only with none does it load from TONE3000.

**Why:** loading from TONE3000 means downloading, and stepping through the
tone's models then downloads each one, with lag on every Left / Right.

### 18. Scrolling while dragging

**What:** while a drawer row is dragged, a 60 Hz timer scrolls the list
when the pointer is near its top or bottom edge, faster closer to the edge
and past the bottom.

**Why:** JUCE's drag and drop doesn't scroll a viewport, so a folder could
only be dragged as far as the list showed.

### 19. Folders in your order

**What:** dragging a folder onto the top or bottom quarter of a sibling
folder's row places it before / after that folder, with a line marking
where. The middle of the row is "into", as before, and so is an open
folder's bottom edge.
- **Storage:** the order is a pref per folder (`kFolderOrderPref`: a
  folder's path → its folders' names), applied in `arrange` among the
  places folders take.
- **What stays put:** captures, linked folders at Local's end and a
  library's sections keep their places.
- **Persistence:** the order is mirrored into the library's state file
  (`folders`, keyed like pictures), carried by Export Backup and Import,
  and kept through renames and moves (`remapPaths`, `forgetPaths`). The
  state file now counts `folders` as something to keep (`holdsAnything`).

**Why:** names were the only way to order folders, so people renamed them
("1. Pedal boost"), which also renames every path that points into them.

## Things to look at closely

- **Threads.** `Backend::getLibrary` (worker), `prepareLocalToneInFolder`
  (worker, then its own thread pool), `libraryDownloads` (a pool of one,
  reporting back only while `downloadsAlive`), the prewarm thread (joined in
  the destructor), and `runLibraryJob` for async file work. The processor's
  destructor stops downloads and scans before anything else.
- **Archive import** validates every entry first (relative paths only, no
  `..`, at most 1 GB per entry), unpacks into a hidden staging folder, then
  moves the files into place.
- **Deletes** go to the OS trash, except on iOS (which has none), where the
  drawer confirms first.
- **Network.** New requests are the ToneArt searches (above), the favorites
  list when the drawer opens signed in, the TONE3000 account's tones (cached
  for offline), Export for Sharing's lookups (at most 300 per export, with
  Cancel), and Keep / Download for TONE3000 captures.
- **Behaviour changes for existing users:**
  - local files play in place (commit 4);
  - ‹ › step a Library folder when the active preset came from one;
  - a local tone that matched a TONE3000 tone shows its image (`ToneImage`);
  - long toasts stay up longer (up to 6 s);
  - menus are as wide as their longest label.

## Tests

- **DSP tests (GoogleTest):**
  - `library_tests.cpp` (38): file layer, formats, archives (the folder
    order included), state, sharing, processor glue, blocks following
    renames and moves (and the model id the Library recomputes);
  - `local_load_tests.cpp` (+6): folder loads, in-place, error pages, sort;
  - `chain_slot_tests.cpp` (4): splicing beside a block;
  - `nam_architecture_tests.cpp` (2): the A1 folder-name rule.
- **UI self-tests:** 19 new groups and about 105 test cases. Among them:
  Library features, drawer, state, paths, keep, kept links, arrangement,
  text (every hint fits the hint bar), ToneArt, picture files, chain slot
  tiles, block card drops, model picker numbers, busy grace, and block
  normalize setting.
- **Scenarios:** ten `library-*` screens, `main-drop-edge` and
  `main-detail-normalize` in `scenarios.json` for the screenshot tooling.

Run only the Library groups with
`UiTestbed --selftest Library ToneArt "Model picker"`, and the DSP side
with `DspTests --gtest_filter="Library*:LocalLoad*:ChainSlot*:NamArch*"`.

## Known gaps

- **Kept-link index and artwork cache growth.** Entries for files outside
  every library are never pruned. Both caches are bounded in practice by the
  user's collection, but nothing caps them.
- **Two `Library.h`** headers (`plugin/include/` for the file layer,
  `plugin/ui/model/` for the UI tree). They're in different include roots,
  but the shared name is confusing.
- **Presets folder state.** The presets folder is mounted from outside the
  Library folder, so pictures and links on it don't travel with a Library
  backup.
- **Branch taps in shares.** When Export for Sharing empties the block that
  feeds a stereo branch, the branch goes too.
- **The audition target** (which block the Library loads into) isn't saved
  when the editor closes.
- **Set Folder...** points the Library at another folder. It doesn't offer
  to move the old one there.
- **Files moved outside the Library** (out of the Library folder and every
  linked folder, in Explorer or Finder) can't be found by commit 14, which
  only looks there. A block playing one plays and saves as before (the
  session stores the bytes), but a retry or switching to another capture of
  its folder reports the file missing. Linking the folder it went to brings
  it back. Kept links and folder pictures of files moved anywhere can be
  reconnected with **Missing Files**, which asks where the files went.

## Ideas for later

### Follow a TONE3000 user

A followed user would appear as a library beside yours and TONE3000's:

- **Their Captures:** their public tones, newest first. This is the creator
  search the TONE3000 library already uses (`ToneQuery::creators` with
  `ToneSort::newest`), cached for offline browsing.
- **New since you last looked:** remember the newest `publishedAt` seen per
  user, mark newer tones, and show a count on the user's row ("3 new"). An
  optional combined **New** folder could gather new tones from everyone you
  follow.
- **Adding one:** from a creator's name on a block card, a browser card or a
  TONE3000 row, or by typing a username.
- **Cost to watch:** a request per followed user on each drawer open needs
  throttling (once a day per user, say) and a cap on how many users are
  followed.

### Public favorites

If the API made favorites public (opt-in per user), a followed user's library
could show their **Favorites** too: the tones a person you trust keeps
coming back to. This is the same `.t3ktone` reference the Library already
uses, so it needs no new item type. The same goes for a user's public
**collections** (named folders of tones) if the site gets them. Each would
show as a folder under that user, read-only, with Copy to Mine.

### A site library

[`library.md`](library.md#a-site-library-later) lists the seams already in
place: catalog ids in `.t3ktone`, libraries as a list of owners, "the latest
replaces the old" import semantics, and the `source` field in the archive
manifest. A synced library would upload your own library's references and
presets, and could render a preset's rig as tiles on the website from the
`ChainSnapshot` inside each `.t3kpreset`.

### Smaller ones

- **Presets folder setting:** a setting for the default user presets folder
  (issue #210, point 2).
- **Self-contained preset export:** export a preset as a folder or zip with
  its captures as files, its picture and a readable summary (#210, point 3).
- **Pruning:** prune kept links and cached artwork for files that no longer
  exist anywhere, on a schedule.
- **Persistent audition target:** remember the audition target per instance,
  with the drawer's view.
- **Set Folder move:** offer to move the Library when Set Folder... points
  elsewhere.
- **Preview before loading:** tap to hear a capture in the drawer without
  loading it into a block.
