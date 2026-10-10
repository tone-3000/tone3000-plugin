# Chain slots

Adding a block beside another, without first making room for it.

## The model

A lane is an ordered list of blocks. Empty slots (the dashed **+** tiles) are
real entries in that list: *insert* placeholders that pass audio straight
through and own no engine. Signal flow is just the order of the tone blocks;
empty slots never change the sound. After every structural change a lane is
padded back to at least `kMinLaneSlots` (5) tiles with at least one empty
slot (`TONE3000Processor::normalizeLaneInserts`), and removing a block closes
the lane up: the blocks after it move up one. That is all as it was; what
follows only adds a way in.

## Adding beside a block

A drop target used to be a whole tile: an empty slot fills, a tone tile swaps.
With no empty slot in front of a block, adding one before it meant dragging the
block out of the way first. Now a tone tile has three drop zones:

| Where | What happens |
| --- | --- |
| The gap before it, and as much again inside its left edge | A new block goes **before** this one (a green insertion bar in the gap) |
| Middle | The tile's tone is **swapped** (dashed outline, as before) |
| The gap after it, and as much again inside its right edge | A new block goes **after** this one |

This applies to everything that can be dropped on a tile: `.nam` / `.wav`
files and folders from the OS, and Library captures, folders and tones.
Library presets replace the whole chain, so they have no edges. The same
thing without a drag: right-click a tone tile → *Add Before...* / *Add After...*
opens the tone browser for that position.

### How it's wired

Load paths take a target block id: an insert slot to fill, or a tone block to
swap. A target can also be `before:<blockId>` / `after:<blockId>`
(`kSlotBeforePrefix` / `kSlotAfterPrefix`; the UI builds them with
`slotBefore` / `slotAfter` in `model/ChainState.h`). `loadTone` then splices
the new block in beside that block instead of consuming a slot, and the tail
re-pads. The prefixed target passes untouched through every existing route
(`LocalFiles::drop`, `LibraryStore::use`, `ToneLoadFlow`, the local loaders'
swap-then-load fallback), so opening the slot and loading into it happen in
one step: one undo entry, and no stray empty slot if the load fails. A target
whose block is gone falls back like a stale insert id: the active lane's first
empty slot.

Tests: `test/src/chain_slot_tests.cpp` (processor), "Chain slot tiles" in
`plugin/ui/testbed/SelfTests.cpp` (drop zones), and the `main-drop-edge`
testbed scenario (the insertion bar).
