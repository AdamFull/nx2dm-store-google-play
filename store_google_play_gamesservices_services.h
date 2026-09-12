#pragma once

#include "store_google_play/store_google_play_gamesservices_platform.h"

#include "store/store_service.h"

#include <jni.h>
#include <mutex>

namespace nxm::store_google_play {

/// The two neutral services Play Games Services fills in for this backend -
/// store.achievements/store.cloud_saves were previously absent because
/// Play Billing (store_google_play_services.h) has neither subsystem, Play
/// Games Services being "a separate, unrelated product" for that, the same
/// phrasing this module's own comments used before this file existed.
/// store.presence stays absent - Play Games' own friends/presence surface
/// needs its own broader scoping pass, out of scope here (matching Game
/// Center's own presence exclusion on iOS, for an analogous per-player
/// authorization reason).
///
/// Both classes share the family's established async-cache-plus-refresh
/// shape: every neutral getter reads a cache, and a `refresh*()`/query call
/// triggers the real Play Games query via `NxPlayGamesServices`'s JNI
/// shim, landing back through the `nativeOnXxx` exports in
/// store_google_play_gamesservices_services.cpp. A mutex guards each cache
/// since `Task.addOnCompleteListener` callbacks are not guaranteed to land
/// on any particular thread relative to the engine thread reading the
/// cache - the same reason `MicrosoftCore`/`MicrosoftIap`/`GameCenter*`
/// already mutex-guard theirs.
class PlayGamesAchievements final : public store::StoreAchievements {
public:
  explicit PlayGamesAchievements(PlayGamesPlatform &platform) noexcept;

  /// `AchievementsClient.unlock()` has no completion callback at all in
  /// Google's own API (it applies locally and syncs in the background), so
  /// this updates `m_unlocked_ids` optimistically the moment the JNI call
  /// returns - the same "assume success" shape `EgsAchievements::unlock()`
  /// already uses, but here because the SDK genuinely never confirms,
  /// rather than by choice.
  bool unlock(nx::string_view id) override;
  [[nodiscard]] bool is_unlocked(nx::string_view id) const override;
  [[nodiscard]] nx::vector<nx::string> achievement_ids() const override;

  /// Honest gap - Play Games Services has no general-purpose stat store
  /// separate from (incremental) achievement progress, the same shape
  /// `GameCenterAchievements::set_stat()`/`stat()` already document for
  /// GameKit.
  bool set_stat(nx::string_view, f64) override { return false; }
  [[nodiscard]] f64 stat(nx::string_view) const override { return 0.0; }

  /// Fires `NxPlayGamesServices.loadAchievements()`, refreshing
  /// achievement_ids()/is_unlocked(). @p stat_ids is ignored - nothing to
  /// scope Play Games' own achievement query by.
  void refresh(const nx::vector<nx::string> & = {}) override;

  static void dispatch_achievements_loaded(jobjectArray ids, jobjectArray unlocked_ids);

private:
  void on_achievements_loaded(jobjectArray ids, jobjectArray unlocked_ids);

  PlayGamesPlatform &m_platform;
  mutable std::mutex m_mutex;
  nx::vector<nx::string> m_achievement_ids;
  nx::vector<nx::string> m_unlocked_ids;

  static PlayGamesAchievements *s_instance;
};

/// Backed by Play Games Services' Snapshots API - a binary-blob/file-like
/// API, not a natural fit for `StoreCloudSaves`'s own "not a file API"
/// key/value model (its own doc comment flags this explicitly), the same
/// mapping gap `GameCenterCloudSaves` already crosses for `GKSavedGame`.
/// Conflict resolution beyond `RESOLUTION_POLICY_MOST_RECENTLY_MODIFIED`'s
/// own auto-resolution is not handled - a real, narrower scope reduction,
/// the same "real gap, not an oversight" bar Game Center's own
/// unhandled `resolveConflictingSavedGames:` already sets.
class PlayGamesCloudSaves final : public store::StoreCloudSaves {
public:
  explicit PlayGamesCloudSaves(PlayGamesPlatform &platform) noexcept;

  bool write(nx::string_view key, nx::string_view value) override;
  /// One in-flight read at a time - `m_read_buffer` is unconditionally
  /// overwritten by whichever `nativeOnSnapshotRead` lands next, the same
  /// simplification `EgsCloudSaves`/`GameCenterCloudSaves` already
  /// document for their own single shared read slot.
  [[nodiscard]] nx::string read(nx::string_view key) const override;
  [[nodiscard]] bool exists(nx::string_view key) const override;
  bool remove(nx::string_view key) override;
  [[nodiscard]] nx::vector<nx::string> keys() const override;

  /// Honest gap - Snapshots has no simple quota-query API, the same shape
  /// EOS's `PlayerDataStorage` and `GameCenterCloudSaves` already have.
  [[nodiscard]] u64 bytes_used() const override { return 0; }
  [[nodiscard]] u64 bytes_total() const override { return 0; }

  /// Fires `NxPlayGamesServices.loadSnapshotMetadata()`, refreshing keys().
  void refresh_keys() override;

  static void dispatch_snapshot_written(jstring name, jboolean success);
  static void dispatch_snapshot_read(jstring name, jbyteArray data);
  static void dispatch_snapshot_deleted(jstring name, jboolean success);
  static void dispatch_snapshot_metadata_loaded(jobjectArray names);

private:
  void on_snapshot_written(jstring name, jboolean success);
  void on_snapshot_read(jstring name, jbyteArray data);
  void on_snapshot_deleted(jstring name, jboolean success);
  void on_snapshot_metadata_loaded(jobjectArray names);

  PlayGamesPlatform &m_platform;
  mutable std::mutex m_mutex;
  nx::vector<nx::string> m_keys;
  nx::string m_read_buffer;

  static PlayGamesCloudSaves *s_instance;
};

}
