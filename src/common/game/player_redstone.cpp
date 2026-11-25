// RepGame member functions: buttons, levers, block updates, and pressure plates.

#include <cmath>
#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/audio.hpp"
#include "common/block_definitions.hpp"
#include "common/dimension_profile.hpp"
#include "common/fluid.hpp"
#include "common/map_gen.hpp"
#include "common/utils/map_storage.hpp"
#include "common/utils/profile.hpp"
#include "common/renderer/texture.hpp"
#include "common/world.hpp"
#include "common/utils/collision.hpp"
#include "common/multiplayer.hpp"
#include "common/mp_test.hpp"
#include "common/harness/run_policy.hpp"
#include "common/harness/debug_commands.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/block_update_events/BlockNextToChangeEvent.hpp"
#include "common/block_update_events/DoorEvent.hpp"
#include "common/block_update_events/TntExplodeEvent.hpp"
#include "common/block_update_events/PressurePlateEvent.hpp"
#include "common/block_update_events/schedule.hpp"
#include "common/game/game_internal.hpp"
#include "common/break_drops.hpp"
#include "common/rail.hpp"
#include "common/repeater.hpp"
#include "common/tree_grow.hpp"
#include "common/vehicle_physics.hpp" // VEHICLE_BOAT_DRAFT


// Right-click a button: powered for a timeout, then released. Wood stays
// pressed ~1.5 s (30 gt), stone ~1 s (20 gt). The press write is instant
// (physics clock) so the click feels immediate; the release waits on the
// redstone clock. A free function so rep_tests can drive it.
void repgame_press_button( RepGameState &state, const glm::ivec3 &pos ) {
    const BlockState target_state = state.world.get_loaded_block( pos );
    if ( target_state.id == LAST_BLOCK_ID || !block_definition_get_definition( target_state.id )->is_button ) {
        return;
    }
    if ( target_state.current_redstone_power == 0 ) {
        BlockState pressed_state = target_state;
        pressed_state.current_redstone_power = REDSTONE_SOURCE_POWER;
        schedule_block_update<PlayerBlockPlacedEvent>( state, 0, pos, pressed_state, true );
    }
    // Stone buttons click sharp, wooden ones thunk (MC's two click sounds).
    audio_play_sfx( target_state.id == STONE_BUTTON || target_state.id == POLISHED_BLACKSTONE_BUTTON ? "click" : "wood_click" );
    // Always (re)queue the release: idempotent while unpressed, and it
    // revives a button saved while pressed (queued events don't
    // persist across loads).
    BlockState released_state = target_state;
    released_state.current_redstone_power = 0;
    const long press_gt = target_state.id == STONE_BUTTON ? STONE_BUTTON_PRESS_GT : WOOD_BUTTON_PRESS_GT;
    schedule_redstone_update<PlayerBlockPlacedEvent>( state, press_gt, pos, released_state, true );
}

// Right-click a lever: latches between unpowered and full power — unlike a
// button there is no release timer. The write is an instant state update on
// the physics clock; BlockNextToChangeEvent owns-power keeps the new level
// until the next toggle. A free function so rep_tests can drive it.
void repgame_toggle_lever( RepGameState &state, const glm::ivec3 &pos ) {
    const BlockState target_state = state.world.get_loaded_block( pos );
    if ( target_state.id == LAST_BLOCK_ID || !block_definition_get_definition( target_state.id )->is_lever ) {
        return;
    }
    BlockState toggled_state = target_state;
    toggled_state.current_redstone_power = target_state.current_redstone_power > 0 ? 0 : REDSTONE_SOURCE_POWER;
    schedule_block_update<PlayerBlockPlacedEvent>( state, 0, pos, toggled_state, true );
}

void RepGame::process_block_updates( ) {
    PROFILE_SCOPE( "block_updates" );
    // Random ticks (growth + grass) stay off in capture/fly/flat/block-world
    // runs — the same gates the mob sim uses — so captures stay
    // deterministic. They also pause while the player is flying or no-
    // clipping: traversal is what covers ground, and every natural write
    // dirties (and eventually persists) the chunk it's in — a spectator's
    // fly-through shouldn't be filing chunks no one edited.
    if ( !RepGame::capture_mode( ) && !RepGame::fly_mode( ) && !repgame_block_world( ) && !repgame_flat_world( ) && !globalGameState.input.player_flying && !globalGameState.input.no_clip ) {
        globalGameState.world.process_random_ticks( globalGameState );
    }
    repgame_drain_block_updates( globalGameState );
}

void RepGame::process_pressure_plates( ) {
    const glm::dvec3 &eye = globalGameState.camera.pos;
    const double half_w = PLAYER_WIDTH / 2.0;
    const double feet_y = eye.y - globalGameState.camera.eye_height;
    // Cells overlapped by the player's feet; the plate lives in the same cell
    // as the feet when standing on it (it doesn't collide, so feet rest at
    // the supporting block's top face).
    const double eps = 1e-4;
    const int y = static_cast<int>( floor( feet_y ) );
    const int x0 = static_cast<int>( floor( eye.x - half_w ) );
    const int x1 = static_cast<int>( floor( eye.x + half_w - eps ) );
    const int z0 = static_cast<int>( floor( eye.z - half_w ) );
    const int z1 = static_cast<int>( floor( eye.z + half_w - eps ) );
    for ( int x = x0; x <= x1; x++ ) {
        for ( int z = z0; z <= z1; z++ ) {
            const glm::ivec3 block_pos( x, y, z );
            const BlockState block_state = globalGameState.world.get_loaded_block( block_pos );
            if ( block_state.id == LAST_BLOCK_ID ) {
                continue;
            }
            // First time standing on this plate starts a check chain; the
            // event reschedules itself until nobody is on it. Watching is what
            // bounds this to one chain per plate.
            if ( block_definition_get_definition( block_state.id )->is_pressure_plate && globalGameState.watched_pressure_plates.insert( block_pos ).second ) {
                schedule_redstone_update<PressurePlateEvent>( globalGameState, 0, block_pos );
            }
        }
    }
}
