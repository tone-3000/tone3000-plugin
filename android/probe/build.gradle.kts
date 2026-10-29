// Probe app for the USB audio spike (plan M1): enumerate, dump descriptors,
// stream, and self-measure round-trip latency against a USB Audio device.
// Deliberately dependency-free (no JUCE, no AndroidX): the native side links
// plugin/usb (t3k_usb_audio) directly, so what the probe proves is exactly
// what the real driver will do.
plugins {
    id("com.android.application")
}

val probeApplicationId = "com.TONE3000.USBCHECK"

android {
    namespace = probeApplicationId
    compileSdk = 36
    // Same pin as app/build.gradle.kts; the two modules must agree.
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = probeApplicationId
        minSdk = 27
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"

        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }

        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_PLATFORM=android-27")
                targets += listOf("t3k_probe")
            }
        }
    }

    // The probe has its own tiny CMake project (plan section 6.6): it pulls
    // in plugin/usb as a static lib and nothing from the main target graph.
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "4.1.2"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
