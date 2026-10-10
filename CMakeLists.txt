cmake_minimum_required(VERSION 3.22.1)

# Minimum macOS version the binaries will run on. Must be set BEFORE project(),
# otherwise CMake defaults it to the build machine's own macOS version and the
# app refuses to launch on anything older ("compatibility" dialog, e.g. built
# on macOS 15 -> won't open on Ventura 13). JUCE 9 supports back to 10.11;
# 10.15 covers the Intel slice, the arm64 slice is floored at 11.0 by the
# toolchain (Apple Silicon minimum). Overridable with
# -DCMAKE_OSX_DEPLOYMENT_TARGET=... ; harmless on non-Apple platforms.
# iOS builds share CMAKE_OSX_DEPLOYMENT_TARGET with macOS but need an iOS
# version number, so the 10.15 floor below would be nonsense there. T3K_IOS is
# the single switch the rest of the build reads; CMAKE_SYSTEM_NAME is set on
# the command line (-DCMAKE_SYSTEM_NAME=iOS) so it is already visible here,
# before project().
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(T3K_IOS TRUE)
endif()

if(T3K_IOS)
    # iOS 16 is the floor JUCE 9 / AVAudioSession are exercised against here,
    # and covers every iPad that can run this at all.
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL "")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "16.0" CACHE STRING "Minimum iOS version" FORCE)
    endif()
elseif(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version" FORCE)
endif()

# Single source of truth for the plugin version (also read by
# plugin/CMakeLists.txt, the packaging scripts and CI).
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" T3K_VERSION)
string(STRIP "${T3K_VERSION}" T3K_VERSION)
# Reconfigure automatically when the VERSION file changes.
# Use an absolute path so it doesn't collide with plugin/CMakeLists.txt's
# ../VERSION entry if both ever end up in the same ninja graph.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

# C is required since JUCE 9 (lunasvg and other C dependencies).
project(TONE3000 VERSION ${T3K_VERSION} LANGUAGES C CXX)
add_compile_options(-fsigned-char)

# A toolchain file sets CMAKE_SYSTEM_NAME too late for the block above (it is
# only read at project()), so T3K_IOS would stay off, the 10.15 macOS floor
# would be forced into the cache and the desktop plugin formats would be
# built for iOS. Fail here with the fix rather than somewhere in the plugin.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT T3K_IOS)
    message(FATAL_ERROR "iOS: pass -DCMAKE_SYSTEM_NAME=iOS on the configure command line "
        "(or use the ios-simulator / ios-device presets); a toolchain file sets it too late "
        "for the deployment-target floor.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs)

# Windows: link the MSVC runtime statically (/MT). The plugin formats load
# into host processes we don't control and the installer ships no vc_redist,
# so a dynamically linked runtime fails on any machine without the VC++
# Redistributable installed ("MSVCP140.dll was not found", #129). Set before
# the CPM packages so every target in the tree links the same runtime;
# ignored on non-MSVC platforms.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Download CPM package manager
include(cmake/cpm.cmake) 

# Linux: FreeType is linked statically into every GUI binary instead of
# resolved from the host at load time (issue #181). JUCE's juce_graphics
# module asks pkg-config for the system `freetype2`, which makes the plugin
# .so import FreeType by SONAME. That fails inside hosts that bundle their
# own, older FreeType (the official Ardour / Mixbus builds ship a Debian-11
# era libfreetype.so.6 in /opt/<host>/lib): the vendored HarfBuzz FreeType
# glue in juce_graphics references the FreeType 2.13 COLRv1 API
# (FT_Get_Paint & co) whenever the build host's headers are >= 2.13, lilv
# opens LV2 libraries with RTLD_NOW, and the load dies with "undefined
# symbol: FT_Get_Paint". (The VST3 only survives because Ardour opens it
# RTLD_LAZY and no colour glyph is ever painted.) Pinning the library into
# the binary removes the dependency on whatever FreeType a host happens to
# carry, which is also what the Ardour maintainers require of plugins.
#
# Mechanism: JUCEModuleSupport's _juce_create_pkgconfig_target() returns
# early when `juce::pkgconfig_<module>_LINUX_DEPS` already exists, so the
# target is created here, before JUCE is added, as static FreeType plus the
# pkg-config fontconfig JUCE would otherwise have pulled in alongside it
# (fontconfig stays dynamic: it has to read the host's font configuration
# and only old, stable Fc* entry points are used). Any freetype spill-over
# from fontconfig's Requires.private is filtered out so neither the system
# headers nor -lfreetype can reach the link. The library is built with no
# external dependencies (internal zlib, no PNG/Brotli/bzip2/HarfBuzz: the
# fonts are embedded TTFs), PIC so it links into the plugin modules, and
# with FreeType's own hidden visibility so no FT_* symbol is exported from
# our .so into the host process. 2.13.3 is the bug-fix successor of the
# 2.13.2 the ubuntu-24.04 CI image was already building against, so glyph
# rendering is unchanged. Desktop Linux only: HEADLESS builds take the
# find_package(JUCE) route and macOS / Windows / iOS never touch FreeType.
if (UNIX AND NOT APPLE AND NOT HEADLESS)
    CPMAddPackage(
        NAME freetype
        GITHUB_REPOSITORY freetype/freetype
        GIT_TAG VER-2-13-3
        VERSION 2.13.3
        SOURCE_DIR ${LIB_DIR}/freetype
        EXCLUDE_FROM_ALL YES
        OPTIONS
            "FT_DISABLE_ZLIB ON"
            "FT_DISABLE_BZIP2 ON"
            "FT_DISABLE_PNG ON"
            "FT_DISABLE_HARFBUZZ ON"
            "FT_DISABLE_BROTLI ON"
            "SKIP_INSTALL_ALL ON"
    )
    if (NOT TARGET freetype)
        message(FATAL_ERROR "T3K_STATIC_FREETYPE: the freetype target was not created by "
            "${LIB_DIR}/freetype; its CMake layout likely changed. Update the "
            "package block in CMakeLists.txt.")
    endif()
    set_target_properties(freetype PROPERTIES POSITION_INDEPENDENT_CODE ON)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(T3K_FONTCONFIG REQUIRED fontconfig)
    list(FILTER T3K_FONTCONFIG_LINK_LIBRARIES EXCLUDE REGEX "freetype")
    list(FILTER T3K_FONTCONFIG_INCLUDE_DIRS EXCLUDE REGEX "freetype")

    add_library(pkgconfig_juce_graphics_LINUX_DEPS INTERFACE)
    add_library(juce::pkgconfig_juce_graphics_LINUX_DEPS ALIAS pkgconfig_juce_graphics_LINUX_DEPS)
    target_link_libraries(pkgconfig_juce_graphics_LINUX_DEPS INTERFACE
        freetype
        ${T3K_FONTCONFIG_LINK_LIBRARIES})
    target_include_directories(pkgconfig_juce_graphics_LINUX_DEPS INTERFACE
        ${T3K_FONTCONFIG_INCLUDE_DIRS})
    target_compile_options(pkgconfig_juce_graphics_LINUX_DEPS INTERFACE
        ${T3K_FONTCONFIG_CFLAGS_OTHER})
    # FreeType's own FT_EXPORT macro stamps every API function with
    # visibility("default") (public-macros.h), overriding the hidden preset,
    # so left alone the plugin .so would re-export all ~150 FT_* symbols as
    # preemptible. In a host that already carries a FreeType, the dynamic
    # linker resolves a dlopen'd library's preemptible references against the
    # global scope first: our calls would bind to the host's copy for every
    # symbol it has and to our own for the rest, mixing two implementations
    # on one FT_Library. --exclude-libs makes the archive's symbols local in
    # each binary that links it, so they are neither exported nor preempted.
    target_link_options(pkgconfig_juce_graphics_LINUX_DEPS INTERFACE
        ${T3K_FONTCONFIG_LDFLAGS_OTHER}
        "LINKER:--exclude-libs,libfreetype.a")
    message(STATUS "FreeType 2.13.3 linked statically into the Linux GUI targets (fontconfig stays dynamic)")
endif()

# Download JUCE using CPM
CPMAddPackage(
    NAME JUCE
    GIT_TAG 9.0.3
    VERSION 9.0.3
    GITHUB_REPOSITORY juce-framework/JUCE
    SOURCE_DIR ${LIB_DIR}/juce
)

# JUCE source patches (idempotent, configure time; JUCE lives in the
# gitignored libs/). Each one is anchored on a T3K_* marker so a re-run is a
# no-op, and fails loudly when the JUCE layout moves under it.

# macOS: when Thread::startRealtimeThread's time-constraint upgrade is
# rejected by the kernel (thread_policy_set fails, e.g. for very long
# per-buffer budgets), the new pthread exits BEFORE juce_threadEntryPoint,
# so closeThreadHandle() never runs and the dead thread's handle stays set.
# From then on isThreadRunning() reports true forever: a fallback
# startThread() on the same object silently no-ops, and every stopThread()
# burns its whole timeout before pthread_cancelling a dangling (detached,
# already exited) pthread id. Clear the handle on the failure path so the
# object can be restarted or destroyed cleanly. Idempotent configure-time
# patch, same rationale as the patches above; applied on every platform
# (the file only compiles on macOS, patching it elsewhere is harmless and
# keeps the libs/juce tree identical everywhere).
set(_juce_threads_mac "${LIB_DIR}/juce/modules/juce_core/native/juce_Threads_mac.mm")
if (EXISTS "${_juce_threads_mac}")
    file(READ "${_juce_threads_mac}" _juce_threads_mac_src)
    if (NOT _juce_threads_mac_src MATCHES "T3K_RT_START_STALE_HANDLE")
        string(REPLACE
[=[    return threadId != nullptr
        && threadData.started.get_future().get();]=]
[=[    // T3K_RT_START_STALE_HANDLE: a failed realtime upgrade exits the
    // pthread before juce_threadEntryPoint, so closeThreadHandle() never
    // runs; clear the stale handle so this Thread can be restarted or
    // destroyed without stopThread() stalling on a dead thread.
    const auto started = threadId != nullptr
        && threadData.started.get_future().get();

    if (! started)
    {
        threadHandle = nullptr;
        threadId = {};
    }

    return started;]=]
            _juce_threads_mac_src "${_juce_threads_mac_src}")
        if (NOT _juce_threads_mac_src MATCHES "T3K_RT_START_STALE_HANDLE")
            message(FATAL_ERROR
                "T3K_RT_START_STALE_HANDLE: failed to patch ${_juce_threads_mac}. "
                "The JUCE source layout likely changed in this version. Update the patch "
                "anchor in CMakeLists.txt.")
        endif()
        file(WRITE "${_juce_threads_mac}" "${_juce_threads_mac_src}")
        message(STATUS "Patched JUCE Thread (macOS): stale handle cleared when realtime start fails")
    endif()
endif()

# StandaloneFilterWindow patches (idempotent; JUCE lives in gitignored libs/).
#
# T3K_NO_MUTE_BANNER: never show JUCE's native "Audio input is muted" strip.
# The plugin surfaces mute/feedback state in its own AppBanner; the native
# bar would double up and fight over window height.
#
# T3K_DARK_MUTE_BANNER: leftover restyle of that strip (kept so a missed hide
# patch doesn't flash the stock lightgoldenrodyellow bar). Harmless when hidden.
#
# T3K_AUDIO_SAFE_RESTORE: crash-safe restore of the saved audio device. A tiny
# sibling sentinel next to the settings file is created just before opening the
# saved device and cleared once a live device confirms the open
# (StandaloneAudioSettings::markAudioInitClean). If it's still present next
# launch, last open crashed and we boot on the default device instead.
set(_juce_standalone_h "${LIB_DIR}/juce/modules/juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h")
if (EXISTS "${_juce_standalone_h}")
    file(READ "${_juce_standalone_h}" _juce_standalone_src)
    # Preserve whatever line ending JUCE shipped with (CRLF on some checkouts).
    if (_juce_standalone_src MATCHES "\r\n")
        set(_juce_nl "\r\n")
    else()
        set(_juce_nl "\n")
    endif()

    if (NOT _juce_standalone_src MATCHES "T3K_NO_MUTE_BANNER")
        set(_juce_mute_banner_old
"        void inputMutedChanged (bool newInputMutedValue)
        {
            shouldShowNotification = newInputMutedValue;
            notification.setVisible (shouldShowNotification);")
        set(_juce_mute_banner_new
"        void inputMutedChanged (bool newInputMutedValue)
        {
            // T3K_NO_MUTE_BANNER: the muted-input state is surfaced by the
            // plugin's own AppBanner (\"input-muted\"), which
            // grows the window instead of overlaying the editor. Never show
            // JUCE's native strip so the two can't double up or fight over
            // the window height.
            ignoreUnused (newInputMutedValue);
            shouldShowNotification = false;
            notification.setVisible (shouldShowNotification);")
        string(REPLACE "\n" "${_juce_nl}" _juce_mute_banner_old "${_juce_mute_banner_old}")
        string(REPLACE "\n" "${_juce_nl}" _juce_mute_banner_new "${_juce_mute_banner_new}")
        string(REPLACE
            "${_juce_mute_banner_old}"
            "${_juce_mute_banner_new}"
            _juce_standalone_src "${_juce_standalone_src}")
        if (NOT _juce_standalone_src MATCHES "T3K_NO_MUTE_BANNER")
            message(FATAL_ERROR
                "T3K_NO_MUTE_BANNER: failed to patch ${_juce_standalone_h}. "
                "The JUCE source layout likely changed in this version. Update the patch "
                "anchors in CMakeLists.txt.")
        endif()
        set(_juce_standalone_dirty TRUE)
        message(STATUS "Patched JUCE StandaloneFilterWindow: hide native mute banner")
    endif()

    if (NOT _juce_standalone_src MATCHES "T3K_DARK_MUTE_BANNER")
        string(REPLACE
            "notification.setColour (Label::textColourId, Colours::black);"
            "notification.setColour (Label::textColourId, Colours::white); // T3K_DARK_MUTE_BANNER"
            _juce_standalone_src "${_juce_standalone_src}")
        string(REPLACE
            "g.setColour (Colours::darkgoldenrod);"
            "g.setColour (Colour (0xffffff00)); // T3K_DARK_MUTE_BANNER divider"
            _juce_standalone_src "${_juce_standalone_src}")
        string(REPLACE
            "g.setColour (Colours::lightgoldenrodyellow);"
            "g.setColour (Colours::black); // T3K_DARK_MUTE_BANNER background"
            _juce_standalone_src "${_juce_standalone_src}")
        if (NOT _juce_standalone_src MATCHES "T3K_DARK_MUTE_BANNER")
            message(FATAL_ERROR
                "T3K_DARK_MUTE_BANNER: failed to patch ${_juce_standalone_h}. "
                "The JUCE source layout likely changed in this version. Update the patch "
                "anchors in CMakeLists.txt.")
        endif()
        set(_juce_standalone_dirty TRUE)
        message(STATUS "Patched JUCE StandaloneFilterWindow: dark mute-warning banner")
    endif()

    if (NOT _juce_standalone_src MATCHES "T3K_AUDIO_SAFE_RESTORE")
        set(_juce_sentinel_method
"    void valueChanged (Value& value) override            { muteInput = (bool) value.getValue(); }

    // T3K_AUDIO_SAFE_RESTORE: sentinel file dropped just before opening the
    // saved audio device and cleared once a live device confirms the open
    // survived. If it's still present next launch, the last open crashed (see
    // reloadAudioDeviceState) and we boot on the default device instead. Its
    // own tiny file next to the settings store, never the multi-MB store
    // itself. Empty File() when settings aren't a PropertiesFile.
    File getAudioOpenSentinelFile() const
    {
        if (auto* pf = dynamic_cast<PropertiesFile*> (settings.get()))
            return pf->getFile().getSiblingFile (\"TONE3000-audio-open.tmp\");

        return {};
    }")
        # Normalize the replacement's newlines to match the file.
        string(REPLACE "\n" "${_juce_nl}" _juce_sentinel_method "${_juce_sentinel_method}")
        string(REPLACE
            "    void valueChanged (Value& value) override            { muteInput = (bool) value.getValue(); }"
            "${_juce_sentinel_method}"
            _juce_standalone_src "${_juce_standalone_src}")

        set(_juce_sentinel_reload_old
"            savedState = settings->getXmlValue (\"audioSetup\");

           #if ! (JUCE_IOS || JUCE_ANDROID)
            shouldMuteInput.setValue (settings->getBoolValue (\"shouldMuteInput\", true));
           #endif")
        set(_juce_sentinel_reload_new
"            savedState = settings->getXmlValue (\"audioSetup\");

            // T3K_AUDIO_SAFE_RESTORE: if the sentinel from last launch is still
            // here, opening the saved device crashed (e.g. a mic + Bluetooth
            // output CoreAudio aggregate whose combiner JUCE can crash tearing
            // down when the BT device churns at startup). Boot clean on the
            // default device; a clean run overwrites \"audioSetup\" on exit.
            const auto sentinel = getAudioOpenSentinelFile();
            if (sentinel != File() && sentinel.existsAsFile())
                savedState.reset();
            if (sentinel != File())
                sentinel.create();

           #if ! (JUCE_IOS || JUCE_ANDROID)
            shouldMuteInput.setValue (settings->getBoolValue (\"shouldMuteInput\", true));
           #endif")
        string(REPLACE "\n" "${_juce_nl}" _juce_sentinel_reload_old "${_juce_sentinel_reload_old}")
        string(REPLACE "\n" "${_juce_nl}" _juce_sentinel_reload_new "${_juce_sentinel_reload_new}")
        string(REPLACE
            "${_juce_sentinel_reload_old}"
            "${_juce_sentinel_reload_new}"
            _juce_standalone_src "${_juce_standalone_src}")

        if (NOT _juce_standalone_src MATCHES "T3K_AUDIO_SAFE_RESTORE")
            message(FATAL_ERROR
                "T3K_AUDIO_SAFE_RESTORE: failed to patch ${_juce_standalone_h}. "
                "The JUCE source layout likely changed in this version. Update the patch "
                "anchors in CMakeLists.txt.")
        endif()
        set(_juce_standalone_dirty TRUE)
        message(STATUS "Patched JUCE StandaloneFilterWindow: crash-safe audio device restore")
    endif()

    if (_juce_standalone_dirty)
        file(WRITE "${_juce_standalone_h}" "${_juce_standalone_src}")
    endif()
endif()

# T3K_SETTINGS_IN_APP_FOLDER: keep the standalone app's settings file inside
# the app's own per-user data folder on every platform. Stock JUCE drops
# TONE3000.settings loose into the shared config root (~/.config on Linux,
# ~/Library/Application Support on macOS; Windows already resolves to
# %APPDATA%/TONE3000 and is unchanged). Linux resolves through
# userApplicationDataDirectory so XDG_CONFIG_HOME is honoured, the same root
# as presets and logs. The processor's own machine-wide
# store in the same folder is named preferences.settings (see
# ProcessorState.cpp), so the two files cannot collide. Idempotent
# configure-time patch, same rationale as the patches above.
set(_juce_standalone_cpp "${LIB_DIR}/juce/modules/juce_audio_plugin_client/juce_audio_plugin_client_Standalone.cpp")
if (EXISTS "${_juce_standalone_cpp}")
    file(READ "${_juce_standalone_cpp}" _juce_standalone_cpp_src)
    if (NOT _juce_standalone_cpp_src MATCHES "T3K_SETTINGS_IN_APP_FOLDER")
        if (_juce_standalone_cpp_src MATCHES "\r\n")
            set(_juce_nl "\r\n")
        else()
            set(_juce_nl "\n")
        endif()
        set(_juce_settings_folder_old
[=[       #if JUCE_LINUX || JUCE_BSD
        options.folderName          = "~/.config";
       #else
        options.folderName          = "";
       #endif]=])
        set(_juce_settings_folder_new
[=[        // T3K_SETTINGS_IN_APP_FOLDER: keep the settings file inside the
        // app's own data folder instead of loose in the shared config root
        // (~/.config/<App>.settings on Linux, ~/Library/Application
        // Support/<App>.settings on macOS). Linux resolves through
        // userApplicationDataDirectory so XDG_CONFIG_HOME is honoured.
       #if JUCE_LINUX || JUCE_BSD
        options.folderName          = File::getSpecialLocation (File::userApplicationDataDirectory)
                                          .getChildFile (options.applicationName)
                                          .getFullPathName();
       #else
        options.folderName          = options.applicationName;
       #endif]=])
        string(REPLACE "\n" "${_juce_nl}" _juce_settings_folder_old "${_juce_settings_folder_old}")
        string(REPLACE "\n" "${_juce_nl}" _juce_settings_folder_new "${_juce_settings_folder_new}")
        string(REPLACE "${_juce_settings_folder_old}" "${_juce_settings_folder_new}" _juce_standalone_cpp_src "${_juce_standalone_cpp_src}")
        if (NOT _juce_standalone_cpp_src MATCHES "T3K_SETTINGS_IN_APP_FOLDER")
            message(FATAL_ERROR
                "T3K_SETTINGS_IN_APP_FOLDER: failed to patch ${_juce_standalone_cpp}. "
                "The JUCE source layout likely changed in this version. Update the patch "
                "anchors in CMakeLists.txt.")
        endif()
        file(WRITE "${_juce_standalone_cpp}" "${_juce_standalone_cpp_src}")
        message(STATUS "Patched JUCE StandaloneFilterApp: settings file kept inside the app data folder")
    endif()
endif()

# T3K_AUDIO_DEFAULT_SETUP: the default sample rate for an unset (0) request
# in AudioDeviceManager::setAudioDeviceSetup. JUCE 9.0.3 resolves a 0 rate /
# buffer to device defaults again (9.0.1 fed the 0 to findNearestValue and
# opened at the device minimum, a 16-sample ALSA period the chain could not
# survive), but its default rate is "whatever the device is at now, else
# 44.1 kHz", and that "current" rate is taken on trust. A device that is
# not yet open, or reports a rate outside its own list, then lands wherever
# findNearestValue(44100) falls, which can be below 44.1 kHz. Prefer the
# lowest rate at or above 44.1 kHz instead, falling back to nearest 48 kHz
# (the rate the chains run at); the buffer default is the device's own, as
# upstream. Idempotent configure-time patch, same rationale as the patches
# above. Cross-platform file, so this applies everywhere.
set(_juce_adm_cpp "${LIB_DIR}/juce/modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp")
if (EXISTS "${_juce_adm_cpp}")
    file(READ "${_juce_adm_cpp}" _juce_adm_src)
    if (NOT _juce_adm_src MATCHES "T3K_AUDIO_DEFAULT_SETUP")
        set(_juce_adm_old
[=[static double getDefaultRequestedSampleRate (AudioIODevice& device)
{
    if (const auto current = device.getCurrentSampleRate(); current > 0.0)
        return current;

    return 44100.0;
}]=])
        set(_juce_adm_new
[=[static double getDefaultRequestedSampleRate (AudioIODevice& device)
{
    // T3K_AUDIO_DEFAULT_SETUP: only trust the current rate when the device
    // lists it; otherwise the lowest rate at or above 44.1 kHz, else the
    // caller's findNearestValue settles 48 kHz to the closest available.
    const auto rates = device.getAvailableSampleRates();

    if (const auto current = device.getCurrentSampleRate();
        current > 0.0 && rates.contains (current))
        return current;

    double lowestAbove44 = 0.0;

    for (const auto sr : rates)
        if (sr >= 44100.0 && (lowestAbove44 < 1.0 || sr < lowestAbove44))
            lowestAbove44 = sr;

    return lowestAbove44 > 0.0 ? lowestAbove44 : 48000.0;
}]=])
        string(REPLACE "${_juce_adm_old}" "${_juce_adm_new}" _juce_adm_src "${_juce_adm_src}")
        if (NOT _juce_adm_src MATCHES "T3K_AUDIO_DEFAULT_SETUP")
            message(FATAL_ERROR
                "T3K_AUDIO_DEFAULT_SETUP: failed to patch ${_juce_adm_cpp}. "
                "The JUCE source layout likely changed in this version. Update the patch "
                "anchors in CMakeLists.txt.")
        endif()
        file(WRITE "${_juce_adm_cpp}" "${_juce_adm_src}")
        message(STATUS "Patched JUCE AudioDeviceManager: unset sample rate request prefers 44.1 kHz and up, then 48 kHz")
    endif()
endif()

# T3K_ALSA_CLOSE_STUCK_TIMEOUT: ALSAThread::close() waits a fixed 400ms for
# the audio thread to exit before deciding it's stuck and force-closing the
# ALSA handles from the message thread (closeNow(), i.e. snd_pcm_close while
# the audio thread may still be inside snd_pcm_readi/writei). A single i/o
# period longer than 400ms - a large buffer at a low sample rate - is then
# indistinguishable from "stuck", and the forced close frees the pcm (and the
# input/output buffers backing the callback's channel pointers) out from
# under a thread that's still using them: reliably a use-after-free on any
# device reconfiguration. Confirmed live with gdb: closeNow() fires on
# ordinary channel-selection changes with numCallbacks == callbacksToStop
# (no callback landed during the wait - the thread was mid-read, not
# wedged), matching this fix exactly. Matches upstream JUCE PR
# https://github.com/juce-framework/JUCE/pull/1707 (unmerged as of this
# writing); drop this patch once it lands. Idempotent configure-time patch,
# same rationale as the patches above.
set(_juce_alsa_cpp "${LIB_DIR}/juce/modules/juce_audio_devices/native/juce_ALSA_linux.cpp")
if (EXISTS "${_juce_alsa_cpp}")
    file(READ "${_juce_alsa_cpp}" _juce_alsa_src)
    if (NOT _juce_alsa_src MATCHES "T3K_ALSA_CLOSE_STUCK_TIMEOUT")
        set(_juce_alsa_old
[=[            const int callbacksToStop = numCallbacks;

            if ((! waitForThreadToExit (400)) && audioIoInProgress && numCallbacks == callbacksToStop)]=])
        set(_juce_alsa_new
[=[            const int callbacksToStop = numCallbacks;

            // T3K_ALSA_CLOSE_STUCK_TIMEOUT: a fixed 400ms wait can't tell a
            // stuck thread from one that's simply mid-read on a large
            // buffer/low sample rate combination (a 4096-sample buffer at
            // 8kHz is a 512ms period), so scale the wait by one i/o period,
            // capped so a genuinely wedged device still closes promptly.
            const auto ioPeriodMs = sampleRate > 0.0 ? (int) (1000.0 * bufferSize / sampleRate) : 0;
            const auto stuckTimeoutMs = 400 + jmin (2000, ioPeriodMs);

            if ((! waitForThreadToExit (stuckTimeoutMs)) && audioIoInProgress && numCallbacks == callbacksToStop)]=])
        string(REPLACE "${_juce_alsa_old}" "${_juce_alsa_new}" _juce_alsa_src "${_juce_alsa_src}")
        if (NOT _juce_alsa_src MATCHES "T3K_ALSA_CLOSE_STUCK_TIMEOUT")
            message(FATAL_ERROR
                "T3K_ALSA_CLOSE_STUCK_TIMEOUT: failed to patch ${_juce_alsa_cpp}. "
                "The JUCE source layout likely changed in this version. Update the patch "
                "anchors in CMakeLists.txt.")
        endif()
        file(WRITE "${_juce_alsa_cpp}" "${_juce_alsa_src}")
        message(STATUS "Patched JUCE ALSAThread: scale the stuck-thread close timeout by the i/o period")
    endif()
endif()

# T3K_CALLBACK_ENFORCER_NULL_SAFE: CallbackMaxSizeEnforcer sizes its stored
# channel vectors from the device's active-channel bitsets in
# audioDeviceAboutToStart, but only fills in the number of channels the
# device actually passes to each callback, then forwards the *vector's full
# size* to the inner callback (AudioProcessorPlayer) regardless. If a
# device's bitsets and channel counts ever disagree - JUCE 9.0.3 fixed the
# ALSA case (the input request is widened to the hardware floor before the
# callback array is built), but any other AudioIODevice backend with the
# same class of mismatch is still exposed - the tail of those vectors still
# holds the null pointers std::vector::resize() default-constructed, and
# gets forwarded as if they were real channels: AudioProcessorPlayer memcpys
# from them unconditionally, and LevelMeter::updateLevel dereferences them
# unconditionally too. jassert catches the mismatch in a debug build; a
# Release build (jassert compiles out) passes the nulls straight through and
# crashes on the read. AudioIODeviceCallback's own docs say a channel pointer
# may legitimately be null, so both call sites need to tolerate it as well as
# forwarding the correct (filled-in) count. Fix from upstream JUCE PR
# https://github.com/juce-framework/JUCE/pull/1708 (unmerged as of this
# writing); drop this patch once it lands. Idempotent configure-time patch,
# same rationale as the patches above. Cross-platform file, so this applies
# everywhere.
set(_juce_adm_cpp2 "${LIB_DIR}/juce/modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp")
if (EXISTS "${_juce_adm_cpp2}")
    file(READ "${_juce_adm_cpp2}" _juce_adm_src2)
    if (NOT _juce_adm_src2 MATCHES "T3K_CALLBACK_ENFORCER_NULL_SAFE")
        set(_juce_adm_old2
[=[    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           [[maybe_unused]] int numInputChannels,
                                           float* const* outputChannelData,
                                           [[maybe_unused]] int numOutputChannels,
                                           int numSamples,
                                           const AudioIODeviceCallbackContext& context) override
    {
        jassert ((int) storedInputChannels.size()  == numInputChannels);
        jassert ((int) storedOutputChannels.size() == numOutputChannels);

        int position = 0;

        while (position < numSamples)
        {
            const auto blockLength = jmin (maximumSize, numSamples - position);

            const auto addOffset = [position] (auto ptr) { return ptr + position; };
            std::transform (inputChannelData,  inputChannelData  + numInputChannels,  storedInputChannels .begin(), addOffset);
            std::transform (outputChannelData, outputChannelData + numOutputChannels, storedOutputChannels.begin(), addOffset);

            inner.audioDeviceIOCallbackWithContext (storedInputChannels.data(),
                                                    (int) storedInputChannels.size(),
                                                    storedOutputChannels.data(),
                                                    (int) storedOutputChannels.size(),
                                                    blockLength,
                                                    context);

            position += blockLength;
        }
    }]=])
        set(_juce_adm_new2
[=[    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const AudioIODeviceCallbackContext& context) override
    {
        jassert ((int) storedInputChannels.size()  == numInputChannels);
        jassert ((int) storedOutputChannels.size() == numOutputChannels);

        // T3K_CALLBACK_ENFORCER_NULL_SAFE: a device whose active-channel
        // bitsets disagree with the channel arrays it passes here leaves the
        // tail of the stored vectors holding the null pointers resize()
        // default-constructed, so forward the number of channels actually
        // filled in below, not the vectors' full size.
        const auto numIns  = jmin ((int) storedInputChannels.size(),  numInputChannels);
        const auto numOuts = jmin ((int) storedOutputChannels.size(), numOutputChannels);

        int position = 0;

        while (position < numSamples)
        {
            const auto blockLength = jmin (maximumSize, numSamples - position);

            const auto addOffset = [position] (auto ptr) { return ptr + position; };
            std::transform (inputChannelData,  inputChannelData  + numIns,  storedInputChannels .begin(), addOffset);
            std::transform (outputChannelData, outputChannelData + numOuts, storedOutputChannels.begin(), addOffset);

            inner.audioDeviceIOCallbackWithContext (storedInputChannels.data(),
                                                    numIns,
                                                    storedOutputChannels.data(),
                                                    numOuts,
                                                    blockLength,
                                                    context);

            position += blockLength;
        }
    }]=])
        string(REPLACE "${_juce_adm_old2}" "${_juce_adm_new2}" _juce_adm_src2 "${_juce_adm_src2}")

        set(_juce_adm_old2b
[=[            for (int i = 0; i < numChannels; ++i)
                s += std::abs (channelData[i][j]);]=])
        set(_juce_adm_new2b
[=[            // T3K_CALLBACK_ENFORCER_NULL_SAFE: a channel not specified in
            // AudioIODevice::open() is passed as a null pointer, as
            // documented by AudioIODeviceCallback.
            for (int i = 0; i < numChannels; ++i)
                if (auto* channel = channelData[i])
                    s += std::abs (channel[j]);]=])
        string(REPLACE "${_juce_adm_old2b}" "${_juce_adm_new2b}" _juce_adm_src2 "${_juce_adm_src2}")

        if (NOT _juce_adm_src2 MATCHES "T3K_CALLBACK_ENFORCER_NULL_SAFE")
            message(FATAL_ERROR
                "T3K_CALLBACK_ENFORCER_NULL_SAFE: failed to patch ${_juce_adm_cpp2}. "
                "The JUCE source layout likely changed in this version. Update the patch "
                "anchors in CMakeLists.txt.")
        endif()
        file(WRITE "${_juce_adm_cpp2}" "${_juce_adm_src2}")
        message(STATUS "Patched JUCE AudioDeviceManager: CallbackMaxSizeEnforcer/LevelMeter tolerate null/short channel arrays")
    endif()
endif()

# T3K_ZOOM_TOGGLE: make the standalone window's native zoom button ("+", the
# green traffic light) a real toggle: first click grows the window to the
# editor's maximum scale, second click restores the pre-zoom size. Stock JUCE
# already has this shape in windowWillUseStandardFrame, but three bugs keep it
# from ever toggling back: it reads max size / fixed aspect straight off the
# peer constrainer (the standalone's DecoratorConstrainer only applies those
# inside checkBounds, so it sees "unlimited" and zooms to the whole screen),
# it compares the window *frame* against the *content* bounds (always off by
# the title bar), and it uses exact float equality. Route the frame through
# constrainRect (the same pass AppKit resizing uses) and compare sizes with
# tolerance instead. The zoomed frame keeps the window's top-left corner and
# is clamped on-screen.
set(_juce_peer_mm "${LIB_DIR}/juce/modules/juce_gui_basics/native/juce_NSViewComponentPeer_mac.mm")
if (EXISTS "${_juce_peer_mm}")
    file(READ "${_juce_peer_mm}" _juce_peer_src)
    if (NOT _juce_peer_src MATCHES "T3K_ZOOM_TOGGLE")
        set(_juce_zoom_old
[=[                if (auto* constrainer = owner->getConstrainer())
                {
                    if (auto* screen = [window screen])
                    {
                        const auto safeScreenBounds = convertToRectFloat (flippedScreenRect (owner->hasNativeTitleBar() ? r : [screen visibleFrame]));
                        const auto originalBounds = owner->getFrameSize().addedTo (owner->getComponent().getScreenBounds()).toFloat();
                        const auto expanded = originalBounds.withWidth  ((float) constrainer->getMaximumWidth())
                                                            .withHeight ((float) constrainer->getMaximumHeight());
                        const auto constrained = expanded.constrainedWithin (safeScreenBounds);

                        return flippedScreenRect (makeCGRect ([&]
                                                              {
                                                                  if (constrained == owner->getBounds().toFloat())
                                                                      return owner->lastSizeBeforeZoom.toFloat();

                                                                  owner->lastSizeBeforeZoom = owner->getBounds().toFloat();
                                                                  return constrained;
                                                              }()));
                    }
                }]=])
        set(_juce_zoom_new
[=[                // T3K_ZOOM_TOGGLE (see CMakeLists patch comment)
                if (owner->getConstrainer() != nullptr)
                {
                    const auto currentFrame = [window frame];

                    // Grow around the window's own top-left corner, then let
                    // the real constrainer pass shape it (max size, fixed
                    // aspect ratio, title-bar border).
                    NSRect desired = r;
                    desired.origin.x = currentFrame.origin.x;
                    desired.origin.y = NSMaxY (currentFrame) - desired.size.height;
                    auto standardFrame = owner->constrainRect (desired);
                    standardFrame.origin.y = NSMaxY (currentFrame) - standardFrame.size.height;
                    standardFrame.origin.x = std::max (r.origin.x, std::min (standardFrame.origin.x, NSMaxX (r) - standardFrame.size.width));
                    standardFrame.origin.y = std::max (r.origin.y, std::min (standardFrame.origin.y, NSMaxY (r) - standardFrame.size.height));

                    const auto alreadyZoomed = std::abs (currentFrame.size.width  - standardFrame.size.width)  < 2.0
                                            && std::abs (currentFrame.size.height - standardFrame.size.height) < 2.0;

                    if (alreadyZoomed && ! owner->lastSizeBeforeZoom.isEmpty())
                        return flippedScreenRect (makeCGRect (owner->getFrameSize()
                                                                  .addedTo (owner->lastSizeBeforeZoom.toNearestInt())
                                                                  .toFloat()));

                    owner->lastSizeBeforeZoom = owner->getBounds().toFloat();
                    return standardFrame;
                }]=])
        string(REPLACE "${_juce_zoom_old}" "${_juce_zoom_new}" _juce_peer_src "${_juce_peer_src}")
        if (NOT _juce_peer_src MATCHES "T3K_ZOOM_TOGGLE")
            message(FATAL_ERROR
                "T3K_ZOOM_TOGGLE: failed to patch ${_juce_peer_mm}. "
                "The JUCE source layout likely changed in this version. Update the patch "
                "anchors in CMakeLists.txt.")
        endif()
        file(WRITE "${_juce_peer_mm}" "${_juce_peer_src}")
        message(STATUS "Patched JUCE NSViewComponentPeer: toggling zoom button")
    endif()
endif()

# T3K_X11_HINT_OVERFLOW: fix the Linux standalone opening as a bare title bar
# over a full-width, 1px-high window on scaled X11 desktops (GitHub issue #43).
# JUCE publishes WM_NORMAL_HINTS min/max from the peer's constrainer multiplied
# by the display scale. The standalone window's DecoratorConstrainer reports
# the unset ComponentBoundsConstrainer defaults (max 0x3FFFFFFF), and on any
# desktop that advertises a window scale of 2 (real HiDPI, or Cinnamon/GNOME
# fractional scaling at 125%/150%, which round up to 2 in XSettings
# Gdk/WindowScalingFactor) the published PMaxSize lands within a title-bar's
# height of INT_MAX. Mutter-family window managers (Muffin on Mint, Mutter on
# GNOME) add the frame border back onto those hints, overflow int, and clamp
# the window to the PMinSize floor instead: the window maps at full width but
# 1px high, showing only its title bar, and the editor (still full-size,
# but clipped inside the 1px parent) never becomes visible.
# Clamp the published hints to a bound no real display can reach (1e7 px) so
# WM-side arithmetic stays finite; genuine limits (this editor publishes
# 1x..2x of its design box) pass through unchanged. Computing in doubles also
# keeps the scaled default from overflowing the int cast itself (UB at a
# scale factor of 3+ in stock JUCE). script/test-linux-window-scale.sh
# reproduces the collapse against a stock build (hints reach the WM as max
# 2147483646x2147483609, the window maps at 2048x1) and verifies this fix.
# Idempotent configure-time patch, same rationale as the patches above;
# applied on every platform (the file only compiles on Linux, patching it
# elsewhere is harmless and keeps the libs/juce tree identical everywhere).
# The file ships with CRLF line endings; file(READ) normalizes them to LF,
# so the anchor matches as written and the patched file is written back
# LF-only.
set(_juce_x11_cpp "${LIB_DIR}/juce/modules/juce_gui_basics/native/juce_XWindowSystem_linux.cpp")
if (EXISTS "${_juce_x11_cpp}")
    file(READ "${_juce_x11_cpp}" _juce_x11_src)
    if (NOT _juce_x11_src MATCHES "T3K_X11_HINT_OVERFLOW")
        set(_juce_x11_hints_old
[=[            hints->min_width  = jmax (1, (int) (factor * c->getMinimumWidth())  - leftAndRight);
            hints->max_width  = jmax (1, (int) (factor * c->getMaximumWidth())  - leftAndRight);
            hints->min_height = jmax (1, (int) (factor * c->getMinimumHeight()) - topAndBottom);
            hints->max_height = jmax (1, (int) (factor * c->getMaximumHeight()) - topAndBottom);]=])
        set(_juce_x11_hints_new
[=[            // T3K_X11_HINT_OVERFLOW (see the CMakeLists patch comment): the
            // standalone's unset constrainer maximums (0x3FFFFFFF) times a 2x
            // display scale put PMaxSize within a title-bar's height of
            // INT_MAX; mutter-family WMs re-add the frame border, overflow
            // int, and clamp the window to a full-width, 1px-high strip
            // (GitHub issue #43). Bound the hints below any real display size
            // so WM-side arithmetic stays finite; genuine limits pass through
            // unchanged.
            const auto boundedHint = [] (double v) { return (int) jlimit (1.0, 1.0e7, v); };
            hints->min_width  = boundedHint (factor * c->getMinimumWidth()  - leftAndRight);
            hints->max_width  = boundedHint (factor * c->getMaximumWidth()  - leftAndRight);
            hints->min_height = boundedHint (factor * c->getMinimumHeight() - topAndBottom);
            hints->max_height = boundedHint (factor * c->getMaximumHeight() - topAndBottom);]=])
        string(REPLACE "${_juce_x11_hints_old}" "${_juce_x11_hints_new}" _juce_x11_src "${_juce_x11_src}")
        if (NOT _juce_x11_src MATCHES "T3K_X11_HINT_OVERFLOW")
            message(FATAL_ERROR
                "T3K_X11_HINT_OVERFLOW: failed to patch ${_juce_x11_cpp}. "
                "The JUCE source layout likely changed in this version. Update the patch "
                "anchors in CMakeLists.txt.")
        endif()
        file(WRITE "${_juce_x11_cpp}" "${_juce_x11_src}")
        message(STATUS "Patched JUCE XWindowSystem: WM size hints bounded (no 1px window on scaled X11 desktops)")
    endif()
endif()

# T3K_UIA_ANNOUNCE: stop Windows users with no screen reader hearing a
# synthetic voice from the plugin. Stock JUCE implements
# AccessibilityHandler::postAnnouncement on Windows by speaking the text
# itself through SAPI (ISpVoice::Speak, in-process, default system voice)
# whenever UiaClientsAreListening() || SPI_GETSCREENREADER is true.
# UiaClientsAreListening() is machine-wide and flips on as soon as *any*
# process registers a UI Automation event handler (Chromium/Electron apps,
# Magnifier, Game Bar, overlays, ...) and stays on until that process exits,
# so with Narrator off the plugin would still start reading browser results
# ("12 tones, page 1 of 3") out loud mid-session, until the next reboot.
# Raise a UIA notification event (UiaRaiseNotificationEvent, Windows 10
# 1709+, resolved at runtime) on the focused element instead: a real screen
# reader (Narrator, NVDA, JAWS) speaks it in its own voice and nothing speaks
# when none is running. Idempotent configure-time patch, same rationale as
# the patches above; applied on every platform (the file only compiles on
# Windows, patching it elsewhere is harmless and keeps libs/juce identical).
set(_juce_a11y_win_cpp "${LIB_DIR}/juce/modules/juce_gui_basics/native/accessibility/juce_Accessibility_windows.cpp")
if (EXISTS "${_juce_a11y_win_cpp}")
    file(READ "${_juce_a11y_win_cpp}" _juce_a11y_win_src)
    if (NOT _juce_a11y_win_src MATCHES "T3K_UIA_ANNOUNCE")
        if (_juce_a11y_win_src MATCHES "\r\n")
            set(_juce_nl "\r\n")
        else()
            set(_juce_nl "\n")
        endif()
        set(_juce_a11y_announce_old
[=[void AccessibilityHandler::postAnnouncement (const String& announcementString, AnnouncementPriority priority)
{
    if (! areAnyAccessibilityClientsActive())
        return;

    if (auto* sharedVoice = SpVoiceWrapper::getInstance())
    {
        auto voicePriority = [priority]
        {
            switch (priority)
            {
                case AnnouncementPriority::low:    return SPVPRI_OVER;
                case AnnouncementPriority::medium: return SPVPRI_NORMAL;
                case AnnouncementPriority::high:   return SPVPRI_ALERT;
            }

            jassertfalse;
            return SPVPRI_OVER;
        }();

        sharedVoice->voice->SetPriority (voicePriority);
        sharedVoice->voice->Speak (announcementString.toWideCharPointer(), SPF_ASYNC, nullptr);
    }
}]=])
        set(_juce_a11y_announce_new
[=[// T3K_UIA_ANNOUNCE (see the CMakeLists patch comment): stock JUCE speaks
// announcements itself through SAPI whenever UiaClientsAreListening() is
// true, a machine-wide flag any UIA client process flips on. Hand the text
// to UI Automation as a notification event instead, so only an actual
// screen reader reads it (in its own voice) and nothing speaks otherwise.
static bool t3kRaiseUiaNotification (const String& text, AccessibilityHandler::AnnouncementPriority priority)
{
    auto* handler = []() -> AccessibilityHandler*
    {
        for (auto* c = Component::getCurrentlyFocusedComponent(); c != nullptr; c = c->getParentComponent())
            if (auto* h = c->getAccessibilityHandler())
                return h;

        for (int i = ComponentPeer::getNumPeers(); --i >= 0;)
            if (auto* peer = ComponentPeer::getPeer (i))
                if (auto* h = peer->getComponent().getAccessibilityHandler())
                    return h;

        return nullptr;
    }();

    if (handler == nullptr)
        return false;

    bool raised = false;

    getProviderWithCheckedWrapper (*handler, [&] (WindowsUIAWrapper*, ComSmartPtr<IRawElementProviderSimple>& provider)
    {
        // HRESULT UiaRaiseNotificationEvent (IRawElementProviderSimple*, NotificationKind,
        //                                    NotificationProcessing, BSTR displayString,
        //                                    BSTR activityId) -- Windows 10 1709+, so it is
        // resolved at runtime; older systems simply stay quiet.
        using RaiseFunc = HRESULT (WINAPI*) (IRawElementProviderSimple*, int, int, BSTR, BSTR);

        auto uiaModule = ::GetModuleHandleA ("UIAutomationCore.dll");

        if (uiaModule == nullptr)
            return;

        JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wcast-function-type")
        auto raise = (RaiseFunc) ::GetProcAddress (uiaModule, "UiaRaiseNotificationEvent");
        JUCE_END_IGNORE_WARNINGS_GCC_LIKE

        if (raise == nullptr)
            return;

        const int kind = 2; // NotificationKind_ActionCompleted
        const int processing = priority == AccessibilityHandler::AnnouncementPriority::high   ? 0  // NotificationProcessing_ImportantAll
                             : priority == AccessibilityHandler::AnnouncementPriority::medium ? 2  // NotificationProcessing_All
                                                                                               : 3; // NotificationProcessing_MostRecent

        auto* display  = ::SysAllocString (text.toWideCharPointer());
        auto* activity = ::SysAllocString (L"JUCE.Announcement");

        raised = SUCCEEDED (raise (provider, kind, processing, display, activity));

        ::SysFreeString (activity);
        ::SysFreeString (display);
    });

    return raised;
}

void AccessibilityHandler::postAnnouncement (const String& announcementString, AnnouncementPriority priority)
{
    if (! areAnyAccessibilityClientsActive())
        return;

    // T3K_UIA_ANNOUNCE: never speak in-process through SAPI (SpVoiceWrapper);
    // the UIA notification is read by the user's screen reader, if any.
    t3kRaiseUiaNotification (announcementString, priority);
}]=])
        string(REPLACE "\n" "${_juce_nl}" _juce_a11y_announce_old "${_juce_a11y_announce_old}")
        string(REPLACE "\n" "${_juce_nl}" _juce_a11y_announce_new "${_juce_a11y_announce_new}")
        string(REPLACE "${_juce_a11y_announce_old}" "${_juce_a11y_announce_new}" _juce_a11y_win_src "${_juce_a11y_win_src}")
        if (NOT _juce_a11y_win_src MATCHES "T3K_UIA_ANNOUNCE")
            message(FATAL_ERROR
                "T3K_UIA_ANNOUNCE: failed to patch ${_juce_a11y_win_cpp}. "
                "The JUCE source layout likely changed in this version. Update the patch "
                "anchors in CMakeLists.txt.")
        endif()
        file(WRITE "${_juce_a11y_win_cpp}" "${_juce_a11y_win_src}")
        message(STATUS "Patched JUCE Accessibility (Windows): announcements go to the screen reader via UIA, not in-process SAPI speech")
    endif()
endif()

# Download GoogleTest Framework using CPM
CPMAddPackage(
    NAME GOOGLETEST
    GITHUB_REPOSITORY google/googletest
    GIT_TAG v1.15.2
    VERSION 1.15.2
    SOURCE_DIR ${LIB_DIR}/googletest
    OPTIONS
        "INSTALL_GTEST OFF"
)

enable_testing() # Allow running build tests

add_subdirectory(plugin) # Add plugin project
# The DSP suite is a host-run console binary (it reads test assets off the
# build machine's disk and is driven by script/test-dsp.sh). Cross-compiling it
# for iOS produces something that cannot be run, so keep it out of that build.
if (NOT T3K_IOS)
    add_subdirectory(test)   # DSP test suite (DspTests, see script/test-dsp.sh)
    add_subdirectory(tools)  # maintainer utilities (PresetTool), EXCLUDE_FROM_ALL
endif()
