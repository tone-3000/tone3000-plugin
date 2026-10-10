# The Library

A place to organize and share tones and rigs. A library has two halves:
**Captures** (single amps, pedals and cabs: TONE3000 tone references and
local `.nam` / IR `.wav` files, plus your TONE3000 Favorites) and
**Presets** (whole rigs). Today it is local, plain files in a
folder you choose. The formats and the code are shaped so a TONE3000 site
library (public, searchable, followable) can plug in later without a
migration ([below](#a-site-library-later)).

Code: `plugin/include/Library.h` + `plugin/src/Library.cpp` (the file
layer), `plugin/src/ProcessorLibrary.cpp` (processor glue),
`plugin/ui/services/LibraryStore` (UI state and actions),
`plugin/ui/model/Library` (the tree and the filter),
`plugin/ui/views/library/LibraryDrawer` (the drawer). Tests:
`test/src/library_tests.cpp` (files, formats, processor glue) and the
`Library` self-test plus the `library-*` scenarios in the UI testbed.

## On disk

```
<Library folder>/                 Documents/TONE3000/Library by default
  tonehound/                      your library
    Captures/                     your own captures
      Clean/
        Plexi - Crunch.t3ktone
        my-amp.nam
    Local/                        local files that aren't your own captures
      Pedal boost/                (a keep folder, a collection you copied in)
      NAM Captures ⛓              a linked folder, wherever it lives
    Presets/  ⇢                   the user presets folder, mounted
      Setlist/
        01. Best song.t3kpreset
        02. Next best song.t3kpreset
  awesomeuser/                    an imported library (read-only)
    Captures/  Presets/
```

**As listed** every library is a user's, with up to four sections, each
shown only when it has something in it (`LibraryStore::arrange`, after
every scan; paths stay the ones on disk, so every action works unchanged):

```
TONE3000                          TONE3000, as a user (no folder of its own)
  Captures                        tones the TONE3000 account published (a creator
                                  search, cached for offline: t3k.librarySiteTones)
  Presets                         the factory presets
tonehound                         you
  Captures                        your captures folders
  Favorites                       your TONE3000 favorites
  Presets ⇢                       your saved presets (the user presets folder)
  Local                           your Local folder, the linked folders in it (yours only)
awesomeuser                       imported: its Captures and Presets
```

TONE3000's Favorites aren't public, so it has none. Local is a real folder
(`<your library>/Local`, made on first use): move folders into it, make new
ones, keep into it; the linked folders list at its end without being moved. An empty section of yours
(your Captures before the first folder) isn't listed but still exists: a
drop on your library, its Add here, and its New Folder land in the right
one (`LibraryTree::hidden`, `LibraryStore::halfFor`).

**Filtering.** Under the search, a row of gear toggles (the tone browser's
gear kinds, plus Presets) filters every library at once: turn on any number
and only items of those kinds show, with the folders holding them (open);
all off shows everything. It works with the search (both must hold) and is
part of the instance's saved view. A tone's gear is its catalog gear; a
local capture's is its folder's, read once per folder during the scan (the
first `.nam`'s `metadata.gear_type`, the first IR's length: up to 1 s a
cabinet, longer a space), so a folder mixing kinds files under its first
file's. A folder (or one up to two above it) that matched a TONE3000 tone
(the artwork lookup) takes that tone's gear instead, which beats a file's
metadata (a DI capture's `gear_type` often says amp + cab for an amp head).
Captures show their gear's icon in the list; hovering an item puts its icon
(gear, preset, folder) before its name and path in the hint bar.

Libraries list in your order: right-click one → Move Up / Move Down, or drag
it onto another (it lands just above). The order is a per-machine pref
(`t3k.libraryOrder`, library paths top first); a library it doesn't name
follows in scan order.

Folders can go in your order too: drag a folder onto the top or bottom edge
of another folder in the same folder (a line marks where it goes; the
middle of the row is "into", as always, and so is an open folder's bottom
edge, its contents following). Only folders move: captures and presets keep
their natural order, a linked folder keeps its place at Local's end, and a
library's sections keep theirs. The order is kept per folder
(`t3k.libraryFolderOrder`: a folder's path → its folders' names, in order;
folders it doesn't name follow in natural order), mirrored into the
library's state file like the pictures (`folders`), carried by Export
Backup and Import, and kept through renames and moves. While a row is
dragged, the list scrolls near its top and bottom edges (faster closer to
the edge, and past the bottom).

**Typed halves.** Every folder takes its type from the half it is in:
captures folders list and take only captures and references, presets
folders only presets, and moves, copies, adds and saves refuse the other
kind (a stray file of the wrong kind in a folder is left alone, unlisted).
That keeps what a click means the same throughout a folder: in Captures,
double-click auditions one block; in Presets, it loads the rig and ‹ ›
walk the folder as a setlist. Your library's own top takes nothing; new
folders go into a half. A folder at a library's top that is in neither half
(a hand-made one) shows as untyped and lists everything.

- **Your library** is one folder named after you. The name is fixed on first
  use (your TONE3000 username when signed in, else "My Library") and kept in
  the per-machine UI prefs (`t3k.libraryOwner`), so signing in or out never
  turns your library into "someone else's". Renaming it in the drawer
  renames the folder and the pref together. A library still called "My
  Library" (nobody was signed in on first use) takes your username once you
  sign in, unless a folder of that name is already there. It also carries a
  hidden id (`.t3klibrary-id`), which is what tells your own backup from
  someone else's library of the same name on import (below).
- **Presets** in your library are the user presets folder
  (`PresetManager`), *mounted*: nothing is moved or copied, every preset you
  saved before the Library existed is in it, and the preset browser, program
  changes and Settings → Presets keep working exactly as before. A folder
  made under it is an ordinary Library folder. Its `Factory/` subfolder and
  `order.json` stay out of the tree.
- **Favorites** mirror TONE3000: your library's Favorites, listing the
  tones you favorited on the site, fetched when the drawer opens signed in
  (all pages), cached in the UI prefs (`t3k.libraryFavorites`) so it still
  browses offline (loading a tone needs a connection, as always). Dropping a
  tone on it (or picking it for a tile's Add to Library) favorites it on the
  site; Unfavorite removes it there. Local captures can't be favorites. A
  favorite is a tone, not a model: it loads the tone's first model, and
  dragging one into a folder of yours keeps a reference to it.
- **Linked folders** bring a collection in where it already lives (your
  NAM captures folder, the one the NAM app also uses): ⋯ → Link Folder...,
  right-click Local or your library → Link Folder..., or drop a folder from
  the OS on the drawer. It shows at the end of your Local with a link mark,
  nothing copied, and its contents are yours to edit like the rest (deletes
  still go to the trash). The links are a per-machine pref
  (`t3k.libraryLinks`); Unlink only forgets the link. A folder that has gone
  (an unplugged drive) stays listed as "not found" until unlinked. Links
  that would show the same files twice (inside the Library or the presets
  folder, holding either, or overlapping another link) are refused. A
  whole-library Export Backup leaves linked folders out (they aren't the
  library's own files, and can be large); export one on its own to back it
  up. Export for Sharing brings them along as links (below).
- **Import Folder...** is the copy-instead-of-link alternative: ⋯ → Import
  Folder... copies into Local (other people's captures, beside the linked
  folders; Captures is for captures you made), right-click one of your
  folders → Import Folder... into that one, or drop a folder on the drawer
  and pick Copy Here (a drop asks: Link or Copy Here). The folder lands as a
  new folder (subfolders kept,
  " 2" when the name is taken), and only library content comes along
  (`.nam`, `.wav`, `.t3ktone`): trainer leftovers, notes, folders named for
  A1 captures (the Library would hide them) and folders left empty by that
  stay behind, and the source is untouched.
- **Any other folder** at the top is someone else's library: read-only in
  the drawer (a lock on its row), but anything in it can be copied into the
  matching half of yours (drag, or right-click → Copy to Mine), and the
  whole library can be removed. Its `Captures/` and `Presets/` folders are
  typed like yours.
- **Names are file names.** Folders, references and captures are named by
  their files; a preset's display name lives inside it, as everywhere, and a
  rename changes both. Files you move in Finder / Explorer show up on the
  next scan (opening the drawer, any edit, or ⋯ → Refresh).
- **Deletes go to the OS trash** (iOS, which has none, deletes after the
  drawer's confirmation).

## Item formats

**`.t3kpreset`**: the existing preset file (`PresetFile.h`), unchanged. A
preset outside the flat preset list is addressed by path, `file:<absolute
path>` (`PresetManager::fileId`), so it can be the active preset like any
other; it never gets a program-change number.

**`.t3ktone`**: a pointer at one model of a catalog tone, JSON:

```json
{
  "format": "t3ktone", "version": 1, "source": "tone3000",
  "tone":  { "id": 42, "title": "Plexi", "gear": "amp", "format": "nam",
             "image": "https://…", "url": "https://www.tone3000.com/tones/…",
             "user": { "username": "kenji" } },
  "model": { "id": 7, "name": "Crunch" }
}
```

Loading one runs the tone browser's own path: `GET /tones/{id}`, its models
(the saved one, else the first), a fresh token, then the same add-or-swap
landing as a browser pick. It needs a sign-in, like the browser. Only the
ids matter; the rest is there to draw the row and name the file.

**`.nam` / `.wav`**: a local capture, loaded through `loadLocalTonePath`
like a dropped file (and stashed the same way, so presets and undo keep
working if the file later moves).

## Using it

The drawer (header → Library icon) docks at the left edge under the header.
Beside it, the chain band and the faceplate keep their full-width layout
and are scaled down together to fit (laid out taller, so they fill the
height): their parts have fixed footprints (the 800px block card, the
plate's knob groups), so squeezing would overlap them and covering would
hide them. Every tile stays a drop target.

Scans run on a worker thread (`Backend::getLibrary` is the one backend call
that is safe off the message thread; the processor scans a copy of the
location taken under a lock), so a big linked folder never stalls the UI.
The tree stays for the editor's life: reopening the drawer shows it at once
while a fresh scan catches up; only the very first open says "Loading…".

A big linked collection on a slow drive (30,000 items on a spinning disk:
1.5 s or more cold, longer while the disk spins up) is walked once, when it
is first added. The listing is saved (`library-listing.cache` in the app
data folder, keyed by the location: root, owner and links) and the next
session shows it at once (`Backend::getSavedLibrary`, ~0.3 s to read and
build the tree) while the walk checks it in the background; the plugin
starts that walk when it loads (prewarm), so it is usually done before the
drawer opens. The file is rewritten only when a scan listed some folder
again and the result differs.
Favorites changes re-place the Favorites folder in the tree without a
rescan. A scan that was under way when the favorites, TONE3000's tones or
the order changed is arranged again with the current ones when it lands, so
it never brings back the old sections (or closes what you had open in
them).

The drawer's view is each instance's own: open folders, the selection, the
search, the scroll position and whether the drawer is up are kept with the
plugin's state (`getLibraryView` / `setLibraryView`, saved with the project),
so reopening the editor or the project shows it where you left it.

- **Click** a folder to open or close it. **Double-click** a preset to
  load the rig.
- **Audition captures.** Double-click a tone or capture (or right-click →
  Load) to try it: it replaces the block playing the same part of the rig
  (an amp, amp and cab or full rig; a pedal; a cab, NAM or IR; outboard)
  whose card is open, else the one the Library last loaded that part into,
  else lands at the end of the active lane. So trying five amps in a row
  swaps one block five times instead of stacking five amps, the way the NAM
  app swaps its one model; a pedal tried then swaps the pedal (or comes in
  beside the amp), never the amp. With two blocks of that part (two
  pedals) and neither pointed at (no card open, nothing of that part loaded
  from the Library yet), a menu asks which to replace, or to add a new
  block; the pick is remembered for that part. A capture whose gear isn't known (no
  metadata, no TONE3000 match for its folder) goes by format: a NAM capture
  over a NAM block, an IR over an IR. Right-click →
  Add as Block (or a drag onto a + slot) adds a new block instead.
- **What is playing** has the same dot as the loaded preset: the captures
  local blocks play, and TONE3000 tones the chain holds. Stepping the block
  last loaded from the Library to another capture moves the selection to it
  (quietly: a search that hides it stays). The list stays still while that
  row is in view, and scrolls only as far as the edge it steps past.
- **Show in Library** (a tone tile's menu, or the library button in a block
  card's header) opens the drawer on the file the block plays.
- **Open on TONE3000** (a TONE3000 tone's menu) opens its page.
- **Enter** in the search loads the first match: a capture or tone of
  yours first, then any other, then a captures folder whose own name
  matches; a preset (the whole rig) only when nothing else does.
- **Drag** an item onto a tile: a preset loads, a tone or capture lands
  there like a browser pick (an insert slot adds, a tone tile swaps).
- **A folder as one block.** Drag a captures folder onto a tile (or
  right-click → Load as Block): one block whose model picker switches
  between the folder's captures, the tiles' own Load Folder (subfolders
  included, the majority of `.nam` vs `.wav` decides, up to 300 files;
  references in it are not files and stay out).
- **Drag** a row onto one of your folders of its kind: yours move, anyone
  else's (and favorites) are copied in. **Drop OS files** on a folder:
  captures, presets and references are copied in, `.t3klibrary` archives
  imported, folders linked; dropped anywhere else, each goes to its half.
  The toast counts them ("Added 12 to Amps (3 skipped)"); other files say
  so instead of vanishing. Moves, copies, deletes and drops run off the UI
  (a folder moved across drives is a copy), a folder's saying so meanwhile.
- **Keys** once a row is clicked: Up / Down move the selection, Enter loads
  it (a folder with captures of its own opens on its first one and loads
  it; one holding only folders opens or closes), Right opens a folder then steps into it,
  Left closes it then steps up to its folder. **A number** jumps to that
  capture (or tone) in the selected row's folder, counted from 1 (digits
  typed close together make one number: "2", "5" is 25; past the end, the
  last), a toast saying where ("25 / 40"), and loads it: at once when no
  more digits could follow, else after a short pause. Plugin Settings →
  **Library: Number Keys Load** off: it is only selected (Enter loads). A
  block's card takes numbers too once clicked (its model), Left / Right
  step, and **"a"** is A/B: back to what the block played before, and again
  forth. Another model of its tone switches; another folder's capture or
  another TONE3000 tone is loaded into the block again (a capture from its
  file, a TONE3000 tone from TONE3000, signed in). In the drawer "a" is
  A/B for the block the Library last loaded into (else the open card's).
  A folder's files are checked on several threads, and each file's check
  is remembered by its path, size and date: a pack of hundreds loads again
  without reading them again. After a
  load from the keyboard the drawer takes the keyboard back (a host can
  take it from the plugin's window as the chain changes).
- **Order:** your library first and open, then TONE3000 (closed until you
  open it), then anyone else's; drag a library onto another, or Move Up /
  Down, to change it.
- **Right-click** for the rest: New Folder, Save Here (the current rig as a
  preset; presets folders only), Rename, Copy to Mine, Export Backup...,
  Export for Sharing..., Reveal,
  Delete, Link / Unlink, Unfavorite. On one of your captures, **Put in Own
  Folder** moves it into a new folder beside it named after it (its kept
  links go along): New Folder, Rename and a move in one.
- **Add a chain tone**: right-click its tile → Add to Library, then click a
  captures folder (or Favorites). A catalog tone is saved as a `.t3ktone`
  for its active model; a local one as a copy of its active capture file.
- **Keeping** builds a curated folder by ear. Every block card has
  **KEEP** beside its model picker; right-click a captures folder → Keep
  Here makes it the keep folder (the drawer's strip says "Keeping in …",
  Stop ends it, its row carries a mark), and KEEP then copies in one click
  (without a keep folder it asks where). Audition a pedal's folder in one
  block, step through its captures, KEEP the best (a copy of the capture),
  load the next pedal's folder, and so on.
  Loading anything from the keep folder gives one block switching between
  all of it. The keep folder is a per-machine pref (`t3k.libraryCollect`,
  its name from before it was called Keep), so a long session survives
  closing the window; a folder that is gone stops it.
- **A2 only.** The plugin plays A2 captures only, so the listing leaves
  out folders named for A1 captures ("A1", "REVyHI" or "xSTD" as a word of
  its own, bare or in brackets: "Amp (A1)", "[xSTD] Stack"), with what is
  in them, and a folder left empty by those (one that was empty anyway
  stays). Files aren't read for it (too slow on a big collection); an A1
  file elsewhere is listed, and refused when loaded. Loading a folder as a
  block skips the same subfolders. `__MACOSX` folders (what a zip made on a
  Mac leaves beside its files) are hidden too, and skipped by folder loads
  and Import Folder.
- **Kept links.** A capture loaded from disk remembers its file (the model's
  `source_path`; on a desktop the block plays that file in place), and keeping it
  records copy → original (`t3k.libraryKept`, kept in step with moves and
  renames made in the drawer, and in the library itself: Library state,
  below). So the card can go both
  ways, with one button above KEEP: on a kept copy, **SOURCE** swaps the
  block to the folder it was kept from, starting on it (to try the capture
  with a bit more gain); on an original, **KEPT** goes to the copy, or with
  copies in several folders asks which. Above those, **REFRESH** shows when
  the block's folder holds captures of its kind the block doesn't list
  (kept, dropped or copied in since the block loaded it): it reads the
  folder into the block again, staying on the capture it plays. A block's
  captures are otherwise fixed when it loads its folder, so a session
  always reopens as it was saved. Without a recorded link (a copy
  kept before links existed, or Keep on a block restored from an older
  project), a Library capture with the same file name and the same bytes
  stands in: one in a linked collection is the original of one in your own
  folders, and the other way round. A match found is recorded as a link
  (`t3k.libraryLearned` marks it as worked out, not kept), so it stays drawn
  as kept through later scans; it goes quietly when either file does.
- **Keeping a TONE3000 tone.** KEEP keeps the capture the block plays, as a
  file of its own (the bytes native already loaded, else downloaded), named
  "tone - model". Its link names the tone and model instead of a file (the
  same pref, an object for a value), so the copy is drawn as kept, plays
  dressed as the tone (its title, artwork, creator) with the tone's card
  (info, share, stats, KEEP's menu acting for the tone), and SOURCE (or the
  row's Load Source) loads the tone again with all its captures, on that
  one; the tone's card offers KEPT. When you have the tone as files of
  your own, SOURCE loads those instead of TONE3000 (no download, and
  stepping the folder has no lag): a file linked to the same tone and model
  (Download All Captures, an earlier KEEP), the folder holding the most of
  that tone first; else a file with the copy's bytes in a folder the
  artwork lookup matched to the tone (one downloaded from the website). The arrow beside KEEP (TONE3000 tones
  only) has the three ways: **Keep Capture** (as KEEP), **Keep as Reference**
  (its `.t3ktone`: one item holding all its captures, played from
  TONE3000; drawn yellow with a link mark, apart from your files in white
  and kept copies in blue), and **Download All Captures** (a folder named
  after the tone holding every capture as a file, each linked to its model;
  one download at a time, with progress in the toast; downloading it again
  fills in only what is missing, in the same folder). Each goes into the
  keep folder, or with none set asks for a folder (the drawer's Add here,
  its strip saying which way); the folder picked becomes the keep folder,
  so the next KEEP just keeps (the strip says "Keeping in ...", with Stop).
  KEEP of something a folder already holds says "Already in ...".
- **In the drawer** a kept copy draws apart (a blue glyph and a copy mark;
  its hint names the folder it was kept from). Right-click it → **Go to
  Source** opens the original's folders, selects it and scrolls to it (a
  search that hides it is cleared); on an original, **Go to Kept in …**
  does the same for each copy.
- **A kept copy looks like its original.** A block playing a kept copy takes
  the original's folder name as its title and that folder's TONE3000 artwork
  (a keep folder like "Pedal boost" names no tone), and follows the model
  picker: step to another capture in the keep folder and the title and
  artwork follow its original; step to one with no original and the block's
  own title returns.
- Library confirmations ("Kept in …", "Moved to …") use the quiet toast
  (white outline); errors keep the solid one. Work under way ("Importing
  ...", "Downloading ... (3 of 12)") stays up until its result replaces it,
  and a long result stays long enough to read.
- **Folder pictures.** Right-click a captures folder → Set Picture... (Change
  / Remove once it has one), or use the picture button on a local block's
  image (for the folder its capture is in; a kept copy's original's): the
  image is copied into the folder's library (`.t3kpictures/`, so moving or
  unplugging the original doesn't lose it, and it goes where the library
  goes; a folder outside every library, into yours) and a pref maps the
  folder to it (`t3k.libraryPictures`, mirrored in the Library state below).
  An older build's copies in the app data folder (`LibraryPictures/`) move
  over on the next change.
  PNG, JPEG, GIF or WebP; a WebP is decoded once, by the bundled libwebp
  (BSD, fetched like JUCE; `services/PictureFile`), and kept as a PNG.
  Blocks loaded from the folder, or a folder up to two below it, show it in
  place of the TONE3000 lookup, signed in or not; blocks already in the chain
  take it at once.
- **Artwork for local captures** (`services/ToneArt`). A block whose folder
  matched takes the tone's title (a folder is often just "DI") and gear too.
  Every local block in the chain gets this once per folder, a block restored
  with a project included (from the cache when the folder was looked up
  before). A block saved before blocks recorded their file finds it by name
  and bytes in the Library, so its kept links work too. With no match, a
  block from a generic folder ("DI", "NAM", "Captures"…) is named after its
  capture instead (following the model picker), and a gear tag at the front
  of a file name ("[AMP] …", "[PEDAL] …") gives the gear, in the list too. A linked collection
  is often TONE3000 downloads: folders named after the tone, `.nam` files
  whose metadata names the creator (`modeled_by`). When a capture or folder
  is loaded from the Library, its folder (then up to two above it, for a
  generic "DI" or "favorites") is looked up with one title search; a result
  whose title matches the folder and whose creator matches the files (case
  and punctuation aside; with no creator in the files, only an exact title)
  puts its photo, creator and page link on the block
  (`setLocalToneArt`, saved with presets and sessions like any tone field).
  A folder renamed by its owner ("Bogner Uberschall Rev Blue (E34L) - Amp
  Head" for 2dor's "Bogner Uberschall") misses that search; when its files
  name a creator, that creator's tones are searched for the folder name's
  first two words (the creator's own name left out), and the first of at
  most three whose models are the folder's files by name (half of them, or
  five) is the match.
  Responsibly: only while signed in, only for a local block playing a file
  from disk (a Library load, a drop, a restored project), one request at a
  time a second apart, and once per folder ever. Every answer,
  a miss included, is cached in the UI prefs (`t3k.libraryArt`); a network
  error is not an answer, so that folder is asked again another time. The
  search carries the plugin's A2 filter like every catalog search, so a
  folder of A1-only captures doesn't match.
- **Search** filters every library at once: a folder whose name matches
  shows with everything in it ("Metal" brings up the Metal folder), an item
  that matches shows inside its folders ("BE100" finds the preset wherever
  it is). Of the folders whose names match only the first shows open; the
  others show closed until clicked ("Bogner" doesn't spill hundreds of
  captures). Down in the search goes to the first capture shown (the arrow
  keys going on from there), and "s" in the list goes back to the search,
  its text selected. Every word must match; library (owner) names
  alone don't. It runs
  once typing pauses, not per keystroke (a linked collection is tens of
  thousands of rows).
- **Slow work stays off the UI.** Scans, a capture's folder read and checked
  for an audition (up to 300 files), Import Folder, and `.t3klibrary` import
  and export run on worker threads; closing the editor stops a scan under
  way. A file moved or deleted outside the plugin since the last scan says
  so when loaded, and the Library scans again. The big caches (kept links,
  favorites, the TONE3000 tone list, the artwork cache) live in files of
  their own beside the UI prefs (`UiPrefs::storeApart`), read once and again
  only when they change.
- **Text.** Every Library hint is one line in the hint bar (a test measures
  them all against its width), and toasts are short: what happened, not how.
- **Library state** (`LibraryState.h`). What the Library knows about your
  files beyond the files (kept links of both kinds, folder pictures, and
  for your own library the keep folder, the library order and the linked
  folders) is kept in each library folder too: a hidden `.t3klibrary.json`
  beside `.t3klibrary-id`, paths relative to that library (one outside every
  library, in a linked folder, absolute in yours), its pictures in
  `.t3kpictures/`. The UI prefs stay the working copy: a change is written
  into the files a moment later (and before an export), and a file that is
  new or changed since this editor last looked is merged back in, its
  entries winning. So everything comes back after a reinstall or with the
  prefs wiped, in a Library moved or copied to another folder, drive or
  machine (⋯ → Set Folder... there), and across machines sharing a synced
  Library folder: an entry one machine removes goes on the others too (each
  instance remembers what a file held when it last read or wrote it). The
  keep folder, the order and the linked folders are a machine's own (a
  linked folder's path is): a file brings them back only to a machine that
  has none. A copy kept from another library in the Library is linked
  relative to the Library folder, so it follows a move too. A link to a file outside the Library (a linked
  collection) needs that file at the same path; without it, the same-bytes
  match above still finds a linked original.
- **Missing files.** A link to a path in the Library or a linked folder
  that isn't there anymore, on a drive that is (a folder moved or renamed
  in Explorer / Finder by mistake), is missing; links on a drive that isn't
  plugged in aren't. After each scan the drawer's strip says how many
  copies you kept (and the keep folder) are gone ("2 files missing"; an
  original or a picture's folder gone is only in the list, ⋯ → Missing
  Files...); **Show** lists them all, each named with what it is (a kept copy and
  what it was kept from, an original and its copy, a folder with a picture,
  the keep folder; the hint gives the old path). Picking one asks where it
  is now ("Where is Plexi.nam now?"), opening in the nearest folder of its
  old path still there; **Find All...** asks once for all. Pick the folder
  they went to, or the moved folder itself, and each is matched by the longest end of its path found there ("Amps/
  Plexi.nam" under the folder "Amps" went into), its kept links, picture
  and keep folder relinked. A folder picked that was renamed too is told
  by the files found in it, so its picture comes back as well. **Hide** puts the notice away for those
  files, in every session after too (one going missing later shows it
  again); the list (also ⋯ → Missing Files...) has **Forget N Missing Files**
  (asked first) for files deleted on purpose. A copy kept from a TONE3000
  tone needs no finding: its row says "(from TONE3000)" and downloads it
  again where it was, still linked to its tone (**Download All Again** for
  several). A link whose copy is gone is never dropped on its own. A match
  by the file name alone ("DI.nam" is in many folders) counts only for the
  file picked in the list, or for several from one folder turning up
  together; two missing files found as one file, or a copy found as its own
  original, are left missing.
- **Setlists.** Once a preset is loaded from a Library folder, the preset
  bar's ‹ › and the MIDI *Preset Previous / Next* targets walk *that
  folder*, in natural name order ("2" before "10"), wrapping at the ends.
  A preset from the list (the Presets folder itself, or a factory one)
  steps the list as before. Program changes always address the list, so
  their numbers never move. Saving from the preset bar while a Library
  preset is active saves into its folder ("Save to Setlist"), same-name
  overwriting as everywhere.
- The active preset follows its file: renaming or moving it, or a folder
  above it, keeps it active under its new path (`relinkActivePreset`);
  deleting it leaves the chain as it is with no active preset. Blocks follow
  theirs the same way (`relinkLocalFiles`): a block playing a capture from a
  folder renamed or moved in the drawer (your library renamed included)
  names it where it went, its folder picture too, and an undo bringing a
  removed block back finds the file there. A block whose file isn't at its
  path any more for another reason (a project saved before a rename, a
  folder renamed in Explorer / Finder) is found again too: once the Library
  is listed, each missing file is looked for by its name, a find counting
  only when its bytes give the block's model id (`findMovedFiles`). The same
  bytes in several places (a kept copy and its original) go to the folder
  holding the most of the block's missing files; a folder gone as a whole
  is re-pointed in one go (the rest found there by name). Files on a drive
  that isn't plugged in are left alone.

## Sharing: `.t3klibrary`

Two exports (right-click), both a `.t3klibrary`:

- **Export Backup...** is for yourself: everything as it is (captures,
  presets, references, the Library state below), to restore with Import.
- **Export for Sharing...** is for others, and no capture file goes out.
  A capture from TONE3000 goes as a link (one `.t3ktone` per tone per
  folder): a copy kept from a tone, or one in a folder the artwork lookup
  matched to a tone by its creator (the folder itself; above it only from a
  generic one like "DI", never into Captures, Local or a library). Folders
  never looked up are asked about first, one at a time (the same lookup: a
  second apart, cached; signed in only; only folders whose captures name
  their creator; at most 300 per export, the next export going on from
  there), the drawer's strip counting them with **Cancel**, so a collection
  of TONE3000 downloads goes as links. Every other capture stays home: your own, bought
  ones, anything not found on TONE3000 (the plugin can't tell those apart,
  so none go). A link from a wrong match only points at a tone; bytes are
  held to more: a preset's block of local captures keeps its model bytes
  only when each is byte for byte a capture downloaded from TONE3000 (the
  hash its kept link recorded), and goes without the paths it was loaded
  from; every other local block is emptied (an empty slot, its bytes left
  out; when that block was the stereo branch's tap, the branch goes too).
  Blocks of TONE3000 tones keep their own models' bytes. Your whole library
  brings its linked folders along this way, under Local. A share has no
  library id and no kept links, and an import never takes it for anyone's
  backup (`"shared": true`). The toast says what went, on one line: "Shared
  tonehound: 12 tones as links, 85 local-only left out". With nothing
  that may go, no file is written.

A backup zips a folder, an item, or a whole library:

```
t3klibrary.json      { "format": "t3klibrary", "version": 1,
                       "kind": "library" | "folder" | "item",
                       "type": "captures" | "presets",   (folder / item)
                       "owner": "tonehound", "name": "Metal",
                       "files": 12, "exported": "<ISO time>",
                       "source": "local" }
content/…            the files, relative (a library as Captures/ + Presets/)
state.json           the Library state of those files, keyed relative to content/
pictures/…           the folder pictures it names
```

Only Library content travels (presets, references, captures). Import
(⋯ → Import File..., or drop the file on the drawer) unpacks under the
manifest's owner:

- **Someone else's whole library replaces their folder.** Re-importing a
  newer export is how a library you "follow" updates: the local version of
  following. (Their old copy goes to the trash.)
- **Your own whole library merges back in**, Presets included: restoring a
  backup. "Your own" by the library id the export carries (a fresh install
  with no id yet goes by the owner name and takes the backup's id; one
  signed out, still "My Library" with nothing in it, takes it as its own
  whatever its name); an older archive with no id by the owner name, never
  the default "My Library".
  Someone else's library under a name that is yours lands beside it as
  "<name> (imported)". The archive brings Library files only, a repeated
  entry replaces rather than appends, and no entry may unpack past 1 GB. Identical files are skipped, clashing ones get a " 2".
- **A folder or an item** lands in the matching half (the manifest's
  `type`), beside what is there.
- **The Library state comes along** (`state.json`): kept links and folder
  pictures, re-keyed to wherever their files landed (a folder that came in
  as "Amps 2" keeps its picture and links), merged into the receiving
  library's own; what that library already says about a file stays, and a
  backup's file that clashed with a different one here doesn't take its
  entry. Your own whole library also brings back its keep folder, order and
  links, where it has none. Paths outside the Library travel only in a
  backup of your own library.

Entries are validated before anything is written (relative paths only, no
`..`, nothing outside the staging folder) and unpacked into a hidden
staging folder first, so a bad archive changes nothing.

## A site library later

The local Library is meant to be the offline half of a site feature, so the
seams are where that would attach:

- **Identity.** `.t3ktone` stores catalog ids, so a site library's single
  tones map onto the same reference; a preset keeps its `id` (a uuid) inside
  the file, which a site copy can carry too.
- **Libraries are a list of owners.** The tree is `{ libraries: [...] }`
  (listed in your order); TONE3000's library (`LibraryNode::site`, put
  together in the UI from the favorites and the presets folder) is where a
  site library's own collections would join its Captures. Someone else's
  site library is another entry (read-only, `removable`
  as "unfollow"), and `LibraryNode` already distinguishes yours (`mine`)
  from others. A remote source would add its libraries to `LibraryStore`'s
  tree beside the local scan, with actions (Copy to Mine, Load) that are
  already kind-based rather than path-based where it matters.
- **Follow semantics** are already "the latest state replaces the old":
  re-importing someone's library replaces it, which is what a synced
  follow would do continuously. Copying into your own library is the
  "fork".
- **Manifest `source`** says where an archive came from ("local" today); a
  site export would say "tone3000" and carry the library's remote id, so an
  import can be recognised as a site library's snapshot.
- **Private items.** A local library is private until exported; a site
  library would mark items public or private, and private captures already
  load only for their owner through the API's own checks.

What a site version still needs, and the local one deliberately doesn't do:
syncing your library up, searching other people's libraries server-side,
and previewing a whole preset on the website (its rig is the `ChainSnapshot`
inside the `.t3kpreset`, which a site page could render as tiles).
