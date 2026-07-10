cmake_minimum_required(VERSION 3.22.1)

# Required by JUCE (Change these to project specs)
set(PLUGIN_NAME "TONE3000")            # Name of the plugin (used in the UI)
set(PLUGIN_CODE T3K0)                # Unique 4-letter code for the plugin
project(TONE3000 VERSION 0.0.1)        # Set version of the plugin

option(HEADLESS "Disable GUI (headless/embedded build)" OFF)
option(BUILD_AAX "Build AAX (Avid) format" ON)
set(_plugin_formats "VST3") # Default plugin format

# Build for Desktop or Embedded
if (NOT HEADLESS)
    set(JUCE_WEB_BROWSER_VALUE 1)               # Webview UI for Desktop
    set(NEEDS_WEB_BROWSER_VALUE TRUE)           # enable, need browser for webview on linux
    list(APPEND _plugin_formats "Standalone")   # Add Standalone format
    # Enable AU (Apple) format on macOS
    if (APPLE)
        list(APPEND _plugin_formats "AU")
    endif()
    if (BUILD_AAX)
        list(APPEND _plugin_formats "AAX")
    endif()
    set(VST3_MANIFEST TRUE)                     # Create moduleinfo.json for VST3
    add_compile_definitions(HEADLESS=0)
    if (WIN32)
        set(NEEDS_WEBVIEW2_VALUE TRUE)
    else()
        set(NEEDS_WEBVIEW2_VALUE FALSE)
    endif()
else()
    set(JUCE_WEB_BROWSER_VALUE 0)               # No Webview UI for Embedded
    set(NEEDS_WEB_BROWSER_VALUE FALSE)          # disable, need browser for webview on linux
    find_package(JUCE CONFIG REQUIRED)          # maybe remove?
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF) # Disable LTO
    set(VST3_MANIFEST FALSE)                    # Maybe remove?
    add_compile_definitions(HEADLESS=1)
endif()

add_subdirectory(NeuralAmpModelerCore)

# The NAM dev tools (benchmarks, render, run_tests) are only used by the
# submodule's own CI; nothing in the plugin build needs them. They also don't
# compile with MSVC (GCC/Clang-only flags, M_PI, POSIX dlfcn.h in run_tests),
# and each one recompiles all NAM sources, wasting CI time. Keep them out of
# the default build; build explicitly if needed: cmake --build build --target benchmodel
foreach(_nam_tool tools loadmodel benchmodel render benchmodel_bufsize bench_a2_fast run_tests)
    if(TARGET ${_nam_tool})
        set_target_properties(${_nam_tool} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()
endforeach()

# NAM static library - built here (not in submodule) to keep submodule pristine.
# Must be linked with -force_load/--whole-archive so static factory::Helper
# initializers (ConvNet, LSTM, WaveNet) are not stripped.
set(NAM_SRC "${CMAKE_CURRENT_SOURCE_DIR}/NeuralAmpModelerCore/NAM")
add_library(NAM STATIC
    ${NAM_SRC}/activations.cpp
    ${NAM_SRC}/conv1d.cpp
    ${NAM_SRC}/container.cpp
    ${NAM_SRC}/convnet.cpp
    ${NAM_SRC}/dsp.cpp
    ${NAM_SRC}/get_dsp.cpp
    ${NAM_SRC}/lstm.cpp
    ${NAM_SRC}/ring_buffer.cpp
    ${NAM_SRC}/util.cpp
    ${NAM_SRC}/wavenet/a2_fast.cpp
    ${NAM_SRC}/wavenet/model.cpp
    ${NAM_SRC}/wavenet/slimmable.cpp
)
target_include_directories(NAM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/NeuralAmpModelerCore
    ${CMAKE_CURRENT_SOURCE_DIR}/NeuralAmpModelerCore/Dependencies/eigen
    ${CMAKE_CURRENT_SOURCE_DIR}/NeuralAmpModelerCore/Dependencies/nlohmann
)
target_compile_options(NAM PRIVATE -fPIC)
# A2 full / A2 lite models use the hand-optimized path when config matches (see NeuralAmpModelerCore).
target_compile_definitions(NAM PRIVATE NAM_ENABLE_A2_FAST)

juce_add_plugin(${PROJECT_NAME}
    COMPANY_NAME TONE3000
    PLUGIN_NAME ${PLUGIN_NAME}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    PLUGIN_MANUFACTURER_CODE TFTF
    PLUGIN_CODE ${PLUGIN_CODE}
    FORMATS ${_plugin_formats}
    PRODUCT_NAME ${PROJECT_NAME}
    NEEDS_WEB_BROWSER ${NEEDS_WEB_BROWSER_VALUE}
    VST3_AUTO_MANIFEST ${VST3_MANIFEST}
    NEEDS_WEBVIEW2 ${NEEDS_WEBVIEW2_VALUE}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "TONE3000 needs microphone access for audio input processing."
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icon/icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/icon/icon.png"
)

target_sources(${PROJECT_NAME}
    PRIVATE
        include/BlockEq.h
        src/BlockEq.cpp
        include/BlockSpectrum.h
        src/BlockSpectrum.cpp
        include/ChainBlock.h
        include/ChainHistory.h
        include/Spread.h
        src/Spread.cpp
        include/PresetManager.h
        src/PresetManager.cpp
        include/Processor.h
        src/Processor.cpp
        src/ProcessorChain.cpp
        src/ProcessorHistory.cpp
        src/ProcessorPresets.cpp
        src/ProcessorState.cpp
        src/ProcessorModelLoader.cpp
        include/NamResampler.h
        src/NamResampler.cpp
        include/TunerDetector.h
        src/TunerDetector.cpp
)
# Include GUI for Desktop builds
if (NOT HEADLESS)
    target_sources(${PROJECT_NAME}
        PRIVATE
            include/Editor.h
            src/Editor.cpp
            include/EditorWebViewSetup.h
            src/EditorWebViewSetup.cpp
    )
endif()

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/NeuralAmpModelerCore
        ${CMAKE_CURRENT_SOURCE_DIR}/NeuralAmpModelerCore/Dependencies/eigen
        ${CMAKE_CURRENT_SOURCE_DIR}/NeuralAmpModelerCore/Dependencies/nlohmann
        ${CMAKE_CURRENT_SOURCE_DIR}/AudioDSPTools
        ${CMAKE_CURRENT_SOURCE_DIR}/AudioDSPTools/dsp/ResamplingContainer/Dependencies
)

# JUCE modules used
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        juce::juce_core
        juce::juce_dsp
        juce::juce_events
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_gui_extra
        # NAM: force_load/whole-archive so static factory::Helper initializers
        # (ConvNet, LSTM, WaveNet) are not stripped by the linker
        $<$<PLATFORM_ID:Darwin>:-Wl,-force_load,$<TARGET_FILE:NAM>>
        # Linux: flags and library must be separate link items. A single
        # "-Wl,--whole-archive,NAM,-Wl,..." item gets comma-split by the GCC
        # driver, passing a literal "-Wl" to ld ("unrecognized option '-Wl'").
        $<$<PLATFORM_ID:Linux>:-Wl,--whole-archive>
        $<$<PLATFORM_ID:Linux>:NAM>
        $<$<PLATFORM_ID:Linux>:-Wl,--no-whole-archive>
        # Windows (MSVC): /WHOLEARCHIVE is the equivalent — without it the
        # linker drops the objects holding the self-registering config parsers
        # ("No config parser registered for architecture ..." at NAM load).
        # Leading '-' so CMake treats the item as a flag (a leading '/' parses
        # as a path); MSVC's linker accepts either prefix.
        $<$<PLATFORM_ID:Windows>:NAM>
        $<$<PLATFORM_ID:Windows>:-WHOLEARCHIVE:$<TARGET_FILE:NAM>>
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        #juce::juce_recommended_lto_flags # Maybe remove?
)

# Ensure NAM is built before plugin (force_load path requires lib to exist)
add_dependencies(${PROJECT_NAME} NAM)


if (NOT HEADLESS)
    # Add Webview GUI files as binary data (built by npm run build in ui/)
    file(GLOB_RECURSE WEB_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/webview/*.*")
    # Fallback: CI configures before UI build; use placeholder so juce_add_binary_data succeeds.
    # Reconfigure after "Build UI" step to pick up real assets.
    if (NOT WEB_ASSETS)
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/_webview_placeholder.txt" " ")
        list(APPEND WEB_ASSETS "${CMAKE_CURRENT_BINARY_DIR}/_webview_placeholder.txt")
    endif()

    juce_add_binary_data(WebAssets
    SOURCES ${WEB_ASSETS}
    )

    target_link_libraries(${PROJECT_NAME} PRIVATE WebAssets)
    target_include_directories(${PROJECT_NAME}
        PRIVATE ${WebAssets_BINARY_DIR}
    )
else()
    # Generate a dummy BinaryData.h to avoid missing include errors
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/BinaryData.h" "// Empty stub for Embedded builds\n")
    target_include_directories(${PROJECT_NAME}
        PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()

# These definitions are recommended by JUCE.
target_compile_definitions(${PROJECT_NAME}
    PUBLIC
        JUCE_WEB_BROWSER=${JUCE_WEB_BROWSER_VALUE}
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

# Use native macOS title bar for standalone app
if (APPLE AND NOT HEADLESS)
    target_compile_definitions(${PROJECT_NAME}
        PUBLIC JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0
    )
endif()

# Enable the WKWebView Web Inspector in release builds (see root CMakeLists patch).
# The JUCE module sources compile into this target, so defining the macro here
# reaches juce_WebBrowserComponent_mac.mm. Disable with -DENABLE_WEBVIEW_DEVTOOLS=OFF.
if (APPLE AND NOT HEADLESS AND ENABLE_WEBVIEW_DEVTOOLS)
    target_compile_definitions(${PROJECT_NAME} PUBLIC T3K_ENABLE_DEVTOOLS=1)
endif()

if (WIN32)
    # Prevent windows.h min/max macros from breaking std::min/std::max in AudioDSPTools and plugin code
    target_compile_definitions(${PROJECT_NAME} PUBLIC NOMINMAX)
    if (NOT HEADLESS)
        target_compile_definitions(${PROJECT_NAME}
            PUBLIC JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1  # This will enable WebView2 as the WebView backend on Windows
        )

        # ASIO driver support in the standalone's audio settings. JUCE keeps
        # ASIO off by default because it needs Steinberg's ASIO SDK headers,
        # which are a separate (non-redistributable) download:
        # https://www.steinberg.net/asiosdk — extract it and pass
        # -DASIO_SDK_PATH=<dir> (CI does this automatically). Without the SDK
        # the build still works, just without an ASIO option in the device list.
        set(ASIO_SDK_PATH "" CACHE PATH "Path to the Steinberg ASIO SDK (enables JUCE_ASIO)")
        if (ASIO_SDK_PATH AND EXISTS "${ASIO_SDK_PATH}/common/iasiodrv.h")
            target_compile_definitions(${PROJECT_NAME} PUBLIC JUCE_ASIO=1)
            target_include_directories(${PROJECT_NAME} PUBLIC "${ASIO_SDK_PATH}/common")
            message(STATUS "ASIO enabled (SDK: ${ASIO_SDK_PATH})")
        else()
            message(STATUS "ASIO disabled — set ASIO_SDK_PATH to the Steinberg ASIO SDK to enable it")
        endif()
    endif()
endif()

# Treat all warnings as errors
#if (MSVC)
#    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /WX)
#else()
#    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
#endif()

# Visual Studio Only
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/..) 