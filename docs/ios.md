# iOS (iPad) build

Standalone-only iPad build of the plugin: the same C++ and the same React
UI, with every platform difference behind `#if JUCE_IOS`. Desktop behaviour
is unchanged. AUv3 is out of scope; iPhone is untested.

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
- The app icons are flattened to opaque at configure time with ImageMagick.
  juceaide writes them RGBA whatever the source is, and an alpha channel on the
  1024 icon means ITMS-90717 and no icon in TestFlight. Without ImageMagick the
  configure step warns; Simulator builds are unaffected either way.
- `ITSAppUsesNonExemptEncryption` is false in the Info.plist. The app's only
  encryption is standard HTTPS, and declaring it here answers the
  export-compliance question once instead of on every upload.
- `plugin/icon/icon.png` is 512x512 and juceaide never enlarges a source, so the
  App Store icon is currently that 512 artwork centred on a blank 1024 field.
  Exporting the icon at 1024 fixes it; the configure step warns until then.
- `T3K_IOS_BUILD_NUMBER` is CFBundleVersion, and it defaults to 1. App Store
  Connect refuses a build number it has already seen for the same marketing
  version, so a second upload of one version needs
  `-DT3K_IOS_BUILD_NUMBER=<n>`. Without the setting at all, JUCE uses the
  marketing version as the build number and the second upload always bounces.
- App Store Connect requires uploads built against a current iOS SDK. A runner
  pinned to an older Xcode builds and signs fine and is then refused at upload,
  which reads as a signing problem and is not one.

## Platform notes worth knowing

- **Picker results must be read through security-scoped URLs.** A file chosen
  outside the app container is unreadable through its raw path. A test with
  the file *inside* the container passes and proves nothing.
- **The app data container's UUID rotates on every reinstall and every app
  update.** Any absolute path the plugin persisted then names a directory
  that no longer exists, and the only path it persists is a local model's
  stash URL, in the tone JSON that rides presets, the saved app state and
  undo snapshots. `resolveLocalModelFile` re-roots the stored (content-hashed)
  file name under the current stash folder; a path that still exists is used
  as-is, which is every desktop case. Presets and project state were never
  affected: they embed the model bytes.
- `xcrun simctl privacy grant microphone` does not suppress the prompt;
  `AVAudioSession` still asks once.
- **`UIRequiresFullScreen` no longer opts an app out of multitasking** on
  iPadOS 26: a second app dragged from the Dock windows itself over this one
  regardless. The key is therefore not set. The app is not resized by it (the
  other app floats), so the layout is unaffected.
- The `NAM` static library must be force-loaded on iOS as well as macOS.
  `$<PLATFORM_ID:...>` reports `iOS`, not `Darwin`, when cross-compiling, so
  without both the linker strips the model-architecture registrations and
  loads fail with "No config parser registered for ...".

## Known gaps

- The UI is the desktop UI. It renders and is usable, but it is not yet
  adapted for touch: hit targets, gestures and the "On this iPad" local
  browsing section are a separate change.
- Load Folder is a multi-select on iOS: a security-scoped *directory* cannot
  be enumerated, so the picker returns files instead.
- Dragging a `.nam` from Files onto a tile is untested. The receiving code is
  the same HTML5 drop path the desktop uses, and the app does window alongside
  Files, but the drag could not be driven from the automation.
- AUv3 is not built. Only the Standalone app exists on iOS.
