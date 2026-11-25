#include "common/world.hpp"
#include "common/chunk.hpp"
#include "common/dimension_profile.hpp"
#include "common/chunk_loader.hpp"
#include "common/multiplayer.hpp"
#include "common/utils/env_config.hpp"
#include "common/utils/profile.hpp"
#include "common/renderer/screenshot.hpp"
#include "common/game/game_state.hpp"
#include "common/tree_grow.hpp"
#include "common/light.hpp"
#include "common/fluid.hpp"
#include "common/imgui_overlay.hpp"
#include "common/block_update_events/FluidUpdateEvent.hpp"
#include "common/block_update_events/PistonEvent.hpp"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>


// Grass lifecycle, driven by the source cell (MC GrassBlock.randomTick): a
// grass-family cell covered by an opaque block dies back to dirt; an
// uncovered one may spread to random dirt targets in reach — ±1
// horizontally, 3 down / 1 up — provided the cell above the target is also
// open. Snowy grass spreads plain grass (the snow doesn't propagate);
// mycelium spreads mycelium.
#define GRASS_SPREAD_ATTEMPTS 4
// Fraction of grass random ticks that try to spread — the same roll the
// sapling/crop check uses (~3.5 min mean per grass cell).
#define GRASS_SPREAD_CHANCE_DEN 3
// MC's spread reach: ±1 horizontally, 3 down / 1 up.
#define GRASS_SPREAD_SIDE 1
#define GRASS_SPREAD_DOWN 3
#define GRASS_SPREAD_UP 1

// True when the cell above pos is loaded and light-opaque (casts_shadow —
// the same predicate the light engine's opacity test uses) or holds a
// fluid — MC's opacity-3 water also covers (fluid_in_cell counts
// waterlogged cells as water). Cover kills grass and disqualifies a
// spread target; torches, leaves and partial blocks shade but don't
// cover. An unloaded cell counts as open (the same permissive rule leaf
// decay uses).
bool World::grass_covered( const glm::ivec3 &pos ) const {
    const BlockState above = this->get_loaded_block( pos + glm::ivec3( 0, 1, 0 ) );
    return above.id != LAST_BLOCK_ID &&
           ( block_definition_get_definition( above.id )->casts_shadow || fluid_in_cell( above ) != LAST_BLOCK_ID );
}

void World::grass_random_tick( RepGameState &state, const glm::ivec3 &world_pos, const BlockState &cell ) {
    if ( grass_covered( world_pos ) ) {
        // Covered grass dies to dirt — same natural write + sync as crops.
        BlockState next = cell;
        next.id = DIRT;
        next.display_id = DIRT;
        next.rotation = BLOCK_ROTATE_0;
        this->set_loaded_block_natural( world_pos, next );
        state.multiplayer.set_block( world_pos, next, NET_BU_FLAG_NO_DEBRIS );
        return;
    }
    this->random_tick_state ^= this->random_tick_state << 13;
    this->random_tick_state ^= this->random_tick_state >> 17;
    this->random_tick_state ^= this->random_tick_state << 5;
    if ( this->random_tick_state % GRASS_SPREAD_CHANCE_DEN != 0 ) {
        return;
    }
    const BlockID spread_id = cell.id == SNOWY_GRASS ? GRASS : cell.id;
    for ( int attempt = 0; attempt < GRASS_SPREAD_ATTEMPTS; attempt++ ) {
        this->random_tick_state ^= this->random_tick_state << 13;
        this->random_tick_state ^= this->random_tick_state >> 17;
        this->random_tick_state ^= this->random_tick_state << 5;
        const glm::ivec3 target = world_pos + glm::ivec3( static_cast<int>( this->random_tick_state % ( 2 * GRASS_SPREAD_SIDE + 1 ) ) - GRASS_SPREAD_SIDE,
                                                         static_cast<int>( ( this->random_tick_state >> 8 ) % ( GRASS_SPREAD_DOWN + GRASS_SPREAD_UP + 1 ) ) - GRASS_SPREAD_DOWN,
                                                         static_cast<int>( ( this->random_tick_state >> 16 ) % ( 2 * GRASS_SPREAD_SIDE + 1 ) ) - GRASS_SPREAD_SIDE );
        // get_loaded_block rather than chunk.get_block: dy -3..+1 can reach
        // past the chunk's ±1 halo; LAST_BLOCK_ID targets are skipped.
        const BlockState target_state = this->get_loaded_block( target );
        if ( target_state.id != DIRT || grass_covered( target ) ) {
            continue;
        }
        BlockState next = target_state;
        next.id = spread_id;
        next.display_id = spread_id;
        next.rotation = BLOCK_ROTATE_0;
        this->set_loaded_block_natural( target, next );
        state.multiplayer.set_block( target, next, NET_BU_FLAG_NO_DEBRIS );
    }
}


// Chunks within this many of the player get random ticks.
#define RANDOM_TICK_RADIUS_XZ 2
#define RANDOM_TICK_RADIUS_Y 1
// Cells sampled per live chunk per game tick: ~1 tick per cell every
// (62*62*62 / 24) / 144 s ≈ 69 s.
#define RANDOM_TICKS_PER_CHUNK 24
// Fraction of sapling random ticks that grow: ~3.5 min mean under 9+ light.
#define SAPLING_GROW_CHANCE_NUM 1
#define SAPLING_GROW_CHANCE_DEN 3
// Fraction of crop random ticks that advance a stage: same cadence as the
// sapling check (each sampled cell ticks once per ~69 s, so a wheat field
// matures in ~28 min — near MC's unirrigated rate).
#define CROP_GROW_CHANCE_DEN 3
// Minimum sky or block light level (of 15) for a sapling or crop to grow.
#define SAPLING_GROW_MIN_LIGHT ( 9.0f / 15.0f )
// Fraction of bamboo random ticks that grow a segment — the crop cadence,
// so an open column gains ~1 segment per ~3.5 min near the player.
#define BAMBOO_GROW_CHANCE_DEN 3
// A shoot's growth tick pops a small column instead of one segment, so a
// planted bamboo reads as a plant right away.
#define BAMBOO_SAPLING_SEGMENTS 3

void World::process_random_ticks( RepGameState &state ) {
    const glm::ivec3 center_chunk( static_cast<int>( floor( state.camera.pos.x / CHUNK_SIZE_X ) ),
                                   static_cast<int>( floor( state.camera.pos.y / CHUNK_SIZE_Y ) ),
                                   static_cast<int>( floor( state.camera.pos.z / CHUNK_SIZE_Z ) ) );
    const glm::ivec3 chunk_size( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        Chunk &chunk = this->chunkLoader.chunkArray[ i ];
        if ( chunk.load_state != CHUNK_LIVE ) {
            continue;
        }
        const glm::ivec3 d = chunk.chunk_pos - center_chunk;
        if ( abs( d.x ) > RANDOM_TICK_RADIUS_XZ || abs( d.z ) > RANDOM_TICK_RADIUS_XZ || abs( d.y ) > RANDOM_TICK_RADIUS_Y ) {
            continue;
        }
        const glm::ivec3 chunk_origin = chunk.chunk_pos * chunk_size;
        for ( int s = 0; s < RANDOM_TICKS_PER_CHUNK; s++ ) {
            this->random_tick_state ^= this->random_tick_state << 13;
            this->random_tick_state ^= this->random_tick_state >> 17;
            this->random_tick_state ^= this->random_tick_state << 5;
            const glm::ivec3 local( this->random_tick_state % CHUNK_SIZE_X,
                                    ( this->random_tick_state >> 8 ) % CHUNK_SIZE_Y,
                                    ( this->random_tick_state >> 16 ) % CHUNK_SIZE_Z );
            const BlockState cell = chunk.get_block( local );
            TreeSpec spec;
            const glm::ivec3 world_pos = chunk_origin + local;
            // Flowing fluid outlives its update events when a chunk unloads
            // (the queue isn't persisted): a random tick re-arms it. Sources
            // are stable so only flow levels need the rescue; the scheduler
            // dedupes, making this cheap.
            if ( fluid_level( cell ) > 0 ) {
                fluid_schedule_update( state, world_pos );
            }
            // A MOVING_PISTON outlives its move when its events die with it
            // (a chunk unload drops the queue, or the write raced the
            // slide): cells without a side-table entry land on the spot via
            // their {display_id, rotation} fallback encoding. In multiplayer,
            // remote clients also have no side-table entry but do hold the
            // mirror body sliding into the cell — skip those, their sim's
            // landing write is authoritative.
            if ( cell.id == MOVING_PISTON && state.piston_moves.find( world_pos ) == state.piston_moves.end() &&
                 !this->bodies.piston_move_pending( world_pos ) ) {
                piston_land_cell( state, world_pos );
            }
            const Block *cell_def = cell.id == LAST_BLOCK_ID ? nullptr : block_definition_get_definition( cell.id );
            const bool is_crop_stage = cell_def != nullptr && cell_def->crop_next != LAST_BLOCK_ID;
            const bool is_bamboo_cell = cell.id == BAMBOO || cell.id == BAMBOO_SAPLING;
            if ( !is_crop_stage && !is_bamboo_cell && !tree_spec_for_sapling( cell.id, world_pos, &spec ) ) {
                // Grass-family cells tick from the source: covered grass
                // dies to dirt, open grass may spread to nearby dirt.
                if ( cell.id == GRASS || cell.id == SNOWY_GRASS || cell.id == MYCELIUM ) {
                    this->grass_random_tick( state, world_pos, cell );
                }
                continue;
            }
            const glm::vec2 light = this->chunkLoader.light_sample( glm::vec3( world_pos ) + 0.5f );
            if ( light.x < SAPLING_GROW_MIN_LIGHT && light.y < SAPLING_GROW_MIN_LIGHT ) {
                continue;
            }
            this->random_tick_state ^= this->random_tick_state << 13;
            this->random_tick_state ^= this->random_tick_state >> 17;
            this->random_tick_state ^= this->random_tick_state << 5;
            if ( is_bamboo_cell ) {
                // Only the column's top grows — a ticked interior cell
                // does nothing (MC's per-column rate). A shoot always may
                // grow: it converts and opens the column.
                if ( cell.id == BAMBOO && this->get_loaded_block( world_pos + glm::ivec3( 0, 1, 0 ) ).id == BAMBOO ) {
                    continue;
                }
                if ( this->random_tick_state % BAMBOO_GROW_CHANCE_DEN != 0 ) {
                    continue;
                }
                bamboo_grow_try( state, world_pos, cell.id == BAMBOO_SAPLING ? BAMBOO_SAPLING_SEGMENTS : 1, true );
                continue;
            }
            if ( is_crop_stage ) {
                // Crop growth advances the stage chain in place — a plain
                // write (no debris, no support change), synced like the tree
                // grow below. Not a player edit: the pen rule stays out.
                if ( this->random_tick_state % CROP_GROW_CHANCE_DEN != 0 ) {
                    continue;
                }
                BlockState next = cell;
                next.id = cell_def->crop_next;
                next.display_id = cell_def->crop_next;
                next.rotation = BLOCK_ROTATE_0;
                this->set_loaded_block_natural( world_pos, next );
                state.multiplayer.set_block( world_pos, next, NET_BU_FLAG_NO_DEBRIS );
                continue;
            }
            if ( this->random_tick_state % SAPLING_GROW_CHANCE_DEN >= SAPLING_GROW_CHANCE_NUM ) {
                continue;
            }
            tree_grow_try( state, world_pos, true );
        }
    }
}
