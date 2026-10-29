# Host unit tests for the USB audio driver core (plan L0). Pure C++ and
# GoogleTest, wired like test/ (DspTests) but with no JUCE at all: the core
# under test has none. Runs in seconds on the build machine; the fixtures
# directory holds captured descriptor blobs once the probe produces them
# (plan M1.3) and synthetic blobs until then.

add_executable(UsbTests
    audio_ring_tests.cpp
    clock_model_tests.cpp
    pcm_convert_tests.cpp
    rtl_tests.cpp
    uac_control_tests.cpp
    uac_descriptors_tests.cpp
    usbaudiostream_tests.cpp
    usb_quirks_tests.cpp
)

target_link_libraries(UsbTests PRIVATE t3k_usb_audio GTest::gtest GTest::gtest_main)
target_compile_definitions(UsbTests PRIVATE
    T3K_USB_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

include(GoogleTest)
gtest_discover_tests(UsbTests)
