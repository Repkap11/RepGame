#include <vector>

#include "common/block_update_events/PistonEvent.hpp"
#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"
#include "common/block.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"

// Max blocks a piston can move in one extension (MC's push limit).
#define PISTON_PUSH_LIMIT 12

PistonEvent::PistonEvent( long tick_number, const glm::ivec3 &pos ) : BlockUpdateEvent( tick_number ), block_pos( pos ) {
    this->name = "PistonEvent";
}

// True if a piston can move this block (push it or, for sticky pistons, pull
// it). Only solid blocks move; non-solids in the push path are destroyed and
// can never be pulled back.
static bool piston_can_move( const BlockState &block_state ) {
    if ( block_state.id == AIR || block_state.id == LAST_BLOCK_ID ) {
        return false;
    }
    const Block *block = block_definition_get_definition( block_state.id );
    if ( !block->collides_with_player || block->piston_immovable || block->is_piston_head ) {
        return false;
    }
    // A powered (extended) piston can't be moved: it would leave its head
    // behind pointing at a cell that no longer contains the base.
    if ( block->is_piston && block_state.current_redstone_power > 0 ) {
        return false;
    }
    return true;
}

// Writes a cell directly (bypassing placement validation — the push scan
// already proved each destination legal), relays it to the server, and queues
// the same neighbor updates a PlayerBlockPlacedEvent would so redstone and
// support re-evaluate around every moved block.
static void piston_set_block( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState, const glm::ivec3 &pos, const BlockState &block_state ) {
    repGameState.world.set_loaded_block( pos, block_state );
    repGameState.multiplayer.set_block( pos, block_state );
    queue_neighbor_block_updates( blockUpdateQueue, repGameState.world, repGameState.tick_number, pos );
}

void PistonEvent::performAction( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState ) {
    World &world = repGameState.world;
    const BlockState base_state = world.get_loaded_block( this->block_pos );
    const Block *base = block_definition_get_definition( base_state.id );
    if ( !base->is_piston ) {
        return;
    }
    const glm::ivec3 dir = piston_facing_dir( base_state.rotation );
    const glm::ivec3 head_pos = this->block_pos + dir;
    const BlockState head_state = world.get_loaded_block( head_pos );
    const bool head_present = block_definition_get_definition( head_state.id )->is_piston_head;

    if ( base_state.current_redstone_power > 0 ) {
        // Powered: extend unless already extended.
        if ( head_present ) {
            return;
        }
        // Scan the push path. The first free cell (air or liquid) ends the
        // chain; a breakable non-solid ends it too and gets destroyed.
        BlockState chain[ PISTON_PUSH_LIMIT ];
        int chain_len = 0;
        int dest_i = 0;
        bool destroy = false;
        glm::ivec3 destroy_pos;
        BlockState destroy_state = BLOCK_STATE_AIR;
        for ( int i = 1; i <= PISTON_PUSH_LIMIT + 1; i++ ) {
            const glm::ivec3 cell = this->block_pos + dir * i;
            const BlockState cell_state = world.get_loaded_block( cell );
            if ( cell_state.id == LAST_BLOCK_ID ) {
                return; // never push into an unloaded/loading chunk
            }
            const Block *cell_block = block_definition_get_definition( cell_state.id );
            if ( cell_state.id == AIR || cell_block->can_be_placed_in ) {
                dest_i = i;
                break;
            }
            if ( !cell_block->collides_with_player ) {
                dest_i = i;
                destroy = true;
                destroy_pos = cell;
                destroy_state = cell_state;
                break;
            }
            if ( !piston_can_move( cell_state ) ) {
                return;
            }
            if ( chain_len >= PISTON_PUSH_LIMIT ) {
                return;
            }
            chain[ chain_len++ ] = cell_state;
        }
        if ( dest_i == 0 ) {
            return;
        }

        const BlockID head_id = base->is_sticky_piston ? PISTON_HEAD_STICKY : PISTON_HEAD;
        // Move the chain one cell forward, farthest first so no cell is read
        // after being overwritten.
        for ( int i = dest_i; i >= 2; i-- ) {
            piston_set_block( blockUpdateQueue, repGameState, this->block_pos + dir * i, chain[ i - 2 ] );
        }
        if ( destroy ) {
            world.spawn_block_debris( destroy_pos, destroy_state, dest_i >= 2 ? chain[ dest_i - 2 ].id : head_id );
        }
        const BlockState head_block_state = { head_id, base_state.rotation, 0, head_id, 0 };
        piston_set_block( blockUpdateQueue, repGameState, head_pos, head_block_state );
    } else {
        // Unpowered: retract the head, and sticky pistons pull back the block
        // that was directly in front of it. Only pull when the head was
        // actually removed — a stale duplicate retract must not pull twice.
        if ( !head_present ) {
            return;
        }
        piston_set_block( blockUpdateQueue, repGameState, head_pos, BLOCK_STATE_AIR );
        if ( base->is_sticky_piston ) {
            const glm::ivec3 pull_pos = head_pos + dir;
            const BlockState pull_state = world.get_loaded_block( pull_pos );
            if ( piston_can_move( pull_state ) ) {
                piston_set_block( blockUpdateQueue, repGameState, pull_pos, BLOCK_STATE_AIR );
                piston_set_block( blockUpdateQueue, repGameState, head_pos, pull_state );
            }
        }
    }
}
