#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"
#include "common/block_update_events/BlockNextToChangeEvent.hpp"
#include "common/block_update_events/PistonEvent.hpp"

PlayerBlockPlacedEvent::PlayerBlockPlacedEvent( long tick_number, const glm::ivec3 &offset, BlockState blockState, bool state_update ) //
    : BlockUpdateEvent( tick_number ), block_pos( offset ), blockState( blockState ), state_update( state_update ) {
    this->name = "PlayerBlockPlacedEvent";
}

void queue_neighbor_block_updates( BlockUpdateQueue &blockUpdateQueue, World &world, long tick_number, const glm::ivec3 &pos ) {
    static const glm::ivec3 neighbor_offsets[ 6 ] = {
        glm::ivec3( 0, 1, 0 ),  //
        glm::ivec3( 0, -1, 0 ), //
        glm::ivec3( 1, 0, 0 ),  //
        glm::ivec3( -1, 0, 0 ), //
        glm::ivec3( 0, 0, 1 ),  //
        glm::ivec3( 0, 0, -1 ), //
    };
    for ( const glm::ivec3 &offset : neighbor_offsets ) {
        blockUpdateQueue.addBlockUpdate( std::make_shared<BlockNextToChangeEvent>( tick_number, pos, offset ) );
    }

    // Also notify extended neighbors for vertical dust connections.
    // Dust can connect up over solid blocks or down below non-solid blocks.
    // When a block changes, dust at a different elevation (2 blocks away) may need to update.
    const glm::ivec3 horiz_offsets[ 4 ] = {
        glm::ivec3( 1, 0, 0 ),  //
        glm::ivec3( -1, 0, 0 ), //
        glm::ivec3( 0, 0, 1 ),  //
        glm::ivec3( 0, 0, -1 ), //
    };
    for ( const glm::ivec3 &horiz : horiz_offsets ) {
        glm::ivec3 neighbor_pos = pos + horiz;
        BlockState neighbor = world.get_loaded_block( neighbor_pos );
        if ( neighbor.id == LAST_BLOCK_ID ) {
            continue;
        }
        const Block *neighbor_block = block_definition_get_definition( neighbor.id );
        if ( neighbor_block->collides_with_player ) {
            // Solid neighbor: dust on top might connect via this solid block
            blockUpdateQueue.addBlockUpdate( std::make_shared<BlockNextToChangeEvent>( tick_number, pos, horiz + glm::ivec3( 0, 1, 0 ) ) );
        } else if ( !neighbor_block->is_redstone_dust ) {
            // Non-solid neighbor: dust below might connect via this air block
            blockUpdateQueue.addBlockUpdate( std::make_shared<BlockNextToChangeEvent>( tick_number, pos, horiz + glm::ivec3( 0, -1, 0 ) ) );
        }
    }
}

void PlayerBlockPlacedEvent::performAction( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState ) {
    BlockState current_block_state = repGameState.world.get_loaded_block( this->block_pos );
    // State already matches, don't change anything
    if ( BlockStates_equal( blockState, current_block_state ) ) {
        return;
    }
    // A state update only makes sense for the block it was computed on. If the
    // block is gone (e.g. just broken while a stale update was still queued),
    // drop it instead of resurrecting the block.
    if ( this->state_update && this->blockState.id != current_block_state.id ) {
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
    // Don't place a block that can't survive here (e.g. dust above dust, a
    // torch on a missing attachment): it would sit for a tick and then pop
    // off, spawning break debris for a block that was never really placed.
    if ( !this->state_update && !block_can_survive_at( repGameState.world, this->block_pos, this->blockState ) ) {
        return;
    }
    // A stale survival-kill (non-update AIR write) must not clobber a piston
    // head that is still attached to its base: the kill decision was queued
    // before the head landed in the cell. Heads that lost their base still
    // fail can_survive and are removed as intended.
    if ( this->blockState.id == AIR && !this->state_update && current_block->is_piston_head &&
         block_can_survive_at( repGameState.world, this->block_pos, current_block_state ) ) {
        return;
    }
    // If you're destroying a block, the destroyed block must have can_be_destroyed
    // if (this->blockState.id == AIR && !current_block->can_be_destroyed){
    if ( current_block_state.id == AIR && !1 ) {
        return;
    }

    repGameState.world.set_loaded_block( this->block_pos, this->blockState );
    repGameState.multiplayer.set_block( this->block_pos, this->blockState );
    repGameState.world.spawn_block_debris( this->block_pos, current_block_state, this->blockState.id );

    queue_neighbor_block_updates( blockUpdateQueue, repGameState.world, this->tick_number, this->block_pos );

    // A piston extends/retracts on a power edge. Queue it one tick out so the
    // new power state is visible to the event; the event re-reads live state,
    // so stale triggers are harmless.
    if ( new_block->is_piston && ( current_block_state.current_redstone_power > 0 ) != ( this->blockState.current_redstone_power > 0 ) ) {
        blockUpdateQueue.addBlockUpdate( std::make_shared<PistonEvent>( this->tick_number + 1, this->block_pos ) );
    }
}