plugins {
    id("com.android.application")
}

// Matches T3K_IOS_BUNDLE_ID in plugin/CMakeLists.txt.
val t3kApplicationId = "com.TONE3000.TONE3000"

android {
    namespace = t3kApplicationId
    compileSdk = 36
    // Pinned: NDK 30's <aaudio/AAudio.h> redeclares symbols that conflict
    // with JUCE's vendored Oboe copy. Bump only after checking Oboe against
    // the newer NDK.
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = t3kApplicationId
        // 27 (Android 8.1) is the practical floor: juce_Fonts_android.cpp's
        // AFontMatcher/ASystemFontIterator calls carry a "strict" NDK
        // availability attribute that Clang rejects outright below API 29,
        // even inside JUCE's own __builtin_available guards (see the
        // T3K_ANDROID_FONT_API_29 patch in the root CMakeLists.txt, which
        // compiles those call sites out below 29). Below that patch, bumping
        // this back toward 29 is the fastest unblock.
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
                // Only the Standalone app is meaningful on Android; skip
                // VST3/the DSP test suite/etc.
                targets += listOf("TONE3000_Standalone")
            }
        }
    }

    // Points at the repo's own root CMakeLists.txt, not a duplicate Android
    // tree. Gradle's externalNativeBuild auto-injects
    // CMAKE_TOOLCHAIN_FILE/ANDROID_ABI/ANDROID_PLATFORM for this sub-build.
    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "4.1.2"
        }
    }

    // JUCE's own Android Java glue (JuceApp/JuceActivity, referenced
    // directly in AndroidManifest.xml). Paths vary by module: some use
    // "java", some "javacore"/"javaopt".
    sourceSets["main"].java.srcDirs(
        "../../libs/juce/modules/juce_core/native/java/app",
        "../../libs/juce/modules/juce_core/native/javacore/app",
        "../../libs/juce/modules/juce_core/native/javacore/init",
        "../../libs/juce/modules/juce_gui_basics/native/java/app",
        "../../libs/juce/modules/juce_gui_basics/native/javaopt/app"
    )

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

// Factory presets ride as APK assets (Android has no installer or readable
// bundle path like desktop/iOS); PresetManager::extractFactoryPresetsFromAssets()
// copies them to internal storage on first use.
//
// Registered as an assets source dir - lands at the assets root, matching
// what extractFactoryPresetsFromAssets() scans - rather than a Copy task
// into src/main/assets: AGP tracks sourceSets as real task inputs
// everywhere (lint included), but a Copy task isn't, and needs manual
// dependsOn wiring on every consumer.
android.sourceSets.getByName("main").assets.srcDirs("../../resources/factory-presets")

// The JUCE Java sources above are fetched and patched by the CMake configure
// step (see T3K_ANDROID_HTTP_HEADERS in the root CMakeLists.txt), so compile
// them only after it has run. mustRunAfter, not dependsOn: every build already
// schedules its own variant's configure, and any one of them patches the
// shared libs/juce tree.
tasks.withType<JavaCompile>().configureEach {
    mustRunAfter(tasks.matching { it.name.startsWith("configureCMake") })
}
