#include "store_google_play/store_google_play_gamesservices_leaderboards.h"

#include "core/foundation/platform/android_jni.h"

#include "core/foundation/diagnostics/log.h"

#include <utility>

namespace nxm::store_google_play {
namespace {

/// Same shape as the other two .cpp files in this module - looked up and
/// duplicated locally rather than shared, matching
/// store_google_play_services.cpp's own convention.
void call_shim_static_void(JNIEnv *const env, const char *const name,
                           const char *const signature, jvalue *const args) {
  const jclass shim = find_games_services_shim_class(env);
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

PlayGamesLeaderboards *PlayGamesLeaderboards::s_instance = nullptr;

PlayGamesLeaderboards::PlayGamesLeaderboards(PlayGamesPlatform &platform) noexcept
    : m_platform(platform) {
  s_instance = this;
}

bool PlayGamesLeaderboards::submit_score(const nx::string_view leaderboard_id,
                                         const i64 score) {
  if (!m_platform.authenticated())
    return false;
  return nx::android::run_java(
      m_platform.threads(), m_platform.vm(),
      [&](const nx::android::JniScope &env) -> bool {
        if (!env)
          return false;
        const jstring id = nx::android::to_jstring(env.get(), leaderboard_id);
        if (id == nullptr)
          return false;

        {
          const std::lock_guard lock(m_mutex);
          m_submit_pending = true;
        }

        jvalue args[3];
        args[0].l = m_platform.activity();
        args[1].l = id;
        args[2].j = static_cast<jlong>(score);
        call_shim_static_void(env.get(), "submitScore",
                              "(Landroid/app/Activity;Ljava/lang/String;J)V",
                              args);
        env->DeleteLocalRef(id);
        return true;
      });
}

bool PlayGamesLeaderboards::submit_pending() const noexcept {
  const std::lock_guard lock(m_mutex);
  return m_submit_pending;
}

bool PlayGamesLeaderboards::download(const nx::string_view leaderboard_id) {
  if (!m_platform.authenticated())
    return false;
  return nx::android::run_java(
      m_platform.threads(), m_platform.vm(),
      [&](const nx::android::JniScope &env) -> bool {
        if (!env)
          return false;
        const jstring id = nx::android::to_jstring(env.get(), leaderboard_id);
        if (id == nullptr)
          return false;

        {
          const std::lock_guard lock(m_mutex);
          m_download_pending = true;
        }

        jvalue args[2];
        args[0].l = m_platform.activity();
        args[1].l = id;
        call_shim_static_void(env.get(), "loadTopScores",
                              "(Landroid/app/Activity;Ljava/lang/String;)V",
                              args);
        env->DeleteLocalRef(id);
        return true;
      });
}

bool PlayGamesLeaderboards::download_pending() const noexcept {
  const std::lock_guard lock(m_mutex);
  return m_download_pending;
}

usize PlayGamesLeaderboards::entry_count() const noexcept {
  const std::lock_guard lock(m_mutex);
  return m_entries.size();
}

i64 PlayGamesLeaderboards::entry_rank(const usize index) const noexcept {
  const std::lock_guard lock(m_mutex);
  return index < m_entries.size() ? m_entries[index].rank : 0;
}

i64 PlayGamesLeaderboards::entry_score(const usize index) const noexcept {
  const std::lock_guard lock(m_mutex);
  return index < m_entries.size() ? m_entries[index].score : 0;
}

nx::string_view PlayGamesLeaderboards::entry_name(const usize index) const noexcept {
  const std::lock_guard lock(m_mutex);
  return index < m_entries.size() ? m_entries[index].name.view() : nx::string_view{};
}

void PlayGamesLeaderboards::on_score_submitted(const jboolean) {
  const std::lock_guard lock(m_mutex);
  m_submit_pending = false;
}

void PlayGamesLeaderboards::on_leaderboard_loaded(const jlongArray ranks,
                                                  const jlongArray scores,
                                                  const jobjectArray names) {
  const nx::android::JniScope env(m_platform.vm());
  if (!env)
    return;
  const nx::vector<i64> rank_values = nx::android::to_nx_i64_vector(env.get(), ranks);
  const nx::vector<i64> score_values = nx::android::to_nx_i64_vector(env.get(), scores);
  const nx::vector<nx::string> name_values =
      nx::android::to_nx_string_vector(env.get(), names);

  nx::vector<Entry> entries;
  entries.reserve(rank_values.size());
  for (usize i = 0; i < rank_values.size(); ++i) {
    Entry entry;
    entry.rank = rank_values[i];
    entry.score = i < score_values.size() ? score_values[i] : 0;
    entry.name = i < name_values.size() ? name_values[i] : nx::string{};
    entries.push_back(std::move(entry));
  }

  const std::lock_guard lock(m_mutex);
  m_download_pending = false;
  m_entries = std::move(entries);
}

void PlayGamesLeaderboards::dispatch_score_submitted(const jboolean success) {
  if (s_instance != nullptr)
    s_instance->on_score_submitted(success);
}

void PlayGamesLeaderboards::dispatch_leaderboard_loaded(const jlongArray ranks,
                                                         const jlongArray scores,
                                                         const jobjectArray names) {
  if (s_instance != nullptr)
    s_instance->on_leaderboard_loaded(ranks, scores, names);
}

}

extern "C" JNIEXPORT void JNICALL
Java_com_nx2d_runtime_NxPlayGamesServices_nativeOnScoreSubmitted(
    JNIEnv *, jclass, const jboolean success) {
  nxm::store_google_play::PlayGamesLeaderboards::dispatch_score_submitted(success);
}

extern "C" JNIEXPORT void JNICALL
Java_com_nx2d_runtime_NxPlayGamesServices_nativeOnLeaderboardLoaded(
    JNIEnv *, jclass, const jlongArray ranks, const jlongArray scores,
    const jobjectArray names) {
  nxm::store_google_play::PlayGamesLeaderboards::dispatch_leaderboard_loaded(ranks, scores,
                                                                             names);
}
