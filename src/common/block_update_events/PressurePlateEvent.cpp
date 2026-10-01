#include "common/block_update_events/PressurePlateEvent.hpp"
#include "common/RepGame.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"

// While occupied, how long until the next occupancy check (and thus the max
// release delay after the player steps off).
#define PRESSURE_PLATE_RECHECK_TICKS 8

PressurePlateEvent::PressurePlateEvent( long tick_number, const glm::ivec3 &pos ) : BlockUpdateEvent( tick_number ), block_pos( pos ) {
    this->name = "PressurePlateEvent";
}

// The plate counts as stood on while the player's AABB overlaps the plate's
// detection box: its footprint, extended up to a quarter block so feet
// resting exactly on its top surface still count.
static bool player_stands_on_plate( const glm::dvec3 &eye, const glm::ivec3 &plate_pos ) {
    const double half_w = PLAYER_WIDTH / 2.0;
    const glm::dvec3 pmin( eye.x - half_w, eye.y - EYE_POSITION_OFFSET - PLAYER_HEIGHT / 2.0, eye.z - half_w );
    const glm::dvec3 pmax( eye.x + half_w, eye.y - EYE_POSITION_OFFSET + PLAYER_HEIGHT / 2.0, eye.z + half_w );
    const glm::dvec3 bmin( plate_pos.x + 1.0 / 16.0, plate_pos.y, plate_pos.z + 1.0 / 16.0 );
    const glm::dvec3 bmax( plate_pos.x + 15.0 / 16.0, plate_pos.y + 4.0 / 16.0, plate_pos.z + 15.0 / 16.0 );
    return pmin.x < bmax.x && pmax.x > bmin.x && //
           pmin.y < bmax.y && pmax.y > bmin.y && //
           pmin.z < bmax.z && pmax.z > bmin.z;
}

void PressurePlateEvent::performAction( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState ) {
    const BlockState current_state = repGameState.world.get_loaded_block( this->block_pos );
    const bool still_pressed_area = block_definition_get_definition( current_state.id )->is_pressure_plate &&
                                    player_stands_on_plate( repGameState.camera.pos, this->block_pos );
    if ( still_pressed_area ) {
        if ( current_state.current_redstone_power == 0 ) {
            BlockState pressed_state = current_state;
            pressed_state.current_redstone_power = REDSTONE_SOURCE_POWER;
            blockUpdateQueue.addBlockUpdate( std::make_shared<PlayerBlockPlacedEvent>( this->tick_number, this->block_pos, pressed_state, true ) );
        }
        blockUpdateQueue.addBlockUpdate( std::make_shared<PressurePlateEvent>( this->tick_number + PRESSURE_PLATE_RECHECK_TICKS, this->block_pos ) );
        return;
    }
    // Nobody on it (or the plate is gone/unloaded): release it and end the
    // check chain. Unwatching re-arms the per-tick stand detection.
    if ( block_definition_get_definition( current_state.id )->is_pressure_plate && current_state.current_redstone_power > 0 ) {
        BlockState released_state = current_state;
        released_state.current_redstone_power = 0;
        blockUpdateQueue.addBlockUpdate( std::make_shared<PlayerBlockPlacedEvent>( this->tick_number, this->block_pos, released_state, true ) );
    }
    repGameState.watched_pressure_plates.erase( this->block_pos );
}
