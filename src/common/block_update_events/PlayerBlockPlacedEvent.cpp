#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"
#include "common/block_update_events/BlockNextToChangeEvent.hpp"

PlayerBlockPlacedEvent::PlayerBlockPlacedEvent( long tick_number, int x, int y, int z, BlockState blockState, bool state_update ) //
    : BlockUpdateEvent( tick_number ), block_x( x ), block_y( y ), block_z( z ), blockState( blockState ), state_update( state_update ) {
    this->name = "PlayerBlockPlacedEvent";
}

void PlayerBlockPlacedEvent::performActionToNeighbor( BlockUpdateQueue *blockUpdateQueue, World *world, int i, int j, int k ) {

    BlockUpdateEvent *blockUpdatedEvent = new BlockNextToChangeEvent( this->tick_number, this->block_x, this->block_y, this->block_z, i, j, k );
    blockUpdateQueue->addBlockUpdate( blockUpdatedEvent );
}

void PlayerBlockPlacedEvent::performAction( BlockUpdateQueue *blockUpdateQueue, World *world ) {
    BlockState current_block_state = world_get_loaded_block( world, TRIP_ARGS( this->block_ ) );
    // State already matches, don't change anything
    if ( BlockStates_equal( blockState, current_block_state ) ) {
        return;
    }
    Block *new_block = block_definition_get_definition( this->blockState.id );
    Block *current_block = block_definition_get_definition( current_block_state.id );

    // If you're placeing a block, the block it's replaceing must have "can_be_placed_in"
    if ( this->blockState.id != AIR && !current_block->can_be_placed_in ) {
        // Unless that block flows, and the other block can be broken by fluid
        if ( new_block->flows != 0 && current_block->breaks_in_liquid ) {
            // This position already has a block, but the new block is a liquid, and the current block will break in a liquid
        } else if ( this->state_update && this->blockState.id == current_block_state.id ) {
            // This position already has the same type of block, allow other state to change.
        } else {
            // This position already has a block that can't be replaced.
            return;
        }
    }
    // If you're destroying a block, the destroyed block must have can_be_destroyed
    // if (this->blockState.id == AIR && !current_block->can_be_destroyed){
    if ( current_block_state.id == AIR && !1 ) {
        return;
    }

    world_set_loaded_block( world, TRIP_ARGS( this->block_ ), this->blockState );

    multiplayer_set_block( this->block_x, this->block_y, this->block_z, this->blockState );

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