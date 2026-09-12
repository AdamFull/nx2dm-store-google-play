#pragma once

#include "store_google_play/store_google_play_gamesservices_platform.h"

#include "core/foundation/core/foundation.h"
#include "core/foundation/strings/utf8_string.h"

#include <jni.h>
#include <mutex>

namespace nxm::store_google_play {

/// The genuine backend-specific extra - no neutral `store.leaderboards`
/// service exists anywhere in this codebase, the same shape
/// `EgsLeaderboards`/`SteamLeaderboards`/`GameCenterLeaderboards` already
/// have. Unlike EOS (which ties score submission to `store_set_stat()`),
/// Play Games Services has a real, direct score-upload call, so
/// `submit_score()` here is genuine rather than a documented gap. Fixed
/// top-25/all-time/public download scope, matching Game Center's own fixed
/// range decision - no per-call range/scope parameters.
class PlayGamesLeaderboards {
public:
  explicit PlayGamesLeaderboards(PlayGamesPlatform &platform) noexcept;

  bool submit_score(nx::string_view leaderboard_id, i64 score);
  [[nodiscard]] bool submit_pending() const noexcept;

  bool download(nx::string_view leaderboard_id);
  [[nodiscard]] bool download_pending() const noexcept;
  [[nodiscard]] usize entry_count() const noexcept;
  [[nodiscard]] i64 entry_rank(usize index) const noexcept;
  [[nodiscard]] i64 entry_score(usize index) const noexcept;
  [[nodiscard]] nx::string_view entry_name(usize index) const noexcept;

  static void dispatch_score_submitted(jboolean success);
  static void dispatch_leaderboard_loaded(jlongArray ranks, jlongArray scores,
                                          jobjectArray names);

private:
  void on_score_submitted(jboolean success);
  void on_leaderboard_loaded(jlongArray ranks, jlongArray scores, jobjectArray names);

  PlayGamesPlatform &m_platform;
  mutable std::mutex m_mutex;
  bool m_submit_pending = false;
  bool m_download_pending = false;

  struct Entry {
    i64 rank = 0;
    i64 score = 0;
    nx::string name;
  };
  nx::vector<Entry> m_entries;

  static PlayGamesLeaderboards *s_instance;
};

}
