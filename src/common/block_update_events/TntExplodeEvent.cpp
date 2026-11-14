#include <stdlib.h>
#include <math.h>

#include <vector>

#include "common/block_update_events/TntExplodeEvent.hpp"
#include "common/RepGame.hpp"
#include "common/audio.hpp"
#include "common/multiplayer.hpp"
#include "common/net_entities.hpp"
#include "common/block_definitions.hpp"
#include "common/map_gen.hpp"
#include "common/structure_gen.hpp"
#include "common/chunk.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/block_update_events/schedule.hpp"

TntExplodeEvent::TntExplodeEvent( long tick_number, const glm::ivec3 &pos, const uint32_t body_id, const bool inverse ) : BlockUpdateEvent( tick_number ), block_pos( pos ), body_id( body_id ), inverse( inverse ) {
    this->name = inverse ? "TntExplodeEvent(inverse)" : "TntExplodeEvent";
}

// Launch velocity ranges (blocks/sec) for chain-ignited TNT.
#define TNT_CHAIN_VEL_MIN 6.0f
#define TNT_CHAIN_VEL_SPREAD 5.0f
#define TNT_CHAIN_VEL_UP_MIN 4.5f
#define TNT_CHAIN_VEL_UP_SPREAD 4.0f
// Debris per destroyed block — less than DEBRIS_PER_BLOCK since a blast
// destroys hundreds of blocks in one tick.
#define DEBRIS_PER_EXPLODED_BLOCK 12

static float frand( ) {
    return static_cast<float>( rand( ) ) / static_cast<float>( RAND_MAX );
}

void tnt_launch( RepGameState &repGameState, const glm::vec3 &center, const glm::vec3 &velocity, const long fuse_ticks, const bool inverse ) {
    World &world = repGameState.world;
    const BlockID primed_id = inverse ? INVERSE_TNT : TNT;
    // Callers pass blocks/second; bodies integrate blocks/tick.
    const uint32_t body_id = world.bodies.spawn_primed_tnt( repGameState, glm::dvec3( center ), glm::dvec3( velocity ) / static_cast<double>( UPS_RATE ), primed_id, fuse_ticks, repGameState.tick_number );
    // The fallback position covers bodies that vanish before their fuse ends
    // (creeper blasts carry id 0 and always land here).
    const glm::ivec3 explode_pos = glm::ivec3( glm::floor( center ) );
    schedule_block_update<TntExplodeEvent>( repGameState, fuse_ticks, explode_pos, body_id, inverse );
}

void tnt_ignite( RepGameState &repGameState, const glm::vec3 &center, const glm::vec3 &velocity, const long fuse_ticks, const bool inverse ) {
    World &world = repGameState.world;
    // The block leaves the world immediately — the primed entity replaces it.
    // No break debris: igniting isn't breaking. If the cell is no longer TNT
    // a stale/racing ignite fired — drop it instead of spawning a phantom
    // explosion (and never clobber a different block).
    const glm::ivec3 cell = glm::ivec3( glm::floor( center ) );
    const BlockState cur = world.get_loaded_block( cell );
    if ( cur.id == LAST_BLOCK_ID || !block_definition_get_definition( cur.id )->is_tnt ) {
        return;
    }
    world.set_loaded_block( cell, BLOCK_STATE_AIR );
    repGameState.multiplayer.set_block( cell, BLOCK_STATE_AIR, NET_BU_FLAG_NO_DEBRIS );
    queue_neighbor_block_updates( repGameState, cell );
    audio_play_sfx( "fuse" );
    tnt_launch( repGameState, center, velocity, fuse_ticks, inverse );
}

// Shared chain launch for both blast flavors: each TNT block the blast
// collected becomes a primed body flung away from the center with an
// upward kick and a short fuse, keeping its own normal/inverse flavor.
static void launch_chained_tnt( RepGameState &repGameState, const glm::dvec3 &center, const std::vector<glm::ivec3> &chained_tnt, const std::vector<bool> &chained_inverse ) {
    for ( size_t i = 0; i < chained_tnt.size( ); i++ ) {
        const glm::vec3 tnt_center = glm::vec3( chained_tnt[ i ] ) + 0.5f;
        glm::vec3 away = tnt_center - glm::vec3( center );
        away.y = 0.0f;
        glm::vec3 velocity;
        if ( const float hlen = glm::length( away ); hlen > 0.01f ) {
            velocity = away / hlen * ( TNT_CHAIN_VEL_MIN + frand( ) * TNT_CHAIN_VEL_SPREAD );
        } else {
            const float angle = frand( ) * 6.2832f;
            velocity = glm::vec3( cosf( angle ), 0.0f, sinf( angle ) ) * ( TNT_CHAIN_VEL_MIN + frand( ) * TNT_CHAIN_VEL_SPREAD );
        }
        velocity.y = TNT_CHAIN_VEL_UP_MIN + frand( ) * TNT_CHAIN_VEL_UP_SPREAD;
        const long fuse = TNT_CHAIN_FUSE_MIN_TICKS + rand( ) % TNT_CHAIN_FUSE_SPREAD_TICKS;
        tnt_launch( repGameState, tnt_center, velocity, fuse, chained_inverse[ i ] );
    }
}

void TntExplodeEvent::performAction( RepGameState &repGameState ) {
    World &world = repGameState.world;
    audio_play_sfx( "explode" );
    // The primed body's fuse is up exactly when the blast runs — take it here
    // so the blast lands at the body's real position. id 0 (creepers) and
    // bodies already evicted or cleaned up keep the fallback block_pos.
    glm::dvec3 c;
    if ( world.bodies.take( this->body_id, &c ) ) {
        this->block_pos = glm::ivec3( glm::floor( c ) );
    }
    const glm::dvec3 center = glm::dvec3( this->block_pos ) + glm::dvec3( 0.5 );
    // Multiplayer: other clients play the sound and shove their own
    // entities; the block changes arrive via BLOCK_UPDATE (no-op unbound).
    repGameState.multiplayer.entities.send_explosion( center.x, center.y, center.z, TNT_IMPULSE_RADIUS );

    // Inverse TNT: restore every loaded block in the blast sphere to the
    // full natural world (terrain + structures: trees, decorations, features,
    // crystals) as the CPU generator would produce it — no HIP/CUDA. Fresh
    // chunks are generated on the CPU for the blast area (MapGen::load_block_c
    // + StructureGen::place, the same pipeline chunk loading uses), then each
    // cell in the sphere is read from its owning chunk's interior. Structure
    // gen is deterministic and chunk-independent, so a chunk's interior holds
    // every structure part that belongs in it, including canopies from trees
    // rooted in neighboring chunks. No destruction, no debris — but TNT
    // blocks still chain-ignite: each is collected while its cell restores
    // like any other, then launched as a primed body once the sphere is
    // rewritten. A body regenerated inside solid terrain is embedded and
    // simply detonates in place. Cells already matching the natural state
    // are left alone to avoid needless remesh/light churn.
    if ( this->inverse ) {
        // The blast sphere spans at most 2 chunks per axis (radius 4 << 62).
        // Generate each touched chunk once, then read blocks from it.
        const glm::ivec3 min_pos = this->block_pos - TNT_BLAST_RADIUS;
        const glm::ivec3 max_pos = this->block_pos + TNT_BLAST_RADIUS;
        const glm::ivec3 min_chunk = glm::floor( glm::vec3( min_pos ) / CHUNK_SIZE_F );
        const glm::ivec3 max_chunk = glm::floor( glm::vec3( max_pos ) / CHUNK_SIZE_F );

        struct GenChunk {
            glm::ivec3 pos;
            Chunk *chunk;
        };
        std::vector<GenChunk> gen_chunks;
        auto find_gen_chunk = [ &gen_chunks ]( const glm::ivec3 &cpos ) -> Chunk * {
            for ( const GenChunk &gc : gen_chunks ) {
                if ( gc.pos == cpos ) {
                    return gc.chunk;
                }
            }
            return nullptr;
        };

        for ( int cx = min_chunk.x; cx <= max_chunk.x; cx++ ) {
            for ( int cy = min_chunk.y; cy <= max_chunk.y; cy++ ) {
                for ( int cz = min_chunk.z; cz <= max_chunk.z; cz++ ) {
                    Chunk *chunk = new Chunk( );
                    chunk->chunk_pos = glm::ivec3( cx, cy, cz );
                    chunk->cells = chunk_alloc_cells( );
                    chunk->extras = nullptr;
                    chunk->light.air_mask = nullptr;
                    chunk->light.shade_mask = nullptr;
                    MapGen::load_block_c( chunk );
                    StructureGen::place( *chunk );
                    gen_chunks.push_back( { chunk->chunk_pos, chunk } );
                }
            }
        }

        std::vector<glm::ivec3> chained_tnt;
        std::vector<bool> chained_inverse;

        for ( int dx = -TNT_BLAST_RADIUS; dx <= TNT_BLAST_RADIUS; dx++ ) {
            for ( int dy = -TNT_BLAST_RADIUS; dy <= TNT_BLAST_RADIUS; dy++ ) {
                for ( int dz = -TNT_BLAST_RADIUS; dz <= TNT_BLAST_RADIUS; dz++ ) {
                    if ( dx * dx + dy * dy + dz * dz > TNT_BLAST_RADIUS * TNT_BLAST_RADIUS ) {
                        continue;
                    }
                    const glm::ivec3 pos = this->block_pos + glm::ivec3( dx, dy, dz );
                    const BlockState state = world.get_loaded_block( pos );
                    if ( state.id == LAST_BLOCK_ID ) {
                        continue; // unloaded chunk — leave it alone
                    }
                    const Block *cur_block = block_definition_get_definition( state.id );
                    if ( cur_block->is_tnt ) {
                        // Ignites instead of being overwritten — the cell
                        // still restores below, the launch happens after.
                        chained_tnt.push_back( pos );
                        chained_inverse.push_back( cur_block->is_inverse_tnt );
                    }
                    const glm::ivec3 cpos = glm::floor( glm::vec3( pos ) / CHUNK_SIZE_F );
                    Chunk *gc = find_gen_chunk( cpos );
                    if ( gc == nullptr ) {
                        continue; // shouldn't happen — sphere is covered above
                    }
                    const glm::ivec3 local = pos - cpos * CHUNK_SIZE_I;
                    const BlockState gen_state = gc->get_block( local );
                    if ( BlockStates_equal( state, gen_state ) ) {
                        continue; // already natural
                    }
                    world.set_loaded_block( pos, gen_state );
                    repGameState.multiplayer.set_block( pos, gen_state, NET_BU_FLAG_NO_DEBRIS );
                    queue_neighbor_block_updates( repGameState, pos );
                }
            }
        }

        for ( const GenChunk &gc : gen_chunks ) {
            gc.chunk->free_generated_buffers( );
            delete gc.chunk;
        }

        launch_chained_tnt( repGameState, center, chained_tnt, chained_inverse );
        return;
    }

    std::vector<glm::ivec3> chained_tnt;
    std::vector<bool> chained_inverse;

    // Pass 1: remove every TNT block in the blast sphere up front. They all
    // become primed entities — if they stayed solid while later launches
    // marched their landing paths, an entity could "land" on a TNT block
    // that is itself about to fly away and end up hovering on air.
    for ( int dx = -TNT_BLAST_RADIUS; dx <= TNT_BLAST_RADIUS; dx++ ) {
        for ( int dy = -TNT_BLAST_RADIUS; dy <= TNT_BLAST_RADIUS; dy++ ) {
            for ( int dz = -TNT_BLAST_RADIUS; dz <= TNT_BLAST_RADIUS; dz++ ) {
                if ( dx * dx + dy * dy + dz * dz > TNT_BLAST_RADIUS * TNT_BLAST_RADIUS ) {
                    continue;
                }
                const glm::ivec3 pos = this->block_pos + glm::ivec3( dx, dy, dz );
                const BlockState state = world.get_loaded_block( pos );
                if ( state.id == LAST_BLOCK_ID ) {
                    continue; // unloaded chunk — leave it alone
                }
                if ( block_definition_get_definition( state.id )->is_tnt ) {
                    world.set_loaded_block( pos, BLOCK_STATE_AIR );
                    repGameState.multiplayer.set_block( pos, BLOCK_STATE_AIR, NET_BU_FLAG_NO_DEBRIS );
                    queue_neighbor_block_updates( repGameState, pos );
                    chained_tnt.push_back( pos );
                    chained_inverse.push_back( block_definition_get_definition( state.id )->is_inverse_tnt );
                }
            }
        }
    }

    // Pass 2: destroy the breakable blocks — gravity blocks included; only
    // bodies already in flight get pushed (the impulse after the loop).
    // Unloaded cells, air, and fluids are untouched — water/lava are fluids,
    // not destroyable blocks.
    for ( int dx = -TNT_BLAST_RADIUS; dx <= TNT_BLAST_RADIUS; dx++ ) {
        for ( int dy = -TNT_BLAST_RADIUS; dy <= TNT_BLAST_RADIUS; dy++ ) {
            for ( int dz = -TNT_BLAST_RADIUS; dz <= TNT_BLAST_RADIUS; dz++ ) {
                if ( dx * dx + dy * dy + dz * dz > TNT_BLAST_RADIUS * TNT_BLAST_RADIUS ) {
                    continue;
                }
                const glm::ivec3 pos = this->block_pos + glm::ivec3( dx, dy, dz );
                const BlockState state = world.get_loaded_block( pos );
                if ( state.id == AIR || state.id == LAST_BLOCK_ID || state.id == WATER || state.id == LAVA ) {
                    continue;
                }
                const Block *block = block_definition_get_definition( state.id );
                if ( block->hardness < 0.0f ) {
                    continue; // bedrock, barriers, portal frames survive
                }
                world.set_loaded_block( pos, BLOCK_STATE_AIR );
                repGameState.multiplayer.set_block( pos, BLOCK_STATE_AIR );
                world.spawn_block_debris( pos, state, AIR, DEBRIS_PER_EXPLODED_BLOCK );
                queue_neighbor_block_updates( repGameState, pos );
            }
        }
    }

    // The blast shoves every live body — still-fusing primed TNT and any
    // falling blocks already in flight — and damages/shoves the player.
    world.bodies.impulse( center, TNT_IMPULSE_RADIUS, TNT_IMPULSE_STRENGTH );
    world.items.impulse( center, TNT_IMPULSE_RADIUS, TNT_IMPULSE_STRENGTH );
    repgame_explosion_affect_player( repGameState, center, TNT_IMPULSE_RADIUS );

    // Pass 3: launch the collected TNT. The world now holds only what
    // survives the blast, so the new bodies fly through the final terrain —
    // they can't collide with blocks this explosion removed.
    launch_chained_tnt( repGameState, center, chained_tnt, chained_inverse );
}
