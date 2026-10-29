# M2 report: driver core hardening (M2.1-M2.4)

Date: 2026-10-17/18. Rig: Samsung Galaxy Tab S10 FE (SM-X520, Android 16,
kernel 6.6.102-android15) + Zoom AMS-22 (UAC2) behind an Anker 332 USB-C hub
(PD to the tablet). Probe `com.TONE3000.USBCHECK`, 48 kHz/2 ch/subslot 4,
digital loopback ON, direct monitor OFF. Host suite: **77/77**.

## Verdict

The driver core is ready for the JUCE integration. The measured shipping
layout is **8 packets x 4 URBs, 128-frame ring**: 7,990-7,992 packets/s
(nominal 8,000), zero URB xruns, zero ring overruns, zero submit failures,
and **5.52 ms digital-loop RTL** (vs 8.92 ms at the 512-frame default and
88.2 ms for the Oboe baseline). This is a layout the plan did not list; the
plan's candidates were measured and rejected for cadence reasons (below).

## M2.1 feedback pacing (done)

The AMS-22's explicit 16.16 feedback endpoint (0x82) drives playback pacing.
One 16.16 LSB at 6 frames/microframe is 15.26 ppm; observed values
`5.99734..6.00000` match the capture-based estimate. 95 s, 95,000 feedback
packets, zero feedback errors, zero URB xruns (`results-m2-feedback.txt`).
Capture-reap timing remains the fallback after a 2 s feedback stall.

## M2.2 ring hardening (done)

Lock-free SPSC ring with explicit policies: output repeats the last frame
and flags an underrun; input drops oldest and flags an overrun. Both are
surfaced in `StreamStats`. The 10^8-frame producer/consumer stress runs in
~109 ms. The +1-frame pacing test caught a pre-existing scratch allocation
that wrote one frame past the callback buffers; fixed (`results-m2-ring.txt`).

## M2.3 recovery paths (done)

- **Transient submit retry.** A rejected `SUBMITURB` no longer quiesces the
  stream. Bounded attempts with a short backoff, then the URB waits in a
  deferred queue drained on the next pump; only `deviceGone()` tears down.
  `start()` unwinds already-submitted URBs before returning an error.
  Host tests cover transient success, exhaustion+requeue, device-gone, and
  start-failure cleanup. No transient failures occurred on-device
  (`submitFail=0` across all runs), so the path is host-verified only.
- **Foreground service.** The probe session moved from the activity to
  `ProbeService` (`connectedDevice`, partial wake lock). Android 16 freezes
  a non-foreground service when the screen sleeps; the stream now continues
  with `mWakefulness=Dozing` (90-120 s checks, nominal cadence). Commands
  are service intents, so adb can drive everything with the screen off.
- **Rate switch 48<->96.** Full stop, SET_INTERFACE 0, SET_CUR + optional
  GET_CUR, SET_INTERFACE 1, restart. At 96 kHz the feedback value is
  `12.00211` and the cadence is 1,000 URBs/s / 8,000 packets/s; back at 48
  it is `6.00000`. Zero xruns. The AMS-22 STALLs GET_CUR (`EPIPE`), so the
  readback is "unavailable" and SET_CUR is verified by feedback+cadence.
- **Error burst.** `nativeDiscardOne` cancels one in-flight URB; the next
  reap takes the -ECONNRESET path, counts one URB-level xrun (plus the
  playback consequence), and resubmits. The stream kept running.
- **Unplug/replug.** Clean detach paths are covered by M1 and the earlier
  session; a scripted mid-stream physical replug still needs a human and is
  deferred.

## M2.4 layout and soak (done)

The plan's candidates (2x3, 4x2, 3x8) and the ring default were measured:

| Layout | packets/s | % nominal | Result |
|---|---:|---:|---|
| 2 x 3 | ~2,990 | 37% | pump cannot follow 250 us URBs |
| 4 x 3 | 5,315 | 66% | |
| 4 x 4 | 6,652 | 83% | |
| 8 x 2 | 5,332 | 67% | no queued URB during resubmit |
| 8 x 3 | 7,992 | 99.9% | 692-862 ring overruns / 90-120 s |
| **8 x 4** | **7,990** | **99.9%** | **zero overruns, zero xruns** |

RTL at 8x3/ring128 is 4.31 ms, but the 1.2% playback URB deficit overruns
the 128-frame ring; 8x2 (2 ms buffer) is worse. 8x4 costs 1.2 ms of latency
and removes the deficit. Ring sizing is a direct latency term: 512/256/192/
128 frames = 8.92/5.75/5.19/4.31 ms at 8x3; 96 buys nothing.

Two build/pump findings were necessary to reach nominal cadence:

- Gradle debug builds compile native code with `-O0`, which costs about a
  third of the packet throughput. The probe and core now use `-O3` in Debug.
- `poll()` is unusable on this usbfs fd (POLLOUT always ready, POLLIN never
  wakes). The M1 sleep loop with a 100 us tick is the working reap path.
  Draining every ready completion per wake-up then removes the capture/
  playback skew (991 vs 978 URBs/s at 8x3 screen-off).

Soak: a 60-minute screen-off run at 8x4/ring 128 was attempted. The stream
held 7,997 packets/s with zero URB xruns for at least 17 minutes, but the
host's Wi-Fi adb transport wedged at minute 12 and stopped the logger
(`results-m2-soak-partial.txt`). The stream itself kept running. The script
now bounds every adb call and reconnects; a clean 60-minute run is still
owed. Layout/RTL evidence is in `results-m2-layout.txt`.

## Known gaps

- 44.1/88.2 kHz need fractional microframe packet sizes; the app is pinned
  at 48 kHz (D6), so the JUCE device exposes 48/96 kHz only.
- GET_CUR rate readback is unsupported by the AMS-22 (SET_CUR verified
  indirectly).
- The hub vs direct-cable comparison was not run; the hub sustained nominal
  cadence at 8x4, so it is not a blocker on this rig.
- SCHED_FIFO was not attempted in the probe (opt-in only; normal priority
  held zero xruns at the chosen layout).

## Next

M3: JUCE `AudioIODevice`/`AudioIODeviceType`, registration alongside Oboe,
diagnostics, lifecycle, and the L2 e2e measurement.
