// The plugin's per-user data folder: the "TONE3000" folder under the OS's
// app-data root, where the presets, the local-model stash, the machine-wide
// settings and the UI preferences all live. One place computes it so every
// store lands in the same folder, which is what lets a DAW, the standalone
// app and (on iOS) the AUv3 extension work from one set of files.
//
//   macOS    ~/Library/Application Support/TONE3000
//   Windows  %APPDATA%/TONE3000
//   Linux    $XDG_CONFIG_HOME/TONE3000 (~/.config/TONE3000)
//   iOS      <App Group container>/Library/TONE3000, else ~/Library/TONE3000
//
// iOS is the only platform where the choice is not fixed. The app and its
// AUv3 are separate processes with separate sandboxes, so the folder sits in
// the App Group container both are entitled to (T3K_IOS_APP_GROUP, set in
// plugin/CMakeLists.txt). A build without the group, or a device that cannot
// resolve it, falls back to the process's own container. The first call
// also adopts what earlier iOS builds kept in the app's own container, child
// by child, so an update loses no presets, models or sign-in; root() is
// therefore safe to call from a static initialiser, before any logger
// exists, and adoptionReport() says afterwards what happened.
//
// The log file is not here on purpose: it is per process (the app and the
// extension each write their own), so it stays in each process's container.
#pragma once

#include <juce_core/juce_core.h>

namespace t3k::appdata {

// The folder. Resolved once per process; may not exist yet.
juce::File root();

// What the first root() call moved into the shared container (iOS), one
// line for the log. Empty when nothing was there to move.
juce::String adoptionReport();

}  // namespace t3k::appdata
