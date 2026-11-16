#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cstring>
#include <limits>
#include <math.h>
#include <mutex>
#include <thread>
#include <vector>

#if defined( _MSC_VER ) && !defined( __clang__ )
#include <intrin.h>
#endif

#include "common/Platforms.hpp" // IWYU pragma: keep — GL headers + platform macros
#include "common/Logging.hpp"
#include "common/light.hpp"
#include "common/light_internal.hpp"
#include "common/utils/env_config.hpp"
#include "common/utils/profile.hpp"
#include "common/chunk_loader.hpp"
#include "common/block_definitions.hpp"
#include "common/dimension_profile.hpp"
#include "common/renderer/texture.hpp"

// ---------------------------------------------------------------------------
// Flood-fill lighting engine.
//

// Chunk::light[] is a u16 per cell over the same 34^3 internal volume as
// cells[]: high byte = skylight, low byte = block (emissive) light, both
// in raw units (LIGHT_RAW_PER_LEVEL=17 per display level, 255 = level 15).
// Canonical values live in each chunk's interior (x,y,z in [0,32)); cells on
// a chunk boundary are mirrored into the 7 neighboring chunks that store the
// cell in their halo, so every chunk's light[] stays self-consistent for
// texture upload without cross-chunk reads.
//
// Propagation is a BFS over world-space cells with two queues:
//  - add:   a cell's stored value is spread to its 18 neighbors at a
//           chamfer cost per step (17/24 for face/edge dirs;
//           skylight flowing straight down at full strength doesn't
//           attenuate).
//  - remove: a cell that lost light clears neighbors that were lit through
//           it; boundary cells still lit from elsewhere are re-enqueued as
//           add seeds (the standard remove-then-readd scheme).
// Opaque (casts_shadow) cells never receive light; emitters hold their
// emission value even when opaque (they spread but don't transmit).
// Absorbing cells (Block::light_absorb > 0, e.g. leaves) DO hold light but
// cost 1 + light_absorb to enter, and still stop the vertical skylight
// fill like opaque cells — canopies stay shaded while light seeps through.
//
// Skylight is resolved lazily: workers can't know whether the chunk above is
// loaded, so light_finalize_chunk runs a per-column cascade using
// column_open / sky_open_above flags once neighbors are known, then a border
// sync pass mirrors neighbor edge values into our halo and seeds spreads
// (and removals where our new content is dimmer than a stale mirror).
// ---------------------------------------------------------------------------

int ChunkLoader::light_emit_at( const Chunk &chunk, const int index ) {
    if ( !chunk.cells ) {
        return 0;
    }
    const BlockCell cell = chunk.cells[ index ];
    return light_emit_level( block_definition_get_definition( block_cell_id( cell ) ), block_cell_powered( cell ) );
}

// Bit i of chunk.light.air_mask mirrors the opacity of cells[i]: set = the
// cell can hold light. light_fill_chunk writes every bit; this restamps one
// cell after a post-fill cells[] write so the upload paths never have to
// touch the cells[] array again. A missing mask is always legal —
// uploads fall back to the cells[] walk.
// The bitmap is z-fastest (cells[] index space), so each x column occupies
// CHUNK_SIZE_INTERNAL_Z/8 bytes — the layout requires Z % 8 == 0.
static_assert( CHUNK_SIZE_INTERNAL_Z % 8 == 0,
               "light_air_mask packs cells[] z-fastest 8/byte; CHUNK_SIZE_INTERNAL_Z must be a multiple of 8" );
void light_air_mask_set( const Chunk &chunk, const int index, const BlockID id ) {
    if ( chunk.light.air_mask ) {
        if ( light_opaque_id( id ) ) {
            chunk.light.air_mask[ index >> 3 ] &= static_cast<unsigned char>( ~( 1u << ( index & 7 ) ) );
        } else {
            chunk.light.air_mask[ index >> 3 ] |= static_cast<unsigned char>( 1u << ( index & 7 ) );
        }
    }
    // Same cell in the shade mask: set iff this is a light floor.
    if ( chunk.light.shade_mask ) {
        if ( block_definition_get_definition( id )->blocks_light_down ) {
            chunk.light.shade_mask[ index >> 3 ] |= static_cast<unsigned char>( 1u << ( index & 7 ) );
        } else {
            chunk.light.shade_mask[ index >> 3 ] &= static_cast<unsigned char>( ~( 1u << ( index & 7 ) ) );
        }
    }
}

size_t ChunkLoader::light_add_queue_size( ) const {
    size_t total = 0;
    for ( int s = 0; s < LIGHT_COL_QUEUE_COUNT; s++ ) {
        for ( int ch = 0; ch < 2; ch++ ) {
            for ( int level = 0; level <= LIGHT_MAX_LEVEL; level++ ) {
                total += this->light_add_queue[ s ][ ch ][ level ].size( );
            }
        }
    }
    return total;
}

// Push gate for both seed paths; nonzero = the seed was refused (counter
// bumped inside). Once the backlog is past STARVE_MIN a seed aimed at a
// missing or still-loading chunk is dropped here instead of after a full
// queue round-trip — a loading chunk's whole light volume is rebuilt by its
// own finalize, so the seed is provably moot. Past LIGHT_BFS_CAP_FLOOD,
// internal pushes (march expansion) are refused and the target chunk flagged
// light_stale — the flood dies mid-frontier instead of regenerating a
// plateau, and the chunk re-derives its light via a re-finalize when the
// queue next goes idle or the chunk re-enters the near radius
// (light_heal_* below). light.reseed is set alongside the flag so the heal
// re-finalize runs light_fill_chunk too — the volume is wiped and emitters
// re-seeded, not just cascaded/border-synced. Without it, cells a truncated
// flood unlit around emitters (or half-spread interiors) stayed dark
// forever: no queued seed ever re-pushes an interior light source. External
// far pushes are refused only past LIGHT_BFS_CAP_ALL; near external seeds
// are never refused.
int ChunkLoader::light_seed_gate( const glm::ivec3 &block_pos, const bool near, const bool internal ) {
    const long long pend = light_bfs_pending.load( std::memory_order_relaxed );
    if ( pend <= LIGHT_BFS_STARVE_MIN ) {
        return 0;
    }
    Chunk *c = this->get_chunk( light_chunk_pos_of( block_pos ) );
    if ( !c || c->load_state ) {
        light_dbg_dropped++;
        light_dbg_push_drops++;
        return 1;
    }
    const bool refuse = internal ? ( pend > LIGHT_BFS_CAP_FLOOD ) : ( !near && pend > LIGHT_BFS_CAP_ALL );
    if ( refuse ) {
        c->light.stale_set( );
        // Set at flag time (not heal time) so ANY finalize landing before
        // the heal scan — e.g. a pending-diff drain — already does the full
        // wipe + emitter refill. Benign racy int write: producers on worker
        // threads store 1 here, the finalize's own store of 0 clears it.
        c->light.reseed = 1;
        light_dbg_capped++;
        return 1;
    }
    return 0;
}

// level is the cell's raw light value at push time; it only determines
// which bucket the seed lands in (buckets are per display level, higher
// first), not what gets propagated.
// The seed routes to its target column's slot; drainers claim the nearest
// non-empty slot, so work for the column under the player drains first no
// matter when it was queued.
void ChunkLoader::light_add_seed( const glm::ivec3 &block_pos, const int channel, const int level, const bool internal ) {
    const bool near = light_near_center( this->chunk_center, block_pos );
    if ( this->light_seed_gate( block_pos, near, internal ) ) {
        return;
    }
    const int clamped = std::clamp( level, 0, LIGHT_RAW_MAX );
    const int bucket = clamped / LIGHT_RAW_PER_LEVEL;
    light_dbg_pushes++;
    const int col = light_col_slot( block_pos );
    {
        LightSeedGuard guard( col );
        this->light_add_queue[ col ][ channel ][ bucket ].push_back( block_pos );
        light_colq_pending[ col ].fetch_add( 1, std::memory_order_relaxed );
    }
    light_col_incident_inc( block_pos );
    light_bfs_pending_inc( );
}

void ChunkLoader::light_push_remove_seed( const glm::ivec3 &block_pos, const int channel, const int old_value, const bool internal ) {
    const bool near = light_near_center( this->chunk_center, block_pos );
    if ( this->light_seed_gate( block_pos, near, internal ) ) {
        return;
    }
    const LightRemoveSeed seed{ block_pos, static_cast<unsigned char>( channel ), static_cast<unsigned char>( old_value ) };
    const int col = light_col_slot( block_pos );
    {
        LightSeedGuard guard( col );
        this->light_remove_queue[ col ].push_back( seed );
        light_colq_pending[ col ].fetch_add( 1, std::memory_order_relaxed );
    }
    light_col_incident_inc( block_pos );
    light_bfs_pending_inc( );
}

// ---------------------------------------------------------------------------
// Cell access
// ---------------------------------------------------------------------------

void ChunkLoader::light_materialize( Chunk &chunk ) {
    if ( chunk.light.volume ) {
        return;
    }
    chunk.light.volume = chunk_alloc_light( );
    std::fill_n( chunk.light.volume, CHUNK_BLOCK_SIZE, chunk.light.uniform );
}

// Read the canonical (interior-owner) light value in raw units
// (LIGHT_RAW_PER_LEVEL per display level). Returns -1 when the cell's owner
// chunk isn't loaded / is still generating — the propagation treats it as
// out-of-bounds. A null light[] volume returns the uniform value instead.
int ChunkLoader::light_get( const glm::ivec3 &block_pos, const int channel ) const {
    const Chunk *chunk = this->get_chunk( light_chunk_pos_of( block_pos ) );
    if ( !chunk || chunk->load_state ) {
        return -1;
    }
    const glm::ivec3 local = block_pos - chunk->chunk_pos * CHUNK_SIZE_I;
    if ( !light_local_ok( local ) ) {
        return -1; // chunk_pos raced with slot reuse; treat as out-of-bounds
    }
    const unsigned short v = light_read_cell( *chunk, light_index( local.x, local.y, local.z ) );
    return channel == LIGHT_CHANNEL_BLOCK ? light_get_block( v ) : light_get_sky( v );
}

BlockID ChunkLoader::light_block_id_at( const glm::ivec3 &block_pos ) const {
    const Chunk *chunk = this->get_chunk( light_chunk_pos_of( block_pos ) );
    if ( !chunk || chunk->load_state ) {
        return LAST_BLOCK_ID;
    }
    const glm::ivec3 local = block_pos - chunk->chunk_pos * CHUNK_SIZE_I;
    if ( !light_local_ok( local ) ) {
        return LAST_BLOCK_ID; // chunk_pos raced with slot reuse
    }
    return chunk->get_block( local ).id;
}

// Same lookup as light_block_id_at but for the cell's powered bit — the
// emission floor of a lit_variant block (the powered lamp) depends on it.
bool ChunkLoader::light_powered_at( const glm::ivec3 &block_pos ) const {
    const Chunk *chunk = this->get_chunk( light_chunk_pos_of( block_pos ) );
    if ( !chunk || chunk->load_state ) {
        return false;
    }
    const glm::ivec3 local = block_pos - chunk->chunk_pos * CHUNK_SIZE_I;
    if ( !light_local_ok( local ) ) {
        return false; // chunk_pos raced with slot reuse
    }
    return chunk->get_block( local ).current_redstone_power != 0;
}

// Trilinear sample of the light volume for non-terrain draws (player avatars,
// block-break debris) — those draw all instances in one call and can't bind a
// per-chunk 3D texture, so they bake this CPU sample into instance data.
// Light cells sit at block centers (integer+0.5), matching the texel-center
// convention the chunk shader samples the GPU volume with. Returns
// normalized (sky, block) in [0,1]. Cells whose owner chunk isn't loaded yet
// read as full sky light — the same optimistic fill light_fill_chunk gives a
// fresh chunk — so e.g. the in-hand block tracks the time of day during
// load-in instead of going black. Corners sitting inside opaque blocks
// (e.g. the ground under a resting item) hold no light, so they're masked
// out of the blend; if every corner is opaque the plain blend is returned.
glm::vec2 ChunkLoader::light_sample( const glm::vec3 &pos ) const {
    const glm::vec3 q = pos - 0.5f;
    const glm::ivec3 base = glm::ivec3( glm::floor( q ) );
    const glm::vec3 f = q - glm::vec3( base );
    glm::vec2 accum( 0.0f );
    glm::vec2 masked( 0.0f );
    float wsum = 0.0f;
    for ( int dz = 0; dz <= 1; dz++ ) {
        for ( int dy = 0; dy <= 1; dy++ ) {
            for ( int dx = 0; dx <= 1; dx++ ) {
                const float w = ( dx ? f.x : 1.0f - f.x ) * ( dy ? f.y : 1.0f - f.y ) * ( dz ? f.z : 1.0f - f.z );
                const glm::ivec3 cell = base + glm::ivec3( dx, dy, dz );
                const int sky = this->light_get( cell, LIGHT_CHANNEL_SKY );
                const int blk = this->light_get( cell, LIGHT_CHANNEL_BLOCK );
                const int sky_default = dimension_has_sky( dimension_of_block_y( cell.y ) ) ? LIGHT_RAW_MAX : 0;
                accum.x += w * static_cast<float>( sky < 0 ? sky_default : sky );
                if ( blk > 0 ) {
                    accum.y += w * static_cast<float>( blk );
                }
                if ( sky < 0 || !light_opaque_id( this->light_block_id_at( cell ) ) ) {
                    wsum += w;
                    masked.x += w * static_cast<float>( sky < 0 ? sky_default : sky );
                    if ( blk > 0 ) {
                        masked.y += w * static_cast<float>( blk );
                    }
                }
            }
        }
    }
    if ( wsum > 1e-4f ) {
        return masked / ( wsum * static_cast<float>( LIGHT_RAW_MAX ) );
    }
    return accum / static_cast<float>( LIGHT_RAW_MAX );
}

void ChunkLoader::light_upload_enqueue( Chunk &chunk ) {
    // Hot path: only the first mark per chunk takes the lock.
    if ( !chunk.light.upload_listed.load( std::memory_order_acquire ) ) {
        const bool near = light_near_col( this->chunk_center, chunk.chunk_pos );
        std::lock_guard<std::mutex> lock( light_work_mutex );
        if ( !chunk.light.upload_listed ) {
            chunk.light.upload_listed = 1;
            ( near ? light_dirty_list_near : light_dirty_list_far ).push_back( &chunk );
        }
    }
}

void ChunkLoader::light_mark_texel( Chunk &chunk, const glm::ivec3 &local ) {
    chunk.light.write_tick.store( light_tick_counter.load( std::memory_order_relaxed ), std::memory_order_relaxed );
    const glm::ivec3 texel = local + glm::ivec3( 1 ); // internal -> texture coords
    // CAS-merge the texel into the dirty box BEFORE the listed check: the
    // render thread clears listed and exchanges the box in the same critical
    // section, so a mark whose CAS misses that exchange is guaranteed to
    // observe listed==0 and re-add the chunk — no lost updates. The CAS runs
    // even when the box already covers the texel: as an acquire RMW it both
    // orders the caller's light[] byte store ahead of the uploader's copy
    // and synchronizes with the exchange so the listed==0 check below is
    // reliable.
    uint64_t box = chunk.light.dirty_box.load( std::memory_order_relaxed );
    for ( ;; ) {
        const uint64_t merged = light_box_merge( box, texel );
        if ( chunk.light.dirty_box.compare_exchange_weak( box, merged, std::memory_order_acq_rel,
                                                          std::memory_order_relaxed ) ) {
            break;
        }
    }
    light_upload_enqueue( chunk );
}

// Texel-space variant of light_mark_texel for slab marks (no field-local
// offset — the box is already in texture coords).
void ChunkLoader::light_mark_texel_box( Chunk &chunk, const glm::ivec3 &lo, const glm::ivec3 &hi ) {
    chunk.light.write_tick.store( light_tick_counter.load( std::memory_order_relaxed ), std::memory_order_relaxed );
    const uint64_t add = light_box_pack( lo, hi );
    uint64_t box = chunk.light.dirty_box.load( std::memory_order_relaxed );
    for ( ;; ) {
        const uint64_t merged = light_box_union( box, add );
        if ( chunk.light.dirty_box.compare_exchange_weak( box, merged, std::memory_order_acq_rel,
                                                          std::memory_order_relaxed ) ) {
            break;
        }
    }
    light_upload_enqueue( chunk );
}

// A chunk's presentation mode flipped (Shadows toggle or smoothing-radius
// crossing): neighbours' halo texels it OWNS still carry the byte its old
// mode produced, but a mode flip isn't a field change so the mirror-mark
// walk never fires. Mark each textured neighbour's ring slabs owned by this
// chunk — the slabs facing it (face neighbour: one 32x32 slab; edge: 32
// texels; corner: one texel). Only textured chunks need marks; the rest
// resolve at their first fill.
void ChunkLoader::light_remark_owned_halos( Chunk &flipped ) {
    const int sz[ 3 ] = { CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z };
    for ( int dz = -1; dz <= 1; dz++ ) {
        for ( int dy = -1; dy <= 1; dy++ ) {
            for ( int dx = -1; dx <= 1; dx++ ) {
                const glm::ivec3 d( dx, dy, dz );
                if ( d == glm::ivec3( 0 ) ) {
                    continue;
                }
                Chunk *n = this->get_chunk( flipped.chunk_pos + d );
                if ( n == nullptr || n->load_state || n->light.texture == 0 ) {
                    continue;
                }
                glm::ivec3 lo, hi;
                for ( int a = 0; a < 3; a++ ) {
                    if ( d[ a ] < 0 ) {
                        lo[ a ] = hi[ a ] = sz[ a ] + 1; // far halo ring
                    } else if ( d[ a ] > 0 ) {
                        lo[ a ] = hi[ a ] = 0; // near halo ring
                    } else {
                        lo[ a ] = 1;
                        hi[ a ] = sz[ a ]; // interior span
                    }
                }
                light_mark_texel_box( *n, lo, hi );
            }
        }
    }
}

void ChunkLoader::light_mark_dirty_now( Chunk &chunk, const glm::ivec3 &local ) {
    light_mark_texel( chunk, local );
    if ( (unsigned)local.x >= (unsigned)CHUNK_SIZE_X || (unsigned)local.y >= (unsigned)CHUNK_SIZE_Y ||
         (unsigned)local.z >= (unsigned)CHUNK_SIZE_Z ) {
        return; // halo mark — the owner-side marks already cover every other storer
    }
    // With smoothing off a texel's uploaded value is exactly its own cell —
    // no neighbour's texels can depend on this change, so skip the walk.
    if ( !light_smooth_enabled( ) ) {
        return;
    }
    // Upload smoothing gives a texel's uploaded value a one-cell footprint,
    // so a change here also invalidates the halo copies of our border cells
    // stored in neighbouring chunks (whose smoothed values reach across the
    // border). light_set's write-marks plus the uploader's +/-1 box
    // expansion only cover chunks that store THIS cell; chase the rest:
    // every other chunk that stores a texel within one cell of the change.
    const glm::ivec3 pos = chunk.chunk_pos * CHUNK_SIZE_I + local;
    for ( int dz = -1; dz <= 1; dz++ ) {
        for ( int dy = -1; dy <= 1; dy++ ) {
            for ( int dx = -1; dx <= 1; dx++ ) {
                if ( dx == 0 && dy == 0 && dz == 0 ) {
                    continue;
                }
                const glm::ivec3 w = pos + glm::ivec3( dx, dy, dz );
                const glm::ivec3 op = light_chunk_pos_of( w );
                // A texel at w only depends on this change when w's OWNER
                // smooths (its uploaded value is a 1-cell footprint). Raw
                // owners' texels are plain copies — the direct marks from
                // light_set's storer walk already cover them.
                if ( !light_smooth_in_radius( this->chunk_center, op ) ) {
                    continue;
                }
                const glm::ivec3 wl = w - op * CHUNK_SIZE_I;
                // W's mirror copies live in the chunks across each axis
                // where W sits on its owner's interior edge; the owner
                // itself is already covered by this change's own marks.
                const int ex = wl.x == 0 ? -1 : wl.x == CHUNK_SIZE_X - 1 ? 1 : 0;
                const int ey = wl.y == 0 ? -1 : wl.y == CHUNK_SIZE_Y - 1 ? 1 : 0;
                const int ez = wl.z == 0 ? -1 : wl.z == CHUNK_SIZE_Z - 1 ? 1 : 0;
                if ( ( ex | ey | ez ) == 0 ) {
                    continue;
                }
                int axis_opts[ 3 ][ 2 ];
                int axis_count[ 3 ] = { 1, 1, 1 };
                axis_opts[ 0 ][ 0 ] = 0;
                axis_opts[ 1 ][ 0 ] = 0;
                axis_opts[ 2 ][ 0 ] = 0;
                if ( ex ) {
                    axis_opts[ 0 ][ axis_count[ 0 ]++ ] = ex;
                }
                if ( ey ) {
                    axis_opts[ 1 ][ axis_count[ 1 ]++ ] = ey;
                }
                if ( ez ) {
                    axis_opts[ 2 ][ axis_count[ 2 ]++ ] = ez;
                }
                for ( int ix = 0; ix < axis_count[ 0 ]; ix++ ) {
                    for ( int iy = 0; iy < axis_count[ 1 ]; iy++ ) {
                        for ( int iz = 0; iz < axis_count[ 2 ]; iz++ ) {
                            const glm::ivec3 off( axis_opts[ 0 ][ ix ], axis_opts[ 1 ][ iy ], axis_opts[ 2 ][ iz ] );
                            if ( off == glm::ivec3( 0 ) || op + off == chunk.chunk_pos ) {
                                continue; // owner / self — covered by this change's own marks
                            }
                            Chunk *c = this->get_chunk( op + off );
                            if ( !c || c->load_state ) {
                                continue;
                            }
                            const glm::ivec3 c_local = w - c->chunk_pos * CHUNK_SIZE_I;
                            if ( !light_local_ok( c_local ) ) {
                                continue; // slot recycled mid-scan; torn chunk_pos
                            }
                            light_mark_texel( *c, c_local );
                        }
                    }
                }
            }
        }
    }
}

void ChunkLoader::light_mark_dirty( Chunk &chunk, const glm::ivec3 &local ) {
    // Stamp BEFORE the defer check: the write is happening now regardless of
    // when its texel mark lands, and the upload-quiet window measures writes.
    chunk.light.write_tick.store( light_tick_counter.load( std::memory_order_relaxed ), std::memory_order_relaxed );
    if ( light_defer_marks.load( std::memory_order_relaxed ) > 0 ) {
        std::lock_guard<std::mutex> lock( light_work_mutex );
        light_deferred_marks.push_back( { &chunk, local } );
        return;
    }
    light_mark_dirty_now( chunk, local );
}

void ChunkLoader::light_flush_deferred_marks( ) {
    std::vector<std::pair<Chunk *, glm::ivec3>> pending;
    {
        std::lock_guard<std::mutex> lock( light_work_mutex );
        pending.swap( light_deferred_marks );
    }
    for ( const auto &e : pending ) {
        // The slot may have recycled between recheck and drain — a stale mark
        // on a recycled chunk is harmless (extra texel region), but skip
        // chunks with no live volume.
        if ( e.first->light.volume && !e.first->load_state ) {
            light_mark_dirty_now( *e.first, e.second );
        }
    }
}

// Whole-volume variant for finalize/reseed paths that rewrite everything.
// Dirty coords are texel-space (internal+1), so the full 34^3 texture is
// 0..CHUNK_SIZE_INTERNAL_-1 inclusive.
void ChunkLoader::light_mark_dirty_all( Chunk &chunk ) {
    chunk.light.write_tick.store( light_tick_counter.load( std::memory_order_relaxed ), std::memory_order_relaxed );
    // The full-volume box is the union lattice's top element — a plain store
    // can't lose a concurrent mark (any merge into it is a no-op).
    chunk.light.dirty_box.store(
        light_box_pack( glm::ivec3( 0 ),
                        glm::ivec3( CHUNK_SIZE_INTERNAL_X - 1, CHUNK_SIZE_INTERNAL_Y - 1, CHUNK_SIZE_INTERNAL_Z - 1 ) ),
        std::memory_order_release );
    light_upload_enqueue( chunk );
}

// The "Shadows" quality toggle flipped: the uploaded copy of every live
// texture must be rebuilt under the new smoothing mode (the canonical
// light[] field is unaffected either way).
void ChunkLoader::light_shadow_refresh_all( ) {
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        Chunk &chunk = this->chunkArray[ i ];
        if ( chunk.load_state == 0 && chunk.light.texture != 0 ) {
            light_mark_dirty_all( chunk );
        }
    }
}

// Write to the cell's interior owner AND to every loaded neighbor whose halo
// mirrors it (up to 8 chunks for a corner cell). Mirrors are copies of the
// canonical value; loading chunks get theirs refreshed at finalize instead.
void ChunkLoader::light_set( const glm::ivec3 &block_pos, const int channel, const int value ) {
    const glm::ivec3 cp = light_chunk_pos_of( block_pos );
    Chunk *owner = this->get_chunk( cp );
    if ( !owner || owner->load_state ) {
        return;
    }
    const glm::ivec3 local = block_pos - cp * CHUNK_SIZE_I;
    int axis_opts[ 3 ][ 2 ];
    int axis_count[ 3 ] = { 1, 1, 1 };
    axis_opts[ 0 ][ 0 ] = 0;
    axis_opts[ 1 ][ 0 ] = 0;
    axis_opts[ 2 ][ 0 ] = 0;
    constexpr int axis_size[ 3 ] = { CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z };
    for ( int axis = 0; axis < 3; axis++ ) {
        if ( local[ axis ] == 0 ) {
            axis_opts[ axis ][ axis_count[ axis ]++ ] = -1;
        } else if ( local[ axis ] == axis_size[ axis ] - 1 ) {
            axis_opts[ axis ][ axis_count[ axis ]++ ] = 1;
        }
    }
    for ( int ix = 0; ix < axis_count[ 0 ]; ix++ ) {
        for ( int iy = 0; iy < axis_count[ 1 ]; iy++ ) {
            for ( int iz = 0; iz < axis_count[ 2 ]; iz++ ) {
                const glm::ivec3 off( axis_opts[ 0 ][ ix ], axis_opts[ 1 ][ iy ], axis_opts[ 2 ][ iz ] );
                Chunk *c = owner;
                if ( off != glm::ivec3( 0 ) ) {
                    c = this->get_chunk( cp + off );
                    if ( !c || c->load_state ) {
                        continue;
                    }
                }
                const glm::ivec3 c_local = block_pos - c->chunk_pos * CHUNK_SIZE_I;
                if ( !light_local_ok( c_local ) ) {
                    continue; // slot recycled mid-write; torn chunk_pos
                }
                const int idx = light_index( c_local.x, c_local.y, c_local.z );
                const unsigned short cell = light_read_cell( *c, idx );
                const unsigned short nv = channel == LIGHT_CHANNEL_BLOCK //
                    ? static_cast<unsigned short>( ( cell & 0xFF00 ) | value )
                    : static_cast<unsigned short>( ( cell & 0x00FF ) | ( value << 8 ) );
                if ( nv != cell ) {
                    light_write_cell( *c, idx, nv );
                    light_mark_dirty( *c, c_local );
                }
            }
        }
    }
}

// light_set variant for hot loops: mirror chunks resolve from a pre-built
// 27-entry neighborhood cache around the owning chunk (index
// (dx+1)*9+(dy+1)*3+(dz+1)) instead of per-mirror get_chunk lookups. All
// mirrors of a cell live in its owner's 3x3x3 neighborhood by construction.
void ChunkLoader::light_set_nb( Chunk *const nb_cache[ 27 ], Chunk &owner, const glm::ivec3 &block_pos, const int channel, const int value ) {
    const glm::ivec3 local = block_pos - owner.chunk_pos * CHUNK_SIZE_I;
    int axis_opts[ 3 ][ 2 ];
    int axis_count[ 3 ] = { 1, 1, 1 };
    axis_opts[ 0 ][ 0 ] = 0;
    axis_opts[ 1 ][ 0 ] = 0;
    axis_opts[ 2 ][ 0 ] = 0;
    constexpr int axis_size[ 3 ] = { CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z };
    for ( int axis = 0; axis < 3; axis++ ) {
        if ( local[ axis ] == 0 ) {
            axis_opts[ axis ][ axis_count[ axis ]++ ] = -1;
        } else if ( local[ axis ] == axis_size[ axis ] - 1 ) {
            axis_opts[ axis ][ axis_count[ axis ]++ ] = 1;
        }
    }
    for ( int ix = 0; ix < axis_count[ 0 ]; ix++ ) {
        for ( int iy = 0; iy < axis_count[ 1 ]; iy++ ) {
            for ( int iz = 0; iz < axis_count[ 2 ]; iz++ ) {
                const int ox = axis_opts[ 0 ][ ix ];
                const int oy = axis_opts[ 1 ][ iy ];
                const int oz = axis_opts[ 2 ][ iz ];
                Chunk *c = ( ox || oy || oz ) ? nb_cache[ ( ox + 1 ) * 9 + ( oy + 1 ) * 3 + ( oz + 1 ) ] : &owner;
                if ( !c || c->load_state ) {
                    continue;
                }
                const glm::ivec3 c_local = block_pos - c->chunk_pos * CHUNK_SIZE_I;
                if ( !light_local_ok( c_local ) ) {
                    continue; // slot recycled mid-write; torn chunk_pos
                }
                const int idx = light_index( c_local.x, c_local.y, c_local.z );
                const unsigned short cell = light_read_cell( *c, idx );
                const unsigned short nv = channel == LIGHT_CHANNEL_BLOCK //
                    ? static_cast<unsigned short>( ( cell & 0xFF00 ) | value )
                    : static_cast<unsigned short>( ( cell & 0x00FF ) | ( value << 8 ) );
                if ( nv != cell ) {
                    light_write_cell( *c, idx, nv );
                    light_mark_dirty( *c, c_local );
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Propagation steps (one queue node per call)
// ---------------------------------------------------------------------------

// Spread pos's value outward. Cells only ever gain light this way; a blocked
// (opaque) target can't be lit and never entered the BFS. The owner chunk is
// resolved once and reused for interior neighbors — the hot loop would
// otherwise do ~15 get_chunk lookups per step.
void ChunkLoader::light_add_step( const LightSeed &seed ) {
    const glm::ivec3 cp = light_chunk_pos_of( seed.pos );
    Chunk *chunk = this->get_chunk( cp );
    if ( !chunk || chunk->load_state ) {
        light_dbg_dropped++;
        return;
    }
    const glm::ivec3 local = seed.pos - cp * CHUNK_SIZE_I;
    const unsigned short cell = light_read_cell( *chunk, light_index( local.x, local.y, local.z ) );
    const int v = seed.channel == LIGHT_CHANNEL_BLOCK ? light_get_block( cell ) : light_get_sky( cell );
    if ( v <= 0 ) {
        return;
    }
    const bool sky = seed.channel == LIGHT_CHANNEL_SKY;
    // Floor cells (blocks_light_down: slabs, path) hold light but never
    // pass it downward — both channels skip every downward-component step
    // from one. A null cells[] volume is all-air: nothing is a floor.
    const BlockID src_id = light_cell_id_at( chunk->cells, local );
    const bool src_blocks_down = block_definition_get_definition( src_id )->blocks_light_down;
    for ( int d = 0; d < LIGHT_STEP_COUNT; d++ ) {
        const LightStepDir &sd = light_step_dirs[ d ];
        if ( src_blocks_down && sd.d.y < 0 ) {
            continue;
        }
        const glm::ivec3 n_local = local + sd.d;
        // Best possible offer this direction (target's absorb can only lower
        // it): cells at or above it can't gain light from this step.
        const int best = ( sky && d == LIGHT_DIR_DOWN && v == LIGHT_RAW_MAX ) ? LIGHT_RAW_MAX : v - sd.cost;
        // Strictly-interior neighbor: same owner, no halo mirrors — direct
        // array ops only, no get_chunk/light_set lookups.
        if ( n_local.x > 0 && n_local.x < CHUNK_SIZE_X - 1 && n_local.y > 0 && n_local.y < CHUNK_SIZE_Y - 1 && n_local.z > 0 && n_local.z < CHUNK_SIZE_Z - 1 ) {
            const int nidx = light_index( n_local.x, n_local.y, n_local.z );
            const unsigned short ncell = light_read_cell( *chunk, nidx );
            const int nv = sky ? light_get_sky( ncell ) : light_get_block( ncell );
            if ( best <= nv ) {
                continue;
            }
            const BlockID n_id = light_cell_id_at( chunk->cells, n_local );
            if ( light_opaque_id( n_id ) ) {
                continue;
            }
            const int offered = light_offered( v, seed.channel, d, n_id );
            if ( offered <= nv ) {
                continue;
            }
            // Diagonal steps need an open shared-face route — a step that
            // could only cut through solid corner cells would leak light
            // the face BFS can't carry.
            if ( sd.ngates && !light_diag_open( chunk->cells, local, sd, src_blocks_down ) ) {
                continue;
            }
            light_write_cell( *chunk, nidx, sky ? static_cast<unsigned short>( ( ncell & 0x00FF ) | ( offered << 8 ) ) : static_cast<unsigned short>( ( ncell & 0xFF00 ) | offered ) );
            light_mark_dirty( *chunk, n_local );
            light_dbg_step_push++;
            this->light_add_seed( seed.pos + sd.d, seed.channel, offered, true );
            continue;
        }
        const glm::ivec3 n = seed.pos + sd.d;
        const int nv = this->light_get( n, seed.channel );
        if ( nv < 0 || best <= nv ) {
            continue;
        }
        const BlockID n_id = this->light_block_id_at( n );
        if ( light_opaque_id( n_id ) ) {
            continue;
        }
        const int offered = light_offered( v, seed.channel, d, n_id );
        if ( offered <= nv ) {
            continue;
        }
        if ( sd.ngates && !light_diag_open( chunk->cells, local, sd, src_blocks_down ) ) {
            continue;
        }
        this->light_set( n, seed.channel, offered );
        light_dbg_step_push++;
        this->light_add_seed( n, seed.channel, offered, true );
    }
}

// pos used to hold old_value (it has since been reset to its new value by the
// caller). Neighbors whose light was fed through pos are cleared back to
// their own emission floor and recursed into; neighbors lit from another path
// become add seeds so the region re-converges.
void ChunkLoader::light_remove_step( const LightRemoveSeed &seed ) {
    const glm::ivec3 cp = light_chunk_pos_of( seed.pos );
    Chunk *chunk = this->get_chunk( cp );
    if ( !chunk || chunk->load_state ) {
        light_dbg_dropped++;
        return;
    }
    const glm::ivec3 local = seed.pos - cp * CHUNK_SIZE_I;
    // No floor/gate checks here, deliberately: this cell's CURRENT block
    // isn't what it was while it held old_value — a fresh slab, say, is a
    // light floor now but its predecessor still fed the cells below. The
    // cascade must clear whatever the old value could have carried; cells
    // cleared too aggressively are re-lit by the add-seed fallback.
    for ( int d = 0; d < LIGHT_STEP_COUNT; d++ ) {
        const LightStepDir &sd = light_step_dirs[ d ];
        const glm::ivec3 n = seed.pos + sd.d;
        const glm::ivec3 n_local = local + sd.d;
        const bool interior = n_local.x >= 0 && n_local.x < CHUNK_SIZE_X && n_local.y >= 0 && n_local.y < CHUNK_SIZE_Y && n_local.z >= 0 && n_local.z < CHUNK_SIZE_Z;
        int nv;
        BlockID n_id;
        if ( interior ) {
            const unsigned short ncell = light_read_cell( *chunk, light_index( n_local.x, n_local.y, n_local.z ) );
            nv = seed.channel == LIGHT_CHANNEL_BLOCK ? light_get_block( ncell ) : light_get_sky( ncell );
            // A null cells[] volume is all-air.
            n_id = chunk->cells ? block_cell_id( chunk->cells[ chunk->get_index_from_coords( n_local.x, n_local.y, n_local.z ) ] ) : AIR;
        } else {
            nv = this->light_get( n, seed.channel );
            n_id = this->light_block_id_at( n );
        }
        if ( nv <= 0 ) {
            continue;
        }
        if ( light_opaque_id( n_id ) ) {
            continue;
        }
        const int expected = light_offered( seed.old_value, seed.channel, d, n_id );
        if ( nv <= expected ) {
            const Block *n_def = block_definition_get_definition( n_id );
            // Powered-aware floor: a lit_variant cell (the powered lamp)
            // emits at its variant's level only while its power bit is set.
            // Fetch the bit lazily — variant blocks are rare, so ordinary
            // cells never pay for the extra state read.
            const bool n_powered = n_def->lit_variant != LAST_BLOCK_ID &&
                                   ( interior ? light_cell_powered_at( chunk->cells, n_local ) : this->light_powered_at( n ) );
            const int floor_v = seed.channel == LIGHT_CHANNEL_BLOCK ? light_emit_level( n_def, n_powered ) * LIGHT_RAW_PER_LEVEL : 0;
            // Strictly interior (no halo mirrors): write the floor directly
            // instead of going through light_set's mirror matrix.
            const bool strict = n_local.x > 0 && n_local.x < CHUNK_SIZE_X - 1 && n_local.y > 0 && n_local.y < CHUNK_SIZE_Y - 1 && n_local.z > 0 && n_local.z < CHUNK_SIZE_Z - 1;
            if ( strict ) {
                const int widx = light_index( n_local.x, n_local.y, n_local.z );
                const unsigned short wcell = light_read_cell( *chunk, widx );
                light_write_cell( *chunk, widx, seed.channel == LIGHT_CHANNEL_BLOCK ? static_cast<unsigned short>( ( wcell & 0xFF00 ) | floor_v )
                                                                                  : static_cast<unsigned short>( ( wcell & 0x00FF ) | ( floor_v << 8 ) ) );
                light_mark_dirty( *chunk, n_local );
            } else {
                this->light_set( n, seed.channel, floor_v );
            }
            light_dbg_remove_seed++;
            this->light_push_remove_seed( n, seed.channel, nv, true );
            if ( floor_v > 0 ) {
                this->light_add_seed( n, seed.channel, floor_v, true );
            }
        } else {
            this->light_add_seed( n, seed.channel, nv, true );
        }
    }
}

// Pop the next seed from `shard`'s queues (caller holds the shard's seed
// lock): 1 = add, 2 = remove, 0 = empty. `level`/`pri` record the origin
// bucket so a lock-contested seed can be re-queued at the back of the same
// bucket (level order is preserved; the seed just skips ahead of the
// contested region until next pass).
int ChunkLoader::light_pop_seed( const int col, LightSeed &add_out, LightRemoveSeed &rem_out, int &level ) {
    // Removes first (clear before re-add), then adds highest-level-first:
    // a cell that pops a high seed can't be offered a better value later
    // (any better offer would route through a higher-level seed that was
    // already processed), so each cell settles once.
    if ( !this->light_remove_queue[ col ].empty( ) ) {
        rem_out = this->light_remove_queue[ col ].front( );
        this->light_remove_queue[ col ].pop_front( );
        return 2;
    }
    for ( level = LIGHT_MAX_LEVEL; level >= 0; level-- ) {
        for ( int ch = 0; ch < 2; ch++ ) {
            SeedFifo<glm::ivec3> &bucket = this->light_add_queue[ col ][ ch ][ level ];
            if ( !bucket.empty( ) ) {
                add_out = LightSeed{ bucket.front( ), static_cast<unsigned char>( ch ) };
                bucket.pop_front( );
                return 1;
            }
        }
    }
    return 0;
}

// Drain column slot `col`'s seed queues for up to budget_us. Caller must
// hold the slot's ownership flag (light_claim_column) — pops and pushes
// take the slot's queue lock, so this is what serializes the deque ops.
void ChunkLoader::light_process_queue( const long long budget_us, const int col ) {
    PROFILE_SCOPE( "light_bfs" );
    const long long start = light_now_us( );
    light_dbg_pq_calls++;
    int iters = 0;
    int contested = 0;
    for ( ;; ) {
        // The budget check runs once per pop — steady_clock is a ~20ns vDSO
        // read and drains pop millions of seeds, so sample it every 64 pops
        // (~100µs of work) instead of every iteration.
        if ( ( ++iters & 0x3F ) == 0 && light_now_us( ) - start >= budget_us ) {
            this->light_flush_deferred_marks( );
            return;
        }
        LightSeed add_seed;
        LightRemoveSeed rem_seed;
        int level = 0;
        int kind;
        {
            LightSeedGuard guard( col );
            kind = this->light_pop_seed( col, add_seed, rem_seed, level );
            if ( kind == 0 ) {
                break;
            }
            light_colq_pending[ col ].fetch_sub( 1, std::memory_order_relaxed );
            light_bfs_pending--;
            light_dbg_pops++;
        }
        const glm::ivec3 &pos = kind == 1 ? add_seed.pos : rem_seed.pos;
        // The seed left the queue: its column's incident count drops. If it
        // is contested and re-enqueues below, that push re-increments.
        light_col_incident_dec( pos );
        int cols[ 9 ];
        const int ncols = light_pop_cols( pos, cols );
        int held = 0;
        for ( ; held < ncols; held++ ) {
            if ( !light_col_lock_spin( cols[ held ] ) ) {
                break;
            }
        }
        if ( held < ncols ) {
            for ( int i = 0; i < held; i++ ) {
                light_col_lock_release( cols[ i ] );
            }
            light_dbg_requeued++;
            // The contested column belongs to a neighbor's footprint (a
            // claim or a drainer sweeping the shared edge). The seed has to
            // live in its own column's queue — incident accounting and the
            // pop's get_chunk check are keyed to the target — so push it to
            // the BACK of its level bucket: it rotates behind this column's
            // other seeds, which likely touch columns the neighbor isn't
            // holding. If the whole column is contested the neighbor is
            // sweeping our shared edge; release the slot early and let a
            // later claim retry once the sweep has moved on.
            {
                LightSeedGuard guard( col );
                if ( kind == 1 ) {
                    this->light_add_queue[ col ][ add_seed.channel ][ level ].push_back( add_seed.pos );
                } else {
                    this->light_remove_queue[ col ].push_back( rem_seed );
                }
                light_colq_pending[ col ].fetch_add( 1, std::memory_order_relaxed );
            }
            light_bfs_pending.fetch_add( 1, std::memory_order_relaxed );
            light_col_incident_inc( pos ); // re-enqueued: pairs the pop's dec
            if ( ++contested > 256 ) {
                break;
            }
            continue;
        }
        contested = 0;
        if ( kind == 1 ) {
            this->light_add_step( add_seed );
        } else {
            this->light_remove_step( rem_seed );
        }
        for ( int i = 0; i < ncols; i++ ) {
            light_col_lock_release( cols[ i ] );
        }
    }
    // Deferred recheck marks ride the end of a drain: the emit cell's mark
    // lands after the spread marks its seeds just wrote, so the source
    // can't upload a frame ahead of its own halo.
    this->light_flush_deferred_marks( );
}

// All-columns variant for single-threaded callers (tests, no-thread
// fallback): drains every column slot up to budget_us each.
void ChunkLoader::light_process_queue( const long long budget_us ) {
    for ( int s = 0; s < LIGHT_COL_QUEUE_COUNT; s++ ) {
        this->light_process_queue( budget_us, s );
    }
}
// ---------------------------------------------------------------------------
// Edits
// ---------------------------------------------------------------------------

// A cell's contents changed (edit, diff, piston...). Reset each channel to
// its floor (emission for block light, 0 for sky), remove what the old value
// fed, and re-pull light in from the 6 neighbors.
void ChunkLoader::light_recheck_block( const glm::ivec3 &block_pos ) {
    Chunk *chunk = this->get_chunk( light_chunk_pos_of( block_pos ) );
    if ( !chunk || chunk->load_state ) {
        return;
    }
    const glm::ivec3 local = block_pos - chunk->chunk_pos * CHUNK_SIZE_I;
    const BlockState st = chunk->get_block( local );
    // LAST_BLOCK_ID means the coords fell outside this chunk's ownership
    // (torn slot), not a null cells[] — null blocks reads back as AIR.
    if ( st.id == LAST_BLOCK_ID ) {
        return;
    }
    const Block *def = block_definition_get_definition( st.id );

    // Defer this edit's dirty marks to the next drain (see light_defer_marks):
    // the emit cell then uploads together with the spread it seeds instead of
    // lighting a frame or more before it.
    const long long pend_before = light_bfs_pending.load( std::memory_order_relaxed );
    light_defer_marks++;

    // Opacity changes can open/close a sky column.
    if ( chunk->light.columns ) {
        unsigned char &col_flag = chunk->light.columns[ LIGHT_COLUMN_INDEX( local.x, local.z ) ];
        const bool was_open = col_flag != 0;
        light_compute_one_column( *chunk, local.x, local.z );
        if ( was_open != ( col_flag != 0 ) ) {
            this->light_cascade_column( block_pos.x, block_pos.z );
            if ( !was_open ) {
                this->light_seed_column_boundary( block_pos.x, block_pos.z );
            }
        }
    }

    // The edited cell's texel must upload even when no light value
    // changed: an edit that only flips the shade bit (e.g. a slab placed
    // on open-sky stone — every surrounding value stays 15) otherwise
    // leaves a stale A in the volume, so no contact shadow appears, or a
    // ghost one stays after a break. Mark the cell in every chunk that
    // holds a mirror of it (1-8); the upload re-reads the
    // already-restamped masks. Cheap: rechecks are per-edit.
    {
        const glm::ivec3 cp = light_chunk_pos_of( block_pos );
        int axis_opts[ 3 ][ 2 ];
        int axis_count[ 3 ] = { 1, 1, 1 };
        constexpr int axis_size[ 3 ] = { CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z };
        for ( int axis = 0; axis < 3; axis++ ) {
            if ( local[ axis ] == 0 ) {
                axis_opts[ axis ][ axis_count[ axis ]++ ] = -1;
            } else if ( local[ axis ] == axis_size[ axis ] - 1 ) {
                axis_opts[ axis ][ axis_count[ axis ]++ ] = 1;
            }
        }
        for ( int ix = 0; ix < axis_count[ 0 ]; ix++ ) {
            for ( int iy = 0; iy < axis_count[ 1 ]; iy++ ) {
                for ( int iz = 0; iz < axis_count[ 2 ]; iz++ ) {
                    const glm::ivec3 off( axis_opts[ 0 ][ ix ], axis_opts[ 1 ][ iy ], axis_opts[ 2 ][ iz ] );
                    Chunk *c = chunk;
                    if ( off != glm::ivec3( 0 ) ) {
                        c = this->get_chunk( cp + off );
                        if ( !c || c->load_state ) {
                            continue;
                        }
                    }
                    const glm::ivec3 c_local = block_pos - c->chunk_pos * CHUNK_SIZE_I;
                    if ( !light_local_ok( c_local ) ) {
                        continue; // slot recycled mid-write; torn chunk_pos
                    }
                    light_mark_dirty( *c, c_local );
                }
            }
        }
    }

    const int emit = light_emit_level( def, st.current_redstone_power != 0 ) * LIGHT_RAW_PER_LEVEL;
    for ( int ch = 0; ch < 2; ch++ ) {
        const int floor_v = ch == LIGHT_CHANNEL_BLOCK ? emit : 0;
        const int old_v = this->light_get( block_pos, ch );
        if ( old_v < 0 ) {
            continue;
        }
        if ( floor_v != old_v ) {
            this->light_set( block_pos, ch, floor_v );
            if ( floor_v < old_v ) {
                light_dbg_remove_seed++;
                this->light_push_remove_seed( block_pos, ch, old_v );
            }
        }
        // Re-pull from neighbors: pushes into this cell whatever they offer,
        // and pushes this cell's value back out. All 18 directions — a
        // diagonal neighbor's offer may be this cell's best in-path.
        for ( const LightStepDir &sd : light_step_dirs ) {
            const int nv = this->light_get( block_pos + sd.d, ch );
            if ( nv > 0 ) {
                light_dbg_recheck_seed++;
                this->light_add_seed( block_pos + sd.d, ch, nv );
            }
        }
        const int self_v = this->light_get( block_pos, ch );
        if ( self_v > 0 ) {
            light_dbg_recheck_seed++;
            this->light_add_seed( block_pos, ch, self_v );
        }
    }

    light_defer_marks--;
    // No seeds -> no drain is coming to flush the deferred marks; do it now.
    // (Pops on other columns can run concurrently now, so pending may have
    // grown for unrelated reasons too — either way a drainer will flush.)
    if ( light_bfs_pending.load( std::memory_order_relaxed ) <= pend_before ) {
        this->light_flush_deferred_marks( );
    }
}

// ---------------------------------------------------------------------------
// Day/night
// ---------------------------------------------------------------------------

float light_daylight_factor( const long world_time ) {
    long t = world_time % DAY_LENGTH_TICKS;
    if ( t < 0 ) {
        t += DAY_LENGTH_TICKS;
    }
    const float frac = static_cast<float>( t ) / static_cast<float>( DAY_LENGTH_TICKS );
    // frac 0 = sunrise, 0.25 = noon, 0.5 = sunset, 0.75 = midnight.
    const float sun = sinf( 2.0f * static_cast<float>( M_PI ) * frac );
    // Broad daylight plateau, smooth dawn/dusk, down to 0 at midnight. The
    // actual floor lives in the caller: RepGame.cpp max()es this with
    // night_ambient (moonlight) and the shaders floor everything with
    // u_MinAmbient (caves). A baked floor here would beat both.
    return glm::clamp( ( sun + 0.06f ) * 4.0f, 0.0f, 1.0f );
}

