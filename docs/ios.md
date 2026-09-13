# iOS (iPad) build

Standalone-only iPad port of the plugin: the same C++ and the same React UI,
with every difference gated. `#if JUCE_IOS` covers the C++. The UI gates on
three levels (see Touch adaptation below): `IS_IOS` / `html.t3k-ios` for the
app shell, `IS_COARSE_POINTER` / `html.t3k-touch` for touch-first ergonomics
on any device, and each event's own `pointerType` for behaviors. Desktop
behaviour is unchanged. AUv3 is out of scope; iPhone is untested.

Deployment target iOS 16. Landscape only.

## Build

Configure first, then build the UI, then configure again. `ui/package.json`
resolves `@juce-framework/webview` from `libs/juce`, which the first configure
is what creates, so building the UI first on a clean checkout fails with
`Cannot find module '@juce-framework/webview'`. That first configure embeds a
placeholder UI; the second picks up the real bundle. Same order as the root
README and the `iOS Simulator` CI job.

```sh
cmake --preset ios-simulator   # or ios-device: bootstrap, fetches JUCE into libs/
cd ui && npm ci && npm run build && cd ..

# Simulator
cmake --preset ios-simulator
cmake --build build-ios --config Release --target TONE3000_Standalone -- -sdk iphonesimulator

# Device
cmake --preset ios-device
cmake --build build-ios-device --config Release --target TONE3000_Standalone -- \
  -sdk iphoneos -allowProvisioningUpdates
```

The **Build Plugin** workflow (`.github/workflows/build.yml`) has an
`iOS Simulator` job that runs the same Simulator build on a macOS runner and
uploads the unsigned `.app` as an artifact.

Build **Release** on the Simulator. A Debug iOS build points the WebView at
`http://localhost:5173/`, so it shows a dead page and logs "navigation failed".

`-DT3K_IOS_BUNDLE_ID=<id>` signs under your own identity. Changing it on a
device that already holds the app gives a fresh, empty Documents folder, so
keep it stable once models are loaded. Add
`-DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=<id>` if Xcode cannot pick your team.

**Reconfigure after every UI change.** `plugin/CMakeLists.txt` collects the
webview with `file(GLOB_RECURSE)`, which runs at configure time, and Vite's
asset filenames are content-hashed. Without a reconfigure the app keeps
serving the previously embedded bundle and looks like your change did nothing.
The requested asset name in the app's log tells you which bundle is running.

## Install and log

Each preset writes to its own build directory: `build-ios` for
`ios-simulator`, `build-ios-device` for `ios-device`. The artefact path under
each is the same.

```sh
# Simulator
xcrun simctl install <udid> build-ios/plugin/TONE3000_artefacts/Release/Standalone/TONE3000.app
xcrun simctl launch <udid> <bundle-id>

# Device
xcrun devicectl device install app --device <udid> \
  build-ios-device/plugin/TONE3000_artefacts/Release/Standalone/TONE3000.app

# The app's own log: console.* from the WebView is forwarded into it, which
# is the most useful debugging channel on both Simulator and device.
tail -f "$(xcrun simctl get_app_container <udid> <bundle-id> data)/Library/TONE3000/TONE3000.log"
```

Xcode's Devices and Simulators window installs either build too, if you
prefer it to the command line.

Simulator screenshots come out portrait while the app renders landscape.

## TestFlight and the App Store

A signed build is the `ios-device` preset plus your team. Everything below is
what App Store Connect checks on top of that, and none of it shows up in a
Simulator build.

- `plugin/PrivacyInfo.xcprivacy` declares the required-reason APIs the binary
  reaches through JUCE: user defaults (the WebView component), file timestamps
  and free disk space (both `juce_SharedCode_posix.h`), and system boot time
  (`systemUptime` timestamping touch events in
  `juce_UIViewComponentPeer_ios.mm`, plus `mach_absolute_time`). An upload whose
  binary calls one of those without declaring it is rejected with ITMS-91053,
  so the list is worth re-deriving whenever the JUCE version moves.
- The app icons are flattened to opaque at configure time by
  `script/flatten-icon-alpha.swift`, run through `xcrun swift`. juceaide writes
  them RGBA whatever the source is, and an alpha channel on the 1024 icon means
  ITMS-90717 and no icon in TestFlight. It uses ImageIO rather than a tool from
  a package manager because every machine that can build this target already
  has both, the GitHub macOS runner included.
- `ITSAppUsesNonExemptEncryption` is false in the Info.plist. The app's only
  encryption is standard HTTPS, and declaring it here answers the
  export-compliance question once instead of on every upload.
- `UIRequiresFullScreen` is true. A landscape-only iPad app must either list
  all four orientations or declare itself full-screen; without the key the
  upload is refused with ITMS-90474. It costs nothing at runtime on
  iPadOS 26 (see the multitasking note below).
- `plugin/icon/icon.png` is 512x512 and juceaide never enlarges a source, so the
  App Store icon is currently that 512 artwork centred on a blank 1024 field.
  Exporting the icon at 1024 fixes it; the configure step warns until then.
- `T3K_IOS_BUILD_NUMBER` is CFBundleVersion, and it defaults to 1. App Store
  Connect refuses a build number it has already seen for the same marketing
  version, so a second upload of one version needs
  `-DT3K_IOS_BUILD_NUMBER=<n>`. Without the setting at all, JUCE uses the
  marketing version as the build number and the second upload always bounces.
- The **Deploy to TestFlight** workflow
  (`.github/workflows/deploy-testflight.yml`) does the upload: publishing a
  GitHub Release builds, signs and uploads, and `workflow_dispatch` runs the
  same pipeline against any ref. Signing and upload both use an App Store
  Connect API key from the `builds` environment, so no keychain or stored
  profile is involved, and the build number is the workflow run number. A
  repository without those credentials skips the job instead of failing.
- App Store Connect requires uploads built against a current iOS SDK. A runner
  pinned to an older Xcode builds and signs fine and is then refused at upload,
  which reads as a signing problem and is not one.

## Touch adaptation

The adaptation is deliberately minimal: the desktop UI at the desktop aspect,
letterboxed and vertically centered, with only the touch-ups a finger needs.
Three gates, from narrowest reach to widest:

- `IS_IOS` / `html.t3k-ios`: the app shell only. The document-scroll fix and
  the vertical centering (both in `index.css`), and the long-press
  recognizers that stand in for `contextmenu`, which WKWebView never fires
  for a touch hold (`useTileMenu` in GalleryBlock, `useTouchHold`). Every
  other engine fires the native event and takes the desktop `onContextMenu`
  path.
- `IS_COARSE_POINTER` / `html.t3k-touch` (`pointer: coarse`, see useUiScale):
  static ergonomics for any touch-first device, iPad or Android or Windows
  tablet. The 44 pt hit floor, the touch-field growth, the touch help copy,
  and render-time nudges that follow them.
- `pointerType === 'touch'` per event: behaviors (the knob double tap and
  label tap, the help-bar release). A hybrid device gets touch behavior from
  its touchscreen and desktop behavior from its mouse.

| gesture | result |
| ------- | ------ |
| tap a tile | open the block |
| drag a tile | reorder (the same distance rule as desktop) |
| hold a tile 500 ms | tile menu, while the finger is still down |
| hold the Spread / Align Offset knob | the advanced deck (desktop: right-click the group) |
| press a control | its help in the info bar; release clears it |
| drag a knob up or down | adjust |
| double tap a knob, EQ fader or EQ dot | reset to default |
| tap a knob's label | type the value |

The tile face claims the gesture for dragging (`touch-action: none`, as on
desktop), so lanes scroll from the space around the tiles, not across them.
The info bar teaches each control's touch gestures: the help copy branches on
`IS_COARSE_POINTER` in helpText.ts.

Every touch target meets 44 pt through one rule in `index.css` under
`html.t3k-touch`: an invisible `::after` at `max(100%, 44px)`, centred and out
of flow, so no layout changes.

Local import is desktop's two rows, **Load File** and **Load Folder**, in the
tile menus. On iOS the folder row opens the platform's multi-select file
picker instead (see Known gaps).

## Touch verification

An earlier, larger revision of this branch was driven end to end on the iPad
Simulator and on an iPad Pro (presets, tuner, undo/redo, mono/stereo, EQ,
Spread/Align decks, block swap/remove, keyboard avoidance). After the
slim-down the Simulator build was smoke-checked; the gesture set above needs
one hardware pass: long-press menu, drag reorder, knob double tap and label
tap, the centered layout, and no document scroll.

## Platform notes worth knowing

- **Picker results must be read through security-scoped URLs.** A file chosen
  outside the app container is unreadable through its raw path. A test with
  the file *inside* the container passes and proves nothing.
- **WebKit replays a mouse event pair after every touch**, aimed at the
  element just tapped and landing after `pointerup`. Anything that clears
  state on release has to ignore that replay (see helpText.ts).
- **A control that takes pointer capture retargets its release**, so a release
  that must be seen regardless is watched on `window` in the capture phase.
- **Pressing and holding an `<img>` raises WKWebView's own image callout**
  (Copy / Save to Photos) and cancels the pointer stream under it, which
  silently killed the tiles' long-press menu on any tile with artwork.
  `-webkit-touch-callout: none` on img/svg (index.css) suppresses it.
- **`env(safe-area-inset-*)` is 0 on all sides** here: the WKWebView is
  already inset (1366x999 in a 1024 pt screen), so the faceplate clears the
  home indicator without the page doing anything.
- **`100vh` is not the viewport, and the document scrolled because of it.**
  An unwanted vertical scroll that hurt navigation was reported. It was
  real and it was global. This WKWebView lays out in a 1366x999 box, but
  `100vh`, `100dvh`, `innerHeight` and `visualViewport.height` all report 1024,
  the screen height: measured in the running app,
  `documentElement.clientHeight` was 999 against a `scrollHeight` of 1024. So
  `#root { height: 100vh }` built a root 25 px taller than the box holding it,
  html and body kept `overflow: visible`, and the whole document became
  scrollable by exactly that 25 px, on every screen: a vertical swipe anywhere
  shifted the entire UI, header and faceplate included. `overscroll-behavior:
  none` did not stop it and could not, because it only suppresses rubber-band
  on a scroll with nowhere to go and this scroll had somewhere to go. The fix
  is `height: 100%` (which chains from the initial containing block, i.e. the
  999 the engine actually laid out) plus `overflow: hidden` on html and body,
  so a document scroll is impossible rather than merely unnecessary. Inner
  containers keep their own scrollers and are not affected. Verified on the
  Simulator with a vertical swipe on the chain, BLOCK, SELECT TONE with
  results, Settings and the Tuner: `scrollHeight` now equals `clientHeight`
  at 999, zero document scroll events fired on any screen, and the Select
  Tone and Settings lists still scroll on their own.
- **The app data container's UUID rotates on every reinstall and every app
  update.** Any absolute path the plugin persisted then names a directory
  that no longer exists, and the only path it persists is a local model's
  stash URL, in the tone JSON that rides presets, the saved app state and
  undo snapshots. `resolveLocalModelFile` re-roots the stored (content-hashed)
  file name under the current stash folder; a path that still exists is used
  as-is, which is every desktop case. Presets and project state were never
  affected: they embed the model bytes.
- **Bluetooth headphones cap the whole session at 16 or 24 kHz.** We hit this
  on an iPad with AirPods: `prepareToPlay: sampleRate=24000` and a sample-rate
  warning in Settings with nothing saying why. JUCE opens the iOS
  session as `PlayAndRecord` with `AllowBluetoothHFP`
  (`juce_Audio_ios.cpp`, `setAudioSessionCategory`), so a headset with a
  microphone wins the route and iOS refuses the requested 48 kHz. Two answers
  ship together, both in `IosAudioRoute` (the Haptics / AudioPermissions
  shim pattern, header-only no-op off iOS):
  - `configureSession()` makes one `setCategory:mode:options:` call: the
    category and options JUCE asked for, minus `AllowBluetoothHFP`, with
    Measurement mode (the raw input path). `AllowBluetoothA2DP` stays, so
    Bluetooth output-only listening still works and only the low-rate
    headset *mic* route goes away. Mode and options go in one call on
    purpose: on iPadOS 26 a bare `setMode:` clears category options.
    Measured on an iPad Pro: from Default mode `0x69` became `0x1` (A2DP,
    AirPlay and DefaultToSpeaker all gone); with the mode already
    Measurement, a repeated `setMode:` turned `0x69` into `0x61`
    (DefaultToSpeaker gone). It is not a JUCE text patch: JUCE sets the
    category when it *opens* a device and never on its own route-change
    `restart()` path, so re-applying it on every device-manager change is
    enough and the JUCE tree stays untouched. On the same iPad Pro, with
    AirPods Pro connected and reading `AVAudioSession` from the app log:
    the first open with HFP allowed came up at 24 kHz; after the call the
    device reopened at 48 kHz on the built-in mic. With a USB interface
    unplugged mid-session, the route went to the built-in mic at 48 kHz,
    then to built-in mic plus AirPods A2DP output at 48 kHz, and back to
    the interface when it was plugged in again, with options `0x69` and
    Measurement mode held through every change.
  - `isBluetoothRoute()` feeds `bluetoothRoute` in the settings state, and
    the UI turns that (or any session under 44.1 kHz) into one plain tip in
    Settings > System Settings, next to Sample Rate: use wired headphones,
    the iPad speaker, or a USB audio interface. The generic "runs lightest
    at 48 kHz" note is suppressed while it shows, so there is one
    explanation instead of two.
- `xcrun simctl privacy grant microphone` does not suppress the prompt;
  `AVAudioSession` still asks once.
- **`UIRequiresFullScreen` no longer opts an app out of multitasking** on
  iPadOS 26: a second app dragged from the Dock windows itself over this one
  regardless. The app is not resized by it (the other app floats), so the
  layout is unaffected. The key is set anyway because App Store validation
  still requires it for a landscape-only iPad app (ITMS-90474) — it changes
  runtime behaviour only on older iPadOS, where it disables Split View.
- The `NAM` static library must be force-loaded on iOS as well as macOS.
  `$<PLATFORM_ID:...>` reports `iOS`, not `Darwin`, when cross-compiling, so
  without both the linker strips the model-architecture registrations and
  loads fail with "No config parser registered for ...".

## Known gaps

- There is no true folder import on iOS: a security-scoped *directory* cannot
  be enumerated, so **Load Folder** opens the platform's multi-select file
  picker instead. Several files still land as one multi-model block, and
  native titles a single pick from the file's name, so both desktop outcomes
  are reachable; only the row's wording is approximate on iOS.
- The double-tap knob reset and the label tap into the type-in editor are
  proved in a browser against the same bundle, not on a device: two taps
  cannot be driven inside 300 ms through the Simulator automation bridge.
- Dragging a `.nam` from Files onto a tile is untested. The receiving code is
  the same HTML5 drop path the desktop uses, and the app does window alongside
  Files, but the drag could not be driven from the automation.
- No haptics: the iPad has no Taptic Engine, so
  `UIImpactFeedbackGenerator` does nothing there and the tile lift and drop
  are silent.
- AUv3 is not built. Only the Standalone app exists on iOS.

## Desktop CI evidence

Nothing on this branch reaches a desktop build. There is no C++ and no
CMake here: the `window.__T3K_PLATFORM__` flag the UI reads already lives in
main (PR 111), so this diff is TypeScript and CSS gated as described under
Touch adaptation. `IS_IOS` / `html.t3k-ios` is false and absent in every
desktop build; `IS_COARSE_POINTER` / `html.t3k-touch` engages only where the
primary pointer is coarse, which on a desktop means a touch-first machine
like a Windows tablet, and that is the intent. The shared `ui` bundle builds,
lints, type-checks and tests clean.
