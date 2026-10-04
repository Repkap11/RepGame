#include <chrono>
#include <cstring>
#include <math.h>
#include <vector>

#include "common/RepGame.hpp" // GL headers + platform macros
#include "common/light.hpp"
#include "common/chunk_loader.hpp"
#include "common/block_definitions.hpp"
#include "common/renderer/texture.hpp"

// ---------------------------------------------------------------------------
// Flood-fill lighting engine.
//
// Chunk::light[] is a byte per cell over the same 34^3 internal volume as
// blocks[]: high nibble = skylight, low nibble = block (emissive) light.
// Canonical values live in each chunk's interior (x,y,z in [0,32)); cells on
// a chunk boundary are mirrored into the 7 neighboring chunks that store the
// cell in their halo, so every chunk's light[] stays self-consistent for
// texture upload without cross-chunk reads.
//
// Propagation is a BFS over world-space cells with two queues:
//  - add:   a cell's stored value is spread to neighbors at -1 per step
//           (skylight flowing straight down at 15 doesn't attenuate).
//  - remove: a cell that lost light clears neighbors that were lit through
//           it; boundary cells still lit from elsewhere are re-enqueued as
//           add seeds (the standard remove-then-readd scheme).
// Opaque (casts_shadow) cells never receive light; emitters hold their
// emission value even when opaque (they spread but don't transmit).
//
// Skylight is resolved lazily: workers can't know whether the chunk above is
// loaded, so light_finalize_chunk runs a per-column cascade using
// column_open / sky_open_above flags once neighbors are known, then a border
// sync pass mirrors neighbor edge values into our halo and seeds spreads
// (and removals where our new content is dimmer than a stale mirror).
// ---------------------------------------------------------------------------

static constexpr glm::ivec3 light_dirs[ 6 ] = {
    glm::ivec3( 1, 0, 0 ),  // +x
    glm::ivec3( -1, 0, 0 ), // -x
    glm::ivec3( 0, 1, 0 ),  // +y (up)
    glm::ivec3( 0, -1, 0 ), // -y (down)
    glm::ivec3( 0, 0, 1 ),  // +z
    glm::ivec3( 0, 0, -1 ), // -z
};
static constexpr int LIGHT_DIR_DOWN = 3;

static inline long long light_now_us( ) {
    return std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now( ).time_since_epoch( ) ).count( );
}

static inline glm::ivec3 light_chunk_pos_of( const glm::ivec3 &block_pos ) {
    return glm::ivec3( glm::floor( glm::vec3( block_pos ) / CHUNK_SIZE_F ) );
}

int ChunkLoader::light_emit_at( const Chunk &chunk, const int index ) {
    if ( !chunk.blocks ) {
        return 0;
    }
    return block_definition_get_definition( chunk.blocks[ index ].id )->emits_light;
}

static inline bool light_opaque_id( BlockID id ) {
    if ( id == LAST_BLOCK_ID ) {
        return true;
    }
    return block_definition_get_definition( id )->casts_shadow;
}

// TEMP diagnostics: which producer feeds the queue.
static long long light_dbg_pushes = 0, light_dbg_pops = 0, light_dbg_finalize = 0,
                 light_dbg_cascade_seed = 0, light_dbg_border_seed = 0,
                 light_dbg_recheck_seed = 0, light_dbg_step_push = 0, light_dbg_remove_seed = 0;
void ChunkLoader::light_dbg_stats( char *buf, size_t n ) {
    snprintf( buf, n, "push=%lld pop=%lld fin=%lld casc=%lld bord=%lld rech=%lld step=%lld rem=%lld",
              light_dbg_pushes, light_dbg_pops, light_dbg_finalize, light_dbg_cascade_seed,
              light_dbg_border_seed, light_dbg_recheck_seed, light_dbg_step_push, light_dbg_remove_seed );
    light_dbg_pushes = light_dbg_pops = light_dbg_finalize = light_dbg_cascade_seed =
        light_dbg_border_seed = light_dbg_recheck_seed = light_dbg_step_push = light_dbg_remove_seed = 0;
}

size_t ChunkLoader::light_add_queue_size( ) const {
    size_t total = 0;
    for ( int ch = 0; ch < 2; ch++ ) {
        for ( int level = 0; level <= LIGHT_MAX_LEVEL; level++ ) {
            total += this->light_add_queue[ ch ][ level ].size( );
        }
    }
    return total;
}

// level is the light value the cell holds at push time; it only determines
// which bucket the seed lands in (higher first), not what gets propagated.
void ChunkLoader::light_add_seed( const glm::ivec3 &block_pos, const int channel, const int level ) {
    const int clamped = level < 0 ? 0 : level > LIGHT_MAX_LEVEL ? LIGHT_MAX_LEVEL : level;
    light_dbg_pushes++;
    this->light_add_queue[ channel ][ clamped ].push_back( { block_pos, static_cast<unsigned char>( channel ) } );
}

// ---------------------------------------------------------------------------
// Cell access
// ---------------------------------------------------------------------------

// Read the canonical (interior-owner) light value. Returns -1 when the cell's
// owner chunk isn't loaded / is still generating / has no light volume — the
// propagation treats it as out-of-bounds.
int ChunkLoader::light_get( const glm::ivec3 &block_pos, const int channel ) const {
    const Chunk *chunk = this->get_chunk( light_chunk_pos_of( block_pos ) );
    if ( !chunk || chunk->is_loading || !chunk->light ) {
        return -1;
    }
    const glm::ivec3 local = block_pos - chunk->chunk_pos * CHUNK_SIZE_I;
    const unsigned char v = chunk->light[ light_index( local.x, local.y, local.z ) ];
    return channel == LIGHT_CHANNEL_BLOCK ? light_get_block( v ) : light_get_sky( v );
}

BlockID ChunkLoader::light_block_id_at( const glm::ivec3 &block_pos ) const {
    const Chunk *chunk = this->get_chunk( light_chunk_pos_of( block_pos ) );
    if ( !chunk || chunk->is_loading || !chunk->blocks ) {
        return LAST_BLOCK_ID;
    }
    return chunk->get_block( block_pos - chunk->chunk_pos * CHUNK_SIZE_I ).id;
}

void ChunkLoader::light_mark_dirty( Chunk &chunk, const glm::ivec3 &local ) {
    const glm::ivec3 texel = local + glm::ivec3( 1 ); // internal -> texture coords
    if ( !chunk.light_dirty ) {
        chunk.light_dirty = 1;
        chunk.light_dirty_min = texel;
        chunk.light_dirty_max = texel;
        return;
    }
    chunk.light_dirty_min = glm::min( chunk.light_dirty_min, texel );
    chunk.light_dirty_max = glm::max( chunk.light_dirty_max, texel );
}

// Write to the cell's interior owner AND to every loaded neighbor whose halo
// mirrors it (up to 8 chunks for a corner cell). Mirrors are copies of the
// canonical value; loading chunks get theirs refreshed at finalize instead.
void ChunkLoader::light_set( const glm::ivec3 &block_pos, const int channel, const int value ) {
    const glm::ivec3 cp = light_chunk_pos_of( block_pos );
    Chunk *owner = this->get_chunk( cp );
    if ( !owner || owner->is_loading || !owner->light ) {
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
                    if ( !c || c->is_loading || !c->light ) {
                        continue;
                    }
                }
                const glm::ivec3 c_local = block_pos - c->chunk_pos * CHUNK_SIZE_I;
                const int idx = light_index( c_local.x, c_local.y, c_local.z );
                unsigned char &cell = c->light[ idx ];
                const unsigned char nv = channel == LIGHT_CHANNEL_BLOCK //
                    ? static_cast<unsigned char>( ( cell & 0xF0 ) | value )
                    : static_cast<unsigned char>( ( cell & 0x0F ) | ( value << 4 ) );
                if ( nv != cell ) {
                    cell = nv;
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
    if ( !chunk || chunk->is_loading || !chunk->light || !chunk->blocks ) {
        return;
    }
    const glm::ivec3 local = seed.pos - cp * CHUNK_SIZE_I;
    const unsigned char cell = chunk->light[ light_index( local.x, local.y, local.z ) ];
    const int v = seed.channel == LIGHT_CHANNEL_BLOCK ? light_get_block( cell ) : light_get_sky( cell );
    if ( v <= 0 ) {
        return;
    }
    const bool sky = seed.channel == LIGHT_CHANNEL_SKY;
    for ( int d = 0; d < 6; d++ ) {
        const glm::ivec3 n_local = local + light_dirs[ d ];
        const int offered = ( sky && d == LIGHT_DIR_DOWN && v == LIGHT_MAX_LEVEL ) ? LIGHT_MAX_LEVEL : v - 1;
        // Strictly-interior neighbor: same owner, no halo mirrors — direct
        // array ops only, no get_chunk/light_set lookups.
        if ( n_local.x > 0 && n_local.x < CHUNK_SIZE_X - 1 && n_local.y > 0 && n_local.y < CHUNK_SIZE_Y - 1 && n_local.z > 0 && n_local.z < CHUNK_SIZE_Z - 1 ) {
            const int nidx = light_index( n_local.x, n_local.y, n_local.z );
            unsigned char &ncell = chunk->light[ nidx ];
            const int nv = sky ? light_get_sky( ncell ) : light_get_block( ncell );
            if ( offered <= nv ) {
                continue;
            }
            if ( light_opaque_id( chunk->blocks[ chunk->get_index_from_coords( n_local.x, n_local.y, n_local.z ) ].id ) ) {
                continue;
            }
            ncell = sky ? static_cast<unsigned char>( ( ncell & 0x0F ) | ( offered << 4 ) ) : static_cast<unsigned char>( ( ncell & 0xF0 ) | offered );
            light_mark_dirty( *chunk, n_local );
            light_dbg_step_push++;
            this->light_add_seed( seed.pos + light_dirs[ d ], seed.channel, offered );
            continue;
        }
        const glm::ivec3 n = seed.pos + light_dirs[ d ];
        const int nv = this->light_get( n, seed.channel );
        if ( nv < 0 || offered <= nv ) {
            continue;
        }
        if ( light_opaque_id( this->light_block_id_at( n ) ) ) {
            continue;
        }
        this->light_set( n, seed.channel, offered );
        light_dbg_step_push++;
        this->light_add_seed( n, seed.channel, offered );
    }
}

// pos used to hold old_value (it has since been reset to its new value by the
// caller). Neighbors whose light was fed through pos are cleared back to
// their own emission floor and recursed into; neighbors lit from another path
// become add seeds so the region re-converges.
void ChunkLoader::light_remove_step( const LightRemoveSeed &seed ) {
    const glm::ivec3 cp = light_chunk_pos_of( seed.pos );
    Chunk *chunk = this->get_chunk( cp );
    if ( !chunk || chunk->is_loading || !chunk->light || !chunk->blocks ) {
        return;
    }
    const glm::ivec3 local = seed.pos - cp * CHUNK_SIZE_I;
    for ( int d = 0; d < 6; d++ ) {
        const glm::ivec3 n = seed.pos + light_dirs[ d ];
        const glm::ivec3 n_local = local + light_dirs[ d ];
        const bool interior = n_local.x >= 0 && n_local.x < CHUNK_SIZE_X && n_local.y >= 0 && n_local.y < CHUNK_SIZE_Y && n_local.z >= 0 && n_local.z < CHUNK_SIZE_Z;
        int nv;
        BlockID n_id;
        if ( interior ) {
            const unsigned char ncell = chunk->light[ light_index( n_local.x, n_local.y, n_local.z ) ];
            nv = seed.channel == LIGHT_CHANNEL_BLOCK ? light_get_block( ncell ) : light_get_sky( ncell );
            n_id = chunk->blocks[ chunk->get_index_from_coords( n_local.x, n_local.y, n_local.z ) ].id;
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
        const int expected = ( seed.channel == LIGHT_CHANNEL_SKY && d == LIGHT_DIR_DOWN && seed.old_value == LIGHT_MAX_LEVEL ) ? LIGHT_MAX_LEVEL : seed.old_value - 1;
        if ( nv <= expected ) {
            const int floor_v = seed.channel == LIGHT_CHANNEL_BLOCK ? block_definition_get_definition( n_id )->emits_light : 0;
            // Strictly interior (no halo mirrors): write the floor directly
            // instead of going through light_set's mirror matrix.
            const bool strict = n_local.x > 0 && n_local.x < CHUNK_SIZE_X - 1 && n_local.y > 0 && n_local.y < CHUNK_SIZE_Y - 1 && n_local.z > 0 && n_local.z < CHUNK_SIZE_Z - 1;
            if ( strict ) {
                unsigned char &wcell = chunk->light[ light_index( n_local.x, n_local.y, n_local.z ) ];
                wcell = seed.channel == LIGHT_CHANNEL_BLOCK ? static_cast<unsigned char>( ( wcell & 0xF0 ) | floor_v )
                                                            : static_cast<unsigned char>( ( wcell & 0x0F ) | ( floor_v << 4 ) );
                light_mark_dirty( *chunk, n_local );
            } else {
                this->light_set( n, seed.channel, floor_v );
            }
            light_dbg_remove_seed++;
            this->light_remove_queue.push_back( { n, seed.channel, static_cast<unsigned char>( nv ) } );
            if ( floor_v > 0 ) {
                this->light_add_seed( n, seed.channel, floor_v );
            }
        } else {
            this->light_add_seed( n, seed.channel, nv );
        }
    }
}

void ChunkLoader::light_process_queue( const long long budget_us ) {
    const long long start = light_now_us( );
    for ( ;; ) {
        if ( light_now_us( ) - start >= budget_us ) {
            return;
        }
        if ( !this->light_remove_queue.empty( ) ) {
            const LightRemoveSeed seed = this->light_remove_queue.front( );
            this->light_remove_queue.pop_front( );
            light_dbg_pops++;
            this->light_remove_step( seed );
            continue;
        }
        // Highest-level-first: a cell that pops here can't be offered a better
        // value later (any better offer would route through a higher-level
        // seed that was already processed), so each cell settles once.
        bool did_work = false;
        for ( int level = LIGHT_MAX_LEVEL; level >= 0 && !did_work; level-- ) {
            for ( int ch = 0; ch < 2; ch++ ) {
                std::deque<LightSeed> &bucket = this->light_add_queue[ ch ][ level ];
                if ( !bucket.empty( ) ) {
                    const LightSeed seed = bucket.front( );
                    bucket.pop_front( );
                    light_dbg_pops++;
                    this->light_add_step( seed );
                    did_work = true;
                    break;
                }
            }
        }
        if ( !did_work ) {
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Columns / skylight
// ---------------------------------------------------------------------------

void ChunkLoader::light_ensure_columns( Chunk &chunk ) {
    if ( !chunk.light_columns ) {
        chunk.light_columns = static_cast<unsigned char *>( calloc( 2 * LIGHT_FLAGS_COUNT, 1 ) );
        light_compute_column_open( chunk );
    }
}

// column_open[x][z] == 1 iff no casts_shadow cell exists anywhere in this
// chunk's interior column. Called by the worker in load_terrain and lazily by
// light_ensure_columns on the render thread.
void ChunkLoader::light_compute_column_open( Chunk &chunk ) {
    if ( !chunk.light_columns || !chunk.blocks ) {
        return;
    }
    for ( int x = 0; x < CHUNK_SIZE_X; x++ ) {
        for ( int z = 0; z < CHUNK_SIZE_Z; z++ ) {
            bool open = true;
            for ( int y = 0; y < CHUNK_SIZE_Y; y++ ) {
                const int idx = chunk.get_index_from_coords( x, y, z );
                if ( light_opaque_id( chunk.blocks[ idx ].id ) ) {
                    open = false;
                    break;
                }
            }
            chunk.light_columns[ LIGHT_COLUMN_INDEX( x, z ) ] = open;
        }
    }
}

// Walk a world-space column top-down through the loaded grid, maintaining
// sky_open_above = (everything above is open). On a flag flip the column's
// sky light is (re)seeded or removed from its top cell; propagation handles
// the rest of the descent, including into the chunk below.
void ChunkLoader::light_cascade_column( const int world_x, const int world_z ) {
    const int cx = static_cast<int>( floorf( world_x / CHUNK_SIZE_F.x ) );
    const int cz = static_cast<int>( floorf( world_z / CHUNK_SIZE_F.z ) );
    const int lx = world_x - cx * CHUNK_SIZE_X;
    const int lz = world_z - cz * CHUNK_SIZE_Z;
    int open = 1; // Nothing above the top of the loaded grid is sky.
    for ( int cy = this->chunk_center.y + CHUNK_RADIUS_Y; cy >= this->chunk_center.y - CHUNK_RADIUS_Y; cy-- ) {
        Chunk *chunk = this->get_chunk( glm::ivec3( cx, cy, cz ) );
        if ( !chunk || chunk->is_loading || !chunk->light ) {
            // Unloaded cells are unknown — keep the previous sky_open_above
            // state below them. When the chunk finishes, its own finalize
            // cascade re-derives the flag through this column.
            continue;
        }
        this->light_ensure_columns( *chunk );
        unsigned char &above_flag = chunk->light_columns[ LIGHT_FLAGS_COUNT + LIGHT_COLUMN_INDEX( lx, lz ) ];
        if ( above_flag != open ) {
            above_flag = open;
            const int top_y = cy * CHUNK_SIZE_Y + CHUNK_SIZE_Y - 1;
            if ( open ) {
                // Sky flows straight down at full strength, so write the
                // descent directly instead of seeding one BFS flood per
                // column — flooding every column of every loaded chunk made
                // the queue explode into millions of steps during load-in.
                // Lateral seeding (into caves/overhang pockets) is deferred to
                // the post-cascade boundary scan in light_finalize_chunk:
                // probing here would race the column processing order, since
                // not-yet-cascaded neighbors still read 0.
                for ( int y = CHUNK_SIZE_Y - 1; y >= 0; y-- ) {
                    const glm::ivec3 pos( world_x, cy * CHUNK_SIZE_Y + y, world_z );
                    if ( light_opaque_id( chunk->blocks[ chunk->get_index_from_coords( lx, y, lz ) ].id ) ) {
                        break;
                    }
                    this->light_set( pos, LIGHT_CHANNEL_SKY, LIGHT_MAX_LEVEL );
                }
            } else {
                // Scan down for the first lit cell; the removal cascade clears
                // the contiguous column it used to feed. The seed cell itself
                // must be reset first — light_remove_step only rechecks its
                // neighbors and assumes pos already holds its new value.
                for ( int y = top_y; y >= cy * CHUNK_SIZE_Y; y-- ) {
                    const glm::ivec3 pos( world_x, y, world_z );
                    const int v = this->light_get( pos, LIGHT_CHANNEL_SKY );
                    if ( v > 0 ) {
                        this->light_set( pos, LIGHT_CHANNEL_SKY, 0 );
                        light_dbg_remove_seed++;
                        this->light_remove_queue.push_back( { pos, LIGHT_CHANNEL_SKY, static_cast<unsigned char>( v ) } );
                        break;
                    }
                }
            }
        }
        open = open && chunk->light_columns[ LIGHT_COLUMN_INDEX( lx, lz ) ];
    }
}

// Bulk version of light_cascade_column for light_finalize_chunk: resolves the
// vertical chunk stack once (instead of 1024 columns x ~17 get_chunk calls)
// and writes the sky descent directly into light[] — light_set's mirror
// matrix only runs for cells that actually have mirrors (the y-shell, plus
// columns on the x/z shell). Per-column ordering is identical to the
// single-column version: each column's flag/fill sequence only depends on
// chunks above it, so interleaving columns across cy iterations is safe.
void ChunkLoader::light_cascade_columns( Chunk &start_chunk ) {
    constexpr int N_LEVELS = 2 * CHUNK_RADIUS_Y + 1;
    Chunk *stack[ N_LEVELS ];
    const int cx = start_chunk.chunk_pos.x;
    const int cz = start_chunk.chunk_pos.z;
    for ( int i = 0; i < N_LEVELS; i++ ) {
        stack[ i ] = this->get_chunk( glm::ivec3( cx, this->chunk_center.y + CHUNK_RADIUS_Y - i, cz ) );
    }
    // open[col] = every loaded chunk above has column_open for this column.
    unsigned char open[ LIGHT_FLAGS_COUNT ];
    memset( open, 1, sizeof( open ) );
    for ( int i = 0; i < N_LEVELS; i++ ) {
        Chunk *chunk = stack[ i ];
        if ( !chunk || chunk->is_loading || !chunk->light ) {
            // Unloaded cells are unknown — keep open[] as-is for chunks below;
            // the missing chunk's own finalize cascade re-derives the flags.
            continue;
        }
        this->light_ensure_columns( *chunk );
        unsigned char *flags = chunk->light_columns;
        const int base_y = chunk->chunk_pos.y * CHUNK_SIZE_Y;
        const int base_x = chunk->chunk_pos.x * CHUNK_SIZE_X;
        const int base_z = chunk->chunk_pos.z * CHUNK_SIZE_Z;
        for ( int x = 0; x < CHUNK_SIZE_X; x++ ) {
            for ( int z = 0; z < CHUNK_SIZE_Z; z++ ) {
                const int ci = LIGHT_COLUMN_INDEX( x, z );
                const int was_open = open[ ci ];
                unsigned char &above_flag = flags[ LIGHT_FLAGS_COUNT + ci ];
                if ( above_flag != was_open ) {
                    above_flag = static_cast<unsigned char>( was_open );
                    if ( was_open ) {
                        // Sky flows straight down at full strength — write the
                        // descent directly instead of seeding BFS floods (see
                        // light_cascade_column for the ordering rationale).
                        const bool col_interior = x > 0 && x < CHUNK_SIZE_X - 1 && z > 0 && z < CHUNK_SIZE_Z - 1;
                        for ( int y = CHUNK_SIZE_Y - 1; y >= 0; y-- ) {
                            if ( light_opaque_id( chunk->blocks[ chunk->get_index_from_coords( x, y, z ) ].id ) ) {
                                break;
                            }
                            if ( col_interior && y > 0 && y < CHUNK_SIZE_Y - 1 ) {
                                unsigned char &cell = chunk->light[ light_index( x, y, z ) ];
                                cell = static_cast<unsigned char>( ( cell & 0x0F ) | ( LIGHT_MAX_LEVEL << 4 ) );
                                light_mark_dirty( *chunk, glm::ivec3( x, y, z ) );
                            } else {
                                // y-shell cells mirror into the chunks
                                // above/below; x/z shell cells mirror
                                // sideways. Those need light_set.
                                this->light_set( glm::ivec3( base_x + x, base_y + y, base_z + z ), LIGHT_CHANNEL_SKY, LIGHT_MAX_LEVEL );
                            }
                        }
                    } else {
                        // Flag flipped closed: find the first lit cell and
                        // seed a removal (light_remove_step expects pos's new
                        // value already written, so clear it first).
                        for ( int y = CHUNK_SIZE_Y - 1; y >= 0; y-- ) {
                            const glm::ivec3 pos( base_x + x, base_y + y, base_z + z );
                            const int v = this->light_get( pos, LIGHT_CHANNEL_SKY );
                            if ( v > 0 ) {
                                this->light_set( pos, LIGHT_CHANNEL_SKY, 0 );
                                light_dbg_remove_seed++;
                                this->light_remove_queue.push_back( { pos, LIGHT_CHANNEL_SKY, static_cast<unsigned char>( v ) } );
                                break;
                            }
                        }
                    }
                }
                open[ ci ] = static_cast<unsigned char>( was_open && flags[ ci ] );
            }
        }
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
    if ( !chunk || chunk->is_loading || !chunk->light ) {
        return;
    }
    const glm::ivec3 local = block_pos - chunk->chunk_pos * CHUNK_SIZE_I;
    const BlockState st = chunk->get_block( local );
    if ( st.id == LAST_BLOCK_ID ) {
        return;
    }
    const Block *def = block_definition_get_definition( st.id );

    // Opacity changes can open/close a sky column.
    if ( chunk->light_columns ) {
        bool now_open = true;
        for ( int y = 0; y < CHUNK_SIZE_Y; y++ ) {
            if ( light_opaque_id( chunk->blocks[ Chunk::get_index_from_coords( local.x, y, local.z ) ].id ) ) {
                now_open = false;
                break;
            }
        }
        unsigned char &col_flag = chunk->light_columns[ LIGHT_COLUMN_INDEX( local.x, local.z ) ];
        if ( ( col_flag != 0 ) != now_open ) {
            col_flag = now_open;
            this->light_cascade_column( block_pos.x, block_pos.z );
            if ( now_open ) {
                this->light_seed_column_boundary( block_pos.x, block_pos.z );
            }
        }
    }

    const int emit = def->emits_light;
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
                this->light_remove_queue.push_back( { block_pos, static_cast<unsigned char>( ch ), static_cast<unsigned char>( old_v ) } );
            }
        }
        // Re-pull from neighbors: pushes into this cell whatever they offer,
        // and pushes this cell's value back out.
        for ( const glm::ivec3 &d : light_dirs ) {
            const int nv = this->light_get( block_pos + d, ch );
            if ( nv > 0 ) {
                light_dbg_recheck_seed++;
                this->light_add_seed( block_pos + d, ch, nv );
            }
        }
        const int self_v = this->light_get( block_pos, ch );
        if ( self_v > 0 ) {
            light_dbg_recheck_seed++;
            this->light_add_seed( block_pos, ch, self_v );
        }
    }
}

// ---------------------------------------------------------------------------
// Chunk finalize / border sync
// ---------------------------------------------------------------------------

// After a chunk's blocks are final (terrain + pending diffs applied):
//  - drain recorded per-cell diffs into rechecks,
//  - resolve skylight column-by-column (cascade touches neighbors too),
//  - sync border mirrors with neighbors and seed spreads/removals.
void ChunkLoader::light_finalize_chunk( Chunk &chunk ) {
    if ( !chunk.light ) {
        return;
    }
    light_dbg_finalize++;
    if ( chunk.light_reseed ) {
        light_fill_chunk( chunk ); // refill emitters + column_open
        chunk.light_reseed = 0;
    }
    // Convert pending diff indices into rechecks (world positions).
    for ( int i = 0; i < chunk.light_pending_count; i++ ) {
        int x, y, z;
        chunk.get_coords_from_index( chunk.light_pending[ i ], x, y, z );
        this->light_recheck_block( chunk.chunk_pos * CHUNK_SIZE_I + glm::ivec3( x, y, z ) );
    }
    chunk.light_pending_count = 0;

    this->light_cascade_columns( chunk );
    this->light_seed_interior_boundary( chunk );
    this->light_border_sync( chunk );

    // Whole volume needs uploading on first draw.
    chunk.light_dirty = 1;
    chunk.light_dirty_min = glm::ivec3( 0 );
    chunk.light_dirty_max = glm::ivec3( CHUNK_SIZE_INTERNAL_X - 1, CHUNK_SIZE_INTERNAL_Y - 1, CHUNK_SIZE_INTERNAL_Z - 1 );
}

// The direct-fill cascade writes skylight without BFS, so anything it borders
// that can't self-fill needs seeds: cave/overhang pockets inside the chunk and
// under-lit cells in neighboring chunks. Runs AFTER all columns cascaded —
// probing during the fill would race the column processing order (neighbors
// not yet cascaded still read 0, which is what produced millions of wasted
// seeds during load-in). Pass 1 of light_border_sync covers the opposite
// direction (neighbors' lit cells spreading into our under-lit interior).
//
// Sky channel only: the worker's light_fill_chunk already BFS'd block light
// over this whole volume, so no interior block-lit cell can border under-lit
// non-opaque space — cross-chunk block spreads are seeded by border sync.
void ChunkLoader::light_seed_interior_boundary( Chunk &chunk ) {
    const glm::ivec3 base = chunk.chunk_pos * CHUNK_SIZE_I;
    for ( int x = 0; x < CHUNK_SIZE_X; x++ ) {
        for ( int y = 0; y < CHUNK_SIZE_Y; y++ ) {
            for ( int z = 0; z < CHUNK_SIZE_Z; z++ ) {
                const int cv = light_get_sky( chunk.light[ light_index( x, y, z ) ] );
                if ( cv <= 1 ) {
                    continue; // offered would be <= 0 — can't feed anything
                }
                for ( int d = 0; d < 6; d++ ) {
                    const glm::ivec3 n_local = glm::ivec3( x, y, z ) + light_dirs[ d ];
                    const int offered = ( d == LIGHT_DIR_DOWN && cv == LIGHT_MAX_LEVEL ) ? LIGHT_MAX_LEVEL : cv - 1;
                    int nv;
                    BlockID n_id;
                    if ( n_local.x >= 0 && n_local.x < CHUNK_SIZE_X && n_local.y >= 0 && n_local.y < CHUNK_SIZE_Y && n_local.z >= 0 && n_local.z < CHUNK_SIZE_Z ) {
                        nv = light_get_sky( chunk.light[ light_index( n_local.x, n_local.y, n_local.z ) ] );
                        n_id = chunk.blocks[ chunk.get_index_from_coords( n_local.x, n_local.y, n_local.z ) ].id;
                    } else {
                        const glm::ivec3 n = base + glm::ivec3( x, y, z ) + light_dirs[ d ];
                        nv = this->light_get( n, LIGHT_CHANNEL_SKY );
                        n_id = this->light_block_id_at( n );
                    }
                    if ( nv >= 0 && nv < offered && !light_opaque_id( n_id ) ) {
                        light_dbg_cascade_seed++;
                        this->light_add_seed( base + glm::ivec3( x, y, z ), LIGHT_CHANNEL_SKY, cv );
                        break;
                    }
                }
            }
        }
    }
}

// Recheck-triggered cascades (a block edit opening/closing a sky column) get
// no whole-volume scan — walk just this column's lit cells and seed wherever
// they border under-lit space, so breaking a skylight open into a cave
// actually floods the cave.
void ChunkLoader::light_seed_column_boundary( const int world_x, const int world_z ) {
    const int cx = static_cast<int>( floorf( world_x / CHUNK_SIZE_F.x ) );
    const int cz = static_cast<int>( floorf( world_z / CHUNK_SIZE_F.z ) );
    const int lx = world_x - cx * CHUNK_SIZE_X;
    const int lz = world_z - cz * CHUNK_SIZE_Z;
    for ( int cy = this->chunk_center.y + CHUNK_RADIUS_Y; cy >= this->chunk_center.y - CHUNK_RADIUS_Y; cy-- ) {
        const Chunk *chunk = this->get_chunk( glm::ivec3( cx, cy, cz ) );
        if ( !chunk || chunk->is_loading || !chunk->light || !chunk->blocks ) {
            continue;
        }
        for ( int y = 0; y < CHUNK_SIZE_Y; y++ ) {
            const unsigned char cell = chunk->light[ light_index( lx, y, lz ) ];
            const int cv = light_get_sky( cell );
            if ( cv <= 0 ) {
                continue;
            }
            const glm::ivec3 pos( world_x, cy * CHUNK_SIZE_Y + y, world_z );
            for ( int d = 0; d < 6; d++ ) {
                const glm::ivec3 n = pos + light_dirs[ d ];
                const int offered = ( d == LIGHT_DIR_DOWN && cv == LIGHT_MAX_LEVEL ) ? LIGHT_MAX_LEVEL : cv - 1;
                const int nv = this->light_get( n, LIGHT_CHANNEL_SKY );
                if ( nv >= 0 && nv < offered && !light_opaque_id( this->light_block_id_at( n ) ) ) {
                    light_dbg_cascade_seed++;
                    this->light_add_seed( pos, LIGHT_CHANNEL_SKY, cv );
                    break;
                }
            }
        }
    }
}

// Our halo cells mirror neighbor interior cells; refresh them from the
// canonical value and seed a spread wherever neighbor light can flow into our
// fresh interior (worker fill only covers emitters). The opposite direction
// is handled by fixing our neighbors' stale mirrors of OUR shell cells —
// a stale-high mirror means the world got dimmer while we were loading, which
// needs a removal cascade.
void ChunkLoader::light_border_sync( Chunk &chunk ) {
    if ( !chunk.light ) {
        return;
    }
    const glm::ivec3 base = chunk.chunk_pos * CHUNK_SIZE_I;

    // Pass 1: our border shell <- canonical owners.
    for ( int x = -1; x <= CHUNK_SIZE_X; x++ ) {
        for ( int y = -1; y <= CHUNK_SIZE_Y; y++ ) {
            for ( int z = -1; z <= CHUNK_SIZE_Z; z++ ) {
                const bool border = x < 0 || x >= CHUNK_SIZE_X || y < 0 || y >= CHUNK_SIZE_Y || z < 0 || z >= CHUNK_SIZE_Z;
                if ( !border ) {
                    continue;
                }
                const glm::ivec3 w = base + glm::ivec3( x, y, z );
                const Chunk *owner = this->get_chunk( light_chunk_pos_of( w ) );
                if ( !owner || owner->is_loading || !owner->light ) {
                    continue;
                }
                const glm::ivec3 o_local = w - owner->chunk_pos * CHUNK_SIZE_I;
                const unsigned char canon = owner->light[ light_index( o_local.x, o_local.y, o_local.z ) ];
                unsigned char &mirror = chunk.light[ light_index( x, y, z ) ];
                if ( mirror != canon ) {
                    mirror = canon;
                    light_mark_dirty( chunk, glm::ivec3( x, y, z ) );
                }
                // Only seed the BFS where this border cell's light can flow
                // into an under-lit non-opaque neighbor — mass load-in would
                // otherwise push one useless seed per nonzero border cell.
                // Only inward neighbors are checked: directions into other
                // chunks' interiors are covered by their own border syncs and
                // by the propagation that wrote this cell in the first place.
                int inward_dirs[ 3 ];
                int inward_count = 0;
                if ( x < 0 ) {
                    inward_dirs[ inward_count++ ] = 0;
                } else if ( x >= CHUNK_SIZE_X ) {
                    inward_dirs[ inward_count++ ] = 1;
                }
                if ( y < 0 ) {
                    inward_dirs[ inward_count++ ] = 2;
                } else if ( y >= CHUNK_SIZE_Y ) {
                    inward_dirs[ inward_count++ ] = 3;
                }
                if ( z < 0 ) {
                    inward_dirs[ inward_count++ ] = 4;
                } else if ( z >= CHUNK_SIZE_Z ) {
                    inward_dirs[ inward_count++ ] = 5;
                }
                for ( int ch = 0; ch < 2; ch++ ) {
                    const int cv = ch == LIGHT_CHANNEL_BLOCK ? light_get_block( canon ) : light_get_sky( canon );
                    if ( cv <= 0 ) {
                        continue;
                    }
                    for ( int i = 0; i < inward_count; i++ ) {
                        const int d = inward_dirs[ i ];
                        const glm::ivec3 n = w + light_dirs[ d ];
                        const int offered = ( ch == LIGHT_CHANNEL_SKY && d == LIGHT_DIR_DOWN && cv == LIGHT_MAX_LEVEL ) ? LIGHT_MAX_LEVEL : cv - 1;
                        const int nv = this->light_get( n, ch );
                        if ( nv >= 0 && nv < offered && !light_opaque_id( this->light_block_id_at( n ) ) ) {
                            light_dbg_border_seed++;
                            this->light_add_seed( w, ch, cv );
                            break;
                        }
                    }
                }
            }
        }
    }

    // Pass 2: our interior shell -> neighbors' mirrors (fix stale + seed).
    for ( int x = 0; x < CHUNK_SIZE_X; x++ ) {
        for ( int y = 0; y < CHUNK_SIZE_Y; y++ ) {
            for ( int z = 0; z < CHUNK_SIZE_Z; z++ ) {
                const bool shell = x == 0 || x == CHUNK_SIZE_X - 1 || y == 0 || y == CHUNK_SIZE_Y - 1 || z == 0 || z == CHUNK_SIZE_Z - 1;
                if ( !shell ) {
                    continue;
                }
                const glm::ivec3 w = base + glm::ivec3( x, y, z );
                const unsigned char canon = chunk.light[ light_index( x, y, z ) ];

                int axis_opts[ 3 ][ 2 ];
                int axis_count[ 3 ] = { 1, 1, 1 };
                axis_opts[ 0 ][ 0 ] = 0;
                axis_opts[ 1 ][ 0 ] = 0;
                axis_opts[ 2 ][ 0 ] = 0;
                constexpr int axis_size[ 3 ] = { CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z };
                const int local3[ 3 ] = { x, y, z };
                for ( int axis = 0; axis < 3; axis++ ) {
                    if ( local3[ axis ] == 0 ) {
                        axis_opts[ axis ][ axis_count[ axis ]++ ] = -1;
                    } else if ( local3[ axis ] == axis_size[ axis ] - 1 ) {
                        axis_opts[ axis ][ axis_count[ axis ]++ ] = 1;
                    }
                }
                for ( int ix = 0; ix < axis_count[ 0 ]; ix++ ) {
                    for ( int iy = 0; iy < axis_count[ 1 ]; iy++ ) {
                        for ( int iz = 0; iz < axis_count[ 2 ]; iz++ ) {
                            const glm::ivec3 off( axis_opts[ 0 ][ ix ], axis_opts[ 1 ][ iy ], axis_opts[ 2 ][ iz ] );
                            if ( off == glm::ivec3( 0 ) ) {
                                continue;
                            }
                            Chunk *nb = this->get_chunk( chunk.chunk_pos + off );
                            if ( !nb || nb->is_loading || !nb->light ) {
                                continue;
                            }
                            const glm::ivec3 n_local = w - nb->chunk_pos * CHUNK_SIZE_I;
                            unsigned char &mirror = nb->light[ light_index( n_local.x, n_local.y, n_local.z ) ];
                            if ( mirror == canon ) {
                                continue;
                            }
                            const unsigned char old_mirror = mirror;
                            mirror = canon;
                            light_mark_dirty( *nb, n_local );
                            for ( int ch = 0; ch < 2; ch++ ) {
                                const int old_v = ch == LIGHT_CHANNEL_BLOCK ? light_get_block( old_mirror ) : light_get_sky( old_mirror );
                                const int new_v = ch == LIGHT_CHANNEL_BLOCK ? light_get_block( canon ) : light_get_sky( canon );
                                if ( old_v > new_v ) {
                                    // The mirror reflects what neighbors last
                                    // propagated with — removal of the stale
                                    // value cleans their interior.
                                    light_dbg_remove_seed++;
                                    this->light_remove_queue.push_back( { w, static_cast<unsigned char>( ch ), static_cast<unsigned char>( old_v ) } );
                                }
                                if ( new_v > 0 ) {
                                    light_dbg_border_seed++;
                                    this->light_add_seed( w, ch, new_v );
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

// Multiplayer diffs (set_block_by_index_if_different) only record cell
// indices on the chunk; turn them into rechecks here on the render thread.
void ChunkLoader::light_drain_pending( ) {
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        Chunk &chunk = this->chunkArray[ i ];
        if ( chunk.light_pending_count == 0 && !chunk.light_reseed ) {
            continue;
        }
        if ( chunk.is_loading || !chunk.light ) {
            continue; // loading chunks drain their queue in light_finalize_chunk
        }
        if ( chunk.light_reseed ) {
            // Full refill (light_pending overflowed). light_fill_chunk wiped
            // skylight and reset sky_open_above, so re-run the column
            // cascade to flip the flags and reseed, plus a border sync. The
            // whole volume was rewritten, so mark it all dirty for upload.
            light_fill_chunk( chunk );
            chunk.light_reseed = 0;
            this->light_cascade_columns( chunk );
            this->light_seed_interior_boundary( chunk );
            this->light_border_sync( chunk );
            chunk.light_dirty = 1;
            chunk.light_dirty_min = glm::ivec3( 0 );
            chunk.light_dirty_max = glm::ivec3( CHUNK_SIZE_INTERNAL_X - 1, CHUNK_SIZE_INTERNAL_Y - 1, CHUNK_SIZE_INTERNAL_Z - 1 );
        }
        for ( int p = 0; p < chunk.light_pending_count; p++ ) {
            int x, y, z;
            chunk.get_coords_from_index( chunk.light_pending[ p ], x, y, z );
            this->light_recheck_block( chunk.chunk_pos * CHUNK_SIZE_I + glm::ivec3( x, y, z ) );
        }
        chunk.light_pending_count = 0;
    }
}

// ---------------------------------------------------------------------------
// Worker-side fill (load_terrain) + relight
// ---------------------------------------------------------------------------

// Per-chunk block-light fill, confined to this chunk's volume. Runs on the
// terrain worker (no neighbor access needed): seeds every emitter then
// spreads -1/step through non-opaque cells. Skylight is deliberately not
// computed here — the above-chunk's state isn't known on the worker, so
// skylight is resolved in light_finalize_chunk on the render thread.
void light_fill_chunk( Chunk &chunk ) {
    if ( !chunk.light ) {
        return;
    }
    memset( chunk.light, 0, CHUNK_BLOCK_SIZE );
    if ( !chunk.blocks ) {
        return;
    }
    // Note the two arrays index differently: blocks[] is z-fastest
    // (Chunk::get_index_from_coords) while light[] is x-fastest
    // (light_index, matching the glTexSubImage3D upload layout). Scan blocks[]
    // linearly — index conversion only happens for the rare emitters.
    std::vector<int> queue;
    for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
        const int emit = ChunkLoader::light_emit_at( chunk, i );
        if ( emit <= 0 ) {
            continue;
        }
        int x, y, z;
        chunk.get_coords_from_index( i, x, y, z );
        const int lidx = light_index( x, y, z );
        chunk.light[ lidx ] = static_cast<unsigned char>( emit );
        queue.push_back( lidx );
    }
    while ( !queue.empty( ) ) {
        const int idx = queue.back( );
        queue.pop_back( );
        const int v = chunk.light[ idx ] & 0x0F;
        if ( v <= 0 ) {
            continue;
        }
        const int offered = v - 1;
        if ( offered <= 0 ) {
            continue;
        }
        // light_index layout: x fastest, then y, then z.
        const int lx = ( idx % CHUNK_SIZE_INTERNAL_X ) - 1;
        const int ly = ( ( idx / CHUNK_SIZE_INTERNAL_X ) % CHUNK_SIZE_INTERNAL_Y ) - 1;
        const int lz = ( idx / ( CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Y ) ) - 1;
        for ( int d = 0; d < 6; d++ ) {
            const int nx = lx + light_dirs[ d ].x;
            const int ny = ly + light_dirs[ d ].y;
            const int nz = lz + light_dirs[ d ].z;
            if ( nx < -1 || nx >= CHUNK_SIZE_X + 1 || ny < -1 || ny >= CHUNK_SIZE_Y + 1 || nz < -1 || nz >= CHUNK_SIZE_Z + 1 ) {
                continue;
            }
            const int nidx = light_index( nx, ny, nz );
            if ( offered <= ( chunk.light[ nidx ] & 0x0F ) ) {
                continue;
            }
            const BlockID n_id = chunk.blocks[ chunk.get_index_from_coords( nx, ny, nz ) ].id;
            if ( light_opaque_id( n_id ) ) {
                continue;
            }
            chunk.light[ nidx ] = static_cast<unsigned char>( ( chunk.light[ nidx ] & 0xF0 ) | offered );
            queue.push_back( nidx );
        }
    }
    ChunkLoader::light_compute_column_open( chunk );
    // The memset above wiped skylight; force every sky_open_above flag back
    // to 0 so the next cascade sees a flip and reseeds columns that are open.
    if ( chunk.light_columns ) {
        memset( chunk.light_columns + LIGHT_FLAGS_COUNT, 0, LIGHT_FLAGS_COUNT );
    }
}

// ---------------------------------------------------------------------------
// GPU upload
// ---------------------------------------------------------------------------

static int s_light_tex_unit = -1;
int light_texture_unit( ) {
    if ( s_light_tex_unit < 0 ) {
        s_light_tex_unit = texture_reserve_unit( );
    }
    return s_light_tex_unit;
}

// light[] packs both channels in one byte (sky<<4 | block), but GL_LINEAR
// interpolates the raw value before any shader-side unpack — sampling between
// a sky-lit cell and a torch-lit cell produces bright ridges where both
// channels should dip. The texture therefore stores each channel separately
// (R=sky, G=block) so filtering stays independent; expansion happens here on
// upload. Values are stored *17 so UNORM8 filtering keeps ~8-bit precision
// across the 0-15 range instead of ~4.
static unsigned char s_light_upload_scratch[ 2 * CHUNK_BLOCK_SIZE ];

static inline void light_expand_cell( const unsigned char packed, unsigned char *out ) {
    out[ 0 ] = static_cast<unsigned char>( ( packed >> 4 ) * 17 ); // sky
    out[ 1 ] = static_cast<unsigned char>( ( packed & 0x0F ) * 17 ); // block
}

void Chunk::light_ensure_texture( ) {
    if ( this->light_texture != 0 || !this->light ) {
        return;
    }
    glGenTextures( 1, &this->light_texture );
    glActiveTexture( GL_TEXTURE0 + light_texture_unit( ) );
    glBindTexture( GL_TEXTURE_3D, this->light_texture );
    // Full upload at creation: a chunk can be drawn before its first
    // light_upload_dirty pass, and must never sample an uninitialized volume.
    // Covers every queued dirty region, so clear the flag.
    // The block atlas (Texture::loadTexture) leaves UNPACK_ROW_LENGTH /
    // UNPACK_IMAGE_HEIGHT set, and the default UNPACK_ALIGNMENT=4 pads our
    // rows — set all three explicitly or the upload reads garbage.
    for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
        light_expand_cell( this->light[ i ], s_light_upload_scratch + 2 * i );
    }
    glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
    glPixelStorei( GL_UNPACK_IMAGE_HEIGHT, 0 );
    glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
    glTexImage3D( GL_TEXTURE_3D, 0, GL_RG8, CHUNK_SIZE_INTERNAL_X, CHUNK_SIZE_INTERNAL_Y, CHUNK_SIZE_INTERNAL_Z, 0, GL_RG, GL_UNSIGNED_BYTE, s_light_upload_scratch );
    glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
    this->light_dirty = 0;
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE );
}

void ChunkLoader::light_upload_dirty( const int max_uploads ) {
    int uploads = 0;
    for ( int i = 0; i < MAX_LOADED_CHUNKS && uploads < max_uploads; i++ ) {
        Chunk &chunk = this->chunkArray[ i ];
        if ( !chunk.light_dirty || chunk.is_loading || !chunk.light ) {
            continue;
        }
        // No texture yet: light_ensure_texture's full upload at first draw
        // already covers every queued dirty region — skip the allocation here
        // so never-drawn (faceless) chunks don't pay 38KB of VRAM.
        if ( chunk.light_texture == 0 ) {
            continue;
        }
        uploads++;
        glActiveTexture( GL_TEXTURE0 + light_texture_unit( ) );
        glBindTexture( GL_TEXTURE_3D, chunk.light_texture );
        const glm::ivec3 lo = chunk.light_dirty_min;
        const glm::ivec3 sz = chunk.light_dirty_max - lo + glm::ivec3( 1 );
        // Expand the packed (sky<<4|block) dirty region to RG so the two
        // channels filter independently — see light_expand_cell. The scratch
        // is tightly packed so the default unpack stride applies.
        for ( int z = 0; z < sz.z; z++ ) {
            for ( int y = 0; y < sz.y; y++ ) {
                const unsigned char *row = chunk.light + ( ( lo.z + z ) * CHUNK_SIZE_INTERNAL_Y + ( lo.y + y ) ) * CHUNK_SIZE_INTERNAL_X + lo.x;
                unsigned char *out = s_light_upload_scratch + 2 * ( z * sz.y + y ) * sz.x;
                for ( int x = 0; x < sz.x; x++ ) {
                    light_expand_cell( row[ x ], out + 2 * x );
                }
            }
        }
        glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
        glPixelStorei( GL_UNPACK_IMAGE_HEIGHT, 0 );
        glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
        glTexSubImage3D( GL_TEXTURE_3D, 0, lo.x, lo.y, lo.z, sz.x, sz.y, sz.z, GL_RG, GL_UNSIGNED_BYTE, s_light_upload_scratch );
        glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
        chunk.light_dirty = 0;
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
    // Broad daylight plateau, smooth dawn/dusk, a dark-but-not-black floor
    // so caves without torches are still faintly visible.
    float d = glm::clamp( ( sun + 0.06f ) * 4.0f, 0.0f, 1.0f );
    return 0.04f + 0.96f * d;
}
