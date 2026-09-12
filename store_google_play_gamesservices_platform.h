#pragma once

#include "core/foundation/core/foundation.h"
#include "core/foundation/strings/utf8_string.h"

#include <jni.h>

namespace nxm::store_google_play {

/// Looks up `com.nx2d.runtime.NxPlayGamesServices` - shared by this
/// platform class and the achievements/cloud-saves/leaderboards classes,
/// each calling a different one of its static methods. Returns a local ref
/// (caller's to `DeleteLocalRef`), or nullptr with any pending exception
/// already cleared.
[[nodiscard]] jclass find_games_services_shim_class(JNIEnv *env);

/// Owns the JNI handle to a second, independent Java-side static facade
/// (`com.nx2d.runtime.NxPlayGamesServices`, contributed by this same
/// module's `android/java` tree) - Play Games Services is a separate
/// Google framework from Play Billing (see store_google_play_platform.h),
/// with its own readiness (sign-in state, nothing to do with
/// `GooglePlayPlatform::ready()`). Same JNI-plumbing shape as
/// `GooglePlayPlatform`: its own `JavaVM`/`Activity` global ref, its own
/// static "current instance" dispatch target for the `nativeOnXxx` JNI
/// exports defined across `store_google_play_gamesservices_platform.cpp`,
/// `_services.cpp`, and `_leaderboards.cpp`.
///
/// Unlike Play Billing's `BillingClient` (built once and held for the
/// module's lifetime), Play Games Services' `PlayGames.getXClient(activity)`
/// accessors are cheap, stateless, per-call lookups - there is no
/// persistent Java-side client object to connect/disconnect, so
/// `shutdown()` only releases the activity global ref.
class PlayGamesPlatform {
public:
  ~PlayGamesPlatform();

  bool initialize();
  void shutdown();

  /// Triggers `GamesSignInClient.signIn()` - the confirmed interactive
  /// extra this backend can support that Game Center (iOS) could not,
  /// since Play Games' own sign-in UI is a system overlay the app never
  /// has to present anything for. `initialize()` itself only performs a
  /// silent `isAuthenticated()` check.
  bool sign_in();
  [[nodiscard]] bool sign_in_pending() const noexcept { return m_sign_in_pending; }
  [[nodiscard]] bool authenticated() const noexcept { return m_authenticated; }

  [[nodiscard]] JavaVM *vm() const noexcept { return m_vm; }
  /// A global ref on the Android `Activity` SDL created this process with -
  /// every `NxPlayGamesServices` call needs one, the same
  /// `PlayGames.getXClient(activity)` shape throughout Google's own API.
  [[nodiscard]] jobject activity() const noexcept { return m_activity; }

  /// Dispatch target for this file's `nativeOnSignInResult` JNI export -
  /// public because a plain `extern "C"` function, not a member, is what
  /// the JVM actually calls.
  static void dispatch_sign_in_result(jboolean authenticated);

private:
  void on_sign_in_result(jboolean authenticated);

  JavaVM *m_vm = nullptr;
  jobject m_activity = nullptr;
  bool m_sign_in_pending = false;
  bool m_authenticated = false;

  static PlayGamesPlatform *s_instance;
};

}
