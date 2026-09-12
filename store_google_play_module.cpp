#include "store_google_play/store_google_play_platform.h"
#include "store_google_play/store_google_play_services.h"

#include "store/store_service.h"

#include "core/app/engine.h"
#include "core/app/module.h"
#include "core/app/module_context.h"

#include "core/foundation/diagnostics/log.h"

namespace nxm::store_google_play {
namespace {

const nx::log::Category log_store_google_play =
    nx::log::category("store_google_play");

// store.achievements/store.cloud_saves/store.presence are deliberately
// absent - Google Play Billing has none of those subsystems (Google Play
// Games Services is a separate, unrelated product for that), the same
// "simply doesn't provide it" shape used elsewhere in this module family
// for a backend whose SDK lacks a subsystem.
constexpr nxe::ModuleService PROVIDED_SERVICES[] = {
    {.id = store::kCoreService, .version = {1, 0, 0}},
    {.id = store::kIapService, .version = {1, 0, 0}},
};

class StoreGooglePlayModule final : public nxe::Module {
public:
  StoreGooglePlayModule() : m_core(m_platform), m_iap(m_platform) {}

  [[nodiscard]] nxe::ModuleDescriptor descriptor() const noexcept override {
    nxe::ModuleDescriptor out{};
    out.id = "store_google_play";
    out.version = {1, 0, 0};
    out.provided_services = PROVIDED_SERVICES;
    out.platforms = nxe::ModulePlatform::Android;
    return out;
  }

  bool on_register(nxe::ModuleContext &ctx) override {
    nxe::ServiceRegistrar registrar = ctx.service_registrar();
    store::StoreCore &core = m_core;
    store::StoreIap &iap = m_iap;
    return registrar.provide(store::kCoreService, PROVIDED_SERVICES[0].version, core) &&
           registrar.provide(store::kIapService, PROVIDED_SERVICES[1].version, iap);
  }

  bool on_attach(nxe::ModuleContext &) override {
    // No pump system registered here, unlike the desktop backends - every
    // Play Billing call resolves through the Java shim's own callbacks,
    // dispatched by the Android runtime itself (see
    // store_google_play_platform.h), not from anything this module needs
    // to poll each frame. There's also no per-project config file: Play
    // Billing takes no developer-supplied credentials at runtime at all -
    // BillingClient resolves everything from the installed Play Store
    // client and the app's own package/signing, matching
    // store_microsoft's own "nothing here to configure" shape.
    if (m_platform.initialize())
      nx::logi(log_store_google_play, "attached, connecting to Play Billing");
    else
      nx::logi(log_store_google_play, "no Android activity available; staying idle");
    return true;
  }

  void on_detach(nxe::ModuleContext &) override { m_platform.shutdown(); }

private:
  GooglePlayPlatform m_platform;
  GooglePlayCore m_core;
  GooglePlayIap m_iap;
};

} // namespace
} // namespace nxm::store_google_play

NX_DECLARE_MODULE(store_google_play, nxm::store_google_play::StoreGooglePlayModule)
