#pragma once

#include "core/foundation/core/foundation.h"
#include "core/foundation/strings/utf8_string.h"

#include <jni.h>

namespace nxm::store_google_play {

/// Looks up `com.nx2d.runtime.NxGooglePlayBilling` - shared by the platform
/// and both services, each of which calls a different one of its static
/// methods. Returns a local ref (caller's to `DeleteLocalRef`), or nullptr
/// with any pending exception already cleared.
[[nodiscard]] jclass find_billing_shim_class(JNIEnv *env);

/// Owns the JNI handle to the Java-side static facade
/// `com.nx2d.runtime.NxGooglePlayBilling` (contributed by this module's own
/// `modules/store_google_play/android/java` tree - see
/// `android/app/build.gradle.kts`'s per-module Java source-dir loop; never
/// referenced from `NxActivity.java`).
///
/// Google Play Billing has **no native/NDK API at all** - unlike every
/// other backend in this family, it is pure Java, confirmed absent from
/// Google's own documentation and forums. This class (and
/// `store_google_play_services.h`) is therefore pure JNI plumbing: call
/// into `NxGooglePlayBilling`'s static methods via
/// `nxe::rt::android::JniScope` (already used by
/// `engine/core/runtime/sdl/sdl_haptics.cpp` for the same "attach whichever
/// thread is calling in, not necessarily Android's main thread" reason),
/// and receive results back through a handful of JNI-exported C++
/// functions Java calls directly
/// (`Java_com_nx2d_runtime_NxGooglePlayBilling_nativeOnXxx`, resolved by
/// the JVM's own symbol-name convention against the already-loaded
/// `libnx2d.so` - no `RegisterNatives`/`JNI_OnLoad` needed). This is the
/// first Java-calls-C++ direction anywhere in this codebase; every
/// existing JNI caller only goes the other way.
///
/// Exactly one `GooglePlayPlatform` (and one `GooglePlayCore`/
/// `GooglePlayIap`) is ever alive in a process, the same invariant
/// `order_modules()` already enforces for "only one store backend active" -
/// each JNI export function below dispatches through a static "current
/// instance" pointer, mirroring `store_stove`'s own pattern for its
/// userdata-less C callbacks.
class GooglePlayPlatform {
public:
  ~GooglePlayPlatform();

  bool initialize();
  void shutdown();

  [[nodiscard]] bool ready() const noexcept { return m_ready; }

  [[nodiscard]] JavaVM *vm() const noexcept { return m_vm; }
  /// A global ref on the Android `Activity` SDL created this process with -
  /// both a `Context` (for `NxGooglePlayBilling.connect()`) and an
  /// `Activity` (for `.purchase()`, which needs one to host Play's own
  /// purchase UI).
  [[nodiscard]] jobject activity() const noexcept { return m_activity; }

  /// Dispatch targets for this module's two `nativeOnXxx` JNI exports
  /// (defined in store_google_play_platform.cpp) - public because a plain
  /// `extern "C"` function, not a member, is what the JVM actually calls.
  static void dispatch_billing_setup_finished(jint response_code);
  static void dispatch_billing_service_disconnected();

private:
  void on_billing_setup_finished(jint response_code);
  void on_billing_service_disconnected();

  JavaVM *m_vm = nullptr;
  jobject m_activity = nullptr;
  bool m_ready = false;

  static GooglePlayPlatform *s_instance;
};

}
