pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "TONE3000"
include(":app")
// USB probe spike (plan M1): a separate, dependency-free application module.
include(":probe")
