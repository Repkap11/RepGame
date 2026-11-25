#include "common/world.hpp"
#include "common/chunk.hpp"
#include "common/dimension_profile.hpp"
#include "common/chunk_loader.hpp"
#include "common/multiplayer.hpp"
#include "common/utils/env_config.hpp"
#include "common/utils/profile.hpp"
#include "common/renderer/screenshot.hpp"
#include "common/Logging.hpp"
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


int World::can_fixup_chunk( const Chunk &chunk, const glm::ivec3 &offset ) const {
    const glm::vec3 chunk_with_offset = chunk.chunk_pos + offset;
    if ( const Chunk *fixupChunk_prt = this->chunkLoader.get_chunk( chunk_with_offset ) ) {
        const Chunk &fixupChunk = *fixupChunk_prt;
        if ( !fixupChunk.load_state ) {
            return 1;
        }
    }
    return 0;
}

void World::fixup_chunk( const Chunk &chunk, const glm::ivec3 &offset, const glm::ivec3 &pos, const BlockState &blockState ) {
    // pr_debug( "Fixup Offset: %d %d %d", x, y, z );
    const glm::vec3 chunk_with_offset = chunk.chunk_pos + offset;
    if ( Chunk *fixupChunk_prt = this->chunkLoader.get_chunk( chunk_with_offset ) ) {
        Chunk &fixupChunk = *fixupChunk_prt;
        if ( fixupChunk.load_state ) {
            // This shouldn't happen because can_fixup_chunk should be called first.
            pr_debug( "Ekk, still loading. You'll probably get a lighting bug." );
            return;
        }
        fixupChunk.set_block( pos, blockState );
        fixupChunk.dirty = 1;
        fixupChunk.needs_repopulation = 1;
        this->chunkLoader.repop_must_enqueue( fixupChunk );
        if ( fixupChunk.is_empty_chunk && fixupChunk.cells ) {
            // The write materialized content into an all-air chunk — same
            // transition as the primary edit path (world.cpp's set_block
            // site): clear the empty flag and flag it drawable so the remesh
            // actually happens.
            fixupChunk.is_empty_chunk = false;
            fixupChunk.set_should_render( 1 );
        }
    }
}

Chunk *World::get_loaded_chunk( const glm::ivec3 &block_pos ) const {
    constexpr glm::vec3 chunk_size = glm::vec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    const glm::ivec3 chunk_pos = glm::floor( glm::vec3( block_pos ) / chunk_size );
    return this->chunkLoader.get_chunk( chunk_pos );
}

BlockState World::get_block_from_chunk( const Chunk &chunk, const glm::ivec3 &block_pos ) {
    constexpr glm::ivec3 chunk_size = glm::ivec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    const glm::ivec3 diff = block_pos - ( chunk.chunk_pos * chunk_size );
    return chunk.get_block( diff );
}

BlockState World::get_loaded_block( const glm::ivec3 &block_pos ) const {
    if ( const Chunk *chunk_prt = this->get_loaded_chunk( block_pos ) ) {
        const Chunk &chunk = *chunk_prt;
        if ( chunk.load_state ) {
            return BLOCK_STATE_LAST_BLOCK_ID;
        }
        const BlockState blockState = get_block_from_chunk( chunk, block_pos );
        return blockState;
    }
    return BLOCK_STATE_LAST_BLOCK_ID;
}

void World::spawn_block_debris( const glm::ivec3 &block_pos, const BlockState &prev_state, const BlockID new_id, const int num_particles ) {
    if ( prev_state.id == AIR || prev_state.id == LAST_BLOCK_ID || prev_state.id == new_id ) {
        return;
    }
    const Block *prev_block = block_definition_get_definition( prev_state.id );
    if ( prev_block->renderOrder == RenderOrder_Water ) {
        return;
    }
    static const bool log_debris = env_is_set( "REPGAME_DEBUG_DEBRIS" );
    if ( log_debris ) {
        const char *new_name = new_id < LAST_BLOCK_ID ? block_definition_get_name( new_id ) : nullptr;
        pr_debug( "DEBRIS pos=(%d,%d,%d) %s -> %s n=%d", block_pos.x, block_pos.y, block_pos.z,
                  block_definition_get_name( prev_state.id ), new_name != nullptr ? new_name : "?", num_particles );
    }
    // Particles live <1s and stay near their cell, so a single light sample at
    // the (now-air) block center is baked into every instance.
    this->blockDebris.spawn_block_break( block_pos, prev_state,
                                         this->chunkLoader.light_sample( glm::vec3( block_pos ) + 0.5f ), num_particles );
}


// void World::overlay_blocks( const glm::ivec3 *block_poses, BlockState *blockStates, int numBlocks ) {
//     const glm::ivec3 CHUNK_SIZE = glm::ivec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
//     for ( int bi = 0; bi < numBlocks; bi++ ) {
//         BlockState &blockState = blockStates[ bi ];
//         const glm::ivec3 &block_pos = block_poses[ bi ];
//         glm::ivec3 chunk_pos = glm::floor( glm::vec3( block_pos ) / glm::vec3( CHUNK_SIZE ) );
//         Chunk *chunk_prt = this->chunkLoader.get_chunk( chunk_pos );
//         if ( chunk_prt ) {
//             Chunk &chunk = *chunk_prt;
//             glm::ivec3 diff = block_pos - chunk_pos * CHUNK_SIZE;
//             // pr_debug( "Orig Offset: %d %d %d", diff_x, diff_y, diff_z );

//             for ( int i = -1; i < 2; i++ ) {
//                 const int needs_update_x = ( ( i != 1 && diff.x == 0 ) || ( i != -1 && diff.x == ( CHUNK_SIZE_X - 1 ) ) ) || i == 0;
//                 if ( !needs_update_x ) {
//                     continue;
//                 }
//                 for ( int j = -1; j < 2; j++ ) {
//                     const int needs_update_y = ( ( j != 1 && diff.y == 0 ) || ( j != -1 && diff.y == ( CHUNK_SIZE_Y - 1 ) ) ) || j == 0;
//                     if ( !needs_update_y ) {
//                         continue;
//                     }
//                     for ( int k = -1; k < 2; k++ ) {
//                         const int needs_update_z = ( ( k != 1 && diff.z == 0 ) || ( k != -1 && diff.z == ( CHUNK_SIZE_Z - 1 ) ) ) || k == 0;
//                         if ( !needs_update_z ) {
//                             continue;
//                         }
//                         // pr_debug( "Chunk Dir: %d %d %d:%d", i, j, k, needs_update );
//                         // pr_debug( "                    Needs Updates: %d %d %d:%d", needs_update_x, needs_update_y, needs_update_z, needs_update );
//                         // pr_debug( "                                New Offset: %d %d %d:%d", new_i, new_j, new_k, needs_update );

//                         glm::ivec3 new_pos = glm::ivec3( i * needs_update_x, j * needs_update_y, k * needs_update_z );
//                         glm::ivec3 offset = glm::ivec3( i, j, k );
//                         glm::ivec3 pos = diff - CHUNK_SIZE * new_pos;
//                         fixup_chunk( chunk, offset, pos, blockState );
//                     }
//                 }
//             }
//             chunk.set_block( diff, blockState );
//             chunk.dirty = 1;
//             chunk.needs_repopulation = 1;

//         } else {
//             // This just means mouse is not pointing at a block
//             // pr_debug( "Could not find the pointed to chunk" );
//         }
//     }
// }
void World::set_loaded_block( const glm::ivec3 &block_pos, BlockState blockState ) {
    glm::ivec3 chunk_pos = glm::floor( glm::vec3( block_pos ) / CHUNK_SIZE_F );

    Chunk *chunk_prt = this->chunkLoader.get_chunk( chunk_pos );
    if ( chunk_prt == nullptr || chunk_prt->load_state ) {
        // Parked/loading slot — there may be no block storage to write
        // (parking releases it). Multiplayer edits re-arrive as diffs on the
        // next load ONLY for other clients: the sender's own BLOCK_UPDATE is
        // never relayed back, so queue the write the same way a received
        // update would be — it lands at the next load's finalize.
        if ( this->multiplayer != nullptr ) {
            NetChunkDiffResultPayload diff;
            diff.chunk_x = chunk_pos.x;
            diff.chunk_y = chunk_pos.y;
            diff.chunk_z = chunk_pos.z;
            NetChunkDiffEntry entry;
            entry.blocks_index = static_cast<uint32_t>( Chunk::get_index_from_coords( block_pos - chunk_pos * CHUNK_SIZE_I ) );
            entry.blockState = blockState;
            diff.diffs.push_back( entry );
            diff.num_diffs = 1;
            this->multiplayer->queue_pending_diff( diff );
        }
        return;
    }
    {
        Chunk &chunk = *chunk_prt;
        glm::ivec3 diff = block_pos - chunk_pos * CHUNK_SIZE_I;
        // pr_debug( "Orig Offset: %d %d %d", diff_x, diff_y, diff_z );

        for ( int i = -1; i < 2; i++ ) {
            const int needs_update_x = ( ( i != 1 && diff.x == 0 ) || ( i != -1 && diff.x == ( CHUNK_SIZE_X - 1 ) ) ) || i == 0;
            if ( !needs_update_x ) {
                continue;
            }
            for ( int j = -1; j < 2; j++ ) {
                const int needs_update_y = ( ( j != 1 && diff.y == 0 ) || ( j != -1 && diff.y == ( CHUNK_SIZE_Y - 1 ) ) ) || j == 0;
                if ( !needs_update_y ) {
                    continue;
                }
                for ( int k = -1; k < 2; k++ ) {
                    const int needs_update_z = ( ( k != 1 && diff.z == 0 ) || ( k != -1 && diff.z == ( CHUNK_SIZE_Z - 1 ) ) ) || k == 0;
                    if ( !needs_update_z ) {
                        continue;
                    }
                    glm::ivec3 offset = glm::ivec3( i, j, k );
                    if ( !can_fixup_chunk( chunk, offset ) ) {
                        pr_debug( "Ekk, can't fixup block, so not placing" );
                        return;
                    }
                }
            }
        }
        for ( int i = -1; i < 2; i++ ) {
            const int needs_update_x = ( ( i != 1 && diff.x == 0 ) || ( i != -1 && diff.x == ( CHUNK_SIZE_X - 1 ) ) ) || i == 0;
            if ( !needs_update_x ) {
                continue;
            }
            for ( int j = -1; j < 2; j++ ) {
                const int needs_update_y = ( ( j != 1 && diff.y == 0 ) || ( j != -1 && diff.y == ( CHUNK_SIZE_Y - 1 ) ) ) || j == 0;
                if ( !needs_update_y ) {
                    continue;
                }
                for ( int k = -1; k < 2; k++ ) {
                    const int needs_update_z = ( ( k != 1 && diff.z == 0 ) || ( k != -1 && diff.z == ( CHUNK_SIZE_Z - 1 ) ) ) || k == 0;
                    if ( !needs_update_z ) {
                        continue;
                    }
                    if ( i == 0 && j == 0 && k == 0 ) {
                        // The (0,0,0) offset is this same cell — the
                        // chunk.set_block below writes it. Writing it here too
                        // would apply set_block's waterlogging normalization
                        // twice (e.g. breaking a waterlogged plant normalizes
                        // AIR->WATER, then a second write into that WATER
                        // would store raw AIR or restore the plant).
                        continue;
                    }

                    // pr_debug( "Chunk Dir: %d %d %d:%d", i, j, k, needs_update );
                    // pr_debug( "                    Needs Updates: %d %d %d:%d", needs_update_x, needs_update_y, needs_update_z, needs_update );
                    // pr_debug( "                                New Offset: %d %d %d:%d", new_i, new_j, new_k, needs_update );

                    glm::ivec3 new_pos = glm::ivec3( i * needs_update_x, j * needs_update_y, k * needs_update_z );
                    glm::ivec3 offset = glm::ivec3( i, j, k );
                    glm::ivec3 pos = diff - CHUNK_SIZE_I * new_pos;
                    fixup_chunk( chunk, offset, pos, blockState );
                }
            }
        }
        chunk.set_block( diff, blockState );
        chunk.dirty = 1;
        chunk.sidecars.player_edited = 1;
        chunk.needs_repopulation = 1;
        this->chunkLoader.repop_must_enqueue( chunk );
        chunk.is_empty_chunk = false;
        // An empty chunk has should_render == 0 (program_terrain only sets it
        // when a layer has instances), so it's excluded from the drawable list
        // the remeshing loop walks. Set it here so the chunk enters the list
        // and gets remeshed this frame. Safe: Chunk::draw also guards on
        // num_instances != 0, so no stale geometry draws before remeshing.
        chunk.set_should_render( 1 );

        // Flood-fill light: queue a recheck for this cell on the lighting
        // thread (inline when it isn't running). No-op while the chunk has
        // no light volume.
        this->chunkLoader.light_submit_recheck( block_pos );
    }
}

void World::set_loaded_block_natural( const glm::ivec3 &block_pos, BlockState blockState ) {
    Chunk *chunk = this->get_loaded_chunk( block_pos );
    const uint8_t was_edited = chunk != nullptr ? chunk->sidecars.player_edited : 0;
    this->set_loaded_block( block_pos, blockState );
    if ( chunk != nullptr ) {
        chunk->sidecars.player_edited = was_edited;
    }
}
