package com.nx2d.runtime

import android.app.Activity
import com.google.android.gms.games.PlayGames
import com.google.android.gms.games.PlayGamesSdk
import com.google.android.gms.games.SnapshotsClient
import com.google.android.gms.games.achievement.Achievement
import com.google.android.gms.games.leaderboard.LeaderboardVariant
import com.google.android.gms.games.snapshot.SnapshotMetadataChange
import java.io.IOException

// Second Kotlin shim in this module, sibling to NxGooglePlayBilling.kt -
// same @JvmStatic/external fun boundary and the same
// Java_com_nx2d_runtime_NxPlayGamesServices_nativeOnXxx symbol-name
// resolution (no RegisterNatives/JNI_OnLoad here either). Unlike Billing's
// BillingClient, PlayGames.getXClient(activity) accessors are cheap and
// stateless per call, so nothing here is cached across calls - every
// function takes the Activity fresh from the C++ side (which already holds
// a global ref via PlayGamesPlatform), the same per-call-Activity shape
// NxGooglePlayBilling.purchase() already uses instead of NxGooglePlayBilling
// .connect()'s own build-once BillingClient.
object NxPlayGamesServices {

    // -- Sign-in ------------------------------------------------------------

    // PlayGamesSdk.initialize() is required once before any PlayGames.*
    // accessor is used - called here rather than from NxActivity so this
    // shim stays fully self-contained (nothing outside store_google_play's
    // own Kotlin/Java tree needs to know Play Games Services exists).
    // Idempotent per Google's own documentation, so calling it again on a
    // later connect() (e.g. after shutdown()/initialize() cycles in tests)
    // is harmless.
    @JvmStatic
    fun connect(activity: Activity) {
        PlayGamesSdk.initialize(activity)
        PlayGames.getGamesSignInClient(activity).isAuthenticated()
            .addOnCompleteListener { task ->
                nativeOnSignInResult(task.isSuccessful && task.result.isAuthenticated)
            }
    }

    // The confirmed interactive extra - unlike Game Center on iOS, Play
    // Games' own sign-in UI is a system overlay the app never has to
    // present anything for.
    @JvmStatic
    fun signIn(activity: Activity) {
        PlayGames.getGamesSignInClient(activity).signIn()
            .addOnCompleteListener { task ->
                nativeOnSignInResult(task.isSuccessful && task.result.isAuthenticated)
            }
    }

    // -- Achievements ---------------------------------------------------

    // AchievementsClient.unlock() is fire-and-forget by design - Google's
    // own API has no completion Task for it at all (the client applies the
    // change locally and syncs it in the background), so there is no
    // matching nativeOnAchievementUnlocked callback: the C++ side updates
    // its cache optimistically the moment this call returns, the same
    // "assume success, let the next refresh() correct it" shape
    // EgsAchievements::unlock() already uses for a different reason.
    @JvmStatic
    fun unlockAchievement(activity: Activity, id: String) {
        PlayGames.getAchievementsClient(activity).unlock(id)
    }

    @JvmStatic
    fun loadAchievements(activity: Activity) {
        PlayGames.getAchievementsClient(activity).load(true)
            .addOnCompleteListener { task ->
                if (!task.isSuccessful) {
                    nativeOnAchievementsLoaded(emptyArray(), emptyArray())
                    return@addOnCompleteListener
                }
                val buffer = task.result.get()
                val ids = mutableListOf<String>()
                val unlockedIds = mutableListOf<String>()
                buffer?.forEach { achievement ->
                    ids.add(achievement.achievementId)
                    if (achievement.state == Achievement.STATE_UNLOCKED) {
                        unlockedIds.add(achievement.achievementId)
                    }
                }
                buffer?.release()
                nativeOnAchievementsLoaded(ids.toTypedArray(), unlockedIds.toTypedArray())
            }
    }

    // -- Leaderboards -----------------------------------------------------

    // submitScoreImmediate (not the fire-and-forget submitScore) so a real
    // completion signal exists for submit_pending(), the same shape
    // GameCenterLeaderboards::submit_score() already has.
    @JvmStatic
    fun submitScore(activity: Activity, leaderboardId: String, score: Long) {
        PlayGames.getLeaderboardsClient(activity)
            .submitScoreImmediate(leaderboardId, score)
            .addOnCompleteListener { task -> nativeOnScoreSubmitted(task.isSuccessful) }
    }

    // Fixed top-25/all-time/public scope, matching Game Center's own fixed
    // download range decision.
    @JvmStatic
    fun loadTopScores(activity: Activity, leaderboardId: String) {
        PlayGames.getLeaderboardsClient(activity)
            .loadTopScores(
                leaderboardId,
                LeaderboardVariant.TIME_SPAN_ALL_TIME,
                LeaderboardVariant.COLLECTION_PUBLIC,
                25,
            )
            .addOnCompleteListener { task ->
                if (!task.isSuccessful) {
                    nativeOnLeaderboardLoaded(LongArray(0), LongArray(0), emptyArray())
                    return@addOnCompleteListener
                }
                val entries = task.result.get()?.scores
                val count = entries?.count ?: 0
                val ranks = LongArray(count)
                val scores = LongArray(count)
                val names = arrayOfNulls<String>(count)
                entries?.forEachIndexed { i, entry ->
                    ranks[i] = entry.rank
                    scores[i] = entry.rawScore
                    names[i] = entry.scoreHolderDisplayName ?: ""
                }
                entries?.release()
                nativeOnLeaderboardLoaded(ranks, scores, Array(count) { names[it] ?: "" })
            }
    }

    // -- Cloud saves (Snapshots) --------------------------------------------

    // RESOLUTION_POLICY_MOST_RECENTLY_MODIFIED auto-resolves any conflict
    // GameKit's own resolveConflictingSavedGames-equivalent would otherwise
    // surface - deliberately no manual conflict-resolution loop, the same
    // scope reduction GameCenterCloudSaves already has.
    @JvmStatic
    fun writeSnapshot(activity: Activity, name: String, data: ByteArray) {
        val client = PlayGames.getSnapshotsClient(activity)
        client.open(name, true, SnapshotsClient.RESOLUTION_POLICY_MOST_RECENTLY_MODIFIED)
            .addOnCompleteListener { openTask ->
                val snapshot = if (openTask.isSuccessful) openTask.result.data else null
                if (snapshot == null) {
                    nativeOnSnapshotWritten(name, false)
                    return@addOnCompleteListener
                }
                snapshot.snapshotContents.writeBytes(data)
                client.commitAndClose(snapshot, SnapshotMetadataChange.Builder().build())
                    .addOnCompleteListener { commitTask ->
                        nativeOnSnapshotWritten(name, commitTask.isSuccessful)
                    }
            }
    }

    @JvmStatic
    fun readSnapshot(activity: Activity, name: String) {
        val client = PlayGames.getSnapshotsClient(activity)
        client.open(name, false, SnapshotsClient.RESOLUTION_POLICY_MOST_RECENTLY_MODIFIED)
            .addOnCompleteListener { openTask ->
                val snapshot = if (openTask.isSuccessful) openTask.result.data else null
                if (snapshot == null) {
                    nativeOnSnapshotRead(name, null)
                    return@addOnCompleteListener
                }
                val bytes = try {
                    snapshot.snapshotContents.readFully()
                } catch (e: IOException) {
                    null
                }
                client.discardAndClose(snapshot)
                nativeOnSnapshotRead(name, bytes)
            }
    }

    // Opens the snapshot to get its SnapshotMetadata rather than keeping a
    // separate name->metadata cache in this object - delete() needs the
    // metadata object, not just the name, and open() already hands one
    // back for free.
    @JvmStatic
    fun deleteSnapshot(activity: Activity, name: String) {
        val client = PlayGames.getSnapshotsClient(activity)
        client.open(name, false, SnapshotsClient.RESOLUTION_POLICY_MOST_RECENTLY_MODIFIED)
            .addOnCompleteListener { openTask ->
                val snapshot = if (openTask.isSuccessful) openTask.result.data else null
                if (snapshot == null) {
                    nativeOnSnapshotDeleted(name, false)
                    return@addOnCompleteListener
                }
                val metadata = snapshot.metadata
                client.discardAndClose(snapshot)
                client.delete(metadata)
                    .addOnCompleteListener { deleteTask ->
                        nativeOnSnapshotDeleted(name, deleteTask.isSuccessful)
                    }
            }
    }

    @JvmStatic
    fun loadSnapshotMetadata(activity: Activity) {
        PlayGames.getSnapshotsClient(activity).load(true)
            .addOnCompleteListener { task ->
                if (!task.isSuccessful) {
                    nativeOnSnapshotMetadataLoaded(emptyArray())
                    return@addOnCompleteListener
                }
                val buffer = task.result.get()
                val names = mutableListOf<String>()
                buffer?.forEach { metadata -> names.add(metadata.uniqueName) }
                buffer?.release()
                nativeOnSnapshotMetadataLoaded(names.toTypedArray())
            }
    }

    // Declared external (Kotlin's `native`), implemented in C++
    // (store_google_play_gamesservices_*.cpp) and resolved by the JVM's own
    // symbol-name convention against libnx2d.so, the same first
    // Java-calls-C++ direction NxGooglePlayBilling.kt already established
    // in this module.
    @JvmStatic
    private external fun nativeOnSignInResult(authenticated: Boolean)

    @JvmStatic
    private external fun nativeOnAchievementsLoaded(ids: Array<String>, unlockedIds: Array<String>)

    @JvmStatic
    private external fun nativeOnScoreSubmitted(success: Boolean)

    @JvmStatic
    private external fun nativeOnLeaderboardLoaded(
        ranks: LongArray,
        scores: LongArray,
        names: Array<String>,
    )

    @JvmStatic
    private external fun nativeOnSnapshotWritten(name: String, success: Boolean)

    @JvmStatic
    private external fun nativeOnSnapshotRead(name: String, data: ByteArray?)

    @JvmStatic
    private external fun nativeOnSnapshotDeleted(name: String, success: Boolean)

    @JvmStatic
    private external fun nativeOnSnapshotMetadataLoaded(names: Array<String>)
}
