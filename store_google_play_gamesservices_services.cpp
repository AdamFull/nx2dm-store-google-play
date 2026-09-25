#include "store_google_play/store_google_play_gamesservices_services.h"

#include "core/foundation/platform/android_jni.h"

#include "core/foundation/diagnostics/log.h"

#include <utility>

namespace nxm::store_google_play {
namespace {

const nx::log::Category log_store_google_play =
    nx::log::category("store_google_play");

/// Same shape as store_google_play_services.cpp's own
/// call_shim_static_void - looks up and calls a static void method on
/// NxPlayGamesServices by name/signature, forwarding whatever jvalue args
/// the caller already built. Duplicated locally rather than shared, the
/// same file-local-helper convention that file already establishes.
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

// -- PlayGamesAchievements -----------------------------------------------

PlayGamesAchievements *PlayGamesAchievements::s_instance = nullptr;

PlayGamesAchievements::PlayGamesAchievements(PlayGamesPlatform &platform) noexcept
    : m_platform(platform) {
  s_instance = this;
}

bool PlayGamesAchievements::unlock(const nx::string_view id) {
  if (!m_platform.authenticated())
    return false;
  return nx::android::run_java(
      m_platform.threads(), m_platform.vm(),
      [&](const nx::android::JniScope &env) -> bool {
        if (!env)
          return false;
        const jstring jid = nx::android::to_jstring(env.get(), id);
        if (jid == nullptr)
          return false;

        jvalue args[2];
        args[0].l = m_platform.activity();
        args[1].l = jid;
        call_shim_static_void(env.get(), "unlockAchievement",
                              "(Landroid/app/Activity;Ljava/lang/String;)V",
                              args);
        env->DeleteLocalRef(jid);

        const std::lock_guard lock(m_mutex);
        m_unlocked_ids.emplace_back(id);
        return true;
      });
}

bool PlayGamesAchievements::is_unlocked(const nx::string_view id) const {
  const std::lock_guard lock(m_mutex);
  for (const nx::string &unlocked : m_unlocked_ids)
    if (unlocked.view() == id)
      return true;
  return false;
}

nx::vector<nx::string> PlayGamesAchievements::achievement_ids() const {
  const std::lock_guard lock(m_mutex);
  return m_achievement_ids;
}

void PlayGamesAchievements::refresh(const nx::vector<nx::string> &) {
  if (!m_platform.authenticated())
    return;
  nx::android::run_java(m_platform.threads(), m_platform.vm(),
                        [&](const nx::android::JniScope &env) {
                          if (!env)
                            return;
                          jvalue args[1];
                          args[0].l = m_platform.activity();
                          call_shim_static_void(env.get(), "loadAchievements",
                                                "(Landroid/app/Activity;)V",
                                                args);
                        });
}

void PlayGamesAchievements::on_achievements_loaded(const jobjectArray ids,
                                                   const jobjectArray unlocked_ids) {
  const nx::android::JniScope env(m_platform.vm());
  if (!env)
    return;
  nx::vector<nx::string> all_ids = nx::android::to_nx_string_vector(env.get(), ids);
  nx::vector<nx::string> unlocked = nx::android::to_nx_string_vector(env.get(), unlocked_ids);

  const std::lock_guard lock(m_mutex);
  m_achievement_ids = std::move(all_ids);
  m_unlocked_ids = std::move(unlocked);
}

void PlayGamesAchievements::dispatch_achievements_loaded(const jobjectArray ids,
                                                          const jobjectArray unlocked_ids) {
  if (s_instance != nullptr)
    s_instance->on_achievements_loaded(ids, unlocked_ids);
}

// -- PlayGamesCloudSaves --------------------------------------------------

PlayGamesCloudSaves *PlayGamesCloudSaves::s_instance = nullptr;

PlayGamesCloudSaves::PlayGamesCloudSaves(PlayGamesPlatform &platform) noexcept
    : m_platform(platform) {
  s_instance = this;
}

bool PlayGamesCloudSaves::write(const nx::string_view key, const nx::string_view value) {
  if (!m_platform.authenticated())
    return false;
  return nx::android::run_java(
      m_platform.threads(), m_platform.vm(),
      [&](const nx::android::JniScope &env) -> bool {
        if (!env)
          return false;
        const jstring name = nx::android::to_jstring(env.get(), key);
        const jbyteArray data = nx::android::to_jbyte_array(env.get(), value);
        if (name == nullptr || data == nullptr) {
          if (name != nullptr)
            env->DeleteLocalRef(name);
          if (data != nullptr)
            env->DeleteLocalRef(data);
          return false;
        }

        jvalue args[3];
        args[0].l = m_platform.activity();
        args[1].l = name;
        args[2].l = data;
        call_shim_static_void(env.get(), "writeSnapshot",
                              "(Landroid/app/Activity;Ljava/lang/String;[B)V",
                              args);
        env->DeleteLocalRef(name);
        env->DeleteLocalRef(data);
        return true;
      });
}

nx::string PlayGamesCloudSaves::read(const nx::string_view key) const {
  if (!m_platform.authenticated())
    return {};
  return nx::android::run_java(
      m_platform.threads(), m_platform.vm(),
      [&](const nx::android::JniScope &env) -> nx::string {
        if (!env)
          return {};
        const jstring name = nx::android::to_jstring(env.get(), key);
        if (name == nullptr)
          return {};

        jvalue args[2];
        args[0].l = m_platform.activity();
        args[1].l = name;
        call_shim_static_void(env.get(), "readSnapshot",
                              "(Landroid/app/Activity;Ljava/lang/String;)V",
                              args);
        env->DeleteLocalRef(name);

        const std::lock_guard lock(m_mutex);
        return m_read_buffer;
      });
}

bool PlayGamesCloudSaves::exists(const nx::string_view key) const {
  const std::lock_guard lock(m_mutex);
  for (const nx::string &existing : m_keys)
    if (existing.view() == key)
      return true;
  return false;
}

bool PlayGamesCloudSaves::remove(const nx::string_view key) {
  if (!m_platform.authenticated())
    return false;
  return nx::android::run_java(
      m_platform.threads(), m_platform.vm(),
      [&](const nx::android::JniScope &env) -> bool {
        if (!env)
          return false;
        const jstring name = nx::android::to_jstring(env.get(), key);
        if (name == nullptr)
          return false;

        jvalue args[2];
        args[0].l = m_platform.activity();
        args[1].l = name;
        call_shim_static_void(env.get(), "deleteSnapshot",
                              "(Landroid/app/Activity;Ljava/lang/String;)V",
                              args);
        env->DeleteLocalRef(name);
        return true;
      });
}

nx::vector<nx::string> PlayGamesCloudSaves::keys() const {
  const std::lock_guard lock(m_mutex);
  return m_keys;
}

void PlayGamesCloudSaves::refresh_keys() {
  if (!m_platform.authenticated())
    return;
  nx::android::run_java(m_platform.threads(), m_platform.vm(),
                        [&](const nx::android::JniScope &env) {
                          if (!env)
                            return;
                          jvalue args[1];
                          args[0].l = m_platform.activity();
                          call_shim_static_void(
                              env.get(), "loadSnapshotMetadata",
                              "(Landroid/app/Activity;)V", args);
                        });
}

void PlayGamesCloudSaves::on_snapshot_written(const jstring name, const jboolean success) {
  if (success != JNI_TRUE)
    return;
  const nx::android::JniScope env(m_platform.vm());
  if (!env)
    return;
  const nx::string key = nx::android::to_nx_string(env.get(), name);

  const std::lock_guard lock(m_mutex);
  for (const nx::string &existing : m_keys)
    if (existing.view() == key.view())
      return;
  m_keys.push_back(key);
}

void PlayGamesCloudSaves::on_snapshot_read(const jstring, const jbyteArray data) {
  const nx::android::JniScope env(m_platform.vm());
  if (!env)
    return;
  // Unconditionally overwritten regardless of which key this answers - the
  // same one-in-flight-read simplification GameCenterCloudSaves::read()
  // already documents.
  nx::string bytes = nx::android::to_nx_bytes(env.get(), data);

  const std::lock_guard lock(m_mutex);
  m_read_buffer = std::move(bytes);
}

void PlayGamesCloudSaves::on_snapshot_deleted(const jstring name, const jboolean success) {
  if (success != JNI_TRUE)
    return;
  const nx::android::JniScope env(m_platform.vm());
  if (!env)
    return;
  const nx::string key = nx::android::to_nx_string(env.get(), name);

  const std::lock_guard lock(m_mutex);
  for (usize i = 0; i < m_keys.size(); ++i) {
    if (m_keys[i].view() == key.view()) {
      m_keys.erase(m_keys.begin() + static_cast<isize>(i));
      break;
    }
  }
}

void PlayGamesCloudSaves::on_snapshot_metadata_loaded(const jobjectArray names) {
  const nx::android::JniScope env(m_platform.vm());
  if (!env)
    return;
  nx::vector<nx::string> keys = nx::android::to_nx_string_vector(env.get(), names);

  const std::lock_guard lock(m_mutex);
  m_keys = std::move(keys);
}

void PlayGamesCloudSaves::dispatch_snapshot_written(const jstring name, const jboolean success) {
  if (s_instance != nullptr)
    s_instance->on_snapshot_written(name, success);
}

void PlayGamesCloudSaves::dispatch_snapshot_read(const jstring name, const jbyteArray data) {
  if (s_instance != nullptr)
    s_instance->on_snapshot_read(name, data);
}

void PlayGamesCloudSaves::dispatch_snapshot_deleted(const jstring name, const jboolean success) {
  if (s_instance != nullptr)
    s_instance->on_snapshot_deleted(name, success);
}

void PlayGamesCloudSaves::dispatch_snapshot_metadata_loaded(const jobjectArray names) {
  if (s_instance != nullptr)
    s_instance->on_snapshot_metadata_loaded(names);
}

}

extern "C" JNIEXPORT void JNICALL
Java_com_nx2d_runtime_NxPlayGamesServices_nativeOnAchievementsLoaded(
    JNIEnv *, jclass, const jobjectArray ids, const jobjectArray unlocked_ids) {
  nxm::store_google_play::PlayGamesAchievements::dispatch_achievements_loaded(ids,
                                                                              unlocked_ids);
}

extern "C" JNIEXPORT void JNICALL
Java_com_nx2d_runtime_NxPlayGamesServices_nativeOnSnapshotWritten(
    JNIEnv *, jclass, const jstring name, const jboolean success) {
  nxm::store_google_play::PlayGamesCloudSaves::dispatch_snapshot_written(name, success);
}

extern "C" JNIEXPORT void JNICALL
Java_com_nx2d_runtime_NxPlayGamesServices_nativeOnSnapshotRead(
    JNIEnv *, jclass, const jstring name, const jbyteArray data) {
  nxm::store_google_play::PlayGamesCloudSaves::dispatch_snapshot_read(name, data);
}

extern "C" JNIEXPORT void JNICALL
Java_com_nx2d_runtime_NxPlayGamesServices_nativeOnSnapshotDeleted(
    JNIEnv *, jclass, const jstring name, const jboolean success) {
  nxm::store_google_play::PlayGamesCloudSaves::dispatch_snapshot_deleted(name, success);
}

extern "C" JNIEXPORT void JNICALL
Java_com_nx2d_runtime_NxPlayGamesServices_nativeOnSnapshotMetadataLoaded(
    JNIEnv *, jclass, const jobjectArray names) {
  nxm::store_google_play::PlayGamesCloudSaves::dispatch_snapshot_metadata_loaded(names);
}
