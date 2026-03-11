# TONE3000 Plugin

A **JUCE-based VST3/AU plugin** that lets you load **Neural Amp Modeler (NAM)** profiles and **Impulse Responses (IR)** directly from [TONE3000](https://www.tone3000.com) using the [TONE3000 Select flow](https://www.tone3000.com/api#select). No need to download files manually—browse, sign in, and add tones from the TONE3000 catalog straight into your plugin chain.

## What it does

- **Load NAM and IR from TONE3000** — Click the **+** icon to open the TONE3000 Select flow in an in-plugin window. Sign in with your TONE3000 account, pick a tone, and it’s added to your chain with the correct model/IR.
- **Build a signal chain** — Add multiple NAM and IR blocks, reorder them, switch between models per tone, and remove blocks.
- **Cross-platform** — Same plugin on macOS, Windows, and Linux (desktop). UI is a React app served in a native WebView (WebView2 on Windows, WebKit elsewhere).

The plugin uses **NeuralAmpModelerCore** (in-tree) for NAM processing, **AudioDSPTools** (in-tree) for resampling, and the [TONE3000 Select API](https://www.tone3000.com/api#select) for browsing and loading tones.

---

## Prerequisites

- **Build** [CMake](https://cmake.org/download/) 3.22+, [Git](https://git-scm.com/).
- **JUCE** — Fetched automatically via CMake (no manual install).
- **Node.js & npm** — For building the React UI (see [Build the UI](#1-build-the-ui-first)).
- **Windows only:** [WebView2](https://developer.microsoft.com/en-us/microsoft-edge/webview2/) (e.g. via `script/install-webview2.ps1`).

---

## Quick start

### 1. Build the UI first

The plugin embeds a built React UI. From the repo root:

```sh
cd ui
npm install
npm run build
cd ..
```

Do this on all platforms (Linux, macOS, Windows) before building the C++ plugin.

### 2. Get submodules

```sh
git submodule update --init --recursive
```

### 3. Configure and build

```sh
# Debug
cmake -B build -S . -DHEADLESS=OFF -DCMAKE_BUILD_TYPE=Debug

# Release
cmake -B build -S . -DHEADLESS=OFF -DCMAKE_BUILD_TYPE=Release
```

**Linux:** use the project’s Linux toolchain, for example:

```sh
cmake -B build -S . -DHEADLESS=OFF -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/linux-toolchain.cmake
```

Then:

```sh
cmake --build build
```

**Note:** Pick a CMake preset before the first build; if you switch presets later, you may need to remove the `build` directory and reconfigure.

### 4. Run the plugin

**Standalone (double-click / open app)**

```sh
# Debug
cd build/plugin/TONE3000_artefacts/Debug/Standalone

# Release
cd build/plugin/TONE3000_artefacts/Release/Standalone
```

- **macOS:** `open ./TONE3000.app`
- **Linux:** `chmod +x TONE3000 && ./TONE3000`
- **Windows (PowerShell):** `./TONE3000.exe`

**Run Debug with logs in terminal**

To see `DBG()` and other log output, run the standalone **binary** directly so stdout/stderr go to your terminal:

- **macOS:**
  ```sh
  ./build/plugin/TONE3000_artefacts/Debug/Standalone/TONE3000.app/Contents/MacOS/TONE3000
  ```
- **Linux:** from repo root,
  ```sh
  ./build/plugin/TONE3000_artefacts/Debug/Standalone/TONE3000
  ```
- **Windows (PowerShell):** from repo root,
  ```powershell
  .\build\plugin\TONE3000_artefacts\Debug\Standalone\TONE3000.exe
  ```

**In a DAW**

Copy the built plugin into your system plugin folder (or use [script/](#installing-plugins-with-script)):

| OS | Plugin | Copy to |
|----|--------|--------|
| **macOS** | VST3 | `~/Library/Audio/Plug-Ins/VST3/` |
| **macOS** | AU | `~/Library/Audio/Plug-Ins/Components/` |
| **Windows** | VST3 | `C:\Program Files\Common Files\VST3\` |
| **Linux** | VST3 | `~/.vst3/` |

Built artefacts: **Debug** → `build/plugin/TONE3000_artefacts/Debug/VST3/TONE3000.vst3` (or `AU/TONE3000.component` on macOS). **Release** → same path with `Release` instead of `Debug`. Then rescan plugins in your DAW.

---

## Installing plugins with script

From the repo root, run **`./script/cp-VST3.sh`** or **`./script/cp-AU.sh`** (macOS only) to copy the built plugin into your system plugin folder. Add `Debug` to install the Debug build: `./script/cp-VST3.sh Debug`. Then rescan plugins in your DAW. Other scripts: `create-dmg.sh` (macOS disk image), `install-webview2.ps1` (Windows WebView2, run once).

---

## Audio processing setup

The plugin is a JUCE processor with a chain of NAM and IR blocks. Audio runs through the chain (with resampling for NAM when the host sample rate differs from the model), then DC blocker and tone EQ. **NeuralAmpModelerCore** (in `plugin/NeuralAmpModelerCore/`) provides the NAM engine; **AudioDSPTools** (in `plugin/AudioDSPTools/`) provides resampling so NAM runs at its native rate.

---

## Windows: WebView2

On Windows, the UI uses WebView2. Install it once (e.g. from the repo):

```powershell
./script/install-webview2.ps1
```

---

## Licensing and JUCE

This project is licensed under the **MIT License** (see [LICENSE](LICENSE)).

It uses the **JUCE** framework, which has its own licensing (including optional commercial terms). See [JUCE Licensing](https://juce.com/juce-6-licence) for details. **AudioDSPTools**’ ResamplingContainer is from the iPlug2 project (see its license in that source). **NeuralAmpModelerCore** has its own license terms in that directory.

---

## Links

- [TONE3000](https://www.tone3000.com) — NAM captures and IRs.
- [TONE3000 API — Select](https://www.tone3000.com/api#select) — Select flow and parameters.
- [Neural Amp Modeler](https://github.com/sdatkinson/neural-amp-modeler) — Original NAM project (plugin uses in-tree **NeuralAmpModelerCore**).
