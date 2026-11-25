// RepGame member functions: arrows and ender-pearl projectiles.

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


// Fires one arrow from the eye along the look direction: survival draws a
// round from the hotbar first then the survival inventory (no ammo, no
// shot); creative fires free and one-shots. Returns whether it fired.
bool repgame_fire_arrow( RepGameState &state ) {
    const bool survival = state.game_mode == GameMode_Survival;
    if ( survival && !state.hotbar.consumeItem( ARROW, 1 ) && !state.survival_inventory.consumeItem( ARROW, 1 ) ) {
        return false; // Out of arrows.
    }
    // Release from the eye, biased a step along the aim so the flight
    // segment can't collide with the shooter's own head cell.
    const glm::dvec3 dir = glm::dvec3( state.camera.look );
    state.world.arrows.spawn( state.camera.pos + dir * 0.4, dir * ArrowEntities::ARROW_SPEED,
                              survival ? ArrowEntities::ARROW_DAMAGE : 1000.0f, survival ? 1 : 0 );
    audio_play_sfx( "bow_shoot" );
    state.vr.haptic_pending = true;
    return true;
}

// Right-click with an ender pearl held: lob one along the look direction.
// Survival consumes the held pearl; creative throws free. The projectile
// itself is part of the arrow sim (NET_ARROW_FLAG_ENDER_PEARL) and reports
// its impact back as an ender-pearl landing.
bool repgame_throw_ender_pearl( RepGameState &state ) {
    if ( state.hotbar.getSelectedItem( ) != ENDER_PEARL ) {
        return false;
    }
    const bool survival = state.game_mode == GameMode_Survival;
    if ( survival && !state.hotbar.consumeSelected( 1 ) ) {
        return false;
    }
    if ( survival ) {
        state.ui.overlay.set_holding_item( state.hotbar.getSelectedItem( ) );
    }
    const glm::dvec3 dir = glm::dvec3( state.camera.look );
    state.world.arrows.spawn_ender_pearl( state.camera.pos + dir * 0.4, dir * ArrowEntities::ENDER_PEARL_SPEED, survival ? 1 : 0 );
    audio_play_sfx_gain( "ender_pearl_throw", 0.5f );
    state.vr.haptic_pending = true;
    return true;
}

// Move the local player so their feet land at `feet_target`, depenetrating
// the body when the pearl ended flush against a wall/ceiling. Motion state
// is zeroed like other teleports so the jump doesn't carry velocity across.
void repgame_ender_pearl_teleport( RepGameState &state, const glm::dvec3 &feet_target, const glm::dvec3 &hit_normal ) {
    glm::dvec3 eye( feet_target.x, feet_target.y + state.camera.eye_height, feet_target.z );
    if ( Collision::collides_at( state.world, eye, state.camera.player_height, state.camera.eye_height ) ) {
        bool resolved = false;
        if ( glm::length( hit_normal ) > 0.5 ) {
            // A side hit starts with the pearl center only ARROW_HALF off the
            // face, while the player hull is much wider. Walk back out along
            // the contact normal instead of letting unstick climb the wall's
            // cell lattice and moving the player upward.
            for ( double push = 0.05; push <= 0.8 && !resolved; push += 0.05 ) {
                const glm::dvec3 candidate = eye + hit_normal * push;
                if ( !Collision::collides_at( state.world, candidate, state.camera.player_height, state.camera.eye_height ) ) {
                    eye = candidate;
                    resolved = true;
                }
            }
        }
        if ( !resolved ) {
            Collision::unstick( state.world, eye, state.camera.player_height, state.camera.eye_height );
        }
    }
    dbgcmd::goto_( state, eye );
    state.camera.y_speed = 0.0;
    state.camera.horizontal_vel = glm::dvec2( 0.0 );
    state.vitals.fall_dist = 0.0;
    audio_play_sfx_gain( "ender_pearl_teleport", 0.5f );
    state.vr.haptic_pending = true;
}
