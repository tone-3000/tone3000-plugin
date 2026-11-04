# Audition loop

A looped riff through candidate tones. The player records a few bars of
dry input once, the loop stands in for their hands, and the tone browser
previews captures through it without committing anything to the chain.
Kemper's Profiler (its looper plus the rig browser) is the workflow this
borrows; the TC Electronic Ditto's one switch and the Boss RC series'
arm-on-signal ("Auto Rec") and LED position ring are the control model.

Three parts: the loop (`plugin/include/LoopRecorder.h`), its button in the
top bar (`plugin/ui/views/LoopButton` over `services/LoopFeed`), and the
audition (`services/ToneLoadFlow` over a chain preview session in the
processor). Without a loop the browser behaves exactly as before.

## The loop

`LoopRecorder` is header-only and JUCE-free. It sits at the chain input,
after the fold-down and before input gain, so the take is the dry DI and
every downstream stage (gate, meters, chain) sees the loop as if it were
the player. Mono: guitar sources are one channel there.

```
empty --tap--> armed --signal (or tap)--> recording --tap (or max)--> playing
playing --tap--> stopped --tap--> playing                any --clear--> empty
```

- **Arm on signal, with pre-roll.** The take starts on the first sample at
  or above −40 dBFS, and the 5 ms before it come along, so the pick attack
  that triggered the take is in it whole. A tap while armed starts
  recording anyway (a quiet intro).
- **Seam fades.** The tail fades over 10 ms; the head over 2.5 ms, inside
  the pre-roll, so it is done before the attack lands. The Ditto blends
  the tail past the loop point into the head instead; that is the upgrade
  if the dip at the seam is audible on a ringing chord.
- **Replace, don't mix.** While the loop plays the live input is gone:
  auditioning is hands-free, and noodling over the loop would defeat the
  comparison. Play and stop crossfade dry and loop over 10 ms.
- **Cap.** 20 s, allocated in `prepare`, never on the audio thread.

`tap()` / `clear()` from the message thread are counted and consumed at
the next `process()`, so two taps inside one block are two steps; state,
length and position are atomics the UI polls at its tick, like the tuner.
A re-prepare at the same rate keeps the take (hosts re-prepare on a
buffer-size change or a transport start); a rate change empties it.
Runtime state only: nothing about the loop goes into chain state, presets
or DAW state.

Channel 0 carries the loop. It is mirrored into channel 1 whenever the
fold-down left both channels equal (a mono chain, a mono device, Input
L / R); a real stereo source into stereo chains, or an engaged dual mono,
keeps its live right channel.

Pinned by `test/loop/loop_recorder_tests.cpp` (`LoopRecorderTests`, which
builds without JUCE in seconds).

## The loop button

One 28 px button beside the tuner, so it is on every screen, the browser
included. Click is the tap, right-click (touch and hold) clears. The glyph
says the state and a ring around it is the loop position; red for armed
and recording, brand blue for a loop that exists. One Help key per state
is the hint and the screen-reader name. `LoopFeed` polls `getLoopState()`
on the UiClock tick like `TunerFeed` and notifies on change only. The
`main-loop-*` scenarios capture each state over the mock, and the `loop`
self-test pins the walk and the help copy.

![The loop button's five states](audition-loop/loop-button-states.gif)

Follow-ups: a bare `L` / `Shift+L` with nothing focused, and a MIDI
footswitch for the tap.

## Audition

A mode the loop unlocks, with no new control: the browser is open with a
pending target (the `+` or ⇄ that opened it) and the loop is playing.

- **Preview.** A card click loads the tone into the pending slot through
  the ordinary load / swap path, inside a *chain preview session*
  (`beginChainPreview`): the first preview snapshots the chain as an undo
  entry would, and while the session is open `pushChainHistory` records
  nothing. The target stays pending, so the next click replaces the same
  block, and the browser stays open. The previewed card wears a blue
  border and a play pill; the hint bar pins the rules.
- **Keep.** The previewed card clicked again. `commitChainPreview` pushes
  the session's snapshot as one undo step (none when nothing changed) and
  the browser closes.
- **Put back.** ← or Escape, or anything else that closes the browser.
  `cancelChainPreview` restores the snapshot through the reconcile undo
  uses, under the chain-edit fade.
- Undo and Redo are refused while a session is open and the header greys
  them. A host state load ends an open session along with the history it
  clears; the browser drops its audition when it sees `previewing` go.
- The loop stopped mid-audition: the next click lands as a plain pick and
  closes the session with it. The loop started under an open browser: the
  cards switch to previewing.

Hover does not preview; a mouse crossing the grid would thrash model
loads. Level-fair by construction: a previewed model plays at the level a
committed one would.

Pinned by `test/src/chain_preview_tests.cpp` and the `audition` self-test;
the `browser-auditioning` scenario captures the marked card.

Known limits: a real stereo source into stereo chains feeds the loop to
Left only, and in a DAW a playing loop replaces the track's recorded input
too (the header shows it).
