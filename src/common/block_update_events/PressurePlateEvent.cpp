#include "common/block_update_events/PressurePlateEvent.hpp"
#include "common/RepGame.hpp"
#include "common/audio.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/block_update_events/schedule.hpp"

PressurePlateEvent::PressurePlateEvent( long tick_number, const glm::ivec3 &pos ) : BlockUpdateEvent( tick_number ), block_pos( pos ) {
    this->name = "PressurePlateEvent";
}

// The plate counts as stood on while the player's AABB overlaps the plate's
// detection box: its footprint, extended up to a quarter block so feet
// resting exactly on its top surface still count. eye_height is the
// feet-to-eye distance (lower while sneaking).
static bool player_stands_on_plate( const glm::dvec3 &eye, double player_height, double eye_height, const glm::ivec3 &plate_pos ) {
    const double half_w = PLAYER_WIDTH / 2.0;
    const glm::dvec3 pmin( eye.x - half_w, eye.y - eye_height, eye.z - half_w );
    const glm::dvec3 pmax( eye.x + half_w, eye.y - eye_height + player_height, eye.z + half_w );
    const glm::dvec3 bmin( plate_pos.x + 1.0 / 16.0, plate_pos.y, plate_pos.z + 1.0 / 16.0 );
    const glm::dvec3 bmax( plate_pos.x + 15.0 / 16.0, plate_pos.y + 4.0 / 16.0, plate_pos.z + 15.0 / 16.0 );
    return pmin.x < bmax.x && pmax.x > bmin.x && //
           pmin.y < bmax.y && pmax.y > bmin.y && //
           pmin.z < bmax.z && pmax.z > bmin.z;
}

void PressurePlateEvent::performAction( RepGameState &repGameState ) {
    const BlockState current_state = repGameState.world.get_loaded_block( this->block_pos );
    const bool still_pressed_area = current_state.id != LAST_BLOCK_ID && block_definition_get_definition( current_state.id )->is_pressure_plate &&
                                    player_stands_on_plate( repGameState.camera.pos, repGameState.camera.player_height, repGameState.camera.eye_height, this->block_pos );
    // Metal/stone plates click sharp, wooden ones thunk.
    const float click_gain = audio_distance_gain( glm::distance( repGameState.camera.pos, glm::dvec3( this->block_pos ) + 0.5 ) );
    const char *click_pool = current_state.id == STONE_PRESSURE_PLATE || current_state.id == POLISHED_BLACKSTONE_PRESSURE_PLATE ||
                                     current_state.id == HEAVY_WEIGHTED_PRESSURE_PLATE || current_state.id == LIGHT_WEIGHTED_PRESSURE_PLATE
                                 ? "click"
                                 : "wood_click";
    if ( still_pressed_area ) {
        if ( current_state.current_redstone_power == 0 ) {
            BlockState pressed_state = current_state;
            pressed_state.current_redstone_power = REDSTONE_SOURCE_POWER;
            schedule_redstone_update<PlayerBlockPlacedEvent>( repGameState, 0, this->block_pos, pressed_state, true );
            audio_play_sfx_gain( click_pool, click_gain );
        }
        schedule_redstone_update<PressurePlateEvent>( repGameState, PRESSURE_PLATE_RECHECK_GT, this->block_pos );
        return;
    }
    // Nobody on it (or the plate is gone/unloaded): release it and end the
    // check chain. Unwatching re-arms the per-tick stand detection.
    if ( current_state.id != LAST_BLOCK_ID && block_definition_get_definition( current_state.id )->is_pressure_plate && current_state.current_redstone_power > 0 ) {
        BlockState released_state = current_state;
        released_state.current_redstone_power = 0;
        schedule_redstone_update<PlayerBlockPlacedEvent>( repGameState, 0, this->block_pos, released_state, true );
        audio_play_sfx_gain( click_pool, click_gain );
    }
    repGameState.watched_pressure_plates.erase( this->block_pos );
}
