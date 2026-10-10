# Maintainer utilities. Not built by default and never shipped:
#   cmake --build build --target PresetTool
#   build/tools/PresetTool_artefacts/Release/PresetTool migrate resources/factory-presets
juce_add_console_app(PresetTool PRODUCT_NAME "PresetTool")
set_target_properties(PresetTool PROPERTIES EXCLUDE_FROM_ALL TRUE)

target_sources(PresetTool PRIVATE
    preset_tool.cpp
    ../plugin/src/LegacyParamIds.cpp
    ../plugin/src/PresetFile.cpp
)
target_include_directories(PresetTool PRIVATE ../plugin/include)
target_compile_definitions(PresetTool PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)
target_link_libraries(PresetTool PRIVATE
    juce::juce_core
    juce::juce_data_structures
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
