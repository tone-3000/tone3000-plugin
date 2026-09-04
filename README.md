# TONE3000 Plugin

A JUCE-based audio plugin (VST3, AU, CLAP, LV2, Standalone) that loads
**Neural Amp Modeler (NAM)** captures and **impulse responses (IRs)** straight
from [TONE3000](https://www.tone3000.com). No manual file downloads: browse
the catalog, sign in, and add tones directly into your signal chain.

[Download the plugin](https://www.tone3000.com/plugin/download) for a
pre-built installer, or see the
[Plugin Guide](https://www.tone3000.com/guides/tone3000-plugin) for how to
install, load tones, and use it.

- **Load NAM and IR from TONE3000.** Click **+** to browse the catalog in the
  plugin (OAuth 2.0 + PKCE via the
  [TONE3000 Select flow](https://www.tone3000.com/api#select)). Pick a tone
  and it lands in the chain with the right model or IR.
- **Or load local files.** Drag a `.nam` file (A2 architecture), an IR
  `.wav`, or a folder of them onto a **+** slot, or right-click a tile and
  pick **Load File / Load Folder**; no account needed. Design notes in
  [`plugin/docs/local-models.md`](plugin/docs/local-models.md).
- **Build a signal chain.** Multiple NAM and IR blocks, per-block EQ and
  gain/mix, drag to reorder, dual chains in stereo mode with branching,
  undo/redo, and presets.
- **Cross-platform.** One plugin on macOS, Windows, and Linux. The UI is a
  React app rendered in a native WebView (WebView2 on Windows, WebKit
  elsewhere).

NAM processing comes from **NeuralAmpModelerCore** (in-tree), resampling from
**AudioDSPTools** (in-tree), and tone browsing/loading from the
[TONE3000 API](https://www.tone3000.com/api).

## Prerequisites

- [CMake](https://cmake.org/download/) 3.22+ and Git
- Node.js and npm (the React UI is built after CMake has fetched JUCE)
- **JUCE** is fetched automatically by CMake into `libs/`; no manual install
- **Windows only:** the Microsoft.Web.WebView2 SDK NuGet package
  (`script/install-webview2.ps1` installs it), needed at build time to
  statically link the WebView2 loader. At run time the plugin UI needs the
  [WebView2 Evergreen Runtime](https://developer.microsoft.com/en-us/microsoft-edge/webview2/):
  Windows 11 ships it, dev machines get it with Edge, and the release
  installer bootstraps it when missing (typically clean Windows 10).

## Quick start

### 1. Get submodules

```sh
git submodule update --init --recursive
```

### 2. Configure CMake

CMake downloads JUCE into `libs/` on first configure. The UI's
`@juce-framework/webview` package is a `file:` dependency on that tree, so
this step has to happen **before** `npm install`. Configure uses a
placeholder for the embedded UI until you build it in the next step.

The default build includes the GUI targets (Standalone, VST3, AU, AAX, LV2,
CLAP). Add `-DHEADLESS=ON` for headless/embedded builds; switch individual
formats off with `-DBUILD_AAX=OFF`, `-DBUILD_LV2=OFF`, `-DBUILD_CLAP=OFF`.
CLAP support comes from
[clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions),
fetched at configure time.

```sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release   # or Debug
```

**Linux:** use the project's toolchain file:

```sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/linux-toolchain.cmake
```

If you switch CMake presets later, remove the `build` directory and
reconfigure.

### 3. Build the UI

The plugin embeds the built React UI as binary data:

```sh
cd ui
npm install
npm run build
cd ..
```

If `npm install` warns that esbuild's install script is not approved
(`npm warn install-scripts ... esbuild`), run
`npm install-scripts approve esbuild` and then `npm install` again. Vite
needs that postinstall to download the esbuild binary.

#### TONE3000 publishable key and redirect URIs

The webview reads your TONE3000 publishable key at build time. Set it before
the first build (or before running the dev server):

```sh
# ui/.env (or pass on the command line for a single build)
VITE_T3K_PUBLISHABLE_KEY=t3k_pub_your_key_here
# Optional: point at staging or self-hosted TONE3000
# VITE_T3K_API_DOMAIN=https://staging.tone3000.com
```

Then, in TONE3000 > Settings > API Keys, register the redirect URIs the
WebView uses. The OAuth flows run in the same single WebView that serves the
main UI, so the redirect URI is just the page React already loads from:

| Build         | Redirect URI                      |
| ------------- | --------------------------------- |
| Vite dev      | `http://localhost:5173/`          |
| macOS / Linux | `juce://juce.backend/index.html`  |
| Windows       | `https://juce.backend/index.html` |

Localhost origins are auto-allowed during development, so only the JUCE
entries need to be registered for release builds.

### 4. Build the plugin

Re-run the same `cmake -B build ...` command from step 2 so CMake picks up
`plugin/webview/`, then compile:

```sh
cmake --build build
```

### 5. Run it

**Standalone:**

```sh
cd build/plugin/TONE3000_artefacts/Release/Standalone   # or Debug
```

- macOS: `open ./TONE3000.app`
- Linux: `./TONE3000`
- Windows (PowerShell): `./TONE3000.exe`

To see `DBG()` output in Debug builds, run the binary directly so
stdout/stderr reach your terminal (on macOS that is
`TONE3000.app/Contents/MacOS/TONE3000`).

**In a DAW:** copy the built plugin to your user plugin folder and rescan.
`./script/install-plugin.sh VST3` (or `AU` / `AAX`) does the copy on macOS
and Linux; pass `Debug` as the second argument for the Debug build. Artefacts
land in `build/plugin/TONE3000_artefacts/<config>/<format>/`.

| OS      | Format | Install to                                              |
| ------- | ------ | ------------------------------------------------------- |
| macOS   | VST3   | `~/Library/Audio/Plug-Ins/VST3/`                        |
| macOS   | AU     | `~/Library/Audio/Plug-Ins/Components/`                  |
| macOS   | AAX    | `/Library/Application Support/Avid/Audio/Plug-Ins/` |
| macOS   | CLAP   | `~/Library/Audio/Plug-Ins/CLAP/`                        |
| Windows | VST3   | `C:\Program Files\Common Files\VST3\`   |
| Windows | CLAP   | `C:\Program Files\Common Files\CLAP\`   |
| Linux   | VST3   | `~/.vst3/`                              |
| Linux   | LV2    | `~/.lv2/`                               |
| Linux   | CLAP   | `~/.clap/`                              |

## Linux runtime dependencies

Windows statically links only the WebView2 loader (the Evergreen Runtime is
a system component; the installer bootstraps it when missing) and macOS uses
the OS WKWebView, but the Linux build renders its UI in the system WebKitGTK,
loaded dynamically at runtime. If it's missing, the plugin window is a black
screen.

Required: WebKitGTK 4.1 (or 4.0), GTK3, ALSA, FreeType.

```sh
sudo apt install libwebkit2gtk-4.1-0      # Ubuntu / Debian
sudo dnf install webkit2gtk4.1            # Fedora
sudo pacman -S webkit2gtk-4.1             # Arch
sudo zypper install libwebkit2gtk-4_1-0   # openSUSE
```

The release tarball's `install.sh` checks for these automatically
(`./install.sh --check` to verify without installing).

## Audio processing

The plugin is a JUCE processor running a chain of NAM and IR blocks, anchored
at 48 kHz (a Lanczos resampler wraps the chain when the host rate differs,
bypassed at 48 kHz).

### Signal flow

The full path in processing order (`TONE3000Processor::processBlock` in
`plugin/src/Processor.cpp`). Stages marked `*` are bypassable or conditional:

```mermaid
flowchart LR
    IN([In]) --> IM["Input Mode *\n(stereo / L / R)"]
    IM --> IG["Input Level"]
    IG --> GATE["Noise Gate *"]
    GATE --> RS(("⇅ 48k"))
    RS --> OS(("×N ↑ *"))
    subgraph CHAINS["Tone chains, 48 kHz × oversampling factor"]
        direction LR
        CL["Left chain\n(NAM / IR blocks)"]
        CR["Right chain\n(stereo mode only)"]
    end
    OS --> CL
    OS --> CR
    CL --> OS2(("×N ↓ *"))
    CR --> OS2
    OS2 --> RS2(("⇅ 48k"))
    RS2 --> IMAGE["Spread * (mono) /\nAlign * (stereo)"]
    IMAGE --> PAN["Balance + Pan *\n(per-chain trim, then\nconstant-power blend)"]
    PAN --> DCB["DC Blocker\n(~5 Hz HPF)"]
    DCB --> TS["Tone Stack *"]
    TS --> OG["Output Level"]
    OG --> OUT([Out])
```

- **Input mode**: when a real stereo source feeds the plugin, a faceplate
  button picks what enters the chain: both channels (default) or one channel
  mirrored onto both. Saved with the session, not with presets; it's I/O
  routing, not tone.
- **Mono mode**: only the Left chain runs and the pan stage is skipped. With
  Spread on, the chain output becomes an ADT-style stereo double; see
  [`plugin/docs/stereo-image.md`](plugin/docs/stereo-image.md) for the design
  (it also covers the stereo-mode Align feature below, and what happens on a
  rig that can't reproduce stereo at all: Spread stays idle and greyed out,
  while stereo chains keep running and are summed to mono).
- **Stereo mode**: channel 0 feeds the Left chain and channel 1 the Right
  chain independently. The Balance trim scales each chain (12 dB opposing)
  before the pan knobs place them with a constant-power law, so a balance
  dialed in to match the chains stays correct at any pan position; each pan
  knob carries a solo that auditions its chain alone (exclusive: engaging
  one clears the other; the mute rides the same smoothed matrix, so it
  never clicks, and stays out of presets) and a polarity flip (Ø) for
  captures that land 180° out (the sign rides the same matrix smoothers, so
  flips glide through zero instead of clicking). Align applies a corrective
  alignment delay (up to 24 ms, sub-sample precise) to one chain, useful when
  NAM models or IRs carry different baked-in latency; the auto-align button
  mutes the output for under half a second, drives both chains with an
  identical internal sweep, and measures the lag and relative polarity from
  the cross-correlation (`plugin/include/AutoOffset.h`). On a rig that can't
  reproduce stereo (mono track, one-channel output device) both chains still
  run and are summed to mono at the output (½(L+R), the host's own mono-fold
  law), with balance/solo/Ø live inside the sum, pans inert, and a MONO chip
  on the pan rail (see the stereo-image doc above).
- **Tone stack**: one global Bass/Middle/Treble EQ after the DC blocker,
  voiced to match the reference
  [NeuralAmpModelerPlugin](https://github.com/sdatkinson/NeuralAmpModelerPlugin)
  tone stack (150 Hz / 425 Hz / 1.8 kHz, ±20 / ±15 / ±10 dB).
- **Oversampling**: a Plugin Settings option runs the whole chain at 2x/4x/8x the
  48 kHz base rate: minimum-phase half-band filters (zero added latency),
  with NAM models phase-interleaved across N native-rate instances so
  harmonics land in the widened band instead of folding back as aliasing.
  IR blocks are the exception: convolution is linear, so each IR convolves
  at the 48 kHz base rate inside a per-block decimate/interpolate island,
  and IR CPU and sound are identical at every factor. Design notes in
  [`plugin/docs/oversampling.md`](plugin/docs/oversampling.md).
- **Multi-core processing**: a Plugin Settings option (on by default,
  machine-wide) spreads independent chain work across a realtime worker
  pool. The two stereo chains process concurrently (the Right chain, or the
  branch lane when branched, on a worker while the audio thread processes
  the other), and an oversampled NAM model's phase instances fork across
  cores too. The forking thread can always steal jobs back and run them
  inline, so the toggle is pure scheduling and the output is bit-identical
  either way (pinned by `test/src/multicore_tests.cpp`). Design notes in
  [`plugin/docs/multicore.md`](plugin/docs/multicore.md).

Inside every tone block:

```mermaid
flowchart LR
    BIN([block in]) --> BIG["In Gain\n±24 dB"]
    BIN -. dry .-> MIX
    BIG --> PEQ["6-band EQ *\n(PRE position)"]
    PEQ --> MODEL["NAM model / IR\n(+ calibration or\nloudness normalize)"]
    MODEL --> BOG["Out Gain\n±24 dB"]
    BOG --> MIX["Dry/Wet Mix"]
    MIX --> BEQ["6-band EQ *\n(POST position)"]
    BEQ --> BOUT([block out])
```

Each block's 6-band EQ runs in exactly one position: after the dry/wet mix
(POST, the default) or between In Gain and the model (PRE), never both. A
flat or bypassed EQ costs nothing on the audio thread.

Meters tap the signal after input gain (input meters, pre-gate), after each
block's In Gain plus its PRE-position EQ and after its final stage (block
LEDs), and after output gain (output meters).

### DSP tests

A GoogleTest suite pins the chain's DSP invariants against the real model and
IR assets in `test/files`:

- `dsp_tests.cpp`: unit-level behavior (oversampler null/transparency/
  aliasing, NAM phase-interleaving exactness, IR island equivalence).
- `processor_tests.cpp`: drives the full `TONE3000Processor` the way a host
  would (48 kHz transparency with zero latency, reported PDC matching the
  measured delay at 44.1/96 kHz, latency stability across oversampling
  toggles, state round trips).
- `multicore_tests.cpp`: parallel stereo output is bit-identical to serial,
  across topologies, host rates, and oversampling factors.
- `spread_tests.cpp`, `swap_fade_tests.cpp`, `branch_tests.cpp`, and friends
  cover the doubler, engine-swap fades, and chain routing.

```sh
./script/test-dsp.sh                        # build + run everything
./script/test-dsp.sh 'IrConvolutionTest.*'  # gtest filter
```

`test/src/os_bench.cpp` is a standalone CPU benchmark for the oversampled NAM
path (build instructions in its header).

### Plugin validation

`./script/validate-plugin.sh` runs each format's official validator against
the built artefacts: [pluginval](https://github.com/Tracktion/pluginval) at
strictness 10 for VST3 and AU (including Apple's `auval`),
[clap-validator](https://github.com/free-audio/clap-validator) for CLAP, and
`lv2lint` for LV2 where available (Linux). AAX (needs Avid's DSH harness and
PACE signing) and Standalone (not a hosted plugin) are skipped. Install
validators with `brew install --cask pluginval` and by dropping a
clap-validator release binary on PATH or in `build/tools/`. Pass a format
and/or build type to narrow the run, e.g. `./script/validate-plugin.sh AU
Debug`.

## Repository layout

| Path            | Contents                                              |
| --------------- | ----------------------------------------------------- |
| `plugin/`       | C++ plugin: processor, DSP, editor, webview bridge; vendors NeuralAmpModelerCore and AudioDSPTools |
| `plugin/docs/`  | Design docs (spread, oversampling, multi-core, local models) |
| `ui/`           | React/TypeScript UI (see [ui/README.md](ui/README.md))|
| `test/`         | GoogleTest DSP suite + test assets                    |
| `script/`       | Build, packaging, and install helpers                 |
| `libs/`         | CPM-fetched dependencies (JUCE, GoogleTest, ...)      |
| `design/`       | Figma exports and UI reference assets                 |

## Licensing

This project is licensed under the **MIT License** (see [LICENSE](LICENSE)).

JUCE has its own licensing, including optional commercial terms; see
[JUCE Licensing](https://juce.com/legal/juce-9-licence/). **NeuralAmpModelerCore**
carries its own license terms in its directory. **AudioDSPTools**'
`ResamplingContainer` originates from the iPlug2 project (license in that
source). The CLAP build uses **clap-juce-extensions** and the **CLAP** SDK
(both MIT), fetched at configure time.

## Credits

- [Neural Amp Modeler](https://github.com/sdatkinson/neural-amp-modeler) by
  Steven Atkinson: the NAM ecosystem and
  [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore),
  which powers all amp modeling here.
- [NeuralAmpModelerPlugin](https://github.com/sdatkinson/NeuralAmpModelerPlugin):
  the reference NAM plugin; the faceplate tone stack borrows its band voicing
  and the DC blocker matches its behavior.
- [AudioDSPTools](https://github.com/sdatkinson/AudioDSPTools): resampling
  around the 48 kHz chain boundary, with `ResamplingContainer` from
  [iPlug2](https://github.com/iPlug2/iPlug2).
- [NAM-Oversampler](https://github.com/DLC86/NAM-Oversampler) by DLC86:
  oversampled NAM processing; the chain oversampler's half-band
  allpass coefficients are adapted from its AudioDSPTools fork (MIT). See
  [`plugin/docs/oversampling.md`](plugin/docs/oversampling.md).
- [JUCE](https://juce.com): plugin framework, DSP building blocks, and the
  WebView UI bridge.
- [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions):
  the CLAP wrapper.
- O. Das, ["An Open-Source Stereo Widening Plugin"](https://www.dafx.de/paper-archive/2024/papers/DAFx24_paper_92.pdf)
  (DAFx24): the allpass decorrelation approach used by the spread doubler.
- A. Farina, ["Simultaneous Measurement of Impulse Response and Distortion
  with a Swept-Sine Technique"](https://www.melaudia.net/zdoc/sweepSine.PDF)
  (108th AES Convention, 2000) and C. Knapp & G. Carter, "The Generalized
  Correlation Method for Estimation of Time Delay" (IEEE TASSP, 1976): the
  sweep probe and GCC-PHAT estimator behind auto-align.
- [dnd-kit](https://dndkit.com), [lucide](https://lucide.dev), and
  [react-knob-headless](https://github.com/satelllte/react-knob-headless) in
  the UI.

## Links

- [TONE3000](https://www.tone3000.com): NAM captures and IRs.
- [Download the plugin](https://www.tone3000.com/plugin/download): pre-built
  installers for Mac, Windows, and Linux.
- [Plugin Guide](https://www.tone3000.com/guides/tone3000-plugin): how to
  install, load tones, and use the plugin.
- [TONE3000 API](https://www.tone3000.com/api): full API reference, including
  the Select flow.
- [TONE3000 API examples](https://github.com/tone-3000/api): reference
  integrations, including the `tone3000-client.ts` this plugin's client is
  adapted from.
