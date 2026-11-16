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
// Per-chunk lighting pipeline: skylight column tracking, chunk finalize and
// border sync against neighbors, pending-diff drains, and the worker-side
// light_fill_chunk. Shared helpers/state live in common/light_internal.hpp.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------

void ChunkLoader::light_ensure_columns( Chunk &chunk ) {
    if ( !chunk.light.columns ) {
        chunk.light.columns = chunk_alloc_light_columns( );
        light_compute_column_open( chunk );
    }
}

// Scan one interior column top-down: column_open=1 iff no column-blocking
// (opaque or absorbing) cell, and fill_from = one past the topmost blocker
// (the first y skylight reaches). The scan stops at the first blocker, so
// closed columns cost just the air above the surface; fully-open columns get
// fill_from=0.
void ChunkLoader::light_compute_one_column( Chunk &chunk, const int x, const int z ) {
    unsigned char open = 1;
    int fill_from = 0;
    // A null cells[] volume is all-air: every column is open.
    if ( chunk.cells ) {
        for ( int y = CHUNK_SIZE_Y - 1; y >= 0; y-- ) {
            if ( light_column_blocked_id( block_cell_id( chunk.cells[ Chunk::get_index_from_coords( x, y, z ) ] ) ) ) {
                open = 0;
                fill_from = y + 1;
                break;
            }
        }
    }
    const int ci = LIGHT_COLUMN_INDEX( x, z );
    chunk.light.columns[ ci ] = open;
    chunk.light.columns[ LIGHT_FILL_FROM_INDEX( x, z ) ] = static_cast<unsigned char>( fill_from );
}

// column_open[x][z] == 1 iff no column-blocking (opaque or absorbing) cell
// exists anywhere in this chunk's interior column. Called by the worker in
// load_terrain and lazily by light_ensure_columns on the render thread.
void ChunkLoader::light_compute_column_open( Chunk &chunk ) {
    if ( !chunk.light.columns ) {
        return;
    }
    if ( !chunk.cells ) {
        // All-air: every column open, fill reaches y=0.
        memset( chunk.light.columns, 1, LIGHT_FLAGS_COUNT );
        memset( chunk.light.columns + LIGHT_FILL_FROM_INDEX( 0, 0 ), 0, LIGHT_FLAGS_COUNT );
        return;
    }
    for ( int x = 0; x < CHUNK_SIZE_X; x++ ) {
        for ( int z = 0; z < CHUNK_SIZE_Z; z++ ) {
            light_compute_one_column( chunk, x, z );
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
    // Nothing above the top of the loaded grid is sky — unless the top of the
    // torus sits in a skyless dimension (nether), where no sky exists at all.
    int open = dimension_has_sky( dimension_of_chunk( this->chunk_center.y + CHUNK_RADIUS_Y ) ) ? 1 : 0;
    for ( int cy = this->chunk_center.y + CHUNK_RADIUS_Y; cy >= this->chunk_center.y - CHUNK_RADIUS_Y; cy-- ) {
        Chunk *chunk = this->get_chunk( glm::ivec3( cx, cy, cz ) );
        if ( !chunk || chunk->load_state ) {
            // Unloaded cells are unknown — keep the previous sky_open_above
            // state below them. When the chunk finishes, its own finalize
            // cascade re-derives the flag through this column.
            continue;
        }
        this->light_ensure_columns( *chunk );
        unsigned char &above_flag = chunk->light.columns[ LIGHT_FLAGS_COUNT + LIGHT_COLUMN_INDEX( lx, lz ) ];
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
                // not-yet-cascaded neighbors still read 0. fill_from is one
                // past the topmost opaque cell (see light.hpp), so the fill
                // needs no per-cell cells[] reads.
                const int fill_from = chunk->light.columns[ LIGHT_FILL_FROM_INDEX( lx, lz ) ];
                for ( int y = CHUNK_SIZE_Y - 1; y >= fill_from; y-- ) {
                    const glm::ivec3 pos( world_x, cy * CHUNK_SIZE_Y + y, world_z );
                    this->light_set( pos, LIGHT_CHANNEL_SKY, LIGHT_RAW_MAX );
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
                        this->light_push_remove_seed( pos, LIGHT_CHANNEL_SKY, v );
                        break;
                    }
                }
            }
        }
        open = open && chunk->light.columns[ LIGHT_COLUMN_INDEX( lx, lz ) ];
    }
}

// Bulk version of light_cascade_column for light_finalize_chunk: resolves the
// vertical chunk stack once (instead of 1024 columns x ~17 get_chunk calls)
// and writes the sky descent directly into light[] — light_set's mirror
// matrix only runs for cells that actually have mirrors (the y-shell, plus
// columns on the x/z shell). Per-column ordering is identical to the
// single-column version: each column's flag/fill sequence only depends on
// chunks above it, so interleaving columns across cy iterations is safe.
int ChunkLoader::light_cascade_columns( Chunk &start_chunk ) {
    constexpr int N_LEVELS = 2 * CHUNK_RADIUS_Y + 1;
    Chunk *stack[ N_LEVELS ];
    const int cx = start_chunk.chunk_pos.x;
    const int cz = start_chunk.chunk_pos.z;
    int cells_filled = 0;
    for ( int i = 0; i < N_LEVELS; i++ ) {
        stack[ i ] = this->get_chunk( glm::ivec3( cx, this->chunk_center.y + CHUNK_RADIUS_Y - i, cz ) );
    }
    // open[col] = every loaded chunk above has column_open for this column.
    // Seeded 0 in skyless dimensions: the top of the torus is roofed by the
    // dimension's own ceiling, so no sky enters the stack at all.
    const unsigned char sky_above =
        dimension_has_sky( dimension_of_chunk( this->chunk_center.y + CHUNK_RADIUS_Y ) ) ? 1 : 0;
    unsigned char open[ LIGHT_FLAGS_COUNT ];
    memset( open, sky_above, sizeof( open ) );
    for ( int i = 0; i < N_LEVELS; i++ ) {
        Chunk *chunk = stack[ i ];
        if ( !chunk || chunk->load_state ) {
            // Unloaded cells are unknown — keep open[] as-is for chunks below;
            // the missing chunk's own finalize cascade re-derives the flags.
            continue;
        }
        this->light_ensure_columns( *chunk );
        unsigned char *flags = chunk->light.columns;
        const int base_y = chunk->chunk_pos.y * CHUNK_SIZE_Y;
        const int base_x = chunk->chunk_pos.x * CHUNK_SIZE_X;
        const int base_z = chunk->chunk_pos.z * CHUNK_SIZE_Z;
        // Fill writes to shell cells mirror into neighbors; resolve the 3x3x3
        // neighborhood once per chunk instead of per-mirror get_chunk calls.
        Chunk *nb_cache[ 27 ];
        nb_cache[ ( 0 + 1 ) * 9 + ( 0 + 1 ) * 3 + ( 0 + 1 ) ] = chunk;
        for ( int dx = -1; dx <= 1; dx++ ) {
            for ( int dy = -1; dy <= 1; dy++ ) {
                for ( int dz = -1; dz <= 1; dz++ ) {
                    if ( !dx && !dy && !dz ) {
                        continue;
                    }
                    nb_cache[ ( dx + 1 ) * 9 + ( dy + 1 ) * 3 + ( dz + 1 ) ] =
                        this->get_chunk( chunk->chunk_pos + glm::ivec3( dx, dy, dz ) );
                }
            }
        }
        int level_cells = cells_filled;
        for ( int x = 0; x < CHUNK_SIZE_X; x++ ) {
            for ( int z = 0; z < CHUNK_SIZE_Z; z++ ) {
                const int ci = LIGHT_COLUMN_INDEX( x, z );
                const int was_open = open[ ci ];
                unsigned char &above_flag = flags[ LIGHT_FLAGS_COUNT + ci ];
                if ( !was_open && !above_flag ) {
                    continue; // closed above and already marked closed
                }
                if ( above_flag != was_open ) {
                    above_flag = static_cast<unsigned char>( was_open );
                    const bool col_interior = x > 0 && x < CHUNK_SIZE_X - 1 && z > 0 && z < CHUNK_SIZE_Z - 1;
                    if ( was_open ) {
                        // Sky flows straight down at full strength — write the
                        // descent directly instead of seeding BFS floods (see
                        // light_cascade_column for the ordering rationale).
                        // fill_from (computed on the worker) is one past the
                        // topmost opaque cell, so the fill is a pure range
                        // write with no per-cell cells[] reads. Dirty marking
                        // is batched: the whole volume is flagged once at the
                        // end instead of per cell.
                        const int fill_from = flags[ LIGHT_FILL_FROM_INDEX( x, z ) ];
                        for ( int y = CHUNK_SIZE_Y - 1; y >= fill_from; y-- ) {
                            if ( col_interior && y > 0 && y < CHUNK_SIZE_Y - 1 ) {
                                const int idx = light_index( x, y, z );
                                light_write_cell( *chunk, idx, static_cast<unsigned short>( ( light_read_cell( *chunk, idx ) & 0x00FF ) | ( LIGHT_RAW_MAX << 8 ) ) );
                            } else {
                                // y-shell cells mirror into the chunks
                                // above/below; x/z shell cells mirror
                                // sideways. Those need the mirror write.
                                this->light_set_nb( nb_cache, *chunk, glm::ivec3( base_x + x, base_y + y, base_z + z ), LIGHT_CHANNEL_SKY, LIGHT_RAW_MAX );
                            }
                            cells_filled++;
                        }
                    } else {
                        // Flag flipped closed: find the first lit cell and
                        // seed a removal (light_remove_step expects pos's new
                        // value already written, so clear it first). The scan
                        // is confined to this chunk's own column, so read
                        // light[] directly — light_get's per-cell
                        // chunk_pos/get_chunk resolution costs ~50x a plain
                        // read, and solid underground chunks pay the full
                        // CHUNK_SIZE_Y scan finding nothing.
                        for ( int y = CHUNK_SIZE_Y - 1; y >= 0; y-- ) {
                            const int v = light_get_sky( light_read_cell( *chunk, light_index( x, y, z ) ) );
                            if ( v > 0 ) {
                                const glm::ivec3 pos( base_x + x, base_y + y, base_z + z );
                                if ( col_interior && y > 0 && y < CHUNK_SIZE_Y - 1 ) {
                                    const int idx = light_index( x, y, z );
                                    light_write_cell( *chunk, idx, static_cast<unsigned short>( light_read_cell( *chunk, idx ) & 0x00FF ) );
                                    light_mark_dirty( *chunk, glm::ivec3( x, y, z ) );
                                } else {
                                    this->light_set_nb( nb_cache, *chunk, pos, LIGHT_CHANNEL_SKY, 0 );
                                }
                                light_dbg_remove_seed++;
                                this->light_push_remove_seed( pos, LIGHT_CHANNEL_SKY, v );
                                break;
                            }
                        }
                    }
                }
                open[ ci ] = static_cast<unsigned char>( was_open && flags[ ci ] );
            }
        }
        if ( cells_filled != level_cells ) {
            // Interior fill cells skip per-cell dirty marking; flag the whole
            // volume for upload instead.
            ChunkLoader::light_mark_dirty_all( *chunk );
        }
    }
    return cells_filled;
}

// ---------------------------------------------------------------------------
// Chunk finalize / border sync
// ---------------------------------------------------------------------------

// After a chunk's blocks are final (terrain + pending diffs applied):
//  - drain recorded per-cell diffs into rechecks,
//  - resolve skylight column-by-column (cascade touches neighbors too),
//  - sync border mirrors with neighbors and seed spreads/removals.
void ChunkLoader::light_finalize_chunk( Chunk &chunk ) {
    PROFILE_SCOPE( "light_finalize" );
    if ( chunk.load_state ) {
        // Parked between submit and execution — the work is lost. Nothing
        // to re-flag: the next reactivation marks it stale unconditionally.
        return;
    }
    light_dbg_finalize++;
    const long long t0 = light_now_us( );
    if ( chunk.light.reseed ) {
        light_fill_chunk( chunk ); // refill emitters + column_open
        chunk.light.reseed = 0;
        // The wipe+refill already re-derived the whole volume — a stale flag
        // left over from cap refusals is superseded (racy refusals during
        // this run re-flag it, which is fine: the next heal re-derives).
        chunk.light.stale_clear( );
    }
    // Convert pending diff indices into rechecks (world positions).
    for ( int i = 0; i < chunk.light.pending_count; i++ ) {
        int x, y, z;
        chunk.get_coords_from_index( chunk.light.pending[ i ], x, y, z );
        this->light_recheck_block( chunk.chunk_pos * CHUNK_SIZE_I + glm::ivec3( x, y, z ) );
    }
    chunk.light.pending_count = 0;
    const long long t1 = light_now_us( );
    light_dbg_us_fin_fill += t1 - t0;

    this->light_cascade_columns( chunk );
    const long long t2 = light_now_us( );
    light_dbg_us_fin_casc += t2 - t1;
    // Run unconditionally: the worker's optimistic prefill already wrote
    // sky=15 and set sky_open_above, so the cascade fills nothing
    // (cells_filled==0) in the common case — yet lit cells still border
    // canopy-shaded columns that only get light via this scan's seeds.
    // Fully-dark chunks cost just the per-column lit_top early-out.
    this->light_seed_interior_boundary( chunk );
    const long long t3 = light_now_us( );
    light_dbg_us_fin_bscan += t3 - t2;
    this->light_border_sync( chunk );
    light_dbg_us_fin_border += light_now_us( ) - t3;

    // Whole volume needs uploading on first draw.
    ChunkLoader::light_mark_dirty_all( chunk );
    // Real skylight is derived now — future finalizes route to the far lane.
    chunk.light.finalized.store( 1, std::memory_order_relaxed );
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
// After the direct-fill cascade, interior skylight is exactly "column lit iff
// y >= lit_top" (lit_top = fill_from when sky_open_above, else nothing lit).
// A lit cell can therefore only border under-lit space where a lateral
// neighbor column is shallower (y < neighbor lit_top) or across the chunk
// boundary — vertical neighbors are contiguous within a fill (up is always
// lit; down is lit or the opaque cell that capped the fill). This scans only
// the thin bands where adjacent fill depths differ plus the boundary shell,
// instead of the whole volume.
// One exception to "down is lit or opaque": a light-floor cell
// (blocks_light_down: slabs, path) capping a column has stored sky=0 but
// CAN be lit — the per-column reseed below pushes the lit cell just above
// the cap so its down-spread lights the floor to full sky (a path road or
// slab floor interior would otherwise stay dim; a lit_top==0 floor already
// gets this from the y=0 boundary down-check).
void ChunkLoader::light_seed_interior_boundary( Chunk &chunk ) {
    if ( !chunk.light.columns ) {
        return;
    }
    const glm::ivec3 base = chunk.chunk_pos * CHUNK_SIZE_I;
    const unsigned char *flags = chunk.light.columns;

    // Boundary cells probe across chunk edges; resolve the 3x3x3 neighborhood
    // once instead of a get_chunk per light_get/light_block_id_at call.
    Chunk *nb_cache[ 27 ];
    nb_cache[ ( 0 + 1 ) * 9 + ( 0 + 1 ) * 3 + ( 0 + 1 ) ] = &chunk;
    for ( int dx = -1; dx <= 1; dx++ ) {
        for ( int dy = -1; dy <= 1; dy++ ) {
            for ( int dz = -1; dz <= 1; dz++ ) {
                if ( !dx && !dy && !dz ) {
                    continue;
                }
                nb_cache[ ( dx + 1 ) * 9 + ( dy + 1 ) * 3 + ( dz + 1 ) ] =
                    this->get_chunk( chunk.chunk_pos + glm::ivec3( dx, dy, dz ) );
            }
        }
    }

    // lit_top per column (x,z): the first lit y, or CHUNK_SIZE_Y when unlit.
    for ( int x = 0; x < CHUNK_SIZE_X; x++ ) {
        for ( int z = 0; z < CHUNK_SIZE_Z; z++ ) {
            const int ci = LIGHT_COLUMN_INDEX( x, z );
            const int lit_top = flags[ LIGHT_FLAGS_COUNT + ci ] ? flags[ LIGHT_FILL_FROM_INDEX( x, z ) ] : CHUNK_SIZE_Y;
            if ( lit_top >= CHUNK_SIZE_Y ) {
                continue; // no lit cells in this column
            }
            // Light-floor cap (blocks_light_down): the cell below lit_top is
            // light-holding but stored sky=0 since the fill stopped above
            // it. Re-seed the cell just above the cap — its down-spread
            // lights the floor cell to full sky. Only for floor caps; leaf
            // and other absorbing caps keep their dimmed interior.
            if ( lit_top > 0 && chunk.cells &&
                 block_definition_get_definition( block_cell_id( chunk.cells[ chunk.get_index_from_coords( x, lit_top - 1, z ) ] ) )->blocks_light_down ) {
                const int cv = light_get_sky( light_read_cell( chunk, light_index( x, lit_top, z ) ) );
                light_dbg_cascade_seed++;
                this->light_add_seed( base + glm::ivec3( x, lit_top, z ), LIGHT_CHANNEL_SKY, cv );
            }
            // Bound is the DEEPEST dark neighbor column: our lit cell at y
            // borders darkness iff y < nlt for some of the 8 surrounding
            // columns, so the scan must reach max(nlt), not min — with min,
            // a column between open and shadowed neighbors (e.g. under a
            // tree canopy) seeded nothing and the shadow stayed dark. +1:
            // a lateral diagonal can dip one below its column's lit_top.
            // Shell columns have no local lit_top — the boundary probe
            // below covers them per cell.
            int m = lit_top;
            for ( int dx = -1; dx <= 1; dx++ ) {
                for ( int dz = -1; dz <= 1; dz++ ) {
                    if ( !dx && !dz ) {
                        continue;
                    }
                    const int nx = x + dx;
                    const int nz = z + dz;
                    if ( nx >= 0 && nx < CHUNK_SIZE_X && nz >= 0 && nz < CHUNK_SIZE_Z ) {
                        const int nci = LIGHT_COLUMN_INDEX( nx, nz );
                        const int nlt = flags[ LIGHT_FLAGS_COUNT + nci ] ? flags[ LIGHT_FILL_FROM_INDEX( nx, nz ) ] : CHUNK_SIZE_Y;
                        if ( nlt > m ) {
                            m = nlt;
                        }
                    }
                }
            }
            const auto cell_id_at = [ & ]( const glm::ivec3 &l ) { return light_cell_id_at( chunk.cells, l ); };
            // Interior lateral bands: our lit cells below a neighbor's lit_top
            // border below-fill cells (stored sky=0 in a fresh volume).
            for ( int y = lit_top; y <= m; y++ ) {
                const glm::ivec3 c_local( x, y, z );
                const int cv = light_get_sky( light_read_cell( chunk, light_index( x, y, z ) ) );
                if ( cv <= LIGHT_STEP_FACE ) {
                    continue;
                }
                const bool src_floor = block_definition_get_definition( cell_id_at( c_local ) )->blocks_light_down;
                uint32_t dmask = light_step_masks[ light_cell_class( x, y, z ) ].in;
                if ( src_floor ) {
                    dmask &= ~light_step_down_mask;
                }
                for ( ; dmask; dmask &= dmask - 1 ) {
                    const LightStepDir &sd = light_step_dirs[ light_ctz32( dmask ) ];
                    const glm::ivec3 n_local = c_local + sd.d;
                    const int offered = cv - sd.cost;
                    if ( offered <= 0 ) {
                        continue;
                    }
                    if ( light_get_sky( light_read_cell( chunk, light_index( n_local.x, n_local.y, n_local.z ) ) ) >= offered ) {
                        continue;
                    }
                    // A null cells[] volume is all-air: nothing is opaque.
                    if ( chunk.cells && light_opaque_id( cell_id_at( n_local ) ) ) {
                        continue;
                    }
                    if ( sd.ngates && !light_diag_open( chunk.cells, c_local, sd, src_floor ) ) {
                        continue;
                    }
                    light_dbg_cascade_seed++;
                    this->light_add_seed( base + c_local, LIGHT_CHANNEL_SKY, cv );
                    break;
                }
            }
            // Boundary cells: lit cells on the volume edge can border
            // under-lit cells in neighboring chunks regardless of column
            // fills. Every step dir whose target lands in a foreign chunk's
            // interior counts — under chamfer a diagonal across the corner is
            // a real in-path. A neighbor crossing a chunk face/edge/corner
            // lands in nb_cache[(offset+1)] with the local coord wrapped.
            const bool on_shell = x == 0 || x == CHUNK_SIZE_X - 1 || z == 0 || z == CHUNK_SIZE_Z - 1;
            if ( on_shell ) {
                for ( int y = lit_top; y < CHUNK_SIZE_Y; y++ ) {
                    const glm::ivec3 c_local( x, y, z );
                    const int cv = light_get_sky( light_read_cell( chunk, light_index( x, y, z ) ) );
                    if ( cv <= LIGHT_STEP_FACE ) {
                        continue;
                    }
                    const bool src_floor = block_definition_get_definition( cell_id_at( c_local ) )->blocks_light_down;
                    uint32_t dmask = light_step_masks[ light_cell_class( x, y, z ) ].out_xz;
                    if ( src_floor ) {
                        dmask &= ~light_step_down_mask;
                    }
                    for ( ; dmask; dmask &= dmask - 1 ) {
                        const LightStepDir &sd = light_step_dirs[ light_ctz32( dmask ) ];
                        const glm::ivec3 n_local = c_local + sd.d;
                        const glm::ivec3 o( n_local.x < 0 ? -1 : n_local.x >= CHUNK_SIZE_X ? 1 : 0,
                                            n_local.y < 0 ? -1 : n_local.y >= CHUNK_SIZE_Y ? 1 : 0,
                                            n_local.z < 0 ? -1 : n_local.z >= CHUNK_SIZE_Z ? 1 : 0 );
                        const Chunk *nchunk = nb_cache[ ( o.x + 1 ) * 9 + ( o.y + 1 ) * 3 + ( o.z + 1 ) ];
                        if ( !nchunk || nchunk->load_state ) {
                            continue;
                        }
                        const glm::ivec3 n_loc = n_local - glm::ivec3( o.x * CHUNK_SIZE_X, o.y * CHUNK_SIZE_Y, o.z * CHUNK_SIZE_Z );
                        if ( n_loc.x < 0 || n_loc.x >= CHUNK_SIZE_X || n_loc.y < 0 || n_loc.y >= CHUNK_SIZE_Y || n_loc.z < 0 || n_loc.z >= CHUNK_SIZE_Z ) {
                            continue; // only the neighbor's interior is ours to seed
                        }
                        const int offered = cv - sd.cost;
                        if ( offered <= 0 ) {
                            continue;
                        }
                        const int nv = light_get_sky( light_read_cell( *nchunk, light_index( n_loc.x, n_loc.y, n_loc.z ) ) );
                        if ( nv >= offered ) {
                            continue;
                        }
                        const BlockID n_id = nchunk->cells ? block_cell_id( nchunk->cells[ nchunk->get_index_from_coords( n_loc.x, n_loc.y, n_loc.z ) ] ) : AIR;
                        if ( light_opaque_id( n_id ) ) {
                            continue;
                        }
                        if ( sd.ngates && !light_diag_open( chunk.cells, c_local, sd, src_floor ) ) {
                            continue;
                        }
                        light_dbg_cascade_seed++;
                        this->light_add_seed( base + c_local, LIGHT_CHANNEL_SKY, cv );
                        break;
                    }
                }
            }
            // Top/bottom faces: every column's two y-shell cells probe the
            // chunks above/below (down only reachable in a fully-lit column).
            for ( const int y : { 0, CHUNK_SIZE_Y - 1 } ) {
                if ( y < lit_top ) {
                    continue;
                }
                const glm::ivec3 c_local( x, y, z );
                const int cv = light_get_sky( light_read_cell( chunk, light_index( x, y, z ) ) );
                if ( cv <= LIGHT_STEP_FACE ) {
                    continue;
                }
                const bool src_floor = block_definition_get_definition( cell_id_at( c_local ) )->blocks_light_down;
                uint32_t dmask = light_step_masks[ light_cell_class( x, y, z ) ].out_y;
                if ( src_floor ) {
                    dmask &= ~light_step_down_mask;
                }
                for ( ; dmask; dmask &= dmask - 1 ) {
                    const int d = light_ctz32( dmask );
                    const LightStepDir &sd = light_step_dirs[ d ];
                    const glm::ivec3 n_local = c_local + sd.d;
                    const glm::ivec3 o( n_local.x < 0 ? -1 : n_local.x >= CHUNK_SIZE_X ? 1 : 0,
                                        n_local.y < 0 ? -1 : n_local.y >= CHUNK_SIZE_Y ? 1 : 0,
                                        n_local.z < 0 ? -1 : n_local.z >= CHUNK_SIZE_Z ? 1 : 0 );
                    const Chunk *nchunk = nb_cache[ ( o.x + 1 ) * 9 + ( o.y + 1 ) * 3 + ( o.z + 1 ) ];
                    if ( !nchunk || nchunk->load_state ) {
                        continue;
                    }
                    const glm::ivec3 n_loc = n_local - glm::ivec3( o.x * CHUNK_SIZE_X, o.y * CHUNK_SIZE_Y, o.z * CHUNK_SIZE_Z );
                    if ( n_loc.x < 0 || n_loc.x >= CHUNK_SIZE_X || n_loc.y < 0 || n_loc.y >= CHUNK_SIZE_Y || n_loc.z < 0 || n_loc.z >= CHUNK_SIZE_Z ) {
                        continue;
                    }
                    const int offered = ( d == LIGHT_DIR_DOWN && cv == LIGHT_RAW_MAX ) ? LIGHT_RAW_MAX : cv - sd.cost;
                    if ( offered <= 0 ) {
                        continue;
                    }
                    const int nv = light_get_sky( light_read_cell( *nchunk, light_index( n_loc.x, n_loc.y, n_loc.z ) ) );
                    if ( nv >= offered ) {
                        continue;
                    }
                    const BlockID n_id = nchunk->cells ? block_cell_id( nchunk->cells[ nchunk->get_index_from_coords( n_loc.x, n_loc.y, n_loc.z ) ] ) : AIR;
                    if ( light_opaque_id( n_id ) ) {
                        continue;
                    }
                    if ( sd.ngates && !light_diag_open( chunk.cells, c_local, sd, src_floor ) ) {
                        continue;
                    }
                    light_dbg_cascade_seed++;
                    this->light_add_seed( base + c_local, LIGHT_CHANNEL_SKY, cv );
                    break;
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
        if ( !chunk || chunk->load_state ) {
            continue;
        }
        for ( int y = 0; y < CHUNK_SIZE_Y; y++ ) {
            const unsigned short cell = light_read_cell( *chunk, light_index( lx, y, lz ) );
            const int cv = light_get_sky( cell );
            if ( cv <= 0 ) {
                continue;
            }
            const glm::ivec3 pos( world_x, cy * CHUNK_SIZE_Y + y, world_z );
            const glm::ivec3 c_local( lx, y, lz );
            const bool src_floor = block_definition_get_definition( light_cell_id_at( chunk->cells, c_local ) )->blocks_light_down;
            for ( int d = 0; d < LIGHT_STEP_COUNT; d++ ) {
                const LightStepDir &sd = light_step_dirs[ d ];
                if ( src_floor && sd.d.y < 0 ) {
                    continue;
                }
                const glm::ivec3 n = pos + sd.d;
                const int offered = ( d == LIGHT_DIR_DOWN && cv == LIGHT_RAW_MAX ) ? LIGHT_RAW_MAX : cv - sd.cost;
                if ( offered <= 0 ) {
                    continue;
                }
                const int nv = this->light_get( n, LIGHT_CHANNEL_SKY );
                if ( nv < 0 || nv >= offered ) {
                    continue;
                }
                if ( light_opaque_id( this->light_block_id_at( n ) ) ) {
                    continue;
                }
                if ( sd.ngates && !light_diag_open( chunk->cells, c_local, sd, src_floor ) ) {
                    continue;
                }
                light_dbg_cascade_seed++;
                this->light_add_seed( pos, LIGHT_CHANNEL_SKY, cv );
                break;
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
    // chunk.light.volume may be null (uniform volume): light_read_cell/write_cell
    // keep mirrors materializing only where they actually differ.
    if ( chunk.load_state ) {
        return;
    }
    const glm::ivec3 base = chunk.chunk_pos * CHUNK_SIZE_I;

    // Resolve the 26 neighbors once: both passes would otherwise call
    // get_chunk per halo/shell cell (~10k+ lookups per chunk).
    Chunk *nb_cache[ 27 ];
    for ( int dx = -1; dx <= 1; dx++ ) {
        for ( int dy = -1; dy <= 1; dy++ ) {
            for ( int dz = -1; dz <= 1; dz++ ) {
                const int ci = ( dx + 1 ) * 9 + ( dy + 1 ) * 3 + ( dz + 1 );
                nb_cache[ ci ] = ( dx || dy || dz ) ? this->get_chunk( chunk.chunk_pos + glm::ivec3( dx, dy, dz ) ) : &chunk;
            }
        }
    }

    // Pass 1: our border shell <- canonical owners. Enumerate the six halo
    // faces directly — scanning the full box spent ~91% of its iterations
    // skipping interior cells.
    auto sync_halo_cell = [&]( const int x, const int y, const int z ) {
        const int ox = x < 0 ? -1 : x >= CHUNK_SIZE_X ? 1 : 0;
        const int oy = y < 0 ? -1 : y >= CHUNK_SIZE_Y ? 1 : 0;
        const int oz = z < 0 ? -1 : z >= CHUNK_SIZE_Z ? 1 : 0;
        const Chunk *owner = nb_cache[ ( ox + 1 ) * 9 + ( oy + 1 ) * 3 + ( oz + 1 ) ];
        if ( !owner || owner->load_state ) {
            return;
        }
        // owner's local = our (x,y,z) minus the chunk offset * 32.
        const unsigned short canon = light_read_cell( *owner, light_index( x - ox * CHUNK_SIZE_X, y - oy * CHUNK_SIZE_Y, z - oz * CHUNK_SIZE_Z ) );
        const int midx = light_index( x, y, z );
        if ( light_read_cell( chunk, midx ) != canon ) {
            light_write_cell( chunk, midx, canon );
            light_mark_dirty( chunk, glm::ivec3( x, y, z ) );
        }
        // Only seed the BFS where this border cell's light can flow
        // into an under-lit non-opaque neighbor — mass load-in would
        // otherwise push one useless seed per nonzero border cell.
        // All 18 step dirs count as inward whenever they land inside the
        // volume: a diagonal-only in-path is a real route under chamfer
        // propagation. Directions into other chunks' interiors are covered
        // by their own border syncs and by the propagation that wrote this
        // cell in the first place.
        const BlockID src_id = light_cell_id_at( chunk.cells, glm::ivec3( x, y, z ) );
        const bool src_floor = block_definition_get_definition( src_id )->blocks_light_down;
        // Only interior targets feed our fresh volume: prune to the dirs
        // that can land inside, once per cell (the per-channel loop reruns
        // the dir scan).
        uint32_t dmask = light_step_masks[ light_cell_class( x, y, z ) ].in;
        if ( src_floor ) {
            dmask &= ~light_step_down_mask;
        }
        for ( int ch = 0; ch < 2; ch++ ) {
            const int cv = ch == LIGHT_CHANNEL_BLOCK ? light_get_block( canon ) : light_get_sky( canon );
            if ( cv <= 0 ) {
                continue;
            }
            for ( uint32_t m = dmask; m; m &= m - 1 ) {
                const int d = light_ctz32( m );
                const LightStepDir &sd = light_step_dirs[ d ];
                const int offered = ( ch == LIGHT_CHANNEL_SKY && d == LIGHT_DIR_DOWN && cv == LIGHT_RAW_MAX ) ? LIGHT_RAW_MAX : cv - sd.cost;
                if ( offered <= 0 ) {
                    continue;
                }
                const glm::ivec3 n_local = glm::ivec3( x, y, z ) + sd.d;
                const int nv = ch == LIGHT_CHANNEL_BLOCK ? light_get_block( light_read_cell( chunk, light_index( n_local.x, n_local.y, n_local.z ) ) )
                                                         : light_get_sky( light_read_cell( chunk, light_index( n_local.x, n_local.y, n_local.z ) ) );
                if ( nv >= offered ) {
                    continue;
                }
                const BlockID n_id = light_cell_id_at( chunk.cells, n_local );
                if ( light_opaque_id( n_id ) ) {
                    continue;
                }
                if ( sd.ngates && !light_diag_open( chunk.cells, glm::ivec3( x, y, z ), sd, src_floor ) ) {
                    continue;
                }
                light_dbg_border_seed++;
                this->light_add_seed( base + glm::ivec3( x, y, z ), ch, cv );
                break;
            }
        }
    };
    for ( const int x : { -1, CHUNK_SIZE_X } ) {
        for ( int y = -1; y <= CHUNK_SIZE_Y; y++ ) {
            for ( int z = -1; z <= CHUNK_SIZE_Z; z++ ) {
                sync_halo_cell( x, y, z );
            }
        }
    }
    for ( const int y : { -1, CHUNK_SIZE_Y } ) {
        for ( int x = 0; x < CHUNK_SIZE_X; x++ ) {
            for ( int z = -1; z <= CHUNK_SIZE_Z; z++ ) {
                sync_halo_cell( x, y, z );
            }
        }
    }
    for ( const int z : { -1, CHUNK_SIZE_Z } ) {
        for ( int x = 0; x < CHUNK_SIZE_X; x++ ) {
            for ( int y = 0; y < CHUNK_SIZE_Y; y++ ) {
                sync_halo_cell( x, y, z );
            }
        }
    }

    // Pass 2: our interior shell -> neighbors' mirrors (fix stale + seed).
    // Same trick: enumerate the six interior faces directly instead of
    // scanning the whole volume and skipping non-shell cells.
    auto sync_interior_cell = [&]( const int x, const int y, const int z ) {
        const glm::ivec3 w = base + glm::ivec3( x, y, z );
        const unsigned short canon = light_read_cell( chunk, light_index( x, y, z ) );
        int rem_old[ 2 ] = { 0, 0 };

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
                    const int ox = axis_opts[ 0 ][ ix ];
                    const int oy = axis_opts[ 1 ][ iy ];
                    const int oz = axis_opts[ 2 ][ iz ];
                    if ( !ox && !oy && !oz ) {
                        continue;
                    }
                    Chunk *nb = nb_cache[ ( ox + 1 ) * 9 + ( oy + 1 ) * 3 + ( oz + 1 ) ];
                    if ( !nb || nb->load_state ) {
                        continue;
                    }
                    const glm::ivec3 n_local = w - nb->chunk_pos * CHUNK_SIZE_I;
                    if ( !light_local_ok( n_local ) ) {
                        continue; // slot recycled mid-write; torn chunk_pos
                    }
                    const int midx = light_index( n_local.x, n_local.y, n_local.z );
                    const unsigned short old_mirror = light_read_cell( *nb, midx );
                    if ( old_mirror == canon ) {
                        continue;
                    }
                    light_write_cell( *nb, midx, canon );
                    light_mark_dirty( *nb, n_local );
                    for ( int ch = 0; ch < 2; ch++ ) {
                        const int old_v = ch == LIGHT_CHANNEL_BLOCK ? light_get_block( old_mirror ) : light_get_sky( old_mirror );
                        const int new_v = ch == LIGHT_CHANNEL_BLOCK ? light_get_block( canon ) : light_get_sky( canon );
                        // The mirror reflects what neighbors last
                        // propagated with — removal of the stale
                        // value cleans their interior. Dedup: push at
                        // most one remove seed per (cell, channel),
                        // with the largest stale value seen.
                        if ( old_v > new_v && old_v > rem_old[ ch ] ) {
                            rem_old[ ch ] = old_v;
                        }
                    }
                }
            }
        }
        for ( int ch = 0; ch < 2; ch++ ) {
            const int new_v = ch == LIGHT_CHANNEL_BLOCK ? light_get_block( canon ) : light_get_sky( canon );
            if ( rem_old[ ch ] > new_v ) {
                light_dbg_remove_seed++;
                this->light_push_remove_seed( w, ch, rem_old[ ch ] );
            }
            if ( new_v <= LIGHT_STEP_FACE ) {
                continue; // offered = new_v-17 can't light anything
            }
            // Seed only if the spread would actually go somewhere: a
            // foreign-interior cell adjacent to w that is under-lit and
            // non-opaque. All 18 step dirs count — a diagonal across a
            // chunk corner is a real in-path under chamfer propagation.
            // Mass load-in otherwise pushes thousands of no-op seeds per
            // chunk — mirrors that needed syncing but whose interiors are
            // already lit.
            const BlockID w_id = light_cell_id_at( chunk.cells, glm::ivec3( x, y, z ) );
            const bool w_floor = block_definition_get_definition( w_id )->blocks_light_down;
            const glm::ivec3 w_loc( x, y, z );
            // Own-interior targets are handled by the propagation itself;
            // prune to dirs that actually leave the volume.
            uint32_t dmask = light_step_masks[ light_cell_class( x, y, z ) ].out;
            if ( w_floor ) {
                dmask &= ~light_step_down_mask;
            }
            for ( ; dmask; dmask &= dmask - 1 ) {
                const LightStepDir &sd = light_step_dirs[ light_ctz32( dmask ) ];
                const glm::ivec3 n_pos = w + sd.d;
                const glm::ivec3 rel = n_pos - base;
                const glm::ivec3 o( rel.x < 0 ? -1 : rel.x >= CHUNK_SIZE_X ? 1 : 0,
                                  rel.y < 0 ? -1 : rel.y >= CHUNK_SIZE_Y ? 1 : 0,
                                  rel.z < 0 ? -1 : rel.z >= CHUNK_SIZE_Z ? 1 : 0 );
                const Chunk *nax = nb_cache[ ( o.x + 1 ) * 9 + ( o.y + 1 ) * 3 + ( o.z + 1 ) ];
                if ( !nax || nax->load_state ) {
                    continue;
                }
                const glm::ivec3 n_loc = n_pos - nax->chunk_pos * CHUNK_SIZE_I;
                if ( !light_local_ok( n_loc ) ||
                     n_loc.x < 0 || n_loc.x >= CHUNK_SIZE_X || n_loc.y < 0 || n_loc.y >= CHUNK_SIZE_Y || n_loc.z < 0 ||
                     n_loc.z >= CHUNK_SIZE_Z ) {
                    continue; // torn chunk_pos, or not nax's interior
                }
                const int offered = ( ch == LIGHT_CHANNEL_SKY && sd.d.y == -1 && sd.ngates == 0 && new_v == LIGHT_RAW_MAX ) ? LIGHT_RAW_MAX
                                                                                                                         : new_v - sd.cost;
                const int nv = ch == LIGHT_CHANNEL_BLOCK ? light_get_block( light_read_cell( *nax, light_index( n_loc.x, n_loc.y, n_loc.z ) ) )
                                                         : light_get_sky( light_read_cell( *nax, light_index( n_loc.x, n_loc.y, n_loc.z ) ) );
                if ( nv >= offered ) {
                    continue;
                }
                const BlockID n_id = nax->cells ? block_cell_id( nax->cells[ nax->get_index_from_coords( n_loc.x, n_loc.y, n_loc.z ) ] ) : AIR;
                if ( light_opaque_id( n_id ) ) {
                    continue;
                }
                if ( sd.ngates && !light_diag_open( chunk.cells, w_loc, sd, w_floor ) ) {
                    continue;
                }
                light_dbg_border_seed++;
                this->light_add_seed( w, ch, new_v );
                break;
            }
        }
    };
    for ( const int x : { 0, CHUNK_SIZE_X - 1 } ) {
        for ( int y = 0; y < CHUNK_SIZE_Y; y++ ) {
            for ( int z = 0; z < CHUNK_SIZE_Z; z++ ) {
                sync_interior_cell( x, y, z );
            }
        }
    }
    for ( const int y : { 0, CHUNK_SIZE_Y - 1 } ) {
        for ( int x = 1; x < CHUNK_SIZE_X - 1; x++ ) {
            for ( int z = 0; z < CHUNK_SIZE_Z; z++ ) {
                sync_interior_cell( x, y, z );
            }
        }
    }
    for ( const int z : { 0, CHUNK_SIZE_Z - 1 } ) {
        for ( int x = 1; x < CHUNK_SIZE_X - 1; x++ ) {
            for ( int y = 1; y < CHUNK_SIZE_Y - 1; y++ ) {
                sync_interior_cell( x, y, z );
            }
        }
    }
}

void light_pending_enqueue( Chunk &chunk ) {
    {
        std::lock_guard<std::mutex> lock( light_work_mutex );
        if ( chunk.light.pending_listed ) {
            return;
        }
        chunk.light.pending_listed = 1;
        light_pending_list.push_back( &chunk );
    }
    light_work_cv.notify_one( );
}

// Process one pending-diffs chunk. Caller must own the chunk's column claim
// (light pool) or be the only thread doing light work (fallback path).
void ChunkLoader::light_process_pending_chunk( Chunk &chunk ) {
    // Clear the listed flag BEFORE reading the pending fields: a producer
    // that activates the chunk between the two must observe listed==0 and
    // re-enqueue, so no pending batch is ever dropped.
    chunk.light.pending_listed = 0;
    if ( chunk.load_state ) {
        return; // loading chunks drain their queue in light_finalize_chunk
    }
    if ( chunk.light.reseed ) {
        // Full refill (light_pending overflowed). light_fill_chunk wiped
        // skylight and reset sky_open_above, so re-run the column
        // cascade to flip the flags and reseed, plus a border sync. The
        // whole volume was rewritten, so mark it all dirty for upload.
        light_fill_chunk( chunk );
        chunk.light.reseed = 0;
        chunk.light.stale_clear( );
        this->light_cascade_columns( chunk );
        // Always scan — the refill's optimistic sky_open_above leaves the
        // cascade with nothing to fill, but shaded columns still need seeds.
        this->light_seed_interior_boundary( chunk );
        this->light_border_sync( chunk );
        ChunkLoader::light_mark_dirty_all( chunk );
    }
    for ( int p = 0; p < chunk.light.pending_count; p++ ) {
        int x, y, z;
        chunk.get_coords_from_index( chunk.light.pending[ p ], x, y, z );
        this->light_recheck_block( chunk.chunk_pos * CHUNK_SIZE_I + glm::ivec3( x, y, z ) );
    }
    chunk.light.pending_count = 0;
}

// Multiplayer diffs (set_block_by_index_if_different) only record cell
// indices on the chunk; turn them into rechecks here. Single-threaded path:
// render thread when the light pool isn't running.
void ChunkLoader::light_drain_pending( ) {
    if ( light_pending_list.empty( ) ) {
        return; // racing producers push under the lock; an empty peek is safe
    }
    std::vector<Chunk *> list;
    {
        std::lock_guard<std::mutex> lock( light_work_mutex );
        list.swap( light_pending_list );
    }
    for ( Chunk *chunk_ptr : list ) {
        this->light_process_pending_chunk( *chunk_ptr );
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
    // The memset below is a write the upload-quiet window must see: without
    // this stamp an already-listed chunk could upload mid-wipe, before the
    // cascade/marks restamp it — a one-frame full-volume black flash.
    chunk.light.write_tick.store( light_tick_counter.load( std::memory_order_relaxed ), std::memory_order_relaxed );
    if ( !chunk.cells ) {
        // All-air chunk: no light volume at all.light.volume==nullptr means every
        // cell holds light_uniform — set it to the same optimistic sky=15 the
        // volume path's prefill would produce, so propagation reads it without
        // the array ever existing. Column flags still get computed (all-open
        // + optimistic sky_open_above) so the cascade's flag bookkeeping
        // stays correct.
        if ( chunk.light.volume ) {
            chunk_free_light( chunk.light.volume );
            chunk.light.volume = nullptr;
        }
        // cells[] is gone, so its derived masks must be too (a stale
        // mask would describe the previous occupancy of this chunk slot).
        if ( chunk.light.air_mask ) {
            chunk_free_air_mask( chunk.light.air_mask );
            chunk.light.air_mask = nullptr;
        }
        if ( chunk.light.shade_mask ) {
            chunk_free_air_mask( chunk.light.shade_mask );
            chunk.light.shade_mask = nullptr;
        }
        chunk.light.uniform = static_cast<unsigned short>( dimension_has_sky( dimension_of_chunk( chunk.chunk_pos ) ) ? LIGHT_RAW_MAX << 8 : 0 );
        ChunkLoader::light_compute_column_open( chunk );
        if ( chunk.light.columns ) {
            memset( chunk.light.columns + LIGHT_FLAGS_COUNT, dimension_has_sky( dimension_of_chunk( chunk.chunk_pos ) ) ? 1 : 0,
                    LIGHT_FLAGS_COUNT );
        }
        return;
    }
    if ( !chunk.light.volume ) {
        chunk.light.volume = chunk_alloc_light( );
    }
    std::fill_n( chunk.light.volume, CHUNK_BLOCK_SIZE, static_cast<unsigned short>( 0 ) );
    // The mipmapped light texture's B channel is a per-cell air mask, and
    // computing it at upload time walks chunk.cells[] on the render
    // thread — ~0.2ms/chunk walking the 0.5MB cells[] array.
    // Instead cache it as a 1-bit-per-cell bitmap (32KB) built here, where
    // cells[] is already being streamed: bit i (same index space as
    // cells[]) is set when the cell can hold light. Post-fill writes
    // restamp individual bits via light_air_mask_set; upload paths fall back
    // to the cells[] walk whenever the mask is missing. RG8 mode pays
    // nothing — the mask is only allocated under light_mipmaps_enabled().
    unsigned char *air_mask = nullptr;
    unsigned char *shade_mask = nullptr;
    if ( light_mipmaps_enabled( ) ) {
        if ( chunk.light.air_mask == nullptr ) {
            chunk.light.air_mask = chunk_alloc_air_mask( );
        }
        if ( chunk.light.shade_mask == nullptr ) {
            chunk.light.shade_mask = chunk_alloc_air_mask( );
        }
        // Rewritten wholesale below (this can re-run via light_reseed).
        memset( chunk.light.air_mask, 0, CHUNK_BLOCK_SIZE / 8 );
        memset( chunk.light.shade_mask, 0, CHUNK_BLOCK_SIZE / 8 );
        air_mask = chunk.light.air_mask;
        shade_mask = chunk.light.shade_mask;
    } else {
        // Can't happen (mipmaps is process-lifetime constant), but never
        // leave a mask around that upload would wrongly trust.
        if ( chunk.light.air_mask ) {
            chunk_free_air_mask( chunk.light.air_mask );
            chunk.light.air_mask = nullptr;
        }
        if ( chunk.light.shade_mask ) {
            chunk_free_air_mask( chunk.light.shade_mask );
            chunk.light.shade_mask = nullptr;
        }
    }
    // Note the two arrays index differently: cells[] is z-fastest
    // (Chunk::get_index_from_coords) while light[] is x-fastest
    // (light_index, matching the glTexSubImage3D upload layout). Scan cells[]
    // linearly — index conversion only happens for the rare emitters.
    std::vector<int> queue;
    for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
        // One definition fetch per cell feeds both the air-mask bit and the
        // emitter check (light_emit_at did the same lookup for emit alone).
        const BlockID id = block_cell_id( chunk.cells[ i ] );
        const Block *def = block_definition_get_definition( id );
        if ( air_mask && !light_opaque_id( id ) ) {
            air_mask[ i >> 3 ] |= static_cast<unsigned char>( 1u << ( i & 7 ) );
        }
        if ( shade_mask && def->blocks_light_down ) {
            shade_mask[ i >> 3 ] |= static_cast<unsigned char>( 1u << ( i & 7 ) );
        }
        const int emit = def->emits_light;
        if ( emit <= 0 ) {
            continue;
        }
        int x, y, z;
        chunk.get_coords_from_index( i, x, y, z );
        const int lidx = light_index( x, y, z );
        chunk.light.volume[ lidx ] = static_cast<unsigned short>( emit * LIGHT_RAW_PER_LEVEL );
        queue.push_back( lidx );
    }
    while ( !queue.empty( ) ) {
        const int idx = queue.back( );
        queue.pop_back( );
        const int v = chunk.light.volume[ idx ] & 0xFF;
        if ( v <= LIGHT_STEP_FACE ) {
            continue; // best possible offer (v-17) can't light anything
        }
        // light_index layout: x fastest, then y, then z.
        const int lx = ( idx % CHUNK_SIZE_INTERNAL_X ) - 1;
        const int ly = ( ( idx / CHUNK_SIZE_INTERNAL_X ) % CHUNK_SIZE_INTERNAL_Y ) - 1;
        const int lz = ( idx / ( CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Y ) ) - 1;
        const glm::ivec3 c_local( lx, ly, lz );
        // Same source rule as light_add_step: a floor cell holds light but
        // never spreads it downward.
        const bool src_blocks_down = block_definition_get_definition( block_cell_id( chunk.cells[ chunk.get_index_from_coords( lx, ly, lz ) ] ) )->blocks_light_down;
        for ( int d = 0; d < LIGHT_STEP_COUNT; d++ ) {
            const LightStepDir &sd = light_step_dirs[ d ];
            if ( src_blocks_down && sd.d.y < 0 ) {
                continue;
            }
            const int nx = lx + sd.d.x;
            const int ny = ly + sd.d.y;
            const int nz = lz + sd.d.z;
            if ( nx < -1 || nx >= CHUNK_SIZE_X + 1 || ny < -1 || ny >= CHUNK_SIZE_Y + 1 || nz < -1 || nz >= CHUNK_SIZE_Z + 1 ) {
                continue;
            }
            const int nidx = light_index( nx, ny, nz );
            if ( v - sd.cost <= ( chunk.light.volume[ nidx ] & 0xFF ) ) {
                continue;
            }
            const BlockID n_id = block_cell_id( chunk.cells[ chunk.get_index_from_coords( nx, ny, nz ) ] );
            if ( light_opaque_id( n_id ) ) {
                continue;
            }
            const int offered = v - sd.cost - block_definition_get_definition( n_id )->light_absorb * LIGHT_RAW_PER_LEVEL;
            if ( offered <= ( chunk.light.volume[ nidx ] & 0xFF ) ) {
                continue;
            }
            if ( sd.ngates && !light_diag_open( chunk.cells, c_local, sd, src_blocks_down ) ) {
                continue;
            }
            chunk.light.volume[ nidx ] = static_cast<unsigned short>( ( chunk.light.volume[ nidx ] & 0xFF00 ) | offered );
            queue.push_back( nidx );
        }
    }
    ChunkLoader::light_compute_column_open( chunk );
    if ( chunk.light.columns && dimension_has_sky( dimension_of_chunk( chunk.chunk_pos ) ) ) {
        // Optimistic skylight: assume open above so the chunk doesn't draw
        // black in the window between terrain upload and light finalize.
        // Cells above the topmost opaque cell get a direct sky=15 fill, and
        // sky_open_above=1 records the assumption — if above turns out
        // closed, the cascade flips the flag and the removal BFS clears it.
        // The fill covers the 1-cell halo shell too (meshes sample it at
        // chunk faces): the halo blocks are real terrain data generated and
        // persisted with the chunk, so the edge lights to its own surface
        // line instead of the memset-0 dark edge it had until the
        // neighbors' border sync converged. A wrong guess self-corrects —
        // the owner chunk's border sync rewrites the mirror and pushes a
        // removal when it loads.
        unsigned char *flags = chunk.light.columns;
        for ( int x = -1; x <= CHUNK_SIZE_X; x++ ) {
            for ( int z = -1; z <= CHUNK_SIZE_Z; z++ ) {
                const bool interior_col = x >= 0 && x < CHUNK_SIZE_X && z >= 0 && z < CHUNK_SIZE_Z;
                int top_opaque = -2; // -2 = the whole column is non-opaque
                for ( int y = CHUNK_SIZE_Y; y >= -1; y-- ) {
                    if ( light_column_blocked_id( block_cell_id( chunk.cells[ chunk.get_index_from_coords( x, y, z ) ] ) ) ) {
                        top_opaque = y;
                        break;
                    }
                }
                // Interior cells keep the unconditional open-above fill from
                // fill_from (an opaque mirror capping the column at the y=32
                // shell row doesn't stop it — the cascade owns that call).
                // Shell cells get their column's real surface line.
                int fill_from = top_opaque + 1;
                if ( interior_col ) {
                    const int ci = LIGHT_COLUMN_INDEX( x, z );
                    flags[ LIGHT_FLAGS_COUNT + ci ] = 1;
                    fill_from = flags[ LIGHT_FILL_FROM_INDEX( x, z ) ];
                }
                for ( int y = -1; y <= CHUNK_SIZE_Y; y++ ) {
                    const bool shell = x < 0 || x >= CHUNK_SIZE_X || y < 0 || y >= CHUNK_SIZE_Y || z < 0 || z >= CHUNK_SIZE_Z;
                    if ( y < ( shell ? top_opaque + 1 : fill_from ) ) {
                        continue;
                    }
                    unsigned short &cell = chunk.light.volume[ light_index( x, y, z ) ];
                    cell = static_cast<unsigned short>( ( cell & 0x00FF ) | ( LIGHT_RAW_MAX << 8 ) );
                }
            }
        }
    }
    // Fold constant fills back to the uniform representation — a fully-dark
    // underground chunk (no emitters, all columns closed) or a fully-lit one
    // keeps just the u16 instead of the 512K array.
    {
        const unsigned short first = chunk.light.volume[ 0 ];
        bool uniform = true;
        for ( int i = 1; i < CHUNK_BLOCK_SIZE; i++ ) {
            if ( chunk.light.volume[ i ] != first ) {
                uniform = false;
                break;
            }
        }
        if ( uniform ) {
            chunk.light.uniform = first;
            chunk_free_light( chunk.light.volume );
            chunk.light.volume = nullptr;
        }
    }
}

