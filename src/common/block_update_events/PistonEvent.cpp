#include "common/block_update_events/PistonEvent.hpp"

#include <vector>

#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"
#include "common/block.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/block_update_events/BlockNextToChangeEvent.hpp"
#include "common/block_update_events/schedule.hpp"

// Max blocks a piston can move in one extension (MC's push limit).
#define PISTON_PUSH_LIMIT 12

PistonEvent::PistonEvent( long tick_number, const glm::ivec3 &pos ) : BlockUpdateEvent( tick_number ), block_pos( pos ) {
    this->name = "PistonEvent";
}
PistonLandEvent::PistonLandEvent( long tick_number, const glm::ivec3 &pos ) : BlockUpdateEvent( tick_number ), block_pos( pos ) {
    this->name = "PistonLandEvent";
}

// True if a piston can move this block (push it or, for sticky pistons, pull
// it). Only solid blocks move; non-solids in the push path are destroyed and
// can never be pulled back.
static bool piston_can_move( const BlockState &block_state ) {
    if ( block_state.id == AIR || block_state.id == LAST_BLOCK_ID ) {
        return false;
    }
    const Block *block = block_definition_get_definition( block_state.id );
    if ( !block->collides_with_player || block->piston_immovable || block->is_piston_head || block_state.id == MOVING_PISTON ) {
        return false;
    }
    // A powered (extended) piston can't be moved: it would leave its head
    // behind pointing at a cell that no longer contains the base.
    if ( block->is_piston && block_state.current_redstone_power > 0 ) {
        return false;
    }
    return true;
}

// A move's start writes are local-only — remote clients see the landing
// writes relayed by PistonLandEvent, so they snap to the final state rather
// than animating the slide. Neighbor updates still queue so redstone and
// support re-evaluate around every moved block.
static void piston_set_local( RepGameState &repGameState, const glm::ivec3 &pos, const BlockState &block_state ) {
    repGameState.world.set_loaded_block( pos, block_state );
    queue_neighbor_block_updates( repGameState, pos );
}

// The 3x3x3 around `cell`, snapshotted before an event's MOVING_PISTON/AIR
// writes so a neighbour-dependent model (fence connections, stair corners)
// keeps the shape it had when the move started. Unloaded cells read as AIR.
static BlockEmitNeighbors piston_snapshot( World &world, const glm::ivec3 &cell ) {
    BlockEmitNeighbors nb;
    for ( int dy = -1; dy <= 1; dy++ ) {
        for ( int dz = -1; dz <= 1; dz++ ) {
            for ( int dx = -1; dx <= 1; dx++ ) {
                const BlockState s = world.get_loaded_block( cell + glm::ivec3( dx, dy, dz ) );
                nb.at( dx, dy, dz ) = s.id == LAST_BLOCK_ID ? BLOCK_STATE_AIR : s;
            }
        }
    }
    return nb;
}

// Piston heads and bases have no neighbour-dependent shape — an all-AIR
// snapshot with the centre filled is enough.
static BlockEmitNeighbors piston_nb_air( const BlockState &center ) {
    BlockEmitNeighbors nb;
    for ( int i = 0; i < 27; i++ ) {
        nb.cells[ i ] = BLOCK_STATE_AIR;
    }
    nb.at( 0, 0, 0 ) = center;
    return nb;
}

void piston_land_cell( RepGameState &state, const glm::ivec3 &pos ) {
    World &world = state.world;
    const BlockState cur = world.get_loaded_block( pos );
    if ( cur.id == LAST_BLOCK_ID ) {
        // Land event on an unloaded cell: drop the entry, nothing to write.
        state.piston_moves.erase( pos );
        return;
    }
    const auto it = state.piston_moves.find( pos );
    if ( it != state.piston_moves.end( ) ) {
        if ( cur.id == MOVING_PISTON ) {
            world.set_loaded_block( pos, it->second.carried );
        }
        if ( it->second.body_id != 0 ) {
            // The mover keeps drawing at its slide end until the chunk
            // remeshes — avoids a flicker frame.
            state.world.bodies.take( it->second.body_id, nullptr );
        }
        state.piston_moves.erase( it );
    } else if ( cur.id == MOVING_PISTON ) {
        // No side-table entry (crash or stale cell): land the {display_id,
        // rotation} fallback encoding.
        const BlockState landed = { cur.display_id, cur.rotation, 0, cur.display_id, 0 };
        world.set_loaded_block( pos, landed );
    }
    // A landed piston re-reads live power: a retraction's MOVING_PISTON base
    // cell ignores redstone while it slides, so re-powering during the slide
    // is picked up here instead (also covers the fallback landing path).
    {
        const BlockState settled = world.get_loaded_block( pos );
        if ( settled.id != LAST_BLOCK_ID && block_definition_get_definition( settled.id )->is_piston ) {
            const int power = mechanism_direct_power( world, pos );
            if ( power != settled.current_redstone_power ) {
                BlockState repowered = settled;
                repowered.current_redstone_power = power;
                world.set_loaded_block( pos, repowered );
            }
            if ( power > 0 ) {
                schedule_redstone_update<PistonEvent>( state, 0, pos );
            }
        }
    }
    // Relay what the cell settled to — a carried block, or AIR for a vacated
    // cell — since the start writes never reached the network.
    state.multiplayer.set_block( pos, world.get_loaded_block( pos ), NET_BU_FLAG_NO_DEBRIS );
    queue_neighbor_block_updates( state, pos );
}

void piston_finish_moves( RepGameState &state, const glm::ivec3 &base_pos ) {
    // A power flip mid-move lands the in-flight move first, so the new edge
    // sees settled cells (MC's finish-on-reverse).
    std::vector<glm::ivec3> pending;
    for ( const auto &entry : state.piston_moves ) {
        if ( entry.second.base == base_pos ) {
            pending.push_back( entry.first );
        }
    }
    for ( const glm::ivec3 &pos : pending ) {
        piston_land_cell( state, pos );
    }
}

void piston_update( RepGameState &repGameState, const glm::ivec3 &block_pos ) {
    piston_finish_moves( repGameState, block_pos );

    World &world = repGameState.world;
    const BlockState base_state = world.get_loaded_block( block_pos );
    if ( base_state.id == LAST_BLOCK_ID ) {
        return;
    }
    const Block *base = block_definition_get_definition( base_state.id );
    if ( !base->is_piston ) {
        return;
    }
    const glm::ivec3 dir = piston_facing_dir( base_state.rotation );
    const glm::ivec3 head_pos = block_pos + dir;
    const BlockState head_state = world.get_loaded_block( head_pos );
    const bool head_present = head_state.id != LAST_BLOCK_ID &&
        ( block_definition_get_definition( head_state.id )->is_piston_head ||
          // A MOVING_PISTON carrying a head only survives here when its
          // side-table entry was lost — the display_id fallback still names
          // the head id.
          ( head_state.id == MOVING_PISTON && block_definition_get_definition( head_state.display_id )->is_piston_head ) );
    const long start_gt = redstone_tick_now( repGameState );

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
            const glm::ivec3 cell = block_pos + dir * i;
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

        const BlockID head_id = base->is_sticky_piston ? STICKY_PISTON_ARM_COLLISION : PISTON_ARM_COLLISION;
        // Shape snapshots before any writes — a destination MOVING_PISTON
        // would otherwise clobber a neighbour read for the next mover's
        // source cell.
        BlockEmitNeighbors snap[ PISTON_PUSH_LIMIT ];
        for ( int i = 2; i <= dest_i; i++ ) {
            snap[ i - 2 ] = piston_snapshot( world, block_pos + dir * ( i - 1 ) );
        }
        // MOVING_PISTON writes run farthest-first so no cell is read after
        // being overwritten; each carries its block in the side table and
        // lands through its own PistonLandEvent PISTON_MOVE_GT later.
        for ( int i = dest_i; i >= 2; i-- ) {
            const glm::ivec3 dst = block_pos + dir * i;
            const BlockState carried = chain[ i - 2 ];
            RepGameState::PistonMove move;
            move.carried = carried;
            move.base = block_pos;
            move.body_id = world.bodies.spawn_piston_mover( carried, block_pos + dir * ( i - 1 ), dst, start_gt, PISTON_MOVE_GT, snap[ i - 2 ] );
            repGameState.piston_moves[ dst ] = move;
            const BlockState moving = { MOVING_PISTON, carried.rotation, 0, carried.id, 0 };
            piston_set_local( repGameState, dst, moving );
            schedule_redstone_update<PistonLandEvent>( repGameState, PISTON_MOVE_GT, dst );
        }
        const BlockState head_block_state = { head_id, base_state.rotation, 0, head_id, 0 };
        RepGameState::PistonMove head_move;
        head_move.carried = head_block_state;
        head_move.base = block_pos;
        // The head slides out of the base cell.
        head_move.body_id = world.bodies.spawn_piston_mover( head_block_state, block_pos, head_pos, start_gt, PISTON_MOVE_GT, piston_nb_air( head_block_state ) );
        repGameState.piston_moves[ head_pos ] = head_move;
        const BlockState moving_head = { MOVING_PISTON, base_state.rotation, 0, head_id, 0 };
        piston_set_local( repGameState, head_pos, moving_head );
        schedule_redstone_update<PistonLandEvent>( repGameState, PISTON_MOVE_GT, head_pos );
        if ( destroy ) {
            world.spawn_block_debris( destroy_pos, destroy_state, dest_i >= 2 ? chain[ dest_i - 2 ].id : head_id );
        }
    } else {
        // Unpowered: retract the head, and sticky pistons pull back the block
        // that was directly in front of it. Only pull when the head was
        // actually removed — a stale duplicate retract must not pull twice.
        if ( !head_present ) {
            return;
        }
        // The base keeps drawing its extended model for the slide: the cell
        // becomes MOVING_PISTON carrying the real (power-0) base state, and a
        // stationary mover renders the powered (recessed) base until landing.
        BlockState extended = base_state;
        extended.current_redstone_power = 1;
        RepGameState::PistonMove base_move;
        base_move.carried = base_state;
        base_move.base = block_pos;
        base_move.body_id = world.bodies.spawn_piston_mover( extended, block_pos, block_pos, start_gt, PISTON_MOVE_GT, piston_nb_air( extended ) );
        repGameState.piston_moves[ block_pos ] = base_move;
        const BlockState moving_base = { MOVING_PISTON, base_state.rotation, 0, base_state.id, 0 };
        piston_set_local( repGameState, block_pos, moving_base );
        schedule_redstone_update<PistonLandEvent>( repGameState, PISTON_MOVE_GT, block_pos );
        // The retracting head slides into the base cell and expires at
        // arrival — no table entry, nothing to land.
        BlockState slide_state = head_state;
        if ( head_state.id == MOVING_PISTON ) {
            const auto it = repGameState.piston_moves.find( head_pos );
            slide_state = it != repGameState.piston_moves.end( ) ? it->second.carried : BlockState{ head_state.display_id, head_state.rotation, 0, head_state.display_id, 0 };
        }
        world.bodies.spawn_piston_mover( slide_state, head_pos, block_pos, start_gt, PISTON_MOVE_GT, piston_nb_air( slide_state ) );
        if ( base->is_sticky_piston ) {
            const glm::ivec3 pull_pos = head_pos + dir;
            const BlockState pull_state = world.get_loaded_block( pull_pos );
            if ( piston_can_move( pull_state ) ) {
                // Snapshot before the pull cell's AIR write.
                const BlockEmitNeighbors pull_nb = piston_snapshot( world, pull_pos );
                // The pulled block lands where the head was.
                piston_set_local( repGameState, pull_pos, BLOCK_STATE_AIR );
                RepGameState::PistonMove move;
                move.carried = pull_state;
                move.base = block_pos;
                move.body_id = world.bodies.spawn_piston_mover( pull_state, pull_pos, head_pos, start_gt, PISTON_MOVE_GT, pull_nb );
                repGameState.piston_moves[ head_pos ] = move;
                const BlockState moving = { MOVING_PISTON, pull_state.rotation, 0, pull_state.id, 0 };
                piston_set_local( repGameState, head_pos, moving );
                schedule_redstone_update<PistonLandEvent>( repGameState, PISTON_MOVE_GT, pull_pos );
                schedule_redstone_update<PistonLandEvent>( repGameState, PISTON_MOVE_GT, head_pos );
                return;
            }
        }
        // Nothing follows: the head cell just vacates.
        piston_set_local( repGameState, head_pos, BLOCK_STATE_AIR );
        schedule_redstone_update<PistonLandEvent>( repGameState, PISTON_MOVE_GT, head_pos );
    }
}

void PistonEvent::performAction( RepGameState &repGameState ) {
    piston_update( repGameState, this->block_pos );
}

void PistonLandEvent::performAction( RepGameState &repGameState ) {
    piston_land_cell( repGameState, this->block_pos );
}
