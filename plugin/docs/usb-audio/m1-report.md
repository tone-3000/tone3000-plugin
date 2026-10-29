# M1 report: USB probe spike (plan M1.7)

Date: 2026-10-17. Rig: Samsung Galaxy Tab S10 FE (SM-X520, Android 16,
kernel 6.6.102-android15) + Zoom AMS-22 (UAC2) on an unprivileged app.

## Verdict: GO

A userspace USB audio driver works on this rig. Sustained isochronous
duplex from `com.TONE3000.USBCHECK` with zero xruns over 14.5 minutes,
clean unplug/replug, and a measured self-contribution that meets the plan's
target once the URB layout is chosen deliberately.

## What works

- **Enumeration, permission, claiming.** UsbManager enumeration, the
  permission dialog, `claimInterface(force=true)` on the AudioControl and
  AudioStreaming interfaces (detaches `snd-usb-audio` as required), and the
  quirk registry leaving the extra AC block (iface 3) and MIDI (iface 4)
  bound. `USBDEVFS_GETDRIVER` before claiming recorded what held each
  interface (all `snd-usb-audio`; see `ams22-descriptors.md`).
- **Descriptors (M1.3).** 336-byte blob captured to
  `plugin/usb/test/fixtures/ams22-highspeed.bin` and parsed: UAC2, clock
  source id 40 (SAM_FREQ control), playback `0x02` + explicit isoc feedback
  `0x82`, capture `0x81`, subslot 4 / 24-bit, one altsetting per direction.
  Pinned by the `RealAms22Fixture` host test.
- **Control path (M1.4).** SET_CUR 48 kHz on the clock source, then
  SET_INTERFACE to the active altsettings; streams run at 48 frames per
  millisecond on the nose. GET_CUR readback is not wired (minor gap, M2).
- **Isochronous streaming (M1.5).** 3 URBs x 8 packets per direction at
  48 kHz/2ch/subslot 4: 14.5 min, 869,698 URBs per direction, **zero
  xruns**, CPU ~24%, drift estimate settled at a few hundred ppm
  (`results-m1-stream.txt`). Unplug/replug: stream quiesces on the first
  fatal ioctl, fd closes, interfaces release, no crash, clean reopen
  (`results-m1-stream.txt` detach section).
- **RTL self-measurement (M1.6).** Digital loopback median 9.958 ms and
  analog (cable) median 9.521 ms at the 3x8 default layout; sweep shows the
  contribution falls with URB depth to **1.25 ms at 2 packets x 3 URBs**
  and 1.92 ms at 4 x 2, meeting the <= 2 ms target
  (`results-m1-rtl.txt`).

## What does not work / caveats

- **1-packet URBs are degenerate** in the probe (no detection): 125 us
  URBs need tighter scheduling than the spike's JNI pump provides. Not a
  blocker; 2x3 already meets the target.
- **`start_frame` is 0 on every URB** on this kernel/HCD, so frame
  continuity is unobservable here. The machine checks the observed step
  where HCDs populate it.
- **Drift from reap timing is scheduling noise** (settles to a few hundred
  ppm but starts wildly wrong). The AMS-22 has an explicit feedback
  endpoint (0x82, 16.16 expected) which M2's ClockModel must use instead.
- **Probe tooling**: mode dispatch via intent works only on fresh launches
  in this build (force-stop first); the adb loop does that. A foreground
  service is absent, so Android freezes the probe when the tablet screen
  sleeps (observed: a soak run frozen at 7.5 min until the screen woke).
  The real app's `AudioSessionService` pattern covers this at M2/M3.
- M1.6's per-trial variance (5-12 ms at 3x8) is URB queueing, not signal;
  the correlation peaks are clean (ratios > 18).

## Numbers

| Metric | Value | Source |
| --- | --- | --- |
| Soak, 3x8 | 14.5 min, 0 xruns, CPU 24% | results-m1-stream.txt |
| rtl digital, 3x8 | 9.958 ms median | results-m1-rtl.txt |
| rtl analog, 3x8 | 9.521 ms median | results-m1-rtl.txt |
| rtl digital, 2x3 | 1.25 ms | results-m1-rtl.txt sweep |
| rtl digital, 4x2 | 1.92 ms | results-m1-rtl.txt sweep |
| Baseline (Oboe path) | 88.188 ms | results-baseline-oboe-bypass.txt |

## Next (M2)

Feedback-driven pacing on 0x82, ring stress, recovery paths incl. the
AudioSessionService semantics and 48<->96 rate switch, 60 min L3 soak with
screen off, and the shipping URB layout decision (2x3 vs 4x2 vs 3x8).
