cmake_minimum_required(VERSION 3.22.1)

project(TONE3000 VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs)

# Download CPM package manager
include(cmake/cpm.cmake) 

# Download JUCE using CPM
CPMAddPackage(
    NAME JUCE
    GIT_TAG 8.0.14
    VERSION 8.0.14
    GITHUB_REPOSITORY juce-framework/JUCE
    SOURCE_DIR ${LIB_DIR}/juce
)

# Enable the macOS WKWebView Web Inspector (right-click -> Inspect Element, plus
# console access) in release builds. Stock JUCE only flips developerExtrasEnabled
# behind JUCE_DEBUG, so we widen that gate to our own T3K_ENABLE_DEVTOOLS macro and
# also mark the view inspectable on macOS 13.3+. This is applied as an idempotent
# configure-time patch because JUCE lives in the gitignored libs/ dir (CPM), so a
# tracked source edit wouldn't survive a clean checkout / re-fetch.
# Turn OFF for final shipping builds: -DENABLE_WEBVIEW_DEVTOOLS=OFF
option(ENABLE_WEBVIEW_DEVTOOLS "Enable the WKWebView Web Inspector in release builds" ON)

if (APPLE AND ENABLE_WEBVIEW_DEVTOOLS)
    set(_juce_webview_mm "${LIB_DIR}/juce/modules/juce_gui_extra/native/juce_WebBrowserComponent_mac.mm")
    if (EXISTS "${_juce_webview_mm}")
        file(READ "${_juce_webview_mm}" _juce_webview_src)
        if (NOT _juce_webview_src MATCHES "T3K_ENABLE_DEVTOOLS")
            # Widen the developerExtrasEnabled gate from JUCE_DEBUG to also include our macro.
            string(REGEX REPLACE
                "#if JUCE_DEBUG\n([ \t]*)\\[preferences setValue: @\\(true\\) forKey: @\"developerExtrasEnabled\"\\];"
                "#if JUCE_DEBUG || defined (T3K_ENABLE_DEVTOOLS)\n\\1[preferences setValue: @(true) forKey: @\"developerExtrasEnabled\"];"
                _juce_webview_src "${_juce_webview_src}")
            # Mark the WKWebView inspectable (required for the inspector on macOS 13.3+).
            string(REGEX REPLACE
                "\n([ \t]*)if \\(const auto userAgent = browserOptions\\.getUserAgent\\(\\);"
                "\n       #if JUCE_DEBUG || defined (T3K_ENABLE_DEVTOOLS)\n        if (@available (macOS 13.3, *))\n            [webView.get() setValue: @(true) forKey: @\"inspectable\"];\n       #endif\n\n\\1if (const auto userAgent = browserOptions.getUserAgent();"
                _juce_webview_src "${_juce_webview_src}")
            # Fail loudly if neither anchor matched (e.g. after a JUCE upgrade changed the
            # source layout) so we never silently ship release builds without the inspector.
            if (NOT _juce_webview_src MATCHES "T3K_ENABLE_DEVTOOLS")
                message(FATAL_ERROR
                    "ENABLE_WEBVIEW_DEVTOOLS: failed to patch ${_juce_webview_mm}. "
                    "The JUCE source layout likely changed in this version. Update the patch "
                    "regexes in CMakeLists.txt, or configure with -DENABLE_WEBVIEW_DEVTOOLS=OFF.")
            endif()
            file(WRITE "${_juce_webview_mm}" "${_juce_webview_src}")
            message(STATUS "Patched JUCE WebBrowserComponent: Web Inspector enabled when T3K_ENABLE_DEVTOOLS is defined")
        endif()
    endif()
endif()

# macOS: WKWebView paints an opaque white background until the first page paint,
# which shows as a white flash when the editor opens. JUCE 8 only exposes a
# background-colour option for Windows (WinWebView2), so patch the WKWebView to
# not draw its own background — the editor's black paint() shows through instead.
# Idempotent configure-time patch, same rationale as the devtools patch above.
if (APPLE)
    set(_juce_webview_mm "${LIB_DIR}/juce/modules/juce_gui_extra/native/juce_WebBrowserComponent_mac.mm")
    if (EXISTS "${_juce_webview_mm}")
        file(READ "${_juce_webview_mm}" _juce_webview_src)
        if (NOT _juce_webview_src MATCHES "T3K_WEBVIEW_NO_FLASH")
            string(REGEX REPLACE
                "\n([ \t]*)\\[webView\\.get\\(\\) setNavigationDelegate: webViewDelegate\\.get\\(\\)\\];"
                "\n       #if JUCE_MAC\n        // T3K_WEBVIEW_NO_FLASH: transparent until first page paint (kills the white flash)\n        [webView.get() setValue: @(NO) forKey: @\"drawsBackground\"];\n       #endif\n\n\\1[webView.get() setNavigationDelegate: webViewDelegate.get()];"
                _juce_webview_src "${_juce_webview_src}")
            if (NOT _juce_webview_src MATCHES "T3K_WEBVIEW_NO_FLASH")
                message(FATAL_ERROR
                    "T3K_WEBVIEW_NO_FLASH: failed to patch ${_juce_webview_mm}. "
                    "The JUCE source layout likely changed in this version. Update the patch "
                    "anchor in CMakeLists.txt.")
            endif()
            file(WRITE "${_juce_webview_mm}" "${_juce_webview_src}")
            message(STATUS "Patched JUCE WebBrowserComponent: WKWebView background disabled (no white flash)")
        endif()
    endif()
endif()

# Standalone: the "Audio input is muted" notification banner is hard-coded to
# lightgoldenrodyellow with black text inside JUCE's StandaloneFilterWindow and
# is not themeable via LookAndFeel. Restyle it to the brand palette: black bar,
# white text, pure-yellow divider. Idempotent configure-time patch.
set(_juce_standalone_h "${LIB_DIR}/juce/modules/juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h")
if (EXISTS "${_juce_standalone_h}")
    file(READ "${_juce_standalone_h}" _juce_standalone_src)
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
        file(WRITE "${_juce_standalone_h}" "${_juce_standalone_src}")
        message(STATUS "Patched JUCE StandaloneFilterWindow: dark mute-warning banner")
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
        "gtest_force_shared_crt ON"
)

enable_testing() # Allow running build tests

add_subdirectory(plugin) # Add plugin project
