#include "common/block_update_events/DoorEvent.hpp"
#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"
#include "common/block.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"

DoorEvent::DoorEvent( long tick_number, const glm::ivec3 &pos, Mode mode ) : BlockUpdateEvent( tick_number ), block_pos( pos ), mode( mode ) {
    this->name = "DoorEvent";
}

void DoorEvent::performAction( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState ) {
    World &world = repGameState.world;

    // Resolve the lower half: the event may have been queued on either cell.
    glm::ivec3 lower_pos = this->block_pos;
    BlockState lower_state = world.get_loaded_block( lower_pos );
    if ( lower_state.id == LAST_BLOCK_ID ) {
        return;
    }
    if ( block_definition_get_definition( lower_state.id )->is_door_upper ) {
        lower_pos.y -= 1;
        lower_state = world.get_loaded_block( lower_pos );
        if ( lower_state.id == LAST_BLOCK_ID ) {
            return;
        }
    }
    const Block *lower = block_definition_get_definition( lower_state.id );
    if ( !lower->is_door || lower->is_door_upper ) {
        return;
    }
    const glm::ivec3 upper_pos = lower_pos + glm::ivec3( 0, 1, 0 );
    BlockState upper_state = world.get_loaded_block( upper_pos );
    if ( upper_state.id != lower->door_partner ) {
        return; // orphaned half — the survive check will pop it
    }

    const unsigned char facing = door_facing( lower_state.rotation, upper_state.rotation );
    const bool hinge_right = ( upper_state.rotation & BLOCK_ROTATE_DOOR_FLAG ) != 0;
    const bool open = this->mode == TOGGLE ? ( lower_state.rotation & BLOCK_ROTATE_DOOR_FLAG ) == 0
                                           : lower_state.current_redstone_power > 0 || upper_state.current_redstone_power > 0;
    const unsigned char old_lower_rot = lower_state.rotation;
    const unsigned char old_upper_rot = upper_state.rotation;
    door_rotations( facing, open, hinge_right, lower_state.rotation, upper_state.rotation );
    if ( lower_state.rotation == old_lower_rot && upper_state.rotation == old_upper_rot ) {
        return;
    }
    world.set_loaded_block( lower_pos, lower_state );
    repGameState.multiplayer.set_block( lower_pos, lower_state, NET_BU_FLAG_NO_DEBRIS );
    world.set_loaded_block( upper_pos, upper_state );
    repGameState.multiplayer.set_block( upper_pos, upper_state, NET_BU_FLAG_NO_DEBRIS );
    queue_neighbor_block_updates( blockUpdateQueue, world, this->tick_number, lower_pos );
    queue_neighbor_block_updates( blockUpdateQueue, world, this->tick_number, upper_pos );
}
