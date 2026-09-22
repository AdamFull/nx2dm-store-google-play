#include "store_google_play/store_google_play_gamesservices_scripting.h"

#include "store_google_play/store_google_play_gamesservices_leaderboards.h"
#include "store_google_play/store_google_play_gamesservices_platform.h"

#include "script/script_host.h"

namespace nxm::store_google_play {

namespace {

/// One leaderboard row as a script reads it.
struct LeaderboardEntry {
  f64 rank = 0.0;
  f64 score = 0.0;
  nx::string_view name;
};

}

void expose_store_google_play_extras(nxe::script::Host &host, PlayGamesPlatform &platform,
                                     PlayGamesLeaderboards &leaderboards) {
  // -- Sign-in ------------------------------------------------------------

  host.expose_as("store_google_play_sign_in",
                 [&platform]() { return platform.sign_in(); });
  host.expose_as("store_google_play_sign_in_pending",
                 [&platform]() { return platform.sign_in_pending(); });
  host.expose_as("store_google_play_authenticated",
                 [&platform]() { return platform.authenticated(); });

  // -- Leaderboards ---------------------------------------------------

  host.expose_as("store_google_play_leaderboard_submit_score",
                 [&leaderboards](const nx::string_view leaderboard_id, const f64 score) {
                   return leaderboards.submit_score(leaderboard_id, nx::cast<i64>(score));
                 });
  host.expose_as("store_google_play_leaderboard_submit_pending", [&leaderboards]() {
    return leaderboards.submit_pending();
  });
  host.expose_as("store_google_play_leaderboard_download",
                 [&leaderboards](const nx::string_view leaderboard_id) {
                   return leaderboards.download(leaderboard_id);
                 });
  host.expose_as("store_google_play_leaderboard_download_pending", [&leaderboards]() {
    return leaderboards.download_pending();
  });
  host.expose_as("store_google_play_leaderboard_entries", [&leaderboards] {
    nx::vector<LeaderboardEntry> out;
    out.reserve(leaderboards.entry_count());
    for (usize i = 0; i < leaderboards.entry_count(); ++i)
      out.push_back({static_cast<f64>(leaderboards.entry_rank(i)),
                     static_cast<f64>(leaderboards.entry_score(i)),
                     leaderboards.entry_name(i)});
    return out;
  });
  host.expose_as("store_google_play_leaderboard_entry_count", [&leaderboards]() {
    return static_cast<f64>(leaderboards.entry_count());
  });
  host.expose_as("store_google_play_leaderboard_entry_rank",
                 [&leaderboards](const f64 index) {
                   return static_cast<f64>(leaderboards.entry_rank(nx::cast<usize>(index)));
                 });
  host.expose_as("store_google_play_leaderboard_entry_score",
                 [&leaderboards](const f64 index) {
                   return static_cast<f64>(leaderboards.entry_score(nx::cast<usize>(index)));
                 });
  host.expose_as("store_google_play_leaderboard_entry_name",
                 [&leaderboards](const f64 index) {
                   return leaderboards.entry_name(nx::cast<usize>(index));
                 });
}

}
