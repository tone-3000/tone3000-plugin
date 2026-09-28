# Android build

Standalone-only Android port of the plugin: the same C++ and the same JUCE
UI, with every difference gated. `#if JUCE_ANDROID` covers the platform code
and `T3K_ANDROID` the CMake side; the UI shares the iOS port's fixed-window
handling (`NativeEditor`) and its touch ergonomics through
`design::kCoarsePointer` (see `docs/ios.md`, Touch adaptation). `design::kIos`
stays false, so the iOS-only shell pieces (the Bluetooth sample-rate tip) do
not appear. Desktop behaviour is unchanged. Android has no plugin host, so
only the Standalone app is built.

`minSdk` 29 (Android 10), `targetSdk` 36. ABIs `arm64-v8a` (phones and
tablets) and `x86_64` (the emulator). Landscape only.

`android/` is a hand-written Gradle project, not a Projucer export. Its
`externalNativeBuild` points at the repo's own root `CMakeLists.txt`, so the
native code builds through the same CMake as every other platform, with the
NDK toolchain Gradle injects.

## Prerequisites

Every host needs:

| What | Version | Why |
| ---- | ------- | --- |
| Git | any | clone, submodules, and the CPM fetch of JUCE into `libs/` |
| JDK | 17 or newer, on `JAVA_HOME` | runs the Gradle wrapper; Android Studio's bundled JBR works |
| Android SDK Platform | 36 | `compileSdk` |
| Android SDK Build-Tools | 36.x | `apksigner` for signing |
| Android SDK Platform-Tools | latest | `adb` for installing and debugging |
| NDK | exactly **27.2.12479018** | pinned in `android/app/build.gradle.kts`: NDK 30's `<aaudio/AAudio.h>` conflicts with JUCE 9's vendored Oboe |
| CMake (SDK package) | **4.1.2** | pinned in `android/app/build.gradle.kts`; a system CMake is not used |
| A host C++ compiler | see below | JUCE builds its `juceaide` helper for the build machine before cross-compiling |

Gradle itself (9.7.1) and the Android Gradle Plugin come through the
committed wrapper; nothing to install. Node.js is no longer needed: the UI is
C++.

The SDK packages install from Android Studio (Settings > Languages &
Frameworks > Android SDK; tick **Show Package Details** to pick the exact NDK
and CMake versions) or from the command line:

```sh
sdkmanager --licenses
sdkmanager "platforms;android-36" "build-tools;36.0.0" "platform-tools" \
  "ndk;27.2.12479018" "cmake;4.1.2"
```

A missing NDK or CMake is downloaded by Gradle on the first build, but only
once its licence is accepted; an unaccepted one fails with
`LicenceNotAcceptedException`. Run `sdkmanager --licenses` (or install the
package from Android Studio) to fix it.

### Linux

```sh
sudo apt install openjdk-21-jdk build-essential git \
  pkg-config libfreetype-dev libfontconfig1-dev
export JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64
export ANDROID_HOME=$HOME/Android/Sdk
```

`juceaide` is a Linux binary here, and JUCE resolves its graphics
dependencies through pkg-config, so `pkg-config`, FreeType and Fontconfig are
required even though the app itself never links them. Without them the
configure fails with `Could NOT find PkgConfig`.

### macOS

```sh
xcode-select --install   # clang for juceaide; full Xcode also works
export JAVA_HOME="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
export ANDROID_HOME=$HOME/Library/Android/sdk
```

No pkg-config: `juceaide` uses the Apple frameworks on macOS.

### Windows

- Visual Studio 2022 or its Build Tools with the **Desktop development with
  C++** workload, for `juceaide`. Run the build from an **x64 Native Tools
  Command Prompt for VS 2022** so `cl.exe` is on `PATH`; the CMake configure
  that builds `juceaide` uses the same Ninja generator as the Android build and
  finds the host compiler from the environment.
- `JAVA_HOME` set to Android Studio's JBR, e.g.
  `C:\Program Files\Android\Android Studio\jbr`.
- The SDK defaults to `%LOCALAPPDATA%\Android\Sdk`.
- Keep the clone path short (e.g. `C:\src\tone3000-plugin`) and enable long
  paths in Git (`git config --global core.longpaths true`): the CMake build
  tree under `android\app\.cxx` nests deeply.

## Build

1. Clone and fetch the submodules:

   ```sh
   git submodule update --init --recursive
   ```

2. Put your TONE3000 publishable key in the repo-root `.env` (see
   `.env.example` and the root README). CMake reads it at configure time.

   ```sh
   T3K_PUBLISHABLE_KEY=t3k_pub_your_key_here
   ```

3. Tell Gradle where the SDK is, either with `ANDROID_HOME` (above) or with
   `android/local.properties` (gitignored):

   ```properties
   sdk.dir=/home/<you>/Android/Sdk
   # macOS:   sdk.dir=/Users/<you>/Library/Android/sdk
   # Windows: sdk.dir=C:/Users/<you>/AppData/Local/Android/Sdk
   ```

4. Build:

   ```sh
   cd android
   ./gradlew assembleRelease      # Windows: gradlew.bat assembleRelease
   ```

   Or open `android/` in Android Studio and run the `app` configuration.

The first build fetches JUCE into `libs/`, builds `juceaide`, and compiles
both ABIs; expect several minutes. The unsigned APK lands in
`android/app/build/outputs/apk/release/app-release-unsigned.apk`.

`assembleDebug` produces an APK that is already signed with the debug key and
is debuggable (needed to read the app's log, below), at the cost of an
unoptimised native build.

Troubleshooting:

- A `.env` change is not picked up: delete `android/app/.cxx` to force a fresh
  CMake configure.
- A cold `libs/juce` fetch can fail with "Failed to remove directory" when both
  ABIs configure at once. Retry, or build one ABI with
  `-Pandroid.injected.build.abi=arm64-v8a`.
- To build only for a phone, the same flag skips the `x86_64` compile.

## Sign

A release APK must be signed before it installs. For development, sign it
with a debug keystore (created once; `keytool` ships with the JDK, `apksigner`
with Build-Tools):

```sh
keytool -genkey -v -keystore ~/.android/debug.keystore -alias androiddebugkey \
  -storepass android -keypass android -keyalg RSA -keysize 2048 -validity 10000 \
  -dname "CN=Android Debug,O=Android,C=US"

$ANDROID_HOME/build-tools/36.0.0/apksigner sign \
  --ks ~/.android/debug.keystore --ks-pass pass:android --key-pass pass:android \
  --out app-release-signed.apk \
  android/app/build/outputs/apk/release/app-release-unsigned.apk
```

On Windows the keystore is `%USERPROFILE%\.android\debug.keystore` and the
signer is `apksigner.bat`.

Android refuses an update signed with a different key than the installed app
(`INSTALL_FAILED_UPDATE_INCOMPATIBLE`). Keep one keystore per development
setup; switching keys means uninstalling first, which wipes the app's data
(presets, downloaded models, sign-in).

## Install and log

Enable **Developer options** on the device (tap Build number seven times),
then either USB debugging or Wireless debugging.

```sh
# USB
adb devices

# Wireless (Android 11+): Developer options > Wireless debugging.
# Pair once with "Pair device with pairing code", then connect to the address
# on the main Wireless debugging screen. The two ports differ, and both change
# whenever Wireless debugging restarts.
adb pair <ip>:<pairing-port> <code>
adb connect <ip>:<connect-port>
adb mdns services              # lists the current ports of a paired device

adb install -r app-release-signed.apk
adb shell monkey -p com.TONE3000.TONE3000 -c android.intent.category.LAUNCHER 1
adb exec-out screencap -p > screen.png
```

For wireless debugging the computer and the device must be on the same
network segment: a phone on a separate 5 GHz or guest SSID can pair and then
be unreachable (`No route to host`).

The app's own log (`juce::Logger`; model loads, presets and audio setup write
to it) is `TONE3000/TONE3000.log` in the app's data directory. It can only be
read from a debuggable (`assembleDebug`) build:

```sh
adb shell run-as com.TONE3000.TONE3000 cat TONE3000/TONE3000.log
```

`adb logcat` carries crashes, audio-stream setup (`AAudio`) and activity
starts (`ActivityTaskManager`), but not the app's log lines.

## How the build fits together

All in `plugin/CMakeLists.txt` unless noted; each has a longer comment there.

- The Standalone target's output is renamed to `libjuce_jni.so`: JUCE's Java
  glue hardcodes `System.loadLibrary("juce_jni")`, and JUCE skips its own
  output renaming on Android.
- The Standalone target is built with default symbol visibility. JUCE hides
  symbols on every plugin wrapper, which on Android hides
  `juce_CreateApplication`; the JNI bootstrap then finds nothing and the app
  opens to a blank window without crashing.
- `JUCE_PUSH_NOTIFICATIONS_ACTIVITY` and `JUCE_PUSH_NOTIFICATIONS=1` are set on
  the shared-code target, or `JuceActivity`'s native `appOnResume` is missing
  and the first resume throws `UnsatisfiedLinkError`. No push setup is pulled
  in. `JUCE_CORE_INCLUDE_JNI_HELPERS=1` exposes the JNI helpers the Android
  branches of `AudioPermissions.cpp` and `PresetManager.cpp` use.
- The `.so` is linked with 16 KB page alignment
  (`-Wl,-z,max-page-size=16384`), which NDK 27 does not default to. Without
  it, Android 15+ devices show a compatibility warning at launch and Play
  rejects the upload.
- NAM is whole-archived on Android as on Linux, or model loads fail with
  "No config parser registered for ...".
- curl, JACK and the XInput2 opt-out are desktop-Linux only: `UNIX` is true
  when cross-compiling for Android, so those branches exclude `T3K_ANDROID`.
  Networking goes through JUCE's `HttpURLConnection` backend instead.
- The DSP test suite and `NeuralAmpModelerCore`'s own CMake project are
  skipped, as on iOS.
- `android/app/build.gradle.kts` compiles JUCE's Java glue (`JuceApp`,
  `JuceActivity`, ...) straight out of `libs/juce`, next to the app's own
  `MainActivity`/`AudioSessionService` in `android/app/src/main/java`, and orders Java
  compilation after the CMake configure that fetches and patches that tree.

## JUCE patches

Two configure-time patches in the root `CMakeLists.txt`, idempotent and
failing the configure if their anchors move, like the desktop ones. Both fix
JUCE's Android HTTP backend, which never sent a working POST: the sign-in
token exchange failed with `token_exchange_failed` until both were in.

- `T3K_ANDROID_HTTP_HEADERS`: JUCE joins request headers with CRLF (and adds
  its own `Content-length` line to every POST), but `JuceHTTPStream.java`
  splits them on `\n` alone. Each value kept a trailing `\r`, which
  `HttpURLConnection` rejects by throwing, so the request never left. The
  patch trims both halves of each header line. The `.java` source is what
  runs: Gradle compiles it into the APK and JUCE loads the app's class before
  its embedded bytecode copy.
- `T3K_ANDROID_HTTP_POST`: `juce_Network_android.cpp` told the Java side to
  write a body only for `ParameterHandling::inPostData`, so raw POST data
  (`URL::withPOSTData`, which the UI's `HttpClient` uses) went out as an empty
  POST. The patch passes `hasBodyDataToSend`, which covers both.

## Platform notes worth knowing

- **Factory presets ride as APK assets.** `resources/factory-presets/` is an
  assets source dir in `android/app/build.gradle.kts`, and
  `PresetManager::defaultSystemFactoryDir` copies the `.t3kpreset` files out to
  `TONE3000/Presets/SystemFactory` in the app's data directory on first use
  (`extractFactoryPresetsFromAssets`). A marker file records a completed
  extraction, so a run killed halfway retries. An app update that changes the
  presets does not refresh an already-extracted set.
- **Model downloads resume.** JUCE's Android backend cannot tell a truncated
  body from a complete one (`model_url` redirects, so the length it sees is
  0), so `fetchModelFromUrl` range-resumes until the server answers 416. The
  loop is Android-only.
- **The window is the screen.** Unlike UIKit, Android's peer keeps whatever
  size the editor asks for, so `NativeEditor::parentHierarchyChanged` resizes
  to the primary display once it is known. The UI letterboxes and centres
  into it, as on iOS; on a 19.5:9 phone that leaves bars left and right, which
  also keeps the camera cutout clear.
- **Picked files are `content://` URIs.** `LocalFiles::pick` takes the iOS
  route: `getURLResults()` into `loadLocalToneUrls`, which JUCE resolves
  (display name and stream) through the content resolver. A Storage Access
  Framework folder pick cannot be listed as a `File`, so **Load Folder** opens
  a multi-select file picker instead.
- **Microphone permission** goes through `juce::RuntimePermissions`. Android
  cannot distinguish "never asked" from "denied" through that API, so "not
  granted" reports as unknown and Settings offers the request;
  `openMicSettings` opens the app's system settings page.
- **Sign-in uses the system browser and a loopback redirect**, as on desktop
  (`plugin/docs/native-ui.md` §5.14). When the browser already has a
  TONE3000 session, the authorize page redirects immediately and the landing
  page's `window.close()` closes the tab, since Chrome lets a page close a tab
  another app opened. What stays on screen is an empty new tab: expected, not
  a failure. Switch back to the app.
- **Audio keeps running with the screen locked.** `MainActivity` (a thin
  `JuceActivity` subclass in `android/app/src/main/java`) starts
  `AudioSessionService`, a foreground service of type `microphone`, on every
  resume once `RECORD_AUDIO` is granted. Without it Android 11+ feeds a
  backgrounded app silence from the microphone and drops the process to
  background priority. The service does no work of its own; it shows an
  ongoing "TONE3000 is running" notification and stops when the app is
  finished or swiped out of Recents. On Android 13+ the notification only
  appears if notifications are allowed; `MainActivity` asks once per
  install, right after the microphone is granted. Declining hides the
  notification but the service works either way.
- Space/Enter passthrough is a no-op (`WindowKeyEvents.cpp`): the app is its
  own host.

## Known gaps

- Only Linux has been used as a build host so far. The macOS and Windows
  steps above use the same toolchain but have not been exercised.
- No release signing or Play Store upload is set up; APKs are signed by hand
  with a debug key.
- There is no Android counterpart to iOS's `StandaloneStateAutosave.mm`, which
  saves the app state when the app is backgrounded. Whether the state
  survives Android killing a backgrounded app has not been tested.
- Dragging a file from another app onto a tile is untested.
- The app log is only reachable in debuggable builds.
- The launcher icons in `android/app/src/main/res/mipmap-*` are resized copies
  of `plugin/icon/icon.png`; nothing regenerates them when it changes.
