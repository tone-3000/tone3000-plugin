# USB audio: low-latency custom driver

Goal: replace the Android audio stack (Oboe/AAudio) for the USB interface
with a userspace USB audio driver, and take end-to-end monitoring latency
from the current Oboe path to the 4-10 ms band on the Samsung Galaxy Tab
S10 FE + Zoom AMS-22 rig. Plan: `t3k-usb-low-latency.md` (working copy,
deliberately uncommitted).

## Status (2026-10-19)

- Driver core is host-tested (80/80) and runs on-device: 48-frame callback,
  8 packets x 4 URBs, 128-frame ring, 48 kHz. Measured **5.52 ms
  digital-loop RTL**, zero URB xruns at that layout.
- The app integrates it as the "USB Direct" driver type; Oboe remains the
  default and the fallback. Settings shows the device's reported latency,
  xruns and (USB Direct only) drift; the log gets one `[AudioSettings]`
  line per device change.
- **Android Debug builds must compile the DSP at `-O3`.** Gradle defaults to
  `-O0`; at 48-frame deadlines that alone drops ~900 capture frames/s with a
  two-model NAM chain. `plugin/CMakeLists.txt` links an interface target that
  places `-O3` after JUCE's `-g -O0`; verify `build.ninja` has it before
  trusting any performance number from a debug APK.
- **Buffer size is selectable:** 1, 2, 3 or 4 ms (48/96/144/192 frames at
  48 kHz). The period is both the URB size and the DSP callback block, so it
  trades deadline headroom for latency directly. Default 1 ms.
- Opening at 96 kHz now updates the device clock and packet cadence before
  `applyControl`; previously the config stayed at 48 kHz while the app
  requested 96 kHz.
- **A second interface (EVO8) parses and streams generically now**: UAC2
  terminal channel counts are resolved (6-in/6-out), the shell uses the
  stream's channel count, and the output pair maps onto the wire channels.
- **App startup opens the default (Oboe) device before the selected type**;
  when the kernel driver is detached that open blocks the whole launch
  (workaround: unplug the USB interface, launch, replug). Cold launch was
  2m16s before `f53bf9c` (the DSP worker pool spawned AAudio streams per
  core), 15.3 s after. Fixing the startup order (honour the remembered
  device type before `AudioDeviceManager::initialise`) is the remaining P0
  for upstream.
- **Latency verdict (2026-10-19): accepted in play.** The user runs the full
  chain through USB Direct on the tablet and reports the monitoring latency
  is "excellent". An external PC-rig end-to-end measurement was attempted
  across several sessions and dropped 2026-10-19 as unreliable (see the
  measurement-method note below); it is not part of this project's evidence.
  What remains owed from the plan: the 60-minute screen-off soak and the
  final report.

## Probe app (M1)

`android/probe` (package `com.TONE3000.USBCHECK`) is the spike vehicle:
enumerate, `dump` (descriptor tree + fixtures), `stream` (sustained duplex
with xrun/URB stats), `rtl` (self-measurement, section 4.1), `sweep` (rtl
across the URB layout matrix, plan M1.6). Driven entirely over adb:

```sh
./script/android-dev.sh probe build
./script/android-dev.sh probe install
adb shell am start -n com.TONE3000.USBCHECK/.MainActivity -e mode dump
adb logcat -s T3KUSB                    # structured one-line records
./script/android-dev.sh probe csv       # files/rtl.csv measurement rows
./script/usb-scenarios.sh doctor        # adb, USB topology, battery, power
./script/usb-scenarios.sh start         # shipping layout 8x4 + 128 ring
./script/usb-scenarios.sh status        # one-line counters
./script/usb-scenarios.sh soak 60 --screen-off
```

The probe session lives in `ProbeService` (foreground, partial wake lock), so
the stream survives the screen turning off. Commands are service intents;
the activity only owns the USB permission prompt.

The USB permission dialog needs an unlocked screen; the first launch of each
session asks it once. Claiming the AudioStreaming interfaces detaches the
kernel's `snd-usb-audio`, which is expected and required; the log records
what each interface's kernel driver was beforehand.

## Measurement method

- **L1 device self-measurement** (probe `rtl`/`sweep` modes): the AMS-22's
  digital loopback mixes playback into capture, so the probe measures our
  own buffering directly — 5.52 ms at the shipping layout. This is the
  number to optimize against.
- **L3 soak/robustness** (`script/usb-scenarios.sh soak 60 --screen-off`):
  xruns, ring overruns, drift and packet cadence over long unattended runs.

An external PC-rig end-to-end method (play a burst, record it after the
tablet path, cross-correlate against a reference cable) existed here and was
**dropped 2026-10-19**: on that rig the measurement could not be made
trustworthy (see the gotchas below), and the user's on-device verdict
("latency is excellent", 2026-10-19) is the accepted end-to-end evidence.

Device fact: **the AMS-22 feeds its input to its output at ~1 ms regardless
of its direct-monitor and loopback switches** (proven 2026-10-17: the
+1.08 ms correlation peak survives with the monitoring app force-stopped
and both switches off). Any external measurement of the device's output sees
that hardware feed alongside the app's path.

PC-rig gotchas, kept for the record (see also
`results-baseline-oboe-bypass.txt` header): the EVO8's hardware matrix is a
**mixer**, so the PipeWire channel-to-jack mapping depends on the card
profile and the evo-control mixer rows. Worse, evo-control's capture routing
can starve the USB capture of the analog inputs entirely: the 2-output/DAW
preset routes the PC's own playback back into the capture instead — a
bit-perfect unity copy with no noise floor, easy to mistake for a real path.
A real analog capture channel shows a noise floor and the preamp's gain
setting; verify that with a tone before believing any capture on that rig.

## Latency budget: current Oboe path (M0 baseline)

Measured 2026-10-17 on the Samsung Galaxy Tab S10 FE (SM-X520, Android 16,
kernel 6.6.102-android15) + Zoom AMS-22 (input gain 5/10, output 10/10),
TONE3000 debug build, chain bypassed (no NAM model), monitoring live:

```
e2e (bypass)      =  88.2 ms  [PC rig, median of 20, spread 0.02 ms]
e2e (model)       = 74-137 ms [PC rig, 20 trials, median 81.6; UNSTABLE]
converters        =   ~1.0 ms [measured: the AMS-22's raw feed peak]
app buffer        =  40.0 ms  [samplesPerBlock=1920 at 48 kHz, from app log]
android in        =  ~20 ms   [MMAP capture ring 3072 frames = 64 ms;
                               JUCE polls input from the output callback]
android out       =  ~25 ms   [MMAP playback ring 3072 frames = 64 ms]
```

With the NAM chain active the pipeline latency jitters from trial to trial
(73-137 ms across runs; the bypass path is stable to 0.02 ms). The jitter is
audible as occasional digital artifacts and is a property of the Oboe
two-stream design under DSP load, exactly what the USB-direct driver's fixed
URB cadence replaces.

Attribution notes (from `results-baseline-oboe-dumpsys.txt`):

- The app streams on AAudio MMAP (`AudioMmapIn_236` capture +
  `AudioMmapOut_50` playback, both `MMAP_NOIRQ`, 48 kHz, USB device, one
  track each, pid 24183). Not deep-buffer; the Samsung USB route does grant
  MMAP.
- HAL frame count 3072 (64 ms) per direction is the ring size, not the
  steady-state delay; the dominant term is JUCE 9's Android design (input
  and output are two separate Oboe streams, input polled with blocking read
  from the output callback) plus the 1920-frame block.
- Input stream is mono PCM_16 on the USB capture; output is PCM_8_24
  stereo. No resampler on the app path.
- Samsung effects (`SoundBoosterPlus`, `DG-Action`) are attached to other
  mixer threads, not to the app's MMAP tracks.
- App-reported JUCE `getInput/OutputLatencyInSamples` is not surfaced yet
  (needs code; deferred to M3 per plan M0.5).

Raw evidence: `results-baseline-oboe-bypass.{txt,csv}`,
`results-baseline-oboe-dumpsys.txt`, `results-baseline-oboe-model.txt`.

## Results index

| File | What |
| --- | --- |
| `results-baseline-oboe-bypass.txt/.csv` | M0 baseline, chain bypassed, PC rig |
| `results-baseline-oboe-model.txt` | M0 baseline with a NAM model loaded |
| `results-m1-stream.txt` | M1.5 soak: 14.5 min, zero xruns, detach/replug |
| `results-m1-rtl.txt` | M1.6 rtl medians + URB layout sweep |
| `results-m2-feedback.txt` | M2.1 feedback pacing smoke test |
| `results-m2-ring.txt` | M2.2 ring policy + stress |
| `results-m2-layout.txt` | M2.3/M2.4 cadence, ring, RTL, recovery |
| `results-m2-soak-partial.txt` | M2.4 17-minute continuous soak (transport wedged; 60-min run pending) |
| `m2-report.md` | M2 driver-core report |
| `m3-report.md` | M3 USB Direct integration status + blocker |
| `results-baseline-oboe-dumpsys.txt` | audio_flinger / audio_policy / logcat during streaming |
| `ams22-descriptors.md` | AMS-22 descriptor dump and entity graph (M1.3) |
| `m1-report.md` | M1 probe spike go/no-go (M1.7) |

## Supported devices

| Device | USB class | Status |
| --- | --- | --- |
| Zoom AMS-22 (VID 0x1686, PID 0x066F) | UAC2, explicit feedback | driver core complete (M2): 8x4/128, 5.52 ms digital RTL; USB Direct runs on-device at 48-frame blocks, zero xruns (M3); accepted in play 2026-10-19 ("latency is excellent") |
| Focusrite Scarlett Solo 3rd gen (VID 0x1235, PID 0x8211) | UAC2, synchronous endpoints, no feedback endpoint | runs on USB Direct (Galaxy S25, 2026-10-28) with nominal playback packets; before that fix the capture-ring PI latched at +1 frame after 40-130 s (`ringUnder` +250/s, crackle). Open: `ringOver` creeps up ~2/min (device clock not exactly SOF-locked; implicit-feedback mirroring is the follow-up), and one callback per 10 s runs 10-12 ms against a 4 ms budget at 192 frames |

Quirk registry (`plugin/usb/UsbQuirks.{h,cpp}`, keyed by VID/PID; seed
entries come from what the probe dump actually observed — never speculate):

| Device | Quirk | Value |
| --- | --- | --- |
| Zoom AMS-22 | ignore interfaces | 3 (a second AudioControl block), 4 (MIDI over bulk) |
| Zoom AMS-22 | settle delay after SET_INTERFACE | 0 ms |
| Zoom AMS-22 | force 24-in-32 subslot | no |

Observed device behavior worth knowing: no UAC2 feature unit (gain is
hardware-only — there is no playback volume/mute control to set); UAC2
GET_CUR rate readback STALLs, so verify the rate through feedback and
cadence; and it has a hardwired input-to-output monitor feed at ~1 ms
regardless of its direct-monitor and loopback switches.
