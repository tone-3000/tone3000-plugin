# t3k_usb_audio: the low-latency USB audio driver core (plan D1).
#
# Pure C++: no JUCE, no JNI, no OS headers. Builds for the host (where the
# GoogleTest suite in test/ exercises it) and for Android (where the probe
# app and later the JUCE device link it). Keep it that way: platform code
# lives in the shell, not here.

add_library(t3k_usb_audio STATIC
    AudioRing.cpp
    ClockModel.cpp
    PcmConvert.cpp
    RtlMeasurement.cpp
    UacControl.cpp
    UacDescriptors.cpp
    UsbAudioStream.cpp
    UsbQuirks.cpp
)

target_include_directories(t3k_usb_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(t3k_usb_audio PUBLIC cxx_std_17)
if (NOT MSVC)
    target_compile_options(t3k_usb_audio PRIVATE -Wall -Wextra)
endif()
