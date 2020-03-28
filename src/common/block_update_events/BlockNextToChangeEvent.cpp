#include "common/block_update_events/BlockNextToChangeEvent.hpp"
#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"

BlockNextToChangeEvent::BlockNextToChangeEvent( long tick_number, int x, int y, int z, int i, int j, int k )
    : BlockUpdateEvent( tick_number ), block_x( x + i ), block_y( y + j ), block_z( z + k ), affecting_block_x( x ), affecting_block_y( y ), affecting_block_z( z ) {
    this->name = "BlockNextToChangeEvent";
}

int find_largest_redstone_power_around( World *world, int x, int y, int z ) {
    int max = 0;
    for ( int j = -1; j < 2; j += 2 ) {
        int new_power = world_get_loaded_block( world, x, y + j, z ).current_redstone_power;
        max = new_power > max ? new_power : max;
    }
    for ( int i = -1; i < 2; i += 2 ) {
        int new_power = world_get_loaded_block( world, x + i, y, z ).current_redstone_power;
        max = new_power > max ? new_power : max;
    }
    for ( int k = -1; k < 2; k += 2 ) {
        int new_power = world_get_loaded_block( world, x, y, z + k ).current_redstone_power;
        max = new_power > max ? new_power : max;
    }
    return max;
}

#define REDSTONE_DELAY 0

void perform_checks( BlockUpdateQueue *blockUpdateQueue, World *world, long tick_number, //
                     int block_x, int block_y, int block_z,                              //
                     int affecting_block_x, int affecting_block_y, int affecting_block_z ) {

    // The updating block  was next to the updating one, and might need to change
    BlockState updateing_block_state = world_get_loaded_block( world, block_x, block_y, block_z );
    Block *updateing_block = block_definition_get_definition( updateing_block_state.id );

    // The affecting block is the one that origionally change
    BlockState affecting_block_state = world_get_loaded_block( world, affecting_block_x, affecting_block_y, affecting_block_z );
    Block *affecting_block = block_definition_get_definition( affecting_block_state.id );

    // Water flow needs to start when a block next to water is broken
    if ( updateing_block->flows != 0 && affecting_block->breaks_in_liquid && affecting_block_y <= block_y ) {
        pr_debug( "Expanding a water x:%d y:%d z:%d into x:%d y:%d z:%d ", block_x, block_y, block_z, affecting_block_x, affecting_block_y, affecting_block_z );
        BlockUpdateEvent *blockPlacedEvent =
            new PlayerBlockPlacedEvent( tick_number + updateing_block->flows, affecting_block_x, affecting_block_y, affecting_block_z, {updateing_block_state.id, 0, updateing_block->initial_redstone_power}, false );
        blockUpdateQueue->addBlockUpdate( blockPlacedEvent );
    }

    {
        int new_power;
        if ( affecting_block->initial_redstone_power ) {
            new_power = affecting_block->initial_redstone_power;
        } else {
            int maximum_power = find_largest_redstone_power_around( world, affecting_block_x, affecting_block_y, affecting_block_z );
            new_power = maximum_power > 1 ? maximum_power - 1 : 0;
            if ( !affecting_block->transmits_redstone_power ) {
                if ( new_power > 0 ) {
                    if ( affecting_block->affected_by_redstone_power ) {
                        new_power = 1;
                    } else {
                        new_power = 0;
                    }
                }
            }
        }

        if ( new_power != affecting_block_state.current_redstone_power ) {
            // pr_debug( "Queueing event1 with old:%d new:%d", affecting_block_state.current_redstone_power, new_power );
            BlockUpdateEvent *blockPlacedEvent =
                new PlayerBlockPlacedEvent( tick_number + REDSTONE_DELAY, affecting_block_x, affecting_block_y, affecting_block_z, {affecting_block_state.id, affecting_block_state.rotation, new_power}, true );
            blockUpdateQueue->addBlockUpdate( blockPlacedEvent );
        }
    }

    if ( updateing_block->needs_place_on_solid ) {
        BlockID below_block_id = world_get_loaded_block( world, block_x, block_y - 1, block_z ).id;
        bool block_below_is_good;
        if ( updateing_block->needs_place_on_solid_but_can_stack_on_self && below_block_id == updateing_block_state.id ) {
            block_below_is_good = true;
        } else {
            block_below_is_good = block_definition_get_definition( below_block_id )->collides_with_player;
        }
        if ( !block_below_is_good ) {
            BlockUpdateEvent *blockPlacedEvent = new PlayerBlockPlacedEvent( tick_number, block_x, block_y, block_z, BLOCK_STATE_AIR, false );
            blockUpdateQueue->addBlockUpdate( blockPlacedEvent );
        }
    }
}

void BlockNextToChangeEvent::performAction( BlockUpdateQueue *blockUpdateQueue, World *world ) {

    perform_checks( blockUpdateQueue, world, this->tick_number,  //
                    this->block_x, this->block_y, this->block_z, //
                    this->affecting_block_x, this->affecting_block_y, this->affecting_block_z );
    perform_checks( blockUpdateQueue, world, this->tick_number,                                //
                    this->affecting_block_x, this->affecting_block_y, this->affecting_block_z, //
                    this->block_x, this->block_y, this->block_z );
}