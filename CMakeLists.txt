cmake_minimum_required(VERSION 3.22.1)

project(TONE3000 VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs)

# Download CPM package manager
include(cmake/cpm.cmake) 

# Download JUCE using CPM
CPMAddPackage(
    NAME JUCE
    GIT_TAG 8.0.7
    VERSION 8.0.7
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
            file(WRITE "${_juce_webview_mm}" "${_juce_webview_src}")
            message(STATUS "Patched JUCE WebBrowserComponent: Web Inspector enabled when T3K_ENABLE_DEVTOOLS is defined")
        endif()
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
