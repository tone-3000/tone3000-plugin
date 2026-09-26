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

# Android: unlike T3K_IOS, this has no deployment-target floor that must be
# forced into the cache before project() (CMAKE_OSX_DEPLOYMENT_TARGET has no
# Android equivalent), so CMAKE_SYSTEM_NAME is read here, after project(),
# rather than needing the iOS block's pre-project() timing above. The NDK's
# own android.toolchain.cmake (passed via CMAKE_TOOLCHAIN_FILE, which the
# Gradle externalNativeBuild integration sets automatically) resolves
# CMAKE_SYSTEM_NAME=Android well before this point.
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(T3K_ANDROID TRUE)
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

# Download JUCE using CPM
CPMAddPackage(
    NAME JUCE
    GIT_TAG 9.0.1
    VERSION 9.0.1
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

# T3K_AUDIO_DEFAULT_SETUP: fix a JUCE 9 regression in
# AudioDeviceManager::setAudioDeviceSetup. JUCE 8 treated an unset (0) sample
# rate / buffer size as "use the device defaults" (chooseBestSampleRate /
# chooseBestBufferSize). JUCE 9.0.1 replaced both with
# findNearestValue(available, requested), and nearest-to-0 is the device
# MINIMUM: switching devices with a zeroed setup (our
# StandaloneAudioSettings::setDevice, and JUCE's own first-boot initialise)
# opens e.g. a Focusrite on ALSA at 44.1 kHz with a 16-sample period, which
# the DSP chain cannot survive (observed as a segfault on device pick on
# Linux). Restore the JUCE 8 semantics for unset values; explicit requests
# keep JUCE 9's nearest-supported clamping. Also passes the resolved sample
# rate (not the raw, possibly-0 request) to the post-open buffer-size
# correction, which stock JUCE 9 gets wrong too. Idempotent configure-time
# patch, same rationale as the patches above. Cross-platform file, so this
# applies everywhere. Drop when upstream fixes it.
set(_juce_adm_cpp "${LIB_DIR}/juce/modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp")
if (EXISTS "${_juce_adm_cpp}")
    file(READ "${_juce_adm_cpp}" _juce_adm_src)
    if (NOT _juce_adm_src MATCHES "T3K_AUDIO_DEFAULT_SETUP")
        set(_juce_adm_old
[=[    const auto sampleRate = findNearestValue (Span { currentAudioDevice->getAvailableSampleRates() }, currentSetup.sampleRate);
    const auto preBufferSize = findNearestValue (Span { currentAudioDevice->getAvailableBufferSizes() }, currentSetup.bufferSize);

    error = currentAudioDevice->open (currentSetup.inputChannels,
                                      currentSetup.outputChannels,
                                      sampleRate,
                                      preBufferSize);

    if (error.isEmpty())
    {
        // The available buffer sizes may have changed, if the sample rate has
        // changed. If so we'll need to reconfigure with a different buffer size.
        const auto postBufferSize = findNearestValue (Span { currentAudioDevice->getAvailableBufferSizes() }, currentSetup.bufferSize);

        if (preBufferSize != postBufferSize)
        {
            error = currentAudioDevice->open (currentSetup.inputChannels,
                                              currentSetup.outputChannels,
                                              currentSetup.sampleRate,
                                              postBufferSize);
        }
    }]=])
        set(_juce_adm_new
[=[    // T3K_AUDIO_DEFAULT_SETUP: an unset (0) rate/buffer request must mean
    // "device defaults" (JUCE 8 semantics), never findNearestValue's
    // nearest-to-0, which is the device minimum (16-sample ALSA periods).
    const auto t3kResolveSampleRate = [this]
    {
        if (currentSetup.sampleRate > 0)
            return findNearestValue (Span { currentAudioDevice->getAvailableSampleRates() }, currentSetup.sampleRate);

        const auto rates = currentAudioDevice->getAvailableSampleRates();

        if (const auto current = currentAudioDevice->getCurrentSampleRate();
            current > 0 && rates.contains (current))
            return current;

        double lowestAbove44 = 0.0;

        for (const auto sr : rates)
            if (sr >= 44100.0 && (lowestAbove44 < 1.0 || sr < lowestAbove44))
                lowestAbove44 = sr;

        return lowestAbove44 > 0.0 ? lowestAbove44
                                   : findNearestValue (Span { rates }, 48000.0);
    };

    const auto t3kResolveBufferSize = [this]
    {
        return currentSetup.bufferSize > 0
             ? findNearestValue (Span { currentAudioDevice->getAvailableBufferSizes() }, currentSetup.bufferSize)
             : currentAudioDevice->getDefaultBufferSize();
    };

    const auto sampleRate = t3kResolveSampleRate();
    const auto preBufferSize = t3kResolveBufferSize();

    error = currentAudioDevice->open (currentSetup.inputChannels,
                                      currentSetup.outputChannels,
                                      sampleRate,
                                      preBufferSize);

    if (error.isEmpty())
    {
        // The available buffer sizes may have changed, if the sample rate has
        // changed. If so we'll need to reconfigure with a different buffer size.
        const auto postBufferSize = t3kResolveBufferSize();

        if (preBufferSize != postBufferSize)
        {
            error = currentAudioDevice->open (currentSetup.inputChannels,
                                              currentSetup.outputChannels,
                                              sampleRate,
                                              postBufferSize);
        }
    }]=])
        string(REPLACE "${_juce_adm_old}" "${_juce_adm_new}" _juce_adm_src "${_juce_adm_src}")
        if (NOT _juce_adm_src MATCHES "T3K_AUDIO_DEFAULT_SETUP")
            message(FATAL_ERROR
                "T3K_AUDIO_DEFAULT_SETUP: failed to patch ${_juce_adm_cpp}. "
                "The JUCE source layout likely changed in this version. Update the patch "
                "anchors in CMakeLists.txt.")
        endif()
        file(WRITE "${_juce_adm_cpp}" "${_juce_adm_src}")
        message(STATUS "Patched JUCE AudioDeviceManager: unset rate/buffer requests use device defaults, not device minimum")
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

# T3K_ALSA_INPUT_CHANS_SYNC: ALSAThread::open() fills currentInputChans and
# inputChannelDataForCallback together in one loop over the *requested* input
# mask, then widens currentInputChans alone (ensureMinimumNumBitsSet) up to
# the hardware's channel-count floor afterward - so on a device whose floor
# is above the request (e.g. fixed at 2 channels, request is 1), the bitset
# getActiveInputChannels() reports gains a bit with no matching entry in
# inputChannelDataForCallback. AudioDeviceManager's CallbackMaxSizeEnforcer
# sizes its stored channel vectors from that (now too-wide) bitset, fills in
# only the channels the real callback array actually has, and forwards the
# rest as null/stale pointers - which is what then reads out of bounds
# reliably on any device reconfiguration. Confirmed live with gdb: the input
# open genuinely hands the callback exactly 1 channel pointer while
# getActiveInputChannels() still reports 2. Fix from upstream JUCE PR
# https://github.com/juce-framework/JUCE/pull/1706 (unmerged as of this
# writing) - widen the *request* before the fill loop, so the bitset and the
# channel array agree by construction; drop this patch once it lands.
# Idempotent configure-time patch, same rationale as the patches above.
set(_juce_alsa_cpp2 "${LIB_DIR}/juce/modules/juce_audio_devices/native/juce_ALSA_linux.cpp")
if (EXISTS "${_juce_alsa_cpp2}")
    file(READ "${_juce_alsa_cpp2}" _juce_alsa_src2)
    if (NOT _juce_alsa_src2 MATCHES "T3K_ALSA_INPUT_CHANS_SYNC")
        set(_juce_alsa_old2
[=[        error.clear();
        sampleRate = newSampleRate;
        bufferSize = newBufferSize;

        int maxInputsRequested = inputChannels.getHighestBit() + 1;]=])
        set(_juce_alsa_new2
[=[        error.clear();
        sampleRate = newSampleRate;
        bufferSize = newBufferSize;

        // T3K_ALSA_INPUT_CHANS_SYNC: widen the *request* before the fill loop
        // below builds currentInputChans and inputChannelDataForCallback
        // together, so the two stay in agreement (matches the output path a
        // few lines below, which already widens before its own fill loop). A
        // request for no input channels at all is left alone rather than
        // being widened to the minimum.
        if (inputChannels.getHighestBit() >= 0)
            ensureMinimumNumBitsSet (inputChannels, (int) minChansIn);

        int maxInputsRequested = inputChannels.getHighestBit() + 1;]=])
        string(REPLACE "${_juce_alsa_old2}" "${_juce_alsa_new2}" _juce_alsa_src2 "${_juce_alsa_src2}")

        # The widening used to happen here, after the fill loop; it's now done
        # to the request before the loop runs instead, so drop the old call.
        set(_juce_alsa_old2b
[=[            ensureMinimumNumBitsSet (currentInputChans, (int) minChansIn);

            if (! inputDevice->setParameters ((unsigned int) sampleRate,]=])
        set(_juce_alsa_new2b
[=[            if (! inputDevice->setParameters ((unsigned int) sampleRate,]=])
        string(REPLACE "${_juce_alsa_old2b}" "${_juce_alsa_new2b}" _juce_alsa_src2 "${_juce_alsa_src2}")

        if (NOT _juce_alsa_src2 MATCHES "T3K_ALSA_INPUT_CHANS_SYNC")
            message(FATAL_ERROR
                "T3K_ALSA_INPUT_CHANS_SYNC: failed to patch ${_juce_alsa_cpp2}. "
                "The JUCE source layout likely changed in this version. Update the patch "
                "anchors in CMakeLists.txt.")
        endif()
        file(WRITE "${_juce_alsa_cpp2}" "${_juce_alsa_src2}")
        message(STATUS "Patched JUCE ALSAThread: widen the input channel request before the callback-array fill loop")
    endif()
endif()

# T3K_CALLBACK_ENFORCER_NULL_SAFE: CallbackMaxSizeEnforcer sizes its stored
# channel vectors from the device's active-channel bitsets in
# audioDeviceAboutToStart, but only fills in the number of channels the
# device actually passes to each callback, then forwards the *vector's full
# size* to the inner callback (AudioProcessorPlayer) regardless. If a
# device's bitsets and channel counts ever disagree - which they can even
# with T3K_ALSA_INPUT_CHANS_SYNC applied, for any other AudioIODevice
# backend with the same class of mismatch - the tail of those vectors still
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

# T3K_ANDROID_HTTP_HEADERS: fix every Android POST failing before it is sent
# (the sign-in token exchange surfaced it as "token_exchange_failed"). JUCE
# builds request headers with CRLF line ends and always appends its own
# "Content-length: N\r\n" to a POST (URL::createHeadersAndPostData), but
# JuceHTTPStream.java splits them on '\n' alone, so each value keeps a
# trailing '\r'. HttpURLConnection rejects a control character in a header
# value by throwing, which JuceHTTPStream reports as a failed connection. Trim
# both halves of each header line. The .java source is the one that runs: the
# Gradle project compiles it into the APK (android/app/build.gradle.kts), and
# JUCE loads the app's class before its embedded bytecode copy. Idempotent
# configure-time patch, same rationale as the patches above.
set(_juce_http_stream_java "${LIB_DIR}/juce/modules/juce_core/native/java/app/com/rmsl/juce/JuceHTTPStream.java")
if (EXISTS "${_juce_http_stream_java}")
    file(READ "${_juce_http_stream_java}" _juce_http_stream_src)
    if (NOT _juce_http_stream_src MATCHES "T3K_ANDROID_HTTP_HEADERS")
        string(REPLACE
            "String field = headerLines[i].substring(0, pos);"
            "String field = headerLines[i].substring(0, pos).trim(); // T3K_ANDROID_HTTP_HEADERS"
            _juce_http_stream_src "${_juce_http_stream_src}")
        string(REPLACE
            "String value = headerLines[i].substring(pos + 1);"
            "String value = headerLines[i].substring(pos + 1).trim();"
            _juce_http_stream_src "${_juce_http_stream_src}")
        if (NOT _juce_http_stream_src MATCHES "T3K_ANDROID_HTTP_HEADERS" OR
            NOT _juce_http_stream_src MATCHES "substring\\(pos \\+ 1\\)\\.trim\\(\\)")
            message(FATAL_ERROR
                "T3K_ANDROID_HTTP_HEADERS: failed to patch ${_juce_http_stream_java}. "
                "The JUCE source layout likely changed in this version. Update the patch "
                "anchors in CMakeLists.txt.")
        endif()
        file(WRITE "${_juce_http_stream_java}" "${_juce_http_stream_src}")
        message(STATUS "Patched JUCE JuceHTTPStream (Android): request header lines trimmed")
    endif()
endif()

# T3K_ANDROID_HTTP_POST: the other half of the same failure. The Android
# backend passes addParametersToRequestBody as JuceHTTPStream's isPost flag,
# which is only set for ParameterHandling::inPostData; raw POST data
# (URL::withPOSTData, as the UI's HttpClient sends the token form) leaves it
# false, so the body is never written and the server rejects an empty POST.
# Pass hasBodyDataToSend, which covers both, as every other backend does.
set(_juce_network_android_cpp "${LIB_DIR}/juce/modules/juce_core/native/juce_Network_android.cpp")
if (EXISTS "${_juce_network_android_cpp}")
    file(READ "${_juce_network_android_cpp}" _juce_network_android_src)
    if (NOT _juce_network_android_src MATCHES "T3K_ANDROID_HTTP_POST")
        string(REPLACE
            "(jboolean) addParametersToRequestBody,"
            "(jboolean) hasBodyDataToSend, // T3K_ANDROID_HTTP_POST"
            _juce_network_android_src "${_juce_network_android_src}")
        if (NOT _juce_network_android_src MATCHES "T3K_ANDROID_HTTP_POST")
            message(FATAL_ERROR
                "T3K_ANDROID_HTTP_POST: failed to patch ${_juce_network_android_cpp}. "
                "The JUCE source layout likely changed in this version. Update the patch "
                "anchor in CMakeLists.txt.")
        endif()
        file(WRITE "${_juce_network_android_cpp}" "${_juce_network_android_src}")
        message(STATUS "Patched JUCE Network (Android): raw POST data is sent")
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
# for iOS or Android produces something that cannot be run, so keep it out of
# those builds.
if (NOT T3K_IOS AND NOT T3K_ANDROID)
    add_subdirectory(test)   # DSP test suite (DspTests, see script/test-dsp.sh)
endif()
