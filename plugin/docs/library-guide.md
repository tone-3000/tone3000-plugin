# Using the Library

The Library is a side drawer for your captures and rigs. It lists your own
captures, your TONE3000 favorites, the NAM folders you already have, and your
presets, all in one place. You can try captures by ear, keep the ones you like,
organise them, and back them up or share them.

This page is the user's guide: what you can do, and how the common jobs go
step by step. The design reference, with formats, file layout and edge cases,
is [`library.md`](library.md).

Open the drawer with the **Library** button in the header. Close it the same
way. The chain and the faceplate shrink to fit beside it, and every tile still
takes drops.

## What you can do

### Find and try captures

- **Search** everything at once, and narrow the list with the **gear toggles**
  under the search (amp, pedal, cab, outboard, space, presets).
- **Double-click** a capture (or press **Enter**) to try it. It replaces the
  block that plays the same part of your rig. An amp replaces your amp, and a
  pedal replaces your pedal; a pedal never replaces the amp. If you have two
  pedals, the Library asks which one to replace, and remembers your answer.
- **A capture comes with its folder.** The block's model picker steps through
  the rest of the folder, so you can go through a pack of 200 captures from
  one block.
- **Drag** a capture onto a tile to load it there, onto a tile's left or right
  **edge** to add a new block beside it, or into the **gap** between two
  tiles.
- **Type a number** to jump to that capture in its folder ("25" is the 25th).
- Press **"a"** to switch between what a block plays now and what it played
  before (A/B). This works across folders and across TONE3000 tones.

### Keep the best ones

- **KEEP**, on every block card beside the model picker, copies the capture
  the block plays into your **keep folder**. The first KEEP asks which folder
  that is.
- A kept copy stays linked to where it came from. On the card, **SOURCE**
  loads the original's folder again, starting on it. On the original,
  **KEPT** goes to your copy. For a copy kept from a TONE3000 tone, SOURCE
  uses your own download of that tone when you have one, so there's no
  waiting for TONE3000.
- **REFRESH** shows above them when the block's folder has captures the
  block doesn't list yet (you kept or copied more in since it loaded).
  Press it to add them; the block stays on the capture it plays.
- **TONE3000 tones** can be kept too. The arrow beside KEEP offers **Keep
  Capture** (a copy of the capture as a file), **Keep as Reference** (a small
  file that points at the tone and plays it from TONE3000), and **Download All
  Captures** (every capture of the tone as files, in a folder of its own).

### Organise

- Make folders, then **rename**, **move** (drag), **copy** and **delete**
  them. Deletes go to the system trash.
- **Favorites** shows the tones you favorited on TONE3000. Dropping a tone on
  it favorites the tone on the site.
- **Link Folder...** shows a folder you already have, such as your NAM
  captures folder, without copying anything. **Import Folder...** copies one
  in instead.
- **Set Picture...** on a folder shows your own image on the blocks loaded
  from it. PNG, JPEG, GIF and WebP all work.
- Libraries can be reordered (drag one onto another, or **Move Up / Move
  Down**).
- **Folders can go in your own order:** drag a folder onto the top or bottom
  edge of another folder beside it. A line shows where it will go; dropping
  on the middle of a folder still moves it inside. Captures inside folders
  stay in natural order.
- **Dragging near the top or bottom of the list scrolls it**, so you can
  drag a folder a long way.

### Pictures from TONE3000

A folder of captures you downloaded from TONE3000 gets that tone's picture,
title and creator on its blocks automatically, while you're signed in. The
plugin looks each folder up once and remembers the answer. A folder you
renamed is still found when its files name their creator and match the tone's
captures.

### Presets and setlists

Your saved presets are the Library's **Presets** section. It's the same folder
as before, and nothing moved. Load a preset from a Library folder, and the
preset bar's **‹ ›** (and the MIDI *Preset Previous / Next* controls) step
through *that folder*: a folder of presets is a setlist. **Save Here** saves
the current rig into a folder.

### Back up, share and recover

- **Export Backup...** writes everything (captures, presets, links, pictures)
  into one `.t3klibrary` file. **Import File...** (or dropping that file on
  the drawer) brings it back on any machine.
- **Export for Sharing...** is for other people. It never includes capture
  files. Captures that came from TONE3000 go as links to their tones; your own
  and bought captures stay home.
- **Missing Files** finds files you moved or renamed outside the plugin.
  Point it at the folder they went to, and their links and pictures work
  again.
- What the Library knows about your files (links, pictures, the keep folder)
  is also saved inside the Library folder. A reinstall, a wiped settings
  folder, or a Library moved to another drive or machine keeps it.

## Workflows

### First run: bring your captures in

1. Open the drawer. Your library is named after your TONE3000 username (or
   "My Library" when you're signed out).
2. **⋯ → Link Folder...** and pick the folder where your NAM captures live.
   It appears under **Local** with a link mark. Nothing is copied, and the
   NAM app can keep using the same folder.
3. Optional: drop more folders from Explorer / Finder onto the drawer. It
   asks whether to **Link** or **Copy Here**.

A large collection takes a moment to list the first time. After that, the
listing is saved and the drawer opens at once.

### Find an amp by ear

1. Click in the search and type part of a name, such as "plexi".
2. Press **Down**. The first match is selected and the list has the keyboard.
3. Press **Enter** to load it into your amp block. Use **Up / Down** and
   **Enter** for the next one, or type its number.
4. Press **"a"** to flip between the last two you loaded.
5. Press **"s"** to go back to the search and type something else.

Only the first folder whose name matches opens, so a search like "Bogner"
doesn't spill hundreds of captures. Click another folder (or press **Right**)
to open it.

### Build a "best of" folder

1. Right-click one of your captures folders → **Keep Here**. The drawer's
   strip says "Keeping in ...".
2. Load a pack and step through it with the model picker (numbers, **Left /
   Right**).
3. Press **KEEP** on each capture you like. The toast says where it went.
4. Load the next pack and repeat. **Stop** in the strip ends keeping.

Loading anything from the keep folder gives one block that switches between
all your picks. **SOURCE** takes you back to a pick's original pack, for
example to try the next gain setting up.

### Compare two captures from different folders

Load the first capture, then the second into the same block (double-click
or drag it onto the tile). Press **"a"** on the card, or in the drawer, to
switch between them. A/B remembers the previous capture per block, whatever
folder or tone it came from.

### Put a pedal in front of the amp

Drag the pedal capture onto the **left edge** of the amp's tile. A bar in the
gap shows where it will go. The amp and everything after it stay where they
are. You can also right-click the amp tile → **Add Before...** to pick from
TONE3000.

### Make a setlist

1. Under **Presets**, right-click → **New Folder**, for example "Friday".
2. Build each rig and use **Save Here** on that folder. Names sort
   naturally ("2" before "10"), so "01 Intro", "02 Verse" works.
3. Load the first preset. The preset bar's **‹ ›** (or your MIDI controller's
   Preset Previous / Next) now walks that folder.

### Keep a TONE3000 tone for offline use

On a block playing a TONE3000 tone, use the arrow beside KEEP →
**Download All Captures**. Every capture of the tone lands as a file in a
folder named after it (the keep folder, or one you pick). The progress is in
the toast, and downloading again only fetches what is missing.

### Move to a new computer

1. Right-click your library → **Export Backup...** and save the file.
2. On the new machine, open the drawer and **⋯ → Import File...** (or drop
   the file on the drawer).
3. Link your capture folders again (**⋯ → Link Folder...**). Linked folders
   aren't in the backup, because they live elsewhere and can be huge. If one
   is in a different place now, **Missing Files** reconnects its links.

If you sync the Library folder itself (for example with Dropbox), set it with
**⋯ → Set Folder...** on each machine instead. The Library's own state file
travels with it.

### Share your library with a friend

1. Right-click your library (or one folder) → **Export for Sharing...**.
2. If some folders were never looked up on TONE3000, the strip counts them
   as it checks, with **Cancel**.
3. Send the `.t3klibrary` file. Your friend imports it and gets your folders,
   with links to the TONE3000 tones and your presets. Preset blocks that
   used captures that aren't on TONE3000 arrive empty.

### Fix links after moving a folder by mistake

If you moved or renamed a folder in Explorer / Finder, the strip says how
many kept files are missing. Click **Show**, then **Find All...**, and pick
the folder where the files are now. Each file is matched by the end of its
path, and its links, pictures and keep folder work again. Copies kept from
TONE3000 show **(from TONE3000)** instead, and **Download All Again** fetches
them.

## Keyboard

In the drawer, after clicking a row:

| Key | Does |
| --- | --- |
| Up / Down | Move the selection |
| Enter | Load the selected item. On a folder with captures in it: open it and load its first capture |
| Right / Left | Open a folder and step into it / close it and step out |
| A number | Jump to that capture in the folder and load it ("2" then "5" is the 25th) |
| a | A/B: back to what the block played before, and forth again |
| s | Back to the search, with its text selected |

In the search: **Down** goes to the first capture shown, **Enter** loads the
first match, and **Escape** clears it.

On a block card (click the card, or anywhere outside a text field while it is
open):

| Key | Does |
| --- | --- |
| A number | Pick that model in the block's folder or tone |
| Left / Right | Previous / next model |
| a | A/B |

**Plugin Settings → Library: Number Keys Load**: turn it off to make numbers
in the drawer only select the capture. Enter then loads it.

## Where things are stored

- **The Library folder**: `Documents/TONE3000/Library` by default (change it
  with **⋯ → Set Folder...**). Plain folders and files you can also manage
  in Explorer / Finder. Press **⋯ → Refresh** after changing things there.
- **Your presets** stay in the plugin's user presets folder, shown in the
  Library as your Presets.
- **Linked folders** stay where they are.
- Each library keeps a hidden `.t3klibrary.json` (links, pictures, order)
  and `.t3kpictures/` folder. Leave them in place; they are what lets a
  backup or a moved Library come back complete.
