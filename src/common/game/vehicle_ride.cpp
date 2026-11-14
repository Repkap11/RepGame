// Vehicle riding: mount/dismount/seat-follow for the local player.
// Free functions off RepGame.hpp so rep_tests and the script harness
// drive the same code as the click path.

#include "common/RepGame.hpp"

#include <cmath>

#include "common/block_definitions.hpp"
#include "common/fluid.hpp"
#include "common/vehicle_physics.hpp" // vehicle_wrap_180, VEHICLE_BOAT_DRAFT
#include "common/vehicles.hpp"
#include "common/utils/collision.hpp"
#include "common/world.hpp"

// Right-click placement of a WATER-physics vehicle (a boat): prefer the
// water pick cell — spawn centred on it with the bottom at
// surface - draft; else top a solid clicked block when the hull's box
// fits. Spawned with the camera yaw (MC places the boat along look).
// Survival consumes one item; the caller's click delay matches the
// minecart/egg interactions.
bool repgame_vehicle_place( RepGameState &gs, const VehicleType type ) {
    const VehicleTypeDef &def = vehicle_type_def( type );
    if ( def.physics != VehiclePhysics::WATER ) {
        return false;
    }
    glm::dvec3 spawn_pos;
    bool can_spawn = false;
    if ( gs.block_selection.floatSelectionInBounds ) {
        const glm::ivec3 wcell = gs.block_selection.pos_destroy_water;
        const BlockState ws = gs.world.get_loaded_block( wcell );
        if ( ws.id != LAST_BLOCK_ID && fluid_in_cell( ws ) == WATER ) {
            spawn_pos = glm::dvec3( wcell.x + 0.5,
                                    wcell.y + static_cast<double>( fluid_surface_height( gs.world, wcell ) ) - VEHICLE_BOAT_DRAFT,
                                    wcell.z + 0.5 );
            can_spawn = true;
        }
    }
    if ( !can_spawn && gs.block_selection.selectionInBounds ) {
        const glm::ivec3 tcell = gs.block_selection.pos_destroy;
        const BlockState ts = gs.world.get_loaded_block( tcell );
        if ( ts.id != LAST_BLOCK_ID && block_definition_get_definition( ts.id )->collides_with_player ) {
            spawn_pos = glm::dvec3( tcell.x + 0.5, tcell.y + 1.0, tcell.z + 0.5 );
            can_spawn = !Collision::box_collides( gs.world, vehicle_min( def, spawn_pos ), vehicle_max( def, spawn_pos ) );
        }
    }
    if ( !can_spawn || gs.world.vehicles.spawn( type, spawn_pos, gs.camera.angle_H ) == 0 ) {
        return false;
    }
    if ( gs.game_mode == GameMode_Survival ) {
        gs.hotbar.consumeSelected( 1 );
        gs.ui.overlay.set_holding_item( gs.hotbar.getSelectedItem( ) );
    }
    gs.input.click_delay_right = 30;
    return true;
}

bool repgame_vehicle_mount( RepGameState &gs, uint32_t vehicle_id ) {
    if ( gs.camera.riding_vehicle != 0 || vehicle_id == 0 || gs.world.vehicles.find( vehicle_id ) == nullptr ) {
        return false;
    }
    gs.camera.riding_vehicle = vehicle_id;
    // A sneak already held must be released once before it can dismount.
    gs.camera.ride_sneak_seen_up = gs.input.movement.sneakPressed ? 0 : 1;
    gs.camera.y_speed = 0.0;
    gs.camera.horizontal_vel = glm::dvec2( 0.0 );
    gs.camera.rider_push = glm::dvec2( 0.0 );
    gs.camera.rider_forward = 0.0;
    gs.camera.rider_turn = 0.0;
    gs.vitals.fall_dist = 0.0;
    repgame_vehicle_ride_follow( gs );
    return true;
}

void repgame_vehicle_dismount( RepGameState &gs ) {
    Vehicles::Vehicle *vehicle = gs.world.vehicles.find( gs.camera.riding_vehicle );
    gs.camera.riding_vehicle = 0;
    if ( vehicle == nullptr ) {
        return;
    }
    const VehicleTypeDef &def = vehicle_type_def( vehicle->type );
    const double yaw = glm::radians( static_cast<double>( vehicle->yaw ) );
    const glm::dvec3 fwd( sin( yaw ), 0.0, -cos( yaw ) );
    const glm::dvec3 side( -fwd.z, 0.0, fwd.x );
    const glm::ivec3 base( glm::floor( vehicle->pos ) );
    // Pick the first spot the player box fits: the two cells perpendicular
    // to travel, then forward/back along it, then on top of the vehicle.
    const glm::dvec3 dirs[ 4 ] = { side, -side, fwd, -fwd };
    glm::dvec3 feet;
    bool found = false;
    if ( def.physics == VehiclePhysics::WATER ) {
        // Boats prefer a dry landing: a solid cell beside the hull means
        // a shore, so try standing on its top (one up from the bottom
        // cell) before falling back to swimming beside the boat.
        for ( int i = 0; i < 4 && !found; i++ ) {
            const glm::ivec3 shore = base + glm::ivec3( glm::round( dirs[ i ] ) );
            const BlockState s = gs.world.get_loaded_block( shore );
            if ( s.id == LAST_BLOCK_ID || !block_definition_get_definition( s.id )->collides_with_player ) {
                continue;
            }
            const glm::dvec3 cand( shore.x + 0.5, shore.y + 1.0, shore.z + 0.5 );
            if ( !Collision::box_collides( gs.world, cand - glm::dvec3( PLAYER_WIDTH / 2.0, 0.0, PLAYER_WIDTH / 2.0 ),
                                           cand + glm::dvec3( PLAYER_WIDTH / 2.0, gs.camera.player_height, PLAYER_WIDTH / 2.0 ) ) ) {
                feet = cand;
                found = true;
                break;
            }
        }
    }
    for ( int i = 0; i < 4 && !found; i++ ) {
        const glm::ivec3 cell = base + glm::ivec3( glm::round( dirs[ i ] ) );
        const glm::dvec3 cand( cell.x + 0.5, base.y + 0.0, cell.z + 0.5 );
        if ( !Collision::box_collides( gs.world, cand - glm::dvec3( PLAYER_WIDTH / 2.0, 0.0, PLAYER_WIDTH / 2.0 ),
                                       cand + glm::dvec3( PLAYER_WIDTH / 2.0, gs.camera.player_height, PLAYER_WIDTH / 2.0 ) ) ) {
            feet = cand;
            found = true;
            break;
        }
    }
    if ( !found ) {
        feet = vehicle->pos + glm::dvec3( 0.0, def.height, 0.0 );
    }
    gs.camera.pos = feet + glm::dvec3( 0.0, gs.camera.eye_height, 0.0 );
    gs.camera.y_speed = 0.0;
    gs.camera.horizontal_vel = glm::dvec2( 0.0 );
    gs.vitals.fall_dist = 0.0;
    // No render blend from the seat to the dismount spot.
    gs.camera.snapshot_prev( );
    // Multiplayer: parked-state + DISMOUNT on the wire; the vehicle stays
    // locally simulated until the server's COUNT=0 lands.
    gs.world.vehicles.local_dismount( gs, vehicle );
}

void repgame_vehicle_ride_follow( RepGameState &gs ) {
    if ( gs.camera.riding_vehicle == 0 ) {
        return;
    }
    Vehicles::Vehicle *vehicle = gs.world.vehicles.find( gs.camera.riding_vehicle );
    if ( vehicle == nullptr ) {
        // The ridden vehicle broke, unloaded or was persisted — drop the
        // riding state and leave the camera where it is.
        gs.camera.riding_vehicle = 0;
        return;
    }
    const VehicleTypeDef &def = vehicle_type_def( vehicle->type );
    // A boat turns the camera with its hull: the view picks up the same
    // yaw delta the boat took this tick (MC behaviour) while staying
    // free-look otherwise.
    if ( def.physics == VehiclePhysics::WATER ) {
        gs.camera.angle_H += static_cast<float>( vehicle_wrap_180( vehicle->yaw - vehicle->prev_yaw ) );
    }
    gs.camera.pos = vehicle->pos + glm::dvec3( 0.0, def.rider_eye_offset, 0.0 );
    gs.camera.y_speed = 0.0;
    gs.camera.horizontal_vel = glm::dvec2( 0.0 );
    gs.vitals.fall_dist = 0.0;
}
