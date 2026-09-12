#pragma once

namespace nxe::script {
class Host;
}

namespace nxm::store_google_play {

class PlayGamesPlatform;
class PlayGamesLeaderboards;

/// The Play-Games-Services-only `host.store_google_play_*` surface
/// (interactive sign-in, leaderboards) - deliberately separate from
/// store/store_scripting.cpp, which stays neutral-only. Captured by direct
/// reference rather than looked up through ServiceRegistry, the same
/// `expose_store_egs_extras`/`expose_store_app_store_extras` convention:
/// nothing outside store_google_play itself will ever need to find them.
void expose_store_google_play_extras(nxe::script::Host &host, PlayGamesPlatform &platform,
                                     PlayGamesLeaderboards &leaderboards);

}
