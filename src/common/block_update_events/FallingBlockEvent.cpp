#include "common/block_update_events/FallingBlockEvent.hpp"
#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"

FallingBlockEvent::FallingBlockEvent( long tick_number, const glm::ivec3 &pos ) : BlockUpdateEvent( tick_number ), block_pos( pos ) {
    this->name = "FallingBlockEvent";
}

void FallingBlockEvent::performAction( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState ) {
    World &world = repGameState.world;
    const BlockState state = world.get_loaded_block( this->block_pos );
    if ( state.id == LAST_BLOCK_ID || !block_definition_get_definition( state.id )->falls_with_gravity ) {
        return;
    }
    const BlockState below = world.get_loaded_block( this->block_pos + glm::ivec3( 0, -1, 0 ) );
    if ( below.id == LAST_BLOCK_ID || block_definition_get_definition( below.id )->collides_with_player ) {
        return;
    }

    // The base cell plus the whole contiguous run of falling-type blocks
    // above it release on the same tick.
    for ( glm::ivec3 cell = this->block_pos;; cell.y++ ) {
        const BlockState cur = world.get_loaded_block( cell );
        if ( cur.id == LAST_BLOCK_ID || !block_definition_get_definition( cur.id )->falls_with_gravity ) {
            break;
        }
        const uint32_t id = world.bodies.spawn_falling( cell, cur, glm::dvec3( 0.0 ) );
        if ( id == 0 ) {
            break; // body cap: this block (and everything above it) stays
        }
        world.set_loaded_block( cell, BLOCK_STATE_AIR );
        // The block becomes a falling body, not debris — peers see the same
        // via the entity spawn, so suppress the removal burst.
        repGameState.multiplayer.set_block( cell, BLOCK_STATE_AIR, NET_BU_FLAG_NO_DEBRIS );
        queue_neighbor_block_updates( blockUpdateQueue, world, repGameState.tick_number, cell );
    }
}
