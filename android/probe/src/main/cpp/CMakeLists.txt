# The probe app's native half (plan section 6.6): a tiny standalone CMake
# project that pulls in plugin/usb as a static lib and nothing else. Keep it
# out of the main target graph; Gradle configures it per-module with the NDK
# toolchain.

cmake_minimum_required(VERSION 3.22)
project(t3k_probe LANGUAGES CXX)

# The driver core: identical code to what the host UsbTests suite exercises.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../plugin/usb t3k_usb_audio_build)

add_library(t3k_probe SHARED
    probe_jni.cpp
)

target_compile_features(t3k_probe PRIVATE cxx_std_17)
if (NOT MSVC)
    target_compile_options(t3k_probe PRIVATE -Wall -Wextra)
endif()

# Gradle's debug variant compiles with -O0. At 8,000 isochronous packets per
# second, -O0 loses about a third of real time to per-packet work (measured:
# 5,330 packets/s at 8x2), which reads as a driver cadence failure when it is
# only a build flag. Keep debug symbols, optimize the hot path.
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(t3k_usb_audio PRIVATE -O3)
    target_compile_options(t3k_probe PRIVATE -O3)
endif()

if (ANDROID)
    # 16 KB page alignment (see the same flag on the Standalone target in
    # plugin/CMakeLists.txt): NDK 27 does not default to it and Android 15+
    # shows the "isn't 16 KB compatible" dialog at launch without it.
    target_link_options(t3k_probe PRIVATE "-Wl,-z,max-page-size=16384")
endif()

target_link_libraries(t3k_probe PRIVATE
    t3k_usb_audio
    log   # __android_log_print
)
