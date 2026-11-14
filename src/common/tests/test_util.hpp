#pragma once

// Shared includes + helpers for the rep_tests suites (formerly one big
// rep_tests.cpp). Every test file includes this first so each keeps the same
// compile environment the single TU had.

#include "common/RepGame.hpp"

#include <cmath>

#include "common/perlin_noise.hpp"
#include "common/map_gen.hpp"
#include "common/structure_gen.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/block_update_events/DoorEvent.hpp"
#include "common/block_update_events/TntExplodeEvent.hpp"
#include "common/block_update_events/schedule.hpp"
#include "common/utils/collision.hpp"
#include "common/mobs.hpp"
#include "common/net/packet.hpp"
#include "common/net_entities.hpp"
#include "common/multiplayer.hpp"
#include "common/mob_uv.hpp"
#include "common/item.hpp"
#include "common/utils/map_storage.hpp"
#include "common/utils/file_utils.hpp"
#include "common/utils/ray_traversal.hpp"

#include <stdlib.h>
#include <string.h>

#if defined( REPGAME_WINDOWS )
// POSIX env calls, shimmed so every test TU can use them.
static inline int setenv( const char *name, const char *value, int overwrite ) {
    if ( !overwrite && getenv( name ) ) {
        return 0;
    }
    return _putenv_s( name, value );
}
static inline int unsetenv( const char *name ) {
    return _putenv_s( name, "" );
}
#endif

// Builds a World backed by a single loaded chunk at the origin (no GL needed:
// chunk.cells is a plain array and Chunk::init's GL work is lazy). Positions
// outside that chunk read LAST_BLOCK_ID like an unloaded chunk.
void test_setup_world( World &world );

// Checks the power level at a position, printing a line per check. Returns the
// number of failures.
int check_power( const World &world, const char *name, const glm::ivec3 &pos, int expected, int expected_max = -1 );

// Checks the block id at a position. Returns the number of failures.
int check_id( const World &world, const char *name, const glm::ivec3 &pos, BlockID expected );

// Advances n physics ticks, draining both event queues each tick in the
// same order RepGame::process_block_updates does.
static inline void test_run_game_ticks( RepGameState &gs, int n ) {
    for ( int i = 0; i < n; i++ ) {
        gs.tick_number++;
        repgame_drain_block_updates( gs );
    }
}

// Runs game ticks until the redstone clock has advanced `gt` game ticks,
// plus one extra drain so a write scheduled exactly at now + gt has
// landed. A redstone delay of 0 fires in the next drain; n waits for the
// clock to reach start + n.
static inline void test_run_redstone_ticks( RepGameState &gs, long gt ) {
    const long target = redstone_tick_now( gs ) + gt;
    while ( redstone_tick_now( gs ) < target ) {
        gs.tick_number++;
        repgame_drain_block_updates( gs );
    }
    gs.tick_number++;
    repgame_drain_block_updates( gs );
}

// Fills the hotbar + survival inventory fields headlessly (init() does GL
// work; friend access mirrors test_mobs).
void test_setup_inventories( RepGameState &gs );

// Redirects $HOME to a throwaway dir under /tmp for the lifetime of the
// object (getRepGamePath reads $HOME, so MapStorage writes would land in a
// real world otherwise). Restores the previous $HOME on destruction.
struct ScopedTempHome {
    explicit ScopedTempHome( const char *dir );
    ~ScopedTempHome( );

  private:
    char orig_home[ 512 ];
    bool had_home;
};
