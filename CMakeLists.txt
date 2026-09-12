
option(NX_MODULE_STORE_GOOGLE_PLAY "Build the Google Play Billing storefront backend" OFF)

if (NOT NX_MODULE_STORE_GOOGLE_PLAY)
    return()
endif ()

# nx_module()'s own PLATFORMS gate runs too late to help here - a project
# that also builds for Windows/Linux/macOS/iOS still passes
# -DNX_MODULE_STORE_GOOGLE_PLAY=ON for this platform-irrelevant module (its
# module list isn't per-platform), so this must skip itself before ever
# reaching that gate, the same "skipped on <platform>" outcome
# _nx_module_gate would otherwise report.
if (NOT ANDROID)
    message(STATUS "nx2d: module 'store_google_play' skipped on this platform")
    return()
endif ()

# No vendor SDK to resolve here - Google Play Billing has no native/NDK API
# at all (confirmed absent from Google's own documentation), only a plain
# public Gradle dependency (android/app/build.gradle.kts, gated on this
# same NX_MODULE_STORE_GOOGLE_PLAY flag) and a hand-written Kotlin JNI shim
# (modules/store_google_play/android/java/com/nx2d/runtime/
# NxGooglePlayBilling.kt, picked up by that same build script's per-module
# source-dir loop). This CMakeLists.txt only builds the C++ side of the JNI
# boundary.
nx_module(store_google_play
        DISPLAY "Google Play Billing storefront backend"
        SOURCES store_google_play_module.cpp store_google_play_platform.cpp
                store_google_play_services.cpp
        DEPENDS nx_module_store nx::sdl
        PLATFORMS android)
