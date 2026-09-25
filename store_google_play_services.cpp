#include "store_google_play/store_google_play_services.h"

#include "core/foundation/platform/android_jni.h"

#include "core/foundation/diagnostics/log.h"
#include "core/foundation/strings/format.h"

#include <utility>

namespace nxm::store_google_play {
namespace {

const nx::log::Category log_store_google_play =
    nx::log::category("store_google_play");

/// Looks up and calls a static void method on the Java shim by name/
/// signature, forwarding whatever jvalue args the caller already built -
/// every outbound call in this file (queryPurchases/queryProductDetails/
/// purchase) follows this exact shape, so it's centralized once here
/// rather than repeating the FindClass/GetStaticMethodID/exception-clear
/// dance per call site.
void call_shim_static_void(JNIEnv *const env, const char *const name,
                           const char *const signature, jvalue *const args) {
  const jclass shim = find_billing_shim_class(env);
  if (shim == nullptr)
    return;
  const jmethodID method = nx::android::static_method(env, shim, name, signature);
  if (method != nullptr) {
    env->CallStaticVoidMethodA(shim, method, args);
    if (env->ExceptionCheck())
      env->ExceptionClear();
  }
  env->DeleteLocalRef(shim);
}

} // namespace

// -- GooglePlayCore -----------------------------------------------------

GooglePlayCore *GooglePlayCore::s_instance = nullptr;

GooglePlayCore::GooglePlayCore(GooglePlayPlatform &platform) noexcept
    : m_platform(platform) {
  s_instance = this;
}

GooglePlayCore::~GooglePlayCore() {
  if (s_instance == this)
    s_instance = nullptr;
}

bool GooglePlayCore::is_owned(const nx::string_view dlc_id) const {
  if (!m_platform.ready())
    return false;
  if (dlc_id.empty())
    return true;
  for (const nx::string &id : m_owned_dlc_ids)
    if (id.view() == dlc_id)
      return true;
  return false;
}

void GooglePlayCore::refresh_ownership(const nx::string_view) {
  if (!m_platform.ready())
    return;
  nx::android::run_java(m_platform.threads(), m_platform.vm(),
                        [&](const nx::android::JniScope &env) {
                          if (!env)
                            return;
                          call_shim_static_void(env.get(), "queryPurchases",
                                                "()V", nullptr);
                        });
}

void GooglePlayCore::on_purchases_queried(const jint response_code,
                                          const jobjectArray product_ids) {
  if (response_code != 0)
    return;
  const nx::android::JniScope env(m_platform.vm());
  if (!env)
    return;
  m_owned_dlc_ids = nx::android::to_nx_string_vector(env.get(), product_ids);
}

void GooglePlayCore::dispatch_purchases_queried(const jint response_code,
                                                const jobjectArray product_ids) {
  if (s_instance != nullptr)
    s_instance->on_purchases_queried(response_code, product_ids);
}

// -- GooglePlayIap --------------------------------------------------------

GooglePlayIap *GooglePlayIap::s_instance = nullptr;

GooglePlayIap::GooglePlayIap(GooglePlayPlatform &platform) noexcept
    : m_platform(platform) {
  s_instance = this;
}

GooglePlayIap::~GooglePlayIap() {
  if (s_instance == this)
    s_instance = nullptr;
}

bool GooglePlayIap::purchase(const nx::string_view product_id) {
  if (!m_platform.ready())
    return false;
  return nx::android::run_java(
      m_platform.threads(), m_platform.vm(),
      [&](const nx::android::JniScope &env) -> bool {
        if (!env)
          return false;
        const jstring id = nx::android::to_jstring(env.get(), product_id);
        if (id == nullptr)
          return false;

        m_purchase_pending = true;
        m_purchase_error = nx::string{};

        jvalue args[2];
        args[0].l = m_platform.activity();
        args[1].l = id;
        call_shim_static_void(env.get(), "purchase",
                              "(Landroid/app/Activity;Ljava/lang/String;)V",
                              args);
        env->DeleteLocalRef(id);
        return true;
      });
}

void GooglePlayIap::refresh_products(const nx::vector<nx::string> &product_ids) {
  if (!m_platform.ready())
    return;
  nx::android::run_java(
      m_platform.threads(), m_platform.vm(),
      [&](const nx::android::JniScope &env) {
        if (!env)
          return;
        const jobjectArray ids =
            nx::android::to_jstring_array(env.get(), product_ids);
        if (ids == nullptr)
          return;
        jvalue args[1];
        args[0].l = ids;
        call_shim_static_void(env.get(), "queryProductDetails",
                              "([Ljava/lang/String;)V", args);
        env->DeleteLocalRef(ids);
      });
}

void GooglePlayIap::on_product_details_response(
    const jint response_code, const jobjectArray product_ids,
    const jobjectArray titles, const jobjectArray formatted_prices) {
  if (response_code != 0)
    return;
  const nx::android::JniScope env(m_platform.vm());
  if (!env)
    return;
  const nx::vector<nx::string> ids = nx::android::to_nx_string_vector(env.get(), product_ids);
  const nx::vector<nx::string> names = nx::android::to_nx_string_vector(env.get(), titles);
  const nx::vector<nx::string> prices =
      nx::android::to_nx_string_vector(env.get(), formatted_prices);

  nx::vector<store::StoreProduct> products;
  products.reserve(ids.size());
  for (usize i = 0; i < ids.size(); ++i) {
    store::StoreProduct product;
    product.id = ids[i];
    product.title = i < names.size() ? names[i] : nx::string{};
    product.price_display = i < prices.size() ? prices[i] : nx::string{};
    products.push_back(std::move(product));
  }
  m_products = std::move(products);
}

void GooglePlayIap::on_purchases_updated(const jint response_code,
                                         const jobjectArray,
                                         const jobjectArray) {
  m_purchase_pending = false;
  if (response_code != 0)
    m_purchase_error = nx::format("Google Play error {}", response_code);
}

void GooglePlayIap::on_purchase_launch_failed(const jint response_code) {
  m_purchase_pending = false;
  m_purchase_error = nx::format("Google Play error {}", response_code);
}

void GooglePlayIap::dispatch_product_details_response(
    const jint response_code, const jobjectArray product_ids,
    const jobjectArray titles, const jobjectArray formatted_prices) {
  if (s_instance != nullptr)
    s_instance->on_product_details_response(response_code, product_ids, titles,
                                            formatted_prices);
}

void GooglePlayIap::dispatch_purchases_updated(const jint response_code,
                                               const jobjectArray product_ids,
                                               const jobjectArray purchase_tokens) {
  if (s_instance != nullptr)
    s_instance->on_purchases_updated(response_code, product_ids, purchase_tokens);
}

void GooglePlayIap::dispatch_purchase_launch_failed(const jint response_code) {
  if (s_instance != nullptr)
    s_instance->on_purchase_launch_failed(response_code);
}

}

extern "C" JNIEXPORT void JNICALL
Java_com_nx2d_runtime_NxGooglePlayBilling_nativeOnProductDetailsResponse(
    JNIEnv *, jclass, const jint response_code, const jobjectArray product_ids,
    const jobjectArray titles, const jobjectArray formatted_prices) {
  nxm::store_google_play::GooglePlayIap::dispatch_product_details_response(
      response_code, product_ids, titles, formatted_prices);
}

extern "C" JNIEXPORT void JNICALL
Java_com_nx2d_runtime_NxGooglePlayBilling_nativeOnPurchasesUpdated(
    JNIEnv *, jclass, const jint response_code, const jobjectArray product_ids,
    const jobjectArray purchase_tokens) {
  nxm::store_google_play::GooglePlayIap::dispatch_purchases_updated(
      response_code, product_ids, purchase_tokens);
}

extern "C" JNIEXPORT void JNICALL
Java_com_nx2d_runtime_NxGooglePlayBilling_nativeOnPurchaseLaunchFailed(
    JNIEnv *, jclass, const jint response_code) {
  nxm::store_google_play::GooglePlayIap::dispatch_purchase_launch_failed(response_code);
}

extern "C" JNIEXPORT void JNICALL
Java_com_nx2d_runtime_NxGooglePlayBilling_nativeOnPurchasesQueried(
    JNIEnv *, jclass, const jint response_code, const jobjectArray product_ids) {
  nxm::store_google_play::GooglePlayCore::dispatch_purchases_queried(response_code,
                                                                     product_ids);
}
