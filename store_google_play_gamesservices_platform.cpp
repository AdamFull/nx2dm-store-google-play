#include "store_google_play/store_google_play_gamesservices_platform.h"

#include "core/foundation/platform/android_jni.h"

#include "core/foundation/diagnostics/log.h"

#include <SDL3/SDL_system.h>

namespace nxm::store_google_play {
namespace {

const nx::log::Category log_store_google_play =
    nx::log::category("store_google_play");

constexpr const char *SHIM_CLASS = "com/nx2d/runtime/NxPlayGamesServices";

} // namespace

jclass find_games_services_shim_class(JNIEnv *const env) {
  return nx::android::find_class(env, SHIM_CLASS);
}

PlayGamesPlatform *PlayGamesPlatform::s_instance = nullptr;

PlayGamesPlatform::~PlayGamesPlatform() { shutdown(); }

bool PlayGamesPlatform::initialize() {
  JNIEnv *const env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
  jobject const activity =
      env != nullptr ? static_cast<jobject>(SDL_GetAndroidActivity()) : nullptr;
  if (env == nullptr || activity == nullptr) {
    nx::logw(log_store_google_play,
              "no Android activity available for Play Games Services");
    return false;
  }
  if (env->GetJavaVM(&m_vm) != JNI_OK || m_vm == nullptr) {
    nx::logw(log_store_google_play, "JNI_GetJavaVM failed");
    return false;
  }
  m_activity = env->NewGlobalRef(activity);
  env->DeleteLocalRef(activity);
  if (m_activity == nullptr) {
    nx::logw(log_store_google_play, "failed to hold a global ref on the activity");
    return false;
  }

  const jclass shim = find_games_services_shim_class(env);
  if (shim == nullptr) {
    nx::logw(log_store_google_play,
              "NxPlayGamesServices.class not found - was the module enabled "
              "when the APK was built?");
    return false;
  }
  const jmethodID connect = nx::android::static_method(
      env, shim, "connect", "(Landroid/app/Activity;)V");
  if (connect == nullptr) {
    env->DeleteLocalRef(shim);
    return false;
  }

  s_instance = this;
  env->CallStaticVoidMethod(shim, connect, m_activity);
  if (env->ExceptionCheck())
    env->ExceptionClear();
  env->DeleteLocalRef(shim);
  return true;
}

void PlayGamesPlatform::shutdown() {
  if (m_vm == nullptr)
    return;
  nx::android::run_java(m_threads, m_vm, [&](const nx::android::JniScope &env) {
    if (env && m_activity != nullptr)
      env->DeleteGlobalRef(m_activity);
    m_activity = nullptr;
    m_vm = nullptr;
    m_sign_in_pending = false;
    m_authenticated = false;
    if (s_instance == this)
      s_instance = nullptr;
  });
}

bool PlayGamesPlatform::sign_in() {
  if (m_vm == nullptr || m_activity == nullptr)
    return false;
  return nx::android::run_java(
      m_threads, m_vm, [&](const nx::android::JniScope &env) -> bool {
        if (!env)
          return false;
        const jclass shim = find_games_services_shim_class(env.get());
        if (shim == nullptr)
          return false;
        const jmethodID sign_in_method = nx::android::static_method(
            env.get(), shim, "signIn", "(Landroid/app/Activity;)V");
        if (sign_in_method == nullptr) {
          env->DeleteLocalRef(shim);
          return false;
        }
        m_sign_in_pending = true;
        env->CallStaticVoidMethod(shim, sign_in_method, m_activity);
        if (env->ExceptionCheck())
          env->ExceptionClear();
        env->DeleteLocalRef(shim);
        return true;
      });
}

void PlayGamesPlatform::on_sign_in_result(const jboolean authenticated) {
  m_sign_in_pending = false;
  m_authenticated = authenticated == JNI_TRUE;
  nx::logi(log_store_google_play, "Play Games Services sign-in: {}",
           m_authenticated ? "authenticated" : "not authenticated");
}

void PlayGamesPlatform::dispatch_sign_in_result(const jboolean authenticated) {
  if (s_instance != nullptr)
    s_instance->on_sign_in_result(authenticated);
}

}

extern "C" JNIEXPORT void JNICALL
Java_com_nx2d_runtime_NxPlayGamesServices_nativeOnSignInResult(
    JNIEnv *, jclass, const jboolean authenticated) {
  nxm::store_google_play::PlayGamesPlatform::dispatch_sign_in_result(authenticated);
}
