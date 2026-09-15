#include "store_google_play/store_google_play_gamesservices_leaderboards.h"
#include "store_google_play/store_google_play_gamesservices_platform.h"
#include "store_google_play/store_google_play_gamesservices_scripting.h"
#include "store_google_play/store_google_play_gamesservices_services.h"
#include "store_google_play/store_google_play_platform.h"
#include "store_google_play/store_google_play_services.h"

#include "store/store_service.h"

#include "core/app/engine.h"
#include "core/app/module_system/module.h"
#include "core/app/module_system/module_context.h"

#include "core/foundation/diagnostics/log.h"

namespace nxm::store_google_play {
namespace {

const nx::log::Category log_store_google_play =
    nx::log::category("store_google_play");

// store.presence stays absent - Play Games' own friends/presence surface
// needs its own broader scoping pass, out of scope here (the same
// per-player-authorization reasoning that already keeps store_app_store's
// Game Center presence absent). store.achievements/store.cloud_saves *are*
// now registered, backed by Play Games Services - a second, independent
// Google framework from Play Billing (see StoreGooglePlayModule's members
// below) - Play Billing itself still has neither subsystem.
constexpr nxe::ModuleService PROVIDED_SERVICES[] = {
    {.id = store::kCoreService, .version = {1, 0, 0}},
    {.id = store::kIapService, .version = {1, 0, 0}},
    {.id = store::kAchievementsService, .version = {1, 0, 0}},
    {.id = store::kCloudSavesService, .version = {1, 0, 0}},
};

class StoreGooglePlayModule final : public nxe::Module {
public:
  StoreGooglePlayModule()
      : m_core(m_platform), m_iap(m_platform),
        m_pg_achievements(m_play_games_platform),
        m_pg_cloud_saves(m_play_games_platform),
        m_pg_leaderboards(m_play_games_platform) {}

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
    store::StoreAchievements &achievements = m_pg_achievements;
    store::StoreCloudSaves &cloud_saves = m_pg_cloud_saves;
    return registrar.provide(store::kCoreService, PROVIDED_SERVICES[0].version, core) &&
           registrar.provide(store::kIapService, PROVIDED_SERVICES[1].version, iap) &&
           registrar.provide(store::kAchievementsService,
                              PROVIDED_SERVICES[2].version, achievements) &&
           registrar.provide(store::kCloudSavesService,
                              PROVIDED_SERVICES[3].version, cloud_saves);
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

    // Play Games Services is a second, independent framework - its own
    // readiness (sign-in state) has nothing to do with Play Billing's
    // above (see store_google_play_gamesservices_platform.h).
    m_play_games_platform.initialize();
    return true;
  }

  void on_expose_scripts(nxe::script::Host &host, nxe::ModuleContext &) override {
    expose_store_google_play_extras(host, m_play_games_platform, m_pg_leaderboards);
  }

  void on_detach(nxe::ModuleContext &) override {
    m_platform.shutdown();
    m_play_games_platform.shutdown();
  }

private:
  GooglePlayPlatform m_platform;
  GooglePlayCore m_core;
  GooglePlayIap m_iap;

  // Play Games Services - a second, independent Google framework from Play
  // Billing above, filling in store.achievements/store.cloud_saves (real
  // functionality this backend was missing) plus a leaderboards extra
  // (never part of store_service.h's neutral interface, never registered
  // through ServiceRegistry - see store_google_play_gamesservices_scripting.h).
  PlayGamesPlatform m_play_games_platform;
  PlayGamesAchievements m_pg_achievements;
  PlayGamesCloudSaves m_pg_cloud_saves;
  PlayGamesLeaderboards m_pg_leaderboards;
};

} // namespace
} // namespace nxm::store_google_play

NX_DECLARE_MODULE(store_google_play, nxm::store_google_play::StoreGooglePlayModule)
