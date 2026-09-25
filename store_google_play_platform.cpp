#include "store_google_play/store_google_play_platform.h"

#include "core/foundation/platform/android_jni.h"

#include "core/foundation/diagnostics/log.h"

#include <SDL3/SDL_system.h>

namespace nxm::store_google_play {
namespace {

const nx::log::Category log_store_google_play =
    nx::log::category("store_google_play");

constexpr const char *SHIM_CLASS = "com/nx2d/runtime/NxGooglePlayBilling";

} // namespace

jclass find_billing_shim_class(JNIEnv *const env) {
  return nx::android::find_class(env, SHIM_CLASS);
}

GooglePlayPlatform *GooglePlayPlatform::s_instance = nullptr;

GooglePlayPlatform::~GooglePlayPlatform() { shutdown(); }

bool GooglePlayPlatform::initialize() {
  JNIEnv *const env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
  jobject const activity =
      env != nullptr ? static_cast<jobject>(SDL_GetAndroidActivity()) : nullptr;
  if (env == nullptr || activity == nullptr) {
    nx::logw(log_store_google_play, "no Android activity available");
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

  const jclass shim = find_billing_shim_class(env);
  if (shim == nullptr) {
    nx::logw(log_store_google_play,
              "NxGooglePlayBilling.class not found - was the module enabled "
              "when the APK was built?");
    return false;
  }
  const jmethodID connect = nx::android::static_method(
      env, shim, "connect", "(Landroid/content/Context;)V");
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

void GooglePlayPlatform::shutdown() {
  if (m_vm == nullptr)
    return;
  nx::android::run_java(m_threads, m_vm, [&](const nx::android::JniScope &env) {
    if (env && m_activity != nullptr) {
      const jclass shim = find_billing_shim_class(env.get());
      if (shim != nullptr) {
        const jmethodID disconnect =
            nx::android::static_method(env.get(), shim, "disconnect", "()V");
        if (disconnect != nullptr)
          env->CallStaticVoidMethod(shim, disconnect);
        if (env->ExceptionCheck())
          env->ExceptionClear();
        env->DeleteLocalRef(shim);
      }
      env->DeleteGlobalRef(m_activity);
    }
    m_activity = nullptr;
    m_vm = nullptr;
    m_ready = false;
    if (s_instance == this)
      s_instance = nullptr;
  });
}

void GooglePlayPlatform::on_billing_setup_finished(const jint response_code) {
  // BillingResponseCode.OK == 0 (com.android.billingclient.api.BillingClient
  // .BillingResponseCode) - not worth pulling in a mirrored C++ enum just
  // for this one comparison, the same call store_stove's Result-code checks
  // already make against a bare int.
  m_ready = response_code == 0;
  if (m_ready)
    nx::logi(log_store_google_play, "BillingClient connected");
  else
    nx::logi(log_store_google_play,
              "BillingClient setup failed ({}) - staying idle", response_code);
}

void GooglePlayPlatform::on_billing_service_disconnected() {
  m_ready = false;
  nx::logi(log_store_google_play, "BillingClient disconnected");
}

void GooglePlayPlatform::dispatch_billing_setup_finished(const jint response_code) {
  if (s_instance != nullptr)
    s_instance->on_billing_setup_finished(response_code);
}

void GooglePlayPlatform::dispatch_billing_service_disconnected() {
  if (s_instance != nullptr)
    s_instance->on_billing_service_disconnected();
}

}

extern "C" JNIEXPORT void JNICALL
Java_com_nx2d_runtime_NxGooglePlayBilling_nativeOnBillingSetupFinished(
    JNIEnv *, jclass, const jint response_code) {
  nxm::store_google_play::GooglePlayPlatform::dispatch_billing_setup_finished(
      response_code);
}

extern "C" JNIEXPORT void JNICALL
Java_com_nx2d_runtime_NxGooglePlayBilling_nativeOnBillingServiceDisconnected(
    JNIEnv *, jclass) {
  nxm::store_google_play::GooglePlayPlatform::dispatch_billing_service_disconnected();
}
