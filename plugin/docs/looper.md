# Experimental global mono looper

First enable **Settings → Plugin → Enable Looper** (off by default).
Disabling it stops transport, closes the view and hides the Loop button.
The setting is remembered on this device.

Press **Loop** beside the Spread / Align controls to open a tuner-style
view with Record, Stop, Play, Mix and Pan. Close the view with its X or
press Loop again. Closing the view leaves recording/playback running.

- **Record** discards the previous take and starts a new one. Live guitar
  passes through unchanged while recording; there is no overdubbing.
- **Stop** ends recording or playback and retains the take. Recording also
  stops automatically at 40 seconds.
- **Play** restarts the take from its beginning and repeats until stopped.
- **Mix** adds playback without turning down live guitar: 0 = no loop,
  0.5 = loop at its recorded level, 1 = loop at twice its recorded level.
  The default is 0.5. Live guitar stays at full volume at every setting.
  Centre Pan preserves the loop level; summing live and loop needs headroom.
- **Pan** positions the mono loop between the left (-1), centre (0) and
  right (+1) outputs, with unity gain at centre and a short smoothing ramp.
  It changes playback only, leaving live-guitar routing untouched. A mono
  output ignores Pan. Double-click/tap the control to return to centre.

## One-button MIDI recording

With Enable Looper on, choose **Settings → MIDI → Looper Record / Play**.
Use Learn to assign a CC or note, or type a CC number. Use a dedicated
footswitch message (for example CC 6 value 127) on each press:

1. First press starts recording a fresh take.
2. Second press ends recording and immediately starts repeating it.
3. Third press deletes that take and starts a new recording.
4. Fourth press ends the new recording and starts repeating it again.

There is no overdubbing. A MIDI-started recording automatically begins
playback at the 40-second limit. Momentary CC releases (127 then 0) and
note-off messages do not advance the transport. The global MIDI channel
filter applies, and Learn consumes its capture without starting recording.
Turning Enable Looper off stops transport and ignores the mapped pedal;
the mapping is retained. Closing the looper view does not disable MIDI.
The touch Record, Stop and Play buttons keep their original behavior.
MIDI transport changes apply at audio-buffer boundaries.

The looper sits after both processing chains, their stereo-image stage and
the global tone EQ and master Output. It records the processed guitar at
its current Output level, summing a stereo source to mono. Playback does not
pass through the current amp/IR blocks or Output gain again. Once recorded,
the loop keeps its level when Output changes; Output still controls live
guitar. Use Mix to change loop playback level. The output meters show the
combined live and loop signal. Auto-align pauses the looper while its probe runs.

The tool belongs to the processor rather than a preset or editor: changing
presets, undoing chain edits, or closing/reopening the view keeps the take,
transport, Mix and Pan. Opening the tuner closes the looper view but does
not stop playback. A saved preset/session/backup does not include the take
or these runtime controls. Restarting the app starts an empty looper.
Changing the device/host sample rate clears the take to avoid wrong-speed
playback; changing chain oversampling alone keeps it. Storage is allocated
on preparation, never by Record/Play or the sample-processing loop.

The former experimental looper block and Add Looper menu are retired. If
an old experimental preset contains that block, restoring it drops the
retired block while preserving its other blocks and settings.
