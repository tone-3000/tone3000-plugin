# iOS (iPad) build

Standalone-only iPad port of the plugin: the same C++ and the same JUCE UI,
with every difference gated. `#if JUCE_IOS` covers the platform code; the UI
reads two compile-time flags from `plugin/ui/core/Design.h` (see Touch
adaptation below): `design::kIos` for the app shell and
`design::kCoarsePointer` for touch-first ergonomics on any device, plus
`MouseInputSource::isTouch()` per event for behaviours. Desktop behaviour is
unchanged. AUv3 is out of scope; the app is iPad only (`TARGETED_DEVICE_FAMILY
2`).

Deployment target iOS 16. Landscape only.

## Build

The UI compiles with the plugin, so an iOS build is one configure and one
build. Put your publishable key in the repo-root `.env` first (see the root
README); CMake reads it at configure time.

```sh
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

`-DT3K_IOS_BUNDLE_ID=<id>` signs under your own identity. Changing it on a
device that already holds the app gives a fresh, empty Documents folder, so
keep it stable once models are loaded. Add
`-DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=<id>` if Xcode cannot pick your team.

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

# The app's own log (juce::Logger; the UI's HTTP, OAuth and load flows all
# write to it), the most useful debugging channel on Simulator and device.
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
  reaches through JUCE: file timestamps and free disk space (both
  `juce_SharedCode_posix.h`), system boot time (`systemUptime` timestamping
  touch events in `juce_UIViewComponentPeer_ios.mm`, plus
  `mach_absolute_time`), and user defaults (declared defensively; the JUCE
  call that used it compiles out with `JUCE_WEB_BROWSER=0`). An upload whose
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
- `TARGETED_DEVICE_FAMILY` is `2` (iPad only). JUCE's default is `1,2`, and
  App Store Connect reads the family from the binary, so a build made with
  the default is treated as a universal app: review is blocked until iPhone
  screenshots are uploaded, and the app would install on iPhones the
  faceplate was never sized for.
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
letterboxed and vertically centred in the one fixed full-screen window
(`NativeEditor::fitRoot`; there is no resize, no persisted scale), with only
the touch-ups a finger needs. Three gates, from narrowest reach to widest:

- `design::kIos`: the app shell only. The fixed window, the Bluetooth
  sample-rate tip (`Banners.cpp`), and the multi-select file picker that
  stands in for Load Folder (`LocalFiles::pick`).
- `design::kCoarsePointer` (iOS and Android; the testbed sets it per scenario
  through `T3K_UI_COARSE_POINTER` so touch layouts can be captured on a
  desktop): static ergonomics for a touch-first device. Tile chrome that is
  always visible instead of hover-revealed (`ToneTile`, `GalleryLane`), and
  the touch wording of the hint-bar copy (`Help.cpp`).
- `MouseInputSource::isTouch()` per event: behaviours (a tile's long-press
  menu, the knob's double tap and touch-and-hold). A hybrid device gets touch
  behaviour from its touchscreen and desktop behaviour from its mouse.

| gesture | result |
| ------- | ------ |
| tap a tile | open the block |
| drag a tile | reorder (the same distance rule as desktop) |
| hold a tile 500 ms | tile menu, while the finger is still down (`GalleryTile::kLongPressMs`) |
| hold the Spread / Align Offset knob | the advanced deck (desktop: right-click the group; `Knob::onLongPress`) |
| press a control | its help in the hint bar; release clears it |
| drag a knob up or down | adjust |
| double tap a knob, EQ fader or EQ dot | reset to default |
| tap a knob's label | type the value |

Tiles, knobs and EQ handles claim their own drags
(`setViewportIgnoreDragFlag`), so lanes and lists scroll from the space
around them, not across them; once a drag has become a scroll, the press it
started with never clicks (`Clickable`, see `plugin/docs/native-ui.md` §5.8).

Local import is desktop's two rows, **Load File** and **Load Folder**, in the
tile menus. On iOS the folder row opens the platform's multi-select file
picker instead (see Known gaps).

## Platform notes worth knowing

- **Picker results must be read through security-scoped URLs.** A file chosen
  outside the app container is unreadable through its raw path, which is why
  `LocalFiles::pick` hands `getURLResults()` to `loadLocalToneUrls` on iOS
  instead of paths. A test with the file *inside* the container passes and
  proves nothing.
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
  still requires it for a landscape-only iPad app (ITMS-90474); it changes
  runtime behaviour only on older iPadOS, where it disables Split View.
- The `NAM` static library must be force-loaded on iOS as well as macOS.
  `$<PLATFORM_ID:...>` reports `iOS`, not `Darwin`, when cross-compiling, so
  without both the linker strips the model-architecture registrations and
  loads fail with "No config parser registered for ...".
- **Factory presets ride in the app bundle.** Desktop installers lay the
  `.t3kpreset` files down outside the app: a shared directory on macOS and
  Windows, the per-user Factory folder on Linux. iOS has no installer,
  so the Standalone target carries them as bundle resources and
  `PresetManager::defaultSystemFactoryDir` points there (`plugin/CMakeLists.txt`
  and `plugin/src/PresetManager.cpp`). iOS bundles are flat, so they land at
  `TONE3000.app/FactoryPresets`. The bundle is read-only, which is the contract
  that directory already has, and a file carrying the same preset id in
  `Library/TONE3000/Presets/Factory` inside the app container still overrides a
  bundled entry in `list()`. The glob runs at configure time, so a
  new preset file needs a reconfigure.
- **Sign-in uses the system browser and a loopback redirect**, the same as
  desktop (`plugin/docs/native-ui.md` §5.14). The app declares background
  audio, so the process and its listener socket stay alive while Safari is
  in front, and the redirect to `http://localhost:<port>/` lands back in the
  app.

## Known gaps

- There is no true folder import on iOS: a security-scoped *directory* cannot
  be enumerated, so **Load Folder** opens the platform's multi-select file
  picker instead. Several files still land as one multi-model block, and a
  single pick is titled from the file's name, so both desktop outcomes are
  reachable; only the row's wording is approximate on iOS.
- Dragging a `.nam` from Files onto a tile is untested; the app does window
  alongside Files, but the drag could not be driven from the automation.
- No haptics: the iPad has no Taptic Engine, so
  `UIImpactFeedbackGenerator` does nothing there and the tile lift and drop
  are silent.
- AUv3 is not built. Only the Standalone app exists on iOS.

### Back up user presets to Files or iCloud Drive

In **Settings → Plugin → Presets**, choose **Export all user presets**. Select
**iCloud Drive** or **On My iPad** in Apple's Files picker and save the ZIP.
The backup contains every saved user preset, its embedded NAM/IR data, and the
user preset order. Save any current edits as a preset before exporting. Factory
presets and global audio, MIDI and app settings are not included.

To restore, choose **Import preset backup**, pick an import mode, and select the
ZIP in Files. **Add copies** keeps existing presets and appends the imported
presets in the backup's order. Copies get new IDs and duplicate names get
`(2)`, `(3)`, etc.

**Replace all user presets** asks for confirmation, then validates and stages
the entire ZIP before changing anything. It replaces the user preset list and
its order; importing the same ZIP again does not add duplicates. Factory
presets, their order, and global audio/MIDI/app settings are kept. The currently
playing chain stays loaded until you select a restored preset.

Before replacement, existing saved user presets are backed up to a ZIP in
`PresetBackups`, beside the app's `Presets` folder. If that backup cannot be
created, replacement stops. A failed restore rolls back the original files and
order. Recovery ZIPs are retained after a successful restore and can be imported
through the same picker. Save current edits and keep your own backup in Files
or iCloud Drive before replacing presets; uninstalling the app also removes its
local recovery ZIPs.

This uses Apple's document picker and file providers. iCloud handles uploading
and downloading the ZIP; no TONE3000 account or cloud backup service is needed.
