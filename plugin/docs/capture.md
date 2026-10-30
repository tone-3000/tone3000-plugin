# Standalone gear capture

The standalone app can record hardware for the TONE3000 capture workflow without a DAW. This is a local recorder and measurement tool; it does not train NAM models, upload audio, publish tones, or implement an undocumented training API. A finished NAM model becomes available in the TONE3000 player after website training and saving it to the same account.

## Setup

Choose the audio interface in System Settings. Connect the Kemper via USB audio, analog line outputs, or S/PDIF through an appropriate interface. Available channels and sample rates depend on hardware and firmware. Select **Capture gear** from the account menu. The gear selector identifies Head/PowerHead, Rack/PowerRack, Stage or Player; it is not device detection.

The capture recorder temporarily suspends the standalone player. Its raw callback receives physical input channels before the plugin's gate, EQ, NAM/IR blocks, input/output gains and monitoring. On stop/cancel/close it removes its callback and restores the prior audio-device setup and player. Capture settings are independent of DAW state, presets and host parameters. Capture controls are absent in hosted formats.

Kemper documents USB audio at 44.1kHz on current devices. The recorder prefers 48kHz if the driver supports it, otherwise it records the native device rate and converts both tracks with one delay-compensated windowed-sinc algorithm before export. Exports are mono 24-bit 48kHz WAV. NAM test signals are converted to the native playback rate; original source bytes are preserved as REFERENCE.wav. Conversion and native rate are recorded in session.json.

## Capture methods

- **Dry/wet pair:** split the guitar before the Kemper, recording clean DI and the processed return together from the same input stream. Kemper USB DI/processed channels can also be selected if configured on the device. Play varied notes/chords/dynamics for 2–3 minutes. Export DRY.wav and WET.wav from the same take; do not independently normalize or trim them. Hardware latency and the capture's dynamics should be checked in training results.
- **NAM test signal:** download the current original mono 48kHz test WAV from TONE3000. Import it, choose the hardware send and processed-return input, and start. The app records 500ms of pre-roll and 4 seconds of tail. Correlation estimates the return offset; weak alignment requires a checked manual offset. WET.wav has the original test signal's exact frame count. RETURN-RAW.wav preserves the full return; REFERENCE.wav is the frozen original file, even if the source file was later moved/deleted.
- **Impulse response:** the app generates a 20 Hz–20 kHz log sine sweep, records the return and deconvolves a linear IR. For a Kemper cabinet response, bypass the amp and moving/time-based effects on the device yourself. IR.wav cannot reproduce distortion, compression or a whole Kemper profile. Arrival delay is removed using a 25%-of-peak onset threshold, weak early arrivals can be omitted, and IR peak is normalized to 0.9. The original sweep and return are retained.

Use **Check input levels** before capturing. Keep the signal below clipping. Route test output only to the hardware input/reamp path, avoiding a return-to-send feedback loop. Capture files are written to a new session folder, with JSON settings and handoff instructions. No existing session is overwritten.

## Kemper MIDI

Select a MIDI output port and the Kemper's MIDI channel. Connect sends no settings. The **Apply** button sends requested Generic Gain, generic amp EQ and cabinet enable values; it never stores a rig. Program 1–128 maps to raw MIDI Program Change 0–127 and depends on Kemper mode/bank/assignments. Rig names are entered manually. A successful MIDI send is not hardware acknowledgement or parameter readback.

The documented NRPN mappings are Generic Gain 10/4, amp EQ 11/4–7, cabinet switch 12/2. Switches use integer 0/1, not continuous full scale. Liquid Amp Model Gain is a different parameter and is not controlled by the Generic Gain slider. The published MIDI parameter list does not expose Main/Monitor/Direct Output Source selectors; configure those on the Kemper/Rig Manager unless a supported control path is established.

## Website handoff

Open https://www.tone3000.com/capture after export. Upload DRY.wav + WET.wav as Dry/Wet Pair, or WET.wav with the matching official test-signal method. TONE3000 trains the NAM model; use the same account in its app to load the saved tone. IR.wav is already a finished impulse response and needs no NAM training.

TONE3000's public sharing policy explicitly excludes captures of current modelers, including Kemper. Local recording/export is separate from permission to upload or share a capture. Follow applicable TONE3000 and source-content terms.

## Verification

Deterministic tests cover known FIR recovery, alignment under delay/polarity/distortion, clipping/invalid-data rejection,44.1 ↔ 48kHz conversion, exact PCM24 WAV length/format and frozen-reference export. MIDI tests verify bytes without transmitting to hardware. Physical I/O, actual Kemper response and a real website training result require hardware smoke tests; a green software suite is not that evidence.

References: [TONE3000 dry/wet](https://www.tone3000.com/guides/capture-your-gear-with-nam-dry-wet), [NAM test-signal capture](https://www.tone3000.com/guides/capture-your-gear-with-nam-sweep-method), [sharing rules](https://www.tone3000.com/guides/tone-sharing-guidelines), [Kemper specifications](https://www.kemper-amps.com/profiler/technical-specifications), [Kemper MIDI support](https://www.kemper-amps.com/faqs).
