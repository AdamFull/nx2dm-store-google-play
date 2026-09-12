#pragma once

#include "store_google_play/store_google_play_platform.h"

#include "store/store_service.h"

#include <jni.h>

namespace nxm::store_google_play {

/// Two of the five neutral services (store_service.h), backed by Google
/// Play Billing - store.achievements/store.cloud_saves/store.presence are
/// never registered: Play Billing has none of those subsystems (Google
/// Play Games Services is a separate, unrelated product for that), the
/// same "simply doesn't provide it" shape used elsewhere in this module
/// family for a backend whose SDK lacks a subsystem.
///
/// Every call here is asynchronous via the Java shim's callbacks (see
/// store_google_play_platform.h for the JNI mechanics) - both classes
/// therefore hold a small cache populated by their own query's JNI-exported
/// callback, with the neutral interface's synchronous methods reading
/// whatever is cached so far, the same eventually-consistent shape
/// store_egs's own per-service caches already established for a fully
/// async SDK.

class GooglePlayCore final : public store::StoreCore {
public:
  explicit GooglePlayCore(GooglePlayPlatform &platform) noexcept;
  ~GooglePlayCore() override;

  /// Play Billing has no "own the base game" concept at all - the Play
  /// Store already gates who can install/run the APK, so a non-empty
  /// process implies base ownership. A non-empty @p dlc_id checks the
  /// cache refresh_ownership() populates instead.
  [[nodiscard]] bool is_owned(nx::string_view dlc_id = {}) const override;
  [[nodiscard]] nx::vector<nx::string> owned_dlc_ids() const override {
    return m_owned_dlc_ids;
  }
  [[nodiscard]] nx::string_view store_name() const noexcept override {
    return "google_play";
  }

  /// Fires `NxGooglePlayBilling.queryPurchases()` - Play Billing has no
  /// separate ownership concept from purchase state, an unconsumed
  /// PURCHASED-state INAPP product IS the entitlement, refreshing
  /// owned_dlc_ids().
  void refresh_ownership();

  static void dispatch_purchases_queried(jint response_code,
                                         jobjectArray product_ids);

private:
  void on_purchases_queried(jint response_code, jobjectArray product_ids);

  GooglePlayPlatform &m_platform;
  nx::vector<nx::string> m_owned_dlc_ids;

  static GooglePlayCore *s_instance;
};

class GooglePlayIap final : public store::StoreIap {
public:
  explicit GooglePlayIap(GooglePlayPlatform &platform) noexcept;
  ~GooglePlayIap() override;

  [[nodiscard]] nx::vector<store::StoreProduct> products() const override {
    return m_products;
  }
  bool purchase(nx::string_view product_id) override;
  [[nodiscard]] bool purchase_pending() const override { return m_purchase_pending; }
  [[nodiscard]] nx::string_view purchase_error() const override {
    return m_purchase_error.view();
  }

  /// Fires `NxGooglePlayBilling.queryProductDetails()` for exactly the ids
  /// given - unlike Steam/EGS, Play Billing has no "list everything"
  /// query, the game must know its own product ids up front (the same
  /// limitation GOG Galaxy's DLC-ownership check already has).
  void refresh_products(const nx::vector<nx::string> &product_ids);

  static void dispatch_product_details_response(jint response_code,
                                                 jobjectArray product_ids,
                                                 jobjectArray titles,
                                                 jobjectArray formatted_prices);
  static void dispatch_purchases_updated(jint response_code,
                                         jobjectArray product_ids,
                                         jobjectArray purchase_tokens);
  static void dispatch_purchase_launch_failed(jint response_code);

private:
  void on_product_details_response(jint response_code, jobjectArray product_ids,
                                   jobjectArray titles,
                                   jobjectArray formatted_prices);
  void on_purchases_updated(jint response_code, jobjectArray product_ids,
                            jobjectArray purchase_tokens);
  void on_purchase_launch_failed(jint response_code);

  GooglePlayPlatform &m_platform;
  nx::vector<store::StoreProduct> m_products;
  bool m_purchase_pending = false;
  nx::string m_purchase_error;

  static GooglePlayIap *s_instance;
};

}
