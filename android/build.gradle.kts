// Applied into :app's own build script via apply(from = ...) - see
// android/app/build.gradle.kts's generic per-module loop, which does this
// for any enabled module that ships this exact file. This is NOT a
// separate Gradle subproject (no plugins{}/android{} block here, and no
// separate namespace/manifest) - it executes in :app's own Project context,
// so a plain dependencies{} block here behaves exactly as if it had been
// written directly in :app's build.gradle.kts, without the engine ever
// having to know this module - or Play Billing - exists. Configurations
// are added by string name ("implementation", not the typed
// implementation(...) function) - Gradle's type-safe accessors for a
// project's own configurations aren't generated for a script applied this
// way via apply(from = ...).
//
// Play Billing is a plain public Maven dependency (no partner account,
// unlike the desktop backends' vendored SDKs) - this file is only applied
// when store_google_play is actually enabled, so a build without the
// module carries no Billing Library code or its manifest-merged
// com.android.vending.BILLING permission at all.
dependencies {
    "implementation"("com.android.billingclient:billing:9.1.0")
}
