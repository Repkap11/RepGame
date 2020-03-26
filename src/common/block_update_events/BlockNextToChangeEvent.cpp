#include "common/block_update_events/BlockNextToChangeEvent.hpp"
#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"

BlockNextToChangeEvent::BlockNextToChangeEvent( long tick_number, int x, int y, int z, int i, int j, int k )
    : BlockUpdateEvent( tick_number ), block_x( x + i ), block_y( y + j ), block_z( z + k ), affecting_block_x( x ), affecting_block_y( y ), affecting_block_z( z ) {
    this->name = "BlockNextToChangeEvent";
}

void BlockNextToChangeEvent::performAction( BlockUpdateQueue *blockUpdateQueue, World *world ) {
    BlockState updateing_block_state = world_get_loaded_block( world, this->block_x, this->block_y, this->block_z );
    Block *updateing_block = block_definition_get_definition( updateing_block_state.id );
    BlockState affecting_block_state = world_get_loaded_block( world, this->affecting_block_x, this->affecting_block_y, this->affecting_block_z );
    Block *affecting_block = block_definition_get_definition( affecting_block_state.id );

    // Water flow needs to start when a block next to water is broken
    if ( updateing_block->flows != 0 ) {
        if ( this->affecting_block_y <= this->block_y ) {
            // if ( affecting_block_state.id == LAVA ) {
            //     BlockUpdateEvent *blockPlacedEvent = new PlayerBlockPlacedEvent( this->affecting_block_x, this->affecting_block_y, this->affecting_block_z, {COBBLESTONE, 0} );
            //     blockUpdateQueue->addBlockUpdate( blockPlacedEvent );
            // } else
            if ( affecting_block->breaks_in_liquid ) {
                pr_debug( "Expanding a water x:%d y:%d z:%d into x:%d y:%d z:%d ", this->block_x, this->block_y, this->block_z, this->affecting_block_x, this->affecting_block_y, this->affecting_block_z );
                BlockUpdateEvent *blockPlacedEvent = new PlayerBlockPlacedEvent( this->tick_number + updateing_block->flows, this->affecting_block_x, this->affecting_block_y, this->affecting_block_z, {updateing_block_state.id, 0} );
                blockUpdateQueue->addBlockUpdate( blockPlacedEvent );
            }
        }
    }
}