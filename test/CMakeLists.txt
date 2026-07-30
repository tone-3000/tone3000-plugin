# ── TONE3000 DSP test suite ──
# A GTest console app exercising the chain's DSP invariants (oversampler,
# NamEngine phase interleaving, IR base-rate islands) against the real model
# and IR assets in test/files. Build and run locally:
#
#   ./script/test-dsp.sh                     # configure/build/run in one go
#   ./script/test-dsp.sh 'IrConvolutionTest.*'   # gtest filter
#
# or by hand: cmake --build build --target DspTests, then run the binary in
# build/test/DspTests_artefacts (also registered with ctest).

# The root project only enables CXX; the JUCE modules this app pulls in
# (juce_graphics via juce_audio_processors) include a C source.
enable_language(C)

juce_add_console_app(DspTests PRODUCT_NAME "DspTests")

target_sources(DspTests PRIVATE
    src/auto_offset_tests.cpp
    src/dsp_tests.cpp
    src/processor_tests.cpp
    src/spread_tests.cpp
    # Compiled straight from the plugin tree — the tests exercise the real
    # processor, not a copy. GUI sources are excluded; HEADLESS=1 below
    # compiles the processor exactly like the headless plugin build.
    ../plugin/src/AutoOffset.cpp
    ../plugin/src/BlockEq.cpp
    ../plugin/src/BlockSpectrum.cpp
    ../plugin/src/MidiMapper.cpp
    ../plugin/src/NamEngine.cpp
    ../plugin/src/PresetManager.cpp
    ../plugin/src/Processor.cpp
    ../plugin/src/ProcessorChain.cpp
    ../plugin/src/ProcessorHistory.cpp
    ../plugin/src/ProcessorModelLoader.cpp
    ../plugin/src/ProcessorPresets.cpp
    ../plugin/src/ProcessorState.cpp
    ../plugin/src/Spread.cpp
    ../plugin/src/StereoOffset.cpp
    ../plugin/src/TunerDetector.cpp
)

target_include_directories(DspTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugin/include
    # ChainDomain.h pulls in the AudioDSPTools resampling boundary.
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugin/AudioDSPTools
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugin/AudioDSPTools/dsp/ResamplingContainer/Dependencies
    # NAM core + deps. Added explicitly (not via the NAM target) because on
    # macOS/Windows NAM is linked as a whole-archive *flag*, which doesn't
    # propagate the target's include directories.
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugin/NeuralAmpModelerCore
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugin/NeuralAmpModelerCore/Dependencies/eigen
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugin/NeuralAmpModelerCore/Dependencies/nlohmann
)

target_compile_definitions(DspTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    # Prevent windows.h min/max macros from breaking std::min/std::max in
    # AudioDSPTools and plugin code — same as plugin/CMakeLists.txt.
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
    # The processor compiles in its headless configuration (no Editor /
    # webview / standalone-window code) — the DSP under test is identical.
    HEADLESS=1
    # Absolute path to the test assets, so the binary runs from anywhere.
    T3K_TEST_FILES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/files"
)

target_link_libraries(DspTests
    PRIVATE
        juce::juce_dsp
        juce::juce_audio_formats
        juce::juce_audio_processors
        # NAM must be linked whole-archive so its self-registering config
        # parsers (WaveNet/ConvNet/LSTM static initializers) survive the
        # linker — same recipe as plugin/CMakeLists.txt.
        $<$<PLATFORM_ID:Darwin>:-Wl,-force_load,$<TARGET_FILE:NAM>>
        $<$<PLATFORM_ID:Linux>:-Wl,--whole-archive>
        $<$<PLATFORM_ID:Linux>:NAM>
        $<$<PLATFORM_ID:Linux>:-Wl,--no-whole-archive>
        $<$<PLATFORM_ID:Windows>:NAM>
        $<$<PLATFORM_ID:Windows>:-WHOLEARCHIVE:$<TARGET_FILE:NAM>>
        # dsp_tests.cpp provides main() (JUCE init + InitGoogleTest).
        GTest::gtest
    PUBLIC
        juce::juce_recommended_config_flags
)

add_dependencies(DspTests NAM)

include(GoogleTest)
gtest_discover_tests(DspTests)
