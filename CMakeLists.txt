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

# Linux: JUCE dlopens WebKitGTK/GTK by their UNVERSIONED .so names
# (libwebkit2gtk-4.1.so etc.), but distros only ship those symlinks in the
# -dev packages — end-user systems have only the versioned SONAMEs
# (libwebkit2gtk-4.1.so.0). The dlopen fails silently, webKitIsAvailable
# stays false, every webview call becomes a no-op, and the plugin renders a
# black screen. Patch the names to the upstream SONAMEs, which exist on every
# install (the -dev packages depend on the runtime libs, so this covers both).
# Idempotent configure-time patch, same rationale as the macOS patches above.
# Applied on every platform (the file only compiles on Linux; patching it
# elsewhere is harmless and keeps the libs/juce tree identical everywhere).
set(_juce_webview_linux "${LIB_DIR}/juce/modules/juce_gui_extra/native/juce_WebBrowserComponent_linux.cpp")
if (EXISTS "${_juce_webview_linux}")
    file(READ "${_juce_webview_linux}" _juce_webview_linux_src)
    if (NOT _juce_webview_linux_src MATCHES "T3K_WEBKIT_SONAME")
        string(REPLACE "\"libwebkit2gtk-4.1.so\""        "\"libwebkit2gtk-4.1.so.0\""         _juce_webview_linux_src "${_juce_webview_linux_src}")
        string(REPLACE "\"libjavascriptcoregtk-4.1.so\"" "\"libjavascriptcoregtk-4.1.so.0\""  _juce_webview_linux_src "${_juce_webview_linux_src}")
        string(REPLACE "\"libsoup-3.0.so\""              "\"libsoup-3.0.so.0\""               _juce_webview_linux_src "${_juce_webview_linux_src}")
        string(REPLACE "\"libwebkit2gtk-4.0.so\""        "\"libwebkit2gtk-4.0.so.37\""        _juce_webview_linux_src "${_juce_webview_linux_src}")
        string(REPLACE "\"libjavascriptcoregtk-4.0.so\"" "\"libjavascriptcoregtk-4.0.so.18\"" _juce_webview_linux_src "${_juce_webview_linux_src}")
        string(REPLACE "\"libsoup-2.4.so\""              "\"libsoup-2.4.so.1\""               _juce_webview_linux_src "${_juce_webview_linux_src}")
        string(REPLACE "\"libgtk-3.so\""                 "\"libgtk-3.so.0\""                  _juce_webview_linux_src "${_juce_webview_linux_src}")
        string(REPLACE "\"libglib-2.0.so\""              "\"libglib-2.0.so.0\" /* T3K_WEBKIT_SONAME */" _juce_webview_linux_src "${_juce_webview_linux_src}")
        if (NOT _juce_webview_linux_src MATCHES "T3K_WEBKIT_SONAME")
            message(FATAL_ERROR
                "T3K_WEBKIT_SONAME: failed to patch ${_juce_webview_linux}. "
                "The JUCE source layout likely changed in this version. Update the patch "
                "anchors in CMakeLists.txt.")
        endif()
        file(WRITE "${_juce_webview_linux}" "${_juce_webview_linux_src}")
        message(STATUS "Patched JUCE WebBrowserComponent: WebKitGTK loaded by SONAME (works without -dev packages)")
    endif()
endif()

# Linux: JUCE builds its WebKitGTK view on the DEFAULT WebKitWebContext, which
# breaks login persistence two ways: (1) the default website-data location is
# derived from the process name — and for plugin (non-standalone) builds JUCE
# extracts the webview subprocess to a RANDOMLY-NAMED temp file, so
# localStorage lands in a different directory every session; (2) WebKitGTK
# keeps cookies in memory only unless webkit_cookie_manager_set_persistent_storage()
# is called, which JUCE never does — so the tone3000.com OAuth session dies
# with the process even in the standalone. Patch the subprocess to build its
# context on an explicit WebKitWebsiteDataManager rooted at
# ~/.config/TONE3000/WebKit (the same app-data root as presets/logs) and turn
# on SQLite cookie persistence. All added symbols are dlopened like the rest
# (present in every WebKitGTK 4.0/4.1 since 2.10). Idempotent configure-time
# patch, same rationale and platform note as the SONAME patch above.
if (EXISTS "${_juce_webview_linux}")
    file(READ "${_juce_webview_linux}" _juce_webview_linux_src)
    if (NOT _juce_webview_linux_src MATCHES "T3K_WEBKIT_PERSISTENT_STORAGE")
        # 1) Declare the extra WebKit functions (typed pointers + no-op defaults).
        string(REPLACE
[=[    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (webkit_web_view_new_with_settings, juce_webkit_web_view_new_with_settings,
                                         (WebKitSettings*), GtkWidget*)]=]
[=[    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (webkit_web_view_new_with_settings, juce_webkit_web_view_new_with_settings,
                                         (WebKitSettings*), GtkWidget*)

    // T3K_WEBKIT_PERSISTENT_STORAGE: build the view on an explicit context so
    // localStorage/cookies live at a stable path instead of the process-name
    // derived default (random for plugin subprocesses, memory-only cookies).
    // webkit_website_data_manager_new is variadic, which the macro's default
    // lambda cannot express (a variadic lambda cannot convert to a variadic
    // function pointer on clang), so it is declared by hand with a plain
    // function as the pre-load default.
    static WebKitWebsiteDataManager* juce_webkit_website_data_manager_new_default (const gchar*, ...) { return nullptr; }
    using webkit_website_data_manager_new = WebKitWebsiteDataManager* (*) (const gchar*, ...);
    webkit_website_data_manager_new juce_webkit_website_data_manager_new = &juce_webkit_website_data_manager_new_default;

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (webkit_web_context_new_with_website_data_manager, juce_webkit_web_context_new_with_website_data_manager,
                                         (WebKitWebsiteDataManager*), WebKitWebContext*)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (webkit_web_context_get_cookie_manager, juce_webkit_web_context_get_cookie_manager,
                                         (WebKitWebContext*), WebKitCookieManager*)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (webkit_cookie_manager_set_persistent_storage, juce_webkit_cookie_manager_set_persistent_storage,
                                         (WebKitCookieManager*, const gchar*, WebKitCookiePersistentStorage), void)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (webkit_web_view_new_with_context, juce_webkit_web_view_new_with_context,
                                         (WebKitWebContext*), GtkWidget*)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (webkit_web_view_set_settings, juce_webkit_web_view_set_settings,
                                         (WebKitWebView*, WebKitSettings*), void)]=]
            _juce_webview_linux_src "${_juce_webview_linux_src}")
        if (NOT _juce_webview_linux_src MATCHES "juce_webkit_website_data_manager_new")
            message(FATAL_ERROR
                "T3K_WEBKIT_PERSISTENT_STORAGE: failed to patch symbol declarations in ${_juce_webview_linux}. "
                "The JUCE source layout likely changed in this version. Update the patch anchors in CMakeLists.txt.")
        endif()

        # 2) dlopen bindings for the new symbols.
        string(REPLACE
[=[                            makeSymbolBinding (juce_webkit_web_view_new_with_settings,                           "webkit_web_view_new_with_settings"),]=]
[=[                            makeSymbolBinding (juce_webkit_web_view_new_with_settings,                           "webkit_web_view_new_with_settings"),
                            // T3K_WEBKIT_PERSISTENT_STORAGE
                            makeSymbolBinding (juce_webkit_website_data_manager_new,                             "webkit_website_data_manager_new"),
                            makeSymbolBinding (juce_webkit_web_context_new_with_website_data_manager,            "webkit_web_context_new_with_website_data_manager"),
                            makeSymbolBinding (juce_webkit_web_context_get_cookie_manager,                       "webkit_web_context_get_cookie_manager"),
                            makeSymbolBinding (juce_webkit_cookie_manager_set_persistent_storage,                "webkit_cookie_manager_set_persistent_storage"),
                            makeSymbolBinding (juce_webkit_web_view_new_with_context,                            "webkit_web_view_new_with_context"),
                            makeSymbolBinding (juce_webkit_web_view_set_settings,                                "webkit_web_view_set_settings"),]=]
            _juce_webview_linux_src "${_juce_webview_linux_src}")
        if (NOT _juce_webview_linux_src MATCHES "\"webkit_web_view_new_with_context\"")
            message(FATAL_ERROR
                "T3K_WEBKIT_PERSISTENT_STORAGE: failed to patch symbol bindings in ${_juce_webview_linux}. "
                "The JUCE source layout likely changed in this version. Update the patch anchors in CMakeLists.txt.")
        endif()

        # 3) Create the web view on the persistent context.
        string(REPLACE
[=[        auto* webviewWidget = WebKitSymbols::getInstance()->juce_webkit_web_view_new_with_settings (settings);
        webview = (WebKitWebView*) webviewWidget;]=]
[=[        // T3K_WEBKIT_PERSISTENT_STORAGE: stable per-user storage for
        // localStorage (OAuth tokens) and cookies (tone3000.com session).
        const auto t3kStorageRoot = File::getSpecialLocation (File::userApplicationDataDirectory)
                                        .getChildFile ("TONE3000")
                                        .getChildFile ("WebKit");
        const auto t3kDataDir  = t3kStorageRoot.getChildFile ("data");
        const auto t3kCacheDir = t3kStorageRoot.getChildFile ("cache");
        t3kDataDir.createDirectory();
        t3kCacheDir.createDirectory();

        auto* t3kDataManager = wk.juce_webkit_website_data_manager_new (
            "base-data-directory",  t3kDataDir .getFullPathName().toRawUTF8(),
            "base-cache-directory", t3kCacheDir.getFullPathName().toRawUTF8(),
            nullptr);

        auto* t3kContext = wk.juce_webkit_web_context_new_with_website_data_manager (t3kDataManager);

        static constexpr int webkitCookiePersistentStorageSqliteFlag = 1;
        wk.juce_webkit_cookie_manager_set_persistent_storage (
            wk.juce_webkit_web_context_get_cookie_manager (t3kContext),
            t3kDataDir.getChildFile ("cookies.sqlite").getFullPathName().toRawUTF8(),
            (WebKitCookiePersistentStorage) webkitCookiePersistentStorageSqliteFlag);

        auto* webviewWidget = wk.juce_webkit_web_view_new_with_context (t3kContext);
        wk.juce_webkit_web_view_set_settings ((WebKitWebView*) webviewWidget, settings);
        webview = (WebKitWebView*) webviewWidget;]=]
            _juce_webview_linux_src "${_juce_webview_linux_src}")
        if (NOT _juce_webview_linux_src MATCHES "t3kStorageRoot")
            message(FATAL_ERROR
                "T3K_WEBKIT_PERSISTENT_STORAGE: failed to patch web view creation in ${_juce_webview_linux}. "
                "The JUCE source layout likely changed in this version. Update the patch anchors in CMakeLists.txt.")
        endif()

        file(WRITE "${_juce_webview_linux}" "${_juce_webview_linux_src}")
        message(STATUS "Patched JUCE WebBrowserComponent: WebKitGTK persistent storage (login survives restarts)")
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
