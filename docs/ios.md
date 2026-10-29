# iOS (iPad) build

iPad port of the plugin: the Standalone app with the AUv3 plugin embedded in
it (see AUv3 below), the same C++ and the same JUCE UI, with every difference
gated. `#if JUCE_IOS` covers the platform code; the UI reads two compile-time
flags from `plugin/ui/core/Design.h` (see Touch adaptation below):
`design::kIos` for the app shell and `design::kCoarsePointer` for touch-first
ergonomics on any device, plus `MouseInputSource::isTouch()` per event for
behaviours. Desktop behaviour is unchanged. The app is iPad only
(`TARGETED_DEVICE_FAMILY 2`).

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
The `TONE3000_Standalone` target is the whole product: JUCE makes it depend
on `TONE3000_AUv3` and copies the extension into the app bundle, so there is
nothing separate to build for the plugin.

## AUv3

The plugin ships inside the app: `TONE3000.app/PlugIns/TONE3000.appex`, an
Audio Unit app extension that hosts (AUM, GarageBand, Logic for iPad,
Cubasis, Loopy Pro, ...) discover once the app is installed. There is no
separate download, no plugin installer and nothing to enable; deleting the
app removes the plugin. The same `juce_add_plugin` call builds both formats
(`FORMATS AUv3 Standalone` on iOS in `plugin/CMakeLists.txt`), so the DSP, the
state format and the UI are one build of one source, and the UI runs in the
AUv3 exactly as it does in a desktop host: the host sizes the view, the root
fits into it (`NativeEditor::fitRoot`; the design's resize range is published
through the constrainer for `supportedViewConfigurations`), and the window is
not the screen, so none of the kiosk-window assumptions apply. The two
regimes are one runtime flag in `NativeEditor` (`kiosk_`: iOS *and* the
Standalone wrapper), not a platform `#if`, so the iPad app is unchanged and
nothing is compiled differently per format.

What is different about an app extension, and what the build does about it:

- **Its own process and sandbox.** The extension's `~/Library` is not the
  app's. One App Group (`T3K_IOS_APP_GROUP`, default
  `group.<bundle id>`; CMake passes it as `APP_GROUPS_ENABLED` /
  `APP_GROUP_IDS`, which JUCE turns into the
  `com.apple.security.application-groups` entitlement on both bundles) gives
  them a shared container, and `plugin/include/AppData.h` makes that the data
  root for both: `<group container>/Library/TONE3000` holds `Presets`,
  `LocalModels` and the two settings files (`preferences.settings`, the
  UI's `ui-preferences.settings`). So a preset saved in the app is in the
  plugin's list, a model stashed from Files in the app is loadable from a
  preset in the plugin, and the sign-in is one sign-in. The first launch of
  the app build that has the group moves what the previous build kept in
  the app's own container into the shared one (`AppData.cpp`, child by
  child, only where the shared copy does not exist yet; the log says what
  moved under `[AppData]`). Setting `-DT3K_IOS_APP_GROUP=""` builds without
  a group: both processes then fall back to their own `~/Library/TONE3000`
  and the plugin is a stranger to the app's data.
- **The group has to exist on the App ID.** Automatic signing registers the
  group and adds the capability to the app's and the extension's App IDs on
  the first signed build (`-allowProvisioningUpdates`); a manually managed
  profile needs the App Group capability turned on for both IDs and a
  profile for `<bundle id>.TONE3000AUv3` as well as the app. The TestFlight
  workflow signs both with the same API key.
- **No browser.** An extension cannot open Safari (`UIApplication` is
  unavailable to it) or present `ASWebAuthenticationSession`, and the
  host backgrounding would suspend the loopback listener anyway. What it
  can do is the device flow (RFC 8628), which never leaves the process: so
  with `Tone3000Session::Config::signInViaApp` (set for the iOS AUv3 in
  `NativeEditor.cpp`) `login()` first re-reads the shared preferences and
  simply proceeds when the app has signed in; otherwise it opens the
  sign-in screen straight on the device page (`AuthFlow::deviceOnly`: scan
  the QR code with a phone, or type the code at tone3000.com/activate, no
  browser page and no Copy Link) and, alongside the device poll, re-reads
  the preferences every second for a sign-in made in the TONE3000 app.
  Whichever lands first finishes exactly like a sign-in of the plugin's
  own (identity refreshed, a browse-intent login lands in the tone
  browser), and the tokens are the app's too. Signing out in either signs
  both out (same tokens, same file).
- **The factory presets are the app's.** The extension has none of its own:
  `PresetManager::defaultSystemFactoryDir` walks from the `.appex` up to the
  app bundle's `FactoryPresets`.
- **Two privacy manifests.** App Store Connect checks every bundle in the
  upload, so `PrivacyInfo.xcprivacy` is a resource of both the app and the
  extension.
- **The log stays per process** (`~/Library/TONE3000/TONE3000.log` of the
  app or of the extension's container): two processes appending to one
  file would interleave. `xcrun simctl get_app_container <udid>
  <bundle-id>.TONE3000AUv3 data` finds the extension's.
- **Memory.** iPadOS gives an app extension a much smaller memory budget
  than the app. The plugin's model set is the app's, so this is a budget
  question only for a session that loads many large models at once; the
  host, not the plugin, reports the kill.

Known limitation: JUCE's `InterProcessLock` is a stub on iOS, so the merge
of preference writes between the app and the extension (`UiPrefs`) is not
lock-protected there. Each save is still a whole-file atomic replace, and
the two sides write different keys at different moments (the app writes the
tokens; the plugin reads them), so the practical exposure is a lost toggle
if both are changed within the same instant.

Trying the plugin needs a host, and App Store hosts do not install in the
Simulator: use a device with any AUv3 host (GarageBand is free). It lists
under TONE3000 as a music effect (`AU_MAIN_TYPE` follows `NEEDS_MIDI_INPUT`).
The Simulator still verifies the packaging: `PlugIns/TONE3000.appex` is in
the app, its `Info.plist` carries the `NSExtension` / `AudioComponents`
entry (`plutil -extract NSExtension json -o - <appex>/Info.plist`), `xcrun
simctl spawn <udid> pluginkit -m -p com.apple.AudioUnit-UI` lists the
extension once the app is installed, and `xcrun simctl get_app_container
<udid> <bundle-id> groups` shows the group container the app's log names as
its settings path. The group entitlement itself is in the generated
`build-ios/plugin/TONE3000_artefacts/JuceLibraryCode/TONE3000_{Standalone,AUv3}.entitlements`;
a Simulator build is unsigned, so `codesign -d --entitlements` shows it only
on a device build.

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
# The log is the one file that stays in the app's own container (presets,
# models and settings live in the App Group container, see AUv3 below).
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

The adaptation is deliberately minimal: the desktop UI filling the one fixed
full-screen window (`NativeEditor::fitRoot`; there is no resize, no persisted
size), with only the touch-ups a finger needs. The screen's width sets the
scale and its extra height over the design's 1024:578 stretches the content
column between header and faceplate (about 90 design px on a 4:3 iPad), where
the signal chain's lanes and the block card centre below their titles and the
tone browser, Settings and tuner fill the room; see `plugin/docs/native-ui.md`
§5.1. Three
gates, from narrowest reach to widest:

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
- **Sample-rate discovery uses explicit choices on iOS:** 44.1, 48, 88.2,
  96, 176.4 and 192 kHz.
  Some USB interfaces take long enough to handle JUCE's repeated rate probes
  that startup can exceed the iPadOS launch watchdog deadline. The build sets
  `JUCE_IOS_AUDIO_EXPLICIT_SAMPLERATES` to skip that scan. A configure-time
  patch in the root `CMakeLists.txt` retains the rate negotiation/readback
  before buffer-size discovery, so buffer durations use the actual device
  rate rather than JUCE's 44.1 kHz constructor default. The choices are
  requests; iPadOS still determines the actual rate. Higher rates are only
  negotiated when selected or restored from saved settings; adding choices
  does not add startup probes. If the route negotiates a different rate,
  System Settings shows a warning and the selector reflects the actual rate.
  The actual rate is also included in the selector after startup restoration
  or route changes, even if it is outside the explicit choices (e.g. 24 kHz).
  If opening fails, the app attempts to restore the previous audio setup and
  reports whether recovery succeeded. Re-run the iOS CMake
  preset before building to apply the patch. Desktop rate discovery is
  unchanged.
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
  `TONE3000.app/FactoryPresets`; the AUv3 reads the same copy (see AUv3
  above). The bundle is read-only, which is the contract that directory
  already has, and a file carrying the same preset id in
  `Presets/Factory` under the data root still overrides a bundled entry in
  `list()`. The glob runs at configure time, so a new preset file needs a
  reconfigure.
- **Sign-in runs in an in-app sheet, not Safari.** App Store review
  rejected the hand-off to the system browser (Guideline 4, Design: "the
  user is taken to the default web browser to sign in"), and Apple's answer
  for OAuth is `ASWebAuthenticationSession`: a Safari sheet presented over
  the app with the page's address and certificate visible, Safari's cookies
  shared (an existing tone3000.com login is one tap; the system asks once,
  "TONE3000 wants to use tone3000.com to sign in", before the sheet) and a
  Cancel button. `plugin/ui/services/WebAuth.mm` is the shim
  (header-only no-op off iOS, compiled with `-fobjc-arc` on iOS by
  `NativeUi.cmake`), and `Tone3000Session::openBrowser` uses it in place of
  `launchInDefaultBrowser`. Everything else is the desktop flow
  (`plugin/docs/native-ui.md` §5.14): the sheet is a browser, so it follows
  the redirect to `http://localhost:<port>/` like Safari did, the loopback
  listener takes the code, and the session dismisses the sheet. The server
  sees the same `redirect_uri`, so nothing new is registered for the
  publishable key and no custom URL scheme is declared. Cancel on the sheet
  (or on the consent alert) abandons the sign-in like the server's
  `canceled=true` redirect does; a sheet the system refuses to present
  leaves the sign-in page up with its Copy Link and device-flow fallbacks.
  The app no longer leaves the foreground to sign in, so the listener no
  longer depends on background audio for this. The AUv3 cannot present the
  sheet at all and hands sign-in to the app (see AUv3 above).
- **Account deletion.** The same rejection notes that an app offering
  account creation must also offer account deletion in the app (Guideline
  5.1.1(v)). The sign-in sheet reaches tone3000.com's own register page, so
  expect review to look for a deletion path too; today there is none in
  the plugin, and adding one needs an API endpoint (or at least a deep link
  to the account page) on the TONE3000 side first.

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
- Signing in inside the AUv3 is by device code (see AUv3 above). Scanning
  the QR code needs a second device; typing the code on the same iPad
  means leaving the host (Slide Over Safari works), during which the host
  may suspend the extension: the poll then resumes on return, and the code
  stays valid for the server's `expires_in`. A sign-in made in the
  TONE3000 app is picked up within a second while the page is up, or on
  the next gated action otherwise.
- The AUv3's local import (Load File / Load Folder) presents the document
  picker over the plugin's view (`LocalFiles::pick` passes the editor as
  the chooser's parent in an app extension, which JUCE requires there);
  this is untested across hosts.
- Links out of the AUv3 (the header logo, a tone's page, the update
  notice's download) go through `URL::launchInDefaultBrowser`, which on
  iOS is `UIApplication openURL:`; an app extension is not allowed to open
  URLs that way, so in a host these do nothing. The app is unaffected.
