#include "common/block_update_events/BlockNextToChangeEvent.hpp"
#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"

BlockNextToChangeEvent::BlockNextToChangeEvent( int x, int y, int z, int i, int j, int k ) : block_x( x + i ), block_y( y + j ), block_z( z + k ), affecting_block_x( x ), affecting_block_y( y ), affecting_block_z( z ) {
    this->name = "BlockNextToChangeEvent";
}

void BlockNextToChangeEvent::performAction( BlockUpdateQueue *blockUpdateQueue, World *world ) {
    pr_debug( "Block might need update" );
    BlockState updateing_block_state = world_get_loaded_block( world, this->block_x, this->block_y, this->block_z );
    BlockState affecting_block_state = world_get_loaded_block( world, this->affecting_block_x, this->affecting_block_y, this->affecting_block_z );

    // Water flow
    if ( updateing_block_state.id == WATER ) {
        if ( this->affecting_block_y <= this->block_y ) {
            if ( block_definitions_is_replaced_by_neighboring_water( affecting_block_state ) ) {
                BlockUpdateEvent *blockPlacedEvent = new PlayerBlockPlacedEvent( 1, this->affecting_block_x, this->affecting_block_y, this->affecting_block_z, {WATER, 0} );
                blockUpdateQueue->addBlockUpdate( blockPlacedEvent );
            }
        }
    }
}