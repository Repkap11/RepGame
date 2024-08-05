#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"
#include "common/block_update_events/BlockNextToChangeEvent.hpp"

PlayerBlockPlacedEvent::PlayerBlockPlacedEvent( long tick_number, const glm::ivec3 &offset, BlockState blockState, bool state_update ) //
    : BlockUpdateEvent( tick_number ), block_pos( offset ), blockState( blockState ), state_update( state_update ) {
    this->name = "PlayerBlockPlacedEvent";
}

void PlayerBlockPlacedEvent::performActionToNeighbor( BlockUpdateQueue &blockUpdateQueue, World &world, const glm::ivec3 &offset ) {
    BlockUpdateEvent *blockUpdatedEvent = new BlockNextToChangeEvent( this->tick_number, this->block_pos, offset );
    blockUpdateQueue.addBlockUpdate( *blockUpdatedEvent );
}

void PlayerBlockPlacedEvent::performAction( BlockUpdateQueue &blockUpdateQueue, World &world ) {
    BlockState current_block_state = world.get_loaded_block( this->block_pos );
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

    world.set_loaded_block( this->block_pos, this->blockState );
    // multiplayer_set_block( this->block_pos, this->blockState );//TODO pass multiplayer

    for ( int j = -1; j < 2; j += 2 ) {
        performActionToNeighbor( blockUpdateQueue, world, glm::ivec3( 0, j, 0 ) );
    }
    for ( int i = -1; i < 2; i += 2 ) {
        performActionToNeighbor( blockUpdateQueue, world, glm::ivec3( i, 0, 0 ) );
    }
    for ( int k = -1; k < 2; k += 2 ) {
        performActionToNeighbor( blockUpdateQueue, world, glm::ivec3( 0, 0, k ) );
    }
}