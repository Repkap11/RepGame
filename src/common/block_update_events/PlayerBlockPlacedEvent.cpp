#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"
#include "common/block_update_events/BlockNextToChangeEvent.hpp"

PlayerBlockPlacedEvent::PlayerBlockPlacedEvent( long tick_number, int x, int y, int z, BlockState blockState ) : BlockUpdateEvent( tick_number ), block_x( x ), block_y( y ), block_z( z ), blockState( blockState ) {
    this->name = "PlayerBlockPlacedEvent";
}

void PlayerBlockPlacedEvent::performActionToNeighbor( BlockUpdateQueue *blockUpdateQueue, World *world, int i, int j, int k ) {
    Block *block = block_definition_get_definition( this->blockState.id );

    if ( block->flows != 0 && j != 1 ) {
        int affecting_block_x = this->block_x + i;
        int affecting_block_y = this->block_y + j;
        int affecting_block_z = this->block_z + k;
        BlockState neighbor_block_state = world_get_loaded_block( world, affecting_block_x, affecting_block_y, affecting_block_z );
        Block *neighbor_block = block_definition_get_definition( neighbor_block_state.id );
        if ( neighbor_block->breaks_in_liquid ) {
            BlockUpdateEvent *blockPlacedEvent = new PlayerBlockPlacedEvent( this->tick_number + block->flows, affecting_block_x, affecting_block_y, affecting_block_z, {blockState.id, 0} );
            blockUpdateQueue->addBlockUpdate( blockPlacedEvent );
        }
    }

    BlockUpdateEvent *blockUpdatedEvent = new BlockNextToChangeEvent( this->tick_number + 1, this->block_x, this->block_y, this->block_z, i, j, k );
    blockUpdateQueue->addBlockUpdate( blockUpdatedEvent );
}

void PlayerBlockPlacedEvent::performAction( BlockUpdateQueue *blockUpdateQueue, World *world ) {
    multiplayer_set_block( this->block_x, this->block_y, this->block_z, this->blockState );
    BlockState current_block_state = world_get_loaded_block( world, TRIP_ARGS( this->block_ ) );
    if ( BlockStates_equal( blockState, current_block_state ) ) {
        // State already matches, don't change anything
        return;
    }
    world_set_loaded_block( world, TRIP_ARGS( this->block_ ), this->blockState );

    for ( int j = -1; j < 2; j += 2 ) {
        performActionToNeighbor( blockUpdateQueue, world, 0, j, 0 );
    }
    for ( int i = -1; i < 2; i += 2 ) {
        performActionToNeighbor( blockUpdateQueue, world, i, 0, 0 );
    }
    for ( int k = -1; k < 2; k += 2 ) {
        performActionToNeighbor( blockUpdateQueue, world, 0, 0, k );
    }
}