#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"
#include "common/block_update_events/BlockNextToChangeEvent.hpp"

PlayerBlockPlacedEvent::PlayerBlockPlacedEvent( int place, int x, int y, int z, BlockState blockState ) : place( place ), block_x( x ), block_y( y ), block_z( z ), blockState( blockState ) {
    this->name = "PlayerBlockPlacedEvent";
}

void PlayerBlockPlacedEvent::performAction( BlockUpdateQueue *blockUpdateQueue, World *world ) {
    multiplayer_set_block( this->place, this->block_x, this->block_y, this->block_z, this->blockState );
    world_set_loaded_block( world, TRIP_ARGS( this->block_ ), this->blockState );

    for ( int j = -1; j < 2; j += 2 ) {
        BlockUpdateEvent *blockUpdatedEvent = new BlockNextToChangeEvent( block_x, this->block_y, block_z, 0, j, 0 );
        blockUpdateQueue->addBlockUpdate( blockUpdatedEvent );
    }

    for ( int i = -1; i < 2; i += 2 ) {
        BlockUpdateEvent *blockUpdatedEvent = new BlockNextToChangeEvent( block_x, this->block_y, block_z, i, 0, 0 );
        blockUpdateQueue->addBlockUpdate( blockUpdatedEvent );
    }

    for ( int k = -1; k < 2; k += 2 ) {
        BlockUpdateEvent *blockUpdatedEvent = new BlockNextToChangeEvent( block_x, this->block_y, block_z, 0, 0, k );
        blockUpdateQueue->addBlockUpdate( blockUpdatedEvent );
    }
}