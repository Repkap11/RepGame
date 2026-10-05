#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cstring>
#include <limits>
#include <math.h>
#include <mutex>
#include <thread>
#include <unordered_set>
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
static long long light_dbg_pq_calls = 0, light_dbg_pq_nonempty = 0;
static long long light_dbg_loop_top = 0, light_dbg_after_jobs = 0, light_dbg_jobs_n = 0;
static long long light_dbg_pushes = 0, light_dbg_pops = 0, light_dbg_finalize = 0,
                 light_dbg_cascade_seed = 0, light_dbg_border_seed = 0,
                 light_dbg_recheck_seed = 0, light_dbg_step_push = 0, light_dbg_remove_seed = 0;
// Seeds that died because their cell's chunk was missing/still loading when
// popped. The receiving chunk's own finalize normally re-seeds coverage, so a
// nonzero count is not itself a bug — but a high count plus persistent dark
// areas means dropped work that nothing re-covered.
static std::atomic<long long> light_dbg_dropped{ 0 };
// TEMP frame-phase timing (µs accumulated between probes) + call counts.
long long light_dbg_us_finalize = 0, light_dbg_us_remesh = 0, light_dbg_us_drain = 0,
          light_dbg_us_bfs = 0, light_dbg_us_upload = 0, light_dbg_us_bind = 0,
          light_dbg_us_ensure = 0, light_dbg_n_upload = 0, light_dbg_n_ensure = 0,
          light_dbg_us_light_fin = 0, light_dbg_us_fin_fill = 0, light_dbg_us_fin_casc = 0,
          light_dbg_us_fin_bscan = 0, light_dbg_us_fin_border = 0;
void ChunkLoader::light_dbg_stats( char *buf, size_t n ) {
    snprintf( buf, n, "loop=%lld aj=%lld jn=%lld pq=%lld/%lld push=%lld pop=%lld fin=%lld casc=%lld bord=%lld rech=%lld step=%lld rem=%lld drop=%lld"
                      " | us: fin=%lld(lfin=%lld[fill=%lld casc=%lld bscan=%lld border=%lld]) remesh=%lld drain=%lld bfs=%lld upl=%lld(n=%lld) bind=%lld ensure=%lld(n=%lld)",
              light_dbg_loop_top, light_dbg_after_jobs, light_dbg_jobs_n,
              light_dbg_pq_calls, light_dbg_pq_nonempty,
              light_dbg_pushes, light_dbg_pops, light_dbg_finalize, light_dbg_cascade_seed,
              light_dbg_border_seed, light_dbg_recheck_seed, light_dbg_step_push, light_dbg_remove_seed,
              light_dbg_dropped.load( ),
              light_dbg_us_finalize, light_dbg_us_light_fin, light_dbg_us_fin_fill,
              light_dbg_us_fin_casc, light_dbg_us_fin_bscan, light_dbg_us_fin_border,
              light_dbg_us_remesh, light_dbg_us_drain,
              light_dbg_us_bfs, light_dbg_us_upload, light_dbg_n_upload, light_dbg_us_bind,
              light_dbg_us_ensure, light_dbg_n_ensure );
    light_dbg_pushes = light_dbg_pops = light_dbg_finalize = light_dbg_cascade_seed =
        light_dbg_border_seed = light_dbg_recheck_seed = light_dbg_step_push = light_dbg_remove_seed = 0;
    // loop/aj/jn/pq are cumulative (not reset) — reset races with the light
    // thread would otherwise print impossible combinations.
    light_dbg_us_finalize = light_dbg_us_remesh = light_dbg_us_drain = light_dbg_us_bfs =
        light_dbg_us_upload = light_dbg_us_bind = light_dbg_us_ensure = light_dbg_n_upload =
            light_dbg_n_ensure = light_dbg_us_light_fin = light_dbg_us_fin_fill =
                light_dbg_us_fin_casc = light_dbg_us_fin_bscan = light_dbg_us_fin_border = 0;
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

// Approximate count of queued BFS seeds, kept atomic so light workers can
// test "queue nonempty" from the cv predicate without touching the deques
// (a claimed job may be pushing while another worker waits). ++ on every
// push_back, -- on every pop_front below.
static std::atomic<long long> light_bfs_pending{ 0 };

// Workers holding disjoint column claims push seeds concurrently, so the
// shared deques need serialization on PUSH. Pops are unlocked: the BFS
// drain only runs while no claims are in flight, so nothing pushes then.
// Tiny critical sections (one deque push) → spinlock.
static std::atomic_flag light_seed_lock = ATOMIC_FLAG_INIT;
struct LightSeedGuard {
    LightSeedGuard( ) {
        while ( light_seed_lock.test_and_set( std::memory_order_acquire ) ) {
        }
    }
    ~LightSeedGuard( ) {
        light_seed_lock.clear( std::memory_order_release );
    }
};

// level is the light value the cell holds at push time; it only determines
// which bucket the seed lands in (higher first), not what gets propagated.
void ChunkLoader::light_add_seed( const glm::ivec3 &block_pos, const int channel, const int level ) {
    const int clamped = level < 0 ? 0 : level > LIGHT_MAX_LEVEL ? LIGHT_MAX_LEVEL : level;
    light_dbg_pushes++;
    LightSeedGuard guard;
    this->light_add_queue[ channel ][ clamped ].push_back( { block_pos, static_cast<unsigned char>( channel ) } );
    light_bfs_pending++;
}

// ---------------------------------------------------------------------------
// Cell access
// ---------------------------------------------------------------------------

// Internal coords -1..CHUNK_SIZE_* index into the 34^3 volume. A cached Chunk*
// can refer to a slot recycled mid-access (chunk_pos torn), producing a
// garbage local — guard before indexing or it writes/reads out of bounds.
static inline bool light_local_ok( const glm::ivec3 &l ) {
    return l.x >= -1 && l.x <= CHUNK_SIZE_X && l.y >= -1 && l.y <= CHUNK_SIZE_Y && l.z >= -1 && l.z <= CHUNK_SIZE_Z;
}

// Read the canonical (interior-owner) light value. Returns -1 when the cell's
// owner chunk isn't loaded / is still generating / has no light volume — the
// propagation treats it as out-of-bounds.
int ChunkLoader::light_get( const glm::ivec3 &block_pos, const int channel ) const {
    const Chunk *chunk = this->get_chunk( light_chunk_pos_of( block_pos ) );
    if ( !chunk || chunk->is_loading || !chunk->light ) {
        return -1;
    }
    const glm::ivec3 local = block_pos - chunk->chunk_pos * CHUNK_SIZE_I;
    if ( !light_local_ok( local ) ) {
        return -1; // chunk_pos raced with slot reuse; treat as out-of-bounds
    }
    const unsigned char v = chunk->light[ light_index( local.x, local.y, local.z ) ];
    return channel == LIGHT_CHANNEL_BLOCK ? light_get_block( v ) : light_get_sky( v );
}

BlockID ChunkLoader::light_block_id_at( const glm::ivec3 &block_pos ) const {
    const Chunk *chunk = this->get_chunk( light_chunk_pos_of( block_pos ) );
    if ( !chunk || chunk->is_loading || !chunk->blocks ) {
        return LAST_BLOCK_ID;
    }
    const glm::ivec3 local = block_pos - chunk->chunk_pos * CHUNK_SIZE_I;
    if ( !light_local_ok( local ) ) {
        return LAST_BLOCK_ID; // chunk_pos raced with slot reuse
    }
    return chunk->get_block( local ).id;
}

// ---------------------------------------------------------------------------
// Lighting thread work queue (docs/lighting-thread-plan.md)
//
// All light[]/light_columns mutation happens on ONE thread — the dedicated
// lighting thread on native, or the calling thread when it isn't running
// (tests, WASM without pthreads). Cross-thread traffic is three lists under
// light_work_mutex: jobs (finalize/recheck), pending-diff chunks (produced by
// network/worker threads via light_pending_enqueue), and dirty-upload chunks
// (produced by the light thread, consumed by render's light_upload_dirty).
// ---------------------------------------------------------------------------

enum LightJobType { LIGHT_JOB_FINALIZE, LIGHT_JOB_RECHECK };
struct LightJob {
    LightJobType type;
    Chunk *chunk;
    glm::ivec3 pos; // FINALIZE: expected chunk_pos; RECHECK: block pos
};

static std::mutex light_work_mutex;
static std::condition_variable light_work_cv;
static std::deque<LightJob> light_jobs;
static std::vector<Chunk *> light_pending_list;
static std::vector<Chunk *> light_dirty_list;

// Parallel workers (docs/lighting-thread-plan.md). Two jobs conflict iff
// their chunk columns are <=2 apart in x AND z (halo mirrors reach +-1
// chunk, the sky cascade the whole column) -- a job claims the 5x5 column
// footprint of its column before running, all-or-nothing under the mutex.
// The shared BFS queues can't be claimed per-seed, so light_process_queue
// is mutually exclusive with claimed work via light_bfs_active.
static std::unordered_set<long long> light_claims; // claimed (x,z) columns
static volatile int light_bfs_active = 0;
// Backlog-priority window: while set, no NEW finalize claims are granted so
// in-flight claims drain and a BFS drain can run; interactive work (rechecks,
// pending diffs) still claims so block edits stay responsive. Hysteresis
// (HI/LO) plus a time cap keep the window from starving finalizes forever.
static volatile int light_bfs_priority = 0;
#define LIGHT_BFS_PRI_HI 3000000
#define LIGHT_BFS_PRI_LO 400000
#define LIGHT_BFS_PRI_MAX_US 100000

static inline long long light_col_key( int cx, int cz ) {
    return ( static_cast<long long>( cx ) << 32 ) | static_cast<unsigned int>( cz );
}

// Footprint is free (no claim in flight overlapping cx,cz +-2)? Also requires
// no BFS drain running — a popped seed writes cells anywhere, unconstrained —
// and, unless bypass_priority, that no backlog-priority window is in effect.
static bool light_claim_free( int cx, int cz, const bool bypass_priority ) {
    if ( light_bfs_active || ( light_bfs_priority && !bypass_priority ) ) {
        return false;
    }
    for ( int dx = -2; dx <= 2; dx++ ) {
        for ( int dz = -2; dz <= 2; dz++ ) {
            if ( light_claims.count( light_col_key( cx + dx, cz + dz ) ) ) {
                return false;
            }
        }
    }
    return true;
}

// Stamp the 5x5 footprint. Caller holds light_work_mutex. On conflict,
// already-inserted keys are rolled back (only ours — erase could otherwise
// remove a co-claimer's key).
static bool light_try_claim( int cx, int cz, const bool bypass_priority ) {
    if ( light_bfs_active || ( light_bfs_priority && !bypass_priority ) ) {
        return false;
    }
    long long claimed[ 25 ];
    int n = 0;
    for ( int dx = -2; dx <= 2; dx++ ) {
        for ( int dz = -2; dz <= 2; dz++ ) {
            const long long key = light_col_key( cx + dx, cz + dz );
            if ( !light_claims.insert( key ).second ) {
                while ( n > 0 ) {
                    light_claims.erase( claimed[ --n ] );
                }
                return false;
            }
            claimed[ n++ ] = key;
        }
    }
    return true;
}

static void light_release_claim( int cx, int cz ) {
    for ( int dx = -2; dx <= 2; dx++ ) {
        for ( int dz = -2; dz <= 2; dz++ ) {
            light_claims.erase( light_col_key( cx + dx, cz + dz ) );
        }
    }
}

#define LIGHT_WORKER_COUNT 4
static std::thread light_threads[ LIGHT_WORKER_COUNT ];
static volatile int light_thread_running = 0;
static volatile int light_thread_stop_flag = 0;

// Pack/unpack helpers for Chunk::light_dirty_box (layout in chunk.hpp).
// Texel coords are in [0,34) so 6 bits per component; LIGHT_BOX_EMPTY has
// min=63 > max=0.
static inline uint64_t light_box_pack( const glm::ivec3 &mn, const glm::ivec3 &mx ) {
    return static_cast<uint64_t>( mn.x & 63 ) | ( static_cast<uint64_t>( mn.y & 63 ) << 6 ) |
           ( static_cast<uint64_t>( mn.z & 63 ) << 12 ) | ( static_cast<uint64_t>( mx.x & 63 ) << 18 ) |
           ( static_cast<uint64_t>( mx.y & 63 ) << 24 ) | ( static_cast<uint64_t>( mx.z & 63 ) << 30 );
}

static inline glm::ivec3 light_box_min( const uint64_t box ) {
    return glm::ivec3( static_cast<int>( box & 63 ), static_cast<int>( ( box >> 6 ) & 63 ),
                       static_cast<int>( ( box >> 12 ) & 63 ) );
}

static inline glm::ivec3 light_box_max( const uint64_t box ) {
    return glm::ivec3( static_cast<int>( ( box >> 18 ) & 63 ), static_cast<int>( ( box >> 24 ) & 63 ),
                       static_cast<int>( ( box >> 30 ) & 63 ) );
}

static inline uint64_t light_box_merge( const uint64_t box, const glm::ivec3 &texel ) {
    const glm::ivec3 mn = light_box_min( box );
    if ( mn.x > light_box_max( box ).x ) {
        return light_box_pack( texel, texel ); // EMPTY
    }
    return light_box_pack( glm::min( mn, texel ), glm::max( light_box_max( box ), texel ) );
}

void ChunkLoader::light_upload_enqueue( Chunk &chunk ) {
    // Hot path: only the first mark per chunk takes the lock.
    if ( !chunk.light_upload_listed.load( std::memory_order_acquire ) ) {
        std::lock_guard<std::mutex> lock( light_work_mutex );
        if ( !chunk.light_upload_listed ) {
            chunk.light_upload_listed = 1;
            light_dirty_list.push_back( &chunk );
        }
    }
}

void ChunkLoader::light_mark_dirty( Chunk &chunk, const glm::ivec3 &local ) {
    const glm::ivec3 texel = local + glm::ivec3( 1 ); // internal -> texture coords
    // CAS-merge the texel into the dirty box BEFORE the listed check: the
    // render thread clears listed and exchanges the box in the same critical
    // section, so a mark whose CAS misses that exchange is guaranteed to
    // observe listed==0 and re-add the chunk — no lost updates. The CAS runs
    // even when the box already covers the texel: as an acquire RMW it both
    // orders the caller's light[] byte store ahead of the uploader's copy
    // and synchronizes with the exchange so the listed==0 check below is
    // reliable.
    uint64_t box = chunk.light_dirty_box.load( std::memory_order_relaxed );
    for ( ;; ) {
        const uint64_t merged = light_box_merge( box, texel );
        if ( chunk.light_dirty_box.compare_exchange_weak( box, merged, std::memory_order_acq_rel,
                                                          std::memory_order_relaxed ) ) {
            break;
        }
    }
    light_upload_enqueue( chunk );
}

// Whole-volume variant for finalize/reseed paths that rewrite everything.
// Dirty coords are texel-space (internal+1), so the full 34^3 texture is
// 0..CHUNK_SIZE_INTERNAL_-1 inclusive.
void ChunkLoader::light_mark_dirty_all( Chunk &chunk ) {
    // The full-volume box is the union lattice's top element — a plain store
    // can't lose a concurrent mark (any merge into it is a no-op).
    chunk.light_dirty_box.store(
        light_box_pack( glm::ivec3( 0 ),
                        glm::ivec3( CHUNK_SIZE_INTERNAL_X - 1, CHUNK_SIZE_INTERNAL_Y - 1, CHUNK_SIZE_INTERNAL_Z - 1 ) ),
        std::memory_order_release );
    light_upload_enqueue( chunk );
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
                if ( !light_local_ok( c_local ) ) {
                    continue; // slot recycled mid-write; torn chunk_pos
                }
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
                if ( !c || c->is_loading || !c->light ) {
                    continue;
                }
                const glm::ivec3 c_local = block_pos - c->chunk_pos * CHUNK_SIZE_I;
                if ( !light_local_ok( c_local ) ) {
                    continue; // slot recycled mid-write; torn chunk_pos
                }
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
        light_dbg_dropped++;
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
        light_dbg_dropped++;
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
            {
                LightSeedGuard guard;
                this->light_remove_queue.push_back( { n, seed.channel, static_cast<unsigned char>( nv ) } );
                light_bfs_pending++;
            }
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
    if ( this->light_add_queue_size( ) + this->light_remove_queue.size( ) > 0 ) {
        light_dbg_pq_nonempty++;
    }
    light_dbg_pq_calls++;
    for ( ;; ) {
        if ( light_now_us( ) - start >= budget_us ) {
            return;
        }
        if ( !this->light_remove_queue.empty( ) ) {
            const LightRemoveSeed seed = this->light_remove_queue.front( );
            this->light_remove_queue.pop_front( );
            light_bfs_pending--;
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
                    light_bfs_pending--;
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
        chunk.light_columns = static_cast<unsigned char *>( calloc( 3 * LIGHT_FLAGS_COUNT, 1 ) );
        light_compute_column_open( chunk );
    }
}

// Scan one interior column top-down: column_open=1 iff no casts_shadow cell,
// and fill_from = one past the topmost opaque cell (the first y skylight
// reaches). The scan stops at the first opaque cell, so closed columns cost
// just the air above the surface; fully-open columns get fill_from=0.
void ChunkLoader::light_compute_one_column( Chunk &chunk, const int x, const int z ) {
    unsigned char open = 1;
    int fill_from = 0;
    for ( int y = CHUNK_SIZE_Y - 1; y >= 0; y-- ) {
        if ( light_opaque_id( chunk.blocks[ Chunk::get_index_from_coords( x, y, z ) ].id ) ) {
            open = 0;
            fill_from = y + 1;
            break;
        }
    }
    const int ci = LIGHT_COLUMN_INDEX( x, z );
    chunk.light_columns[ ci ] = open;
    chunk.light_columns[ LIGHT_FILL_FROM_INDEX( x, z ) ] = static_cast<unsigned char>( fill_from );
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
                // not-yet-cascaded neighbors still read 0. fill_from is one
                // past the topmost opaque cell (see light.hpp), so the fill
                // needs no per-cell blocks[] reads.
                const int fill_from = chunk->light_columns[ LIGHT_FILL_FROM_INDEX( lx, lz ) ];
                for ( int y = CHUNK_SIZE_Y - 1; y >= fill_from; y-- ) {
                    const glm::ivec3 pos( world_x, cy * CHUNK_SIZE_Y + y, world_z );
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
                        {
                            LightSeedGuard guard;
                            this->light_remove_queue.push_back( { pos, LIGHT_CHANNEL_SKY, static_cast<unsigned char>( v ) } );
                            light_bfs_pending++;
                        }
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
                    if ( was_open ) {
                        // Sky flows straight down at full strength — write the
                        // descent directly instead of seeding BFS floods (see
                        // light_cascade_column for the ordering rationale).
                        // fill_from (computed on the worker) is one past the
                        // topmost opaque cell, so the fill is a pure range
                        // write with no per-cell blocks[] reads. Dirty marking
                        // is batched: the whole volume is flagged once at the
                        // end instead of per cell.
                        const int fill_from = flags[ LIGHT_FILL_FROM_INDEX( x, z ) ];
                        const bool col_interior = x > 0 && x < CHUNK_SIZE_X - 1 && z > 0 && z < CHUNK_SIZE_Z - 1;
                        for ( int y = CHUNK_SIZE_Y - 1; y >= fill_from; y-- ) {
                            if ( col_interior && y > 0 && y < CHUNK_SIZE_Y - 1 ) {
                                unsigned char &cell = chunk->light[ light_index( x, y, z ) ];
                                cell = static_cast<unsigned char>( ( cell & 0x0F ) | ( LIGHT_MAX_LEVEL << 4 ) );
                            } else {
                                // y-shell cells mirror into the chunks
                                // above/below; x/z shell cells mirror
                                // sideways. Those need the mirror write.
                                this->light_set_nb( nb_cache, *chunk, glm::ivec3( base_x + x, base_y + y, base_z + z ), LIGHT_CHANNEL_SKY, LIGHT_MAX_LEVEL );
                            }
                            cells_filled++;
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
                                {
                                    LightSeedGuard guard;
                                    this->light_remove_queue.push_back( { pos, LIGHT_CHANNEL_SKY, static_cast<unsigned char>( v ) } );
                                    light_bfs_pending++;
                                }
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
        unsigned char &col_flag = chunk->light_columns[ LIGHT_COLUMN_INDEX( local.x, local.z ) ];
        const bool was_open = col_flag != 0;
        light_compute_one_column( *chunk, local.x, local.z );
        if ( was_open != ( col_flag != 0 ) ) {
            this->light_cascade_column( block_pos.x, block_pos.z );
            if ( !was_open ) {
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
                {
                    LightSeedGuard guard;
                    this->light_remove_queue.push_back( { block_pos, static_cast<unsigned char>( ch ), static_cast<unsigned char>( old_v ) } );
                    light_bfs_pending++;
                }
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
    const long long t0 = light_now_us( );
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
void ChunkLoader::light_seed_interior_boundary( Chunk &chunk ) {
    if ( !chunk.light_columns ) {
        return;
    }
    const glm::ivec3 base = chunk.chunk_pos * CHUNK_SIZE_I;
    const unsigned char *flags = chunk.light_columns;

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
    static const glm::ivec2 xz_dirs[ 4 ] = { glm::ivec2( 1, 0 ), glm::ivec2( -1, 0 ), glm::ivec2( 0, 1 ), glm::ivec2( 0, -1 ) };
    for ( int x = 0; x < CHUNK_SIZE_X; x++ ) {
        for ( int z = 0; z < CHUNK_SIZE_Z; z++ ) {
            const int ci = LIGHT_COLUMN_INDEX( x, z );
            const int lit_top = flags[ LIGHT_FLAGS_COUNT + ci ] ? flags[ LIGHT_FILL_FROM_INDEX( x, z ) ] : CHUNK_SIZE_Y;
            if ( lit_top >= CHUNK_SIZE_Y ) {
                continue; // no lit cells in this column
            }
            // Per lateral dir: interior columns get a lit_top cutoff; shell
            // dirs need a per-cell cross-chunk check.
            int nlt[ 4 ];
            int m = lit_top;
            for ( int d = 0; d < 4; d++ ) {
                const int nx = x + xz_dirs[ d ].x;
                const int nz = z + xz_dirs[ d ].y;
                if ( nx >= 0 && nx < CHUNK_SIZE_X && nz >= 0 && nz < CHUNK_SIZE_Z ) {
                    const int nci = LIGHT_COLUMN_INDEX( nx, nz );
                    nlt[ d ] = flags[ LIGHT_FLAGS_COUNT + nci ] ? flags[ LIGHT_FILL_FROM_INDEX( nx, nz ) ] : CHUNK_SIZE_Y;
                    // Bound is the DEEPEST dark neighbor: our lit cell at y
                    // borders darkness iff y < nlt[d] for some dir, so the
                    // scan must reach max(nlt), not min — with min, a column
                    // between open and shadowed neighbors (e.g. under a tree
                    // canopy) seeded nothing and the shadow stayed dark.
                    if ( nlt[ d ] > m ) {
                        m = nlt[ d ];
                    }
                } else {
                    nlt[ d ] = -1; // shell dir: per-cell cross-chunk check
                }
            }
            // Interior lateral bands: our lit cells below a neighbor's lit_top
            // border below-fill cells (stored sky=0 in a fresh volume).
            for ( int y = lit_top; y < m; y++ ) {
                for ( int d = 0; d < 4; d++ ) {
                    if ( nlt[ d ] < 0 || y >= nlt[ d ] ) {
                        continue;
                    }
                    const int nx = x + xz_dirs[ d ].x;
                    const int nz = z + xz_dirs[ d ].y;
                    const int cv = light_get_sky( chunk.light[ light_index( x, y, z ) ] );
                    if ( light_get_sky( chunk.light[ light_index( nx, y, nz ) ] ) >= cv - 1 ) {
                        continue;
                    }
                    if ( light_opaque_id( chunk.blocks[ chunk.get_index_from_coords( nx, y, nz ) ].id ) ) {
                        continue;
                    }
                    light_dbg_cascade_seed++;
                    this->light_add_seed( base + glm::ivec3( x, y, z ), LIGHT_CHANNEL_SKY, cv );
                    break;
                }
            }
            // Boundary cells: lit cells on the volume edge can border
            // under-lit cells in neighboring chunks regardless of column
            // fills. Up is the cell at y=31; down is y=0 (only reachable in a
            // fully-lit column); lateral shell dirs check every lit cell. The
            // neighbor of an axis-aligned dir crosses exactly one chunk face,
            // so it lands in nb_cache[(dir+1)] with the local coord wrapped.
            for ( int d = 2; d <= 3; d++ ) {
                const int y = ( d == 2 ) ? CHUNK_SIZE_Y - 1 : 0;
                if ( y < lit_top ) {
                    continue;
                }
                const glm::ivec3 dir = light_dirs[ d ];
                const Chunk *nchunk = nb_cache[ ( dir.x + 1 ) * 9 + ( dir.y + 1 ) * 3 + ( dir.z + 1 ) ];
                if ( !nchunk || nchunk->is_loading || !nchunk->light ) {
                    continue;
                }
                const int cv = light_get_sky( chunk.light[ light_index( x, y, z ) ] );
                const int offered = ( d == LIGHT_DIR_DOWN && cv == LIGHT_MAX_LEVEL ) ? LIGHT_MAX_LEVEL : cv - 1;
                const int ny = y + dir.y;
                const int nwy = ny < 0 ? CHUNK_SIZE_Y - 1 : ny >= CHUNK_SIZE_Y ? 0 : ny;
                const int nv = light_get_sky( nchunk->light[ light_index( x, nwy, z ) ] );
                if ( cv > 1 && offered > 0 && nv < offered ) {
                    const BlockID n_id = nchunk->blocks ? nchunk->blocks[ nchunk->get_index_from_coords( x, nwy, z ) ].id : LAST_BLOCK_ID;
                    if ( !light_opaque_id( n_id ) ) {
                        light_dbg_cascade_seed++;
                        this->light_add_seed( base + glm::ivec3( x, y, z ), LIGHT_CHANNEL_SKY, cv );
                    }
                }
            }
            for ( int d = 0; d < 4; d++ ) {
                if ( nlt[ d ] >= 0 ) {
                    continue; // interior dir — covered by the band loop above
                }
                const glm::ivec2 dir2 = xz_dirs[ d ];
                const Chunk *nchunk = nb_cache[ ( dir2.x + 1 ) * 9 + ( 0 + 1 ) * 3 + ( dir2.y + 1 ) ];
                if ( !nchunk || nchunk->is_loading || !nchunk->light ) {
                    continue;
                }
                const int nx = x + dir2.x;
                const int nz = z + dir2.y;
                const int nwx = nx < 0 ? CHUNK_SIZE_X - 1 : nx >= CHUNK_SIZE_X ? 0 : nx;
                const int nwz = nz < 0 ? CHUNK_SIZE_Z - 1 : nz >= CHUNK_SIZE_Z ? 0 : nz;
                for ( int y = lit_top; y < CHUNK_SIZE_Y; y++ ) {
                    const int cv = light_get_sky( chunk.light[ light_index( x, y, z ) ] );
                    if ( cv <= 1 ) {
                        continue;
                    }
                    const int nv = light_get_sky( nchunk->light[ light_index( nwx, y, nwz ) ] );
                    if ( nv < cv - 1 ) {
                        const BlockID n_id = nchunk->blocks ? nchunk->blocks[ nchunk->get_index_from_coords( nwx, y, nwz ) ].id : LAST_BLOCK_ID;
                        if ( !light_opaque_id( n_id ) ) {
                            light_dbg_cascade_seed++;
                            this->light_add_seed( base + glm::ivec3( x, y, z ), LIGHT_CHANNEL_SKY, cv );
                            break;
                        }
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

    // Pass 1: our border shell <- canonical owners.
    for ( int x = -1; x <= CHUNK_SIZE_X; x++ ) {
        const int ox = x < 0 ? -1 : x >= CHUNK_SIZE_X ? 1 : 0;
        for ( int y = -1; y <= CHUNK_SIZE_Y; y++ ) {
            const int oy = y < 0 ? -1 : y >= CHUNK_SIZE_Y ? 1 : 0;
            for ( int z = -1; z <= CHUNK_SIZE_Z; z++ ) {
                const int oz = z < 0 ? -1 : z >= CHUNK_SIZE_Z ? 1 : 0;
                if ( !ox && !oy && !oz ) {
                    continue;
                }
                const Chunk *owner = nb_cache[ ( ox + 1 ) * 9 + ( oy + 1 ) * 3 + ( oz + 1 ) ];
                if ( !owner || owner->is_loading || !owner->light ) {
                    continue;
                }
                // owner's local = our (x,y,z) minus the chunk offset * 32.
                const unsigned char canon = owner->light[ light_index( x - ox * CHUNK_SIZE_X, y - oy * CHUNK_SIZE_Y, z - oz * CHUNK_SIZE_Z ) ];
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
                        const int offered = ( ch == LIGHT_CHANNEL_SKY && d == LIGHT_DIR_DOWN && cv == LIGHT_MAX_LEVEL ) ? LIGHT_MAX_LEVEL : cv - 1;
                        const glm::ivec3 n_local = glm::ivec3( x, y, z ) + light_dirs[ d ];
                        int nv;
                        BlockID n_id;
                        if ( n_local.x >= 0 && n_local.x < CHUNK_SIZE_X && n_local.y >= 0 && n_local.y < CHUNK_SIZE_Y && n_local.z >= 0 && n_local.z < CHUNK_SIZE_Z ) {
                            // Inward lands in our interior: direct array reads
                            // instead of light_get's per-call chunk lookup.
                            // blocks[] is only consulted when the neighbor can
                            // actually gain light.
                            nv = ch == LIGHT_CHANNEL_BLOCK ? light_get_block( chunk.light[ light_index( n_local.x, n_local.y, n_local.z ) ] )
                                                           : light_get_sky( chunk.light[ light_index( n_local.x, n_local.y, n_local.z ) ] );
                            if ( nv >= offered ) {
                                continue;
                            }
                            n_id = chunk.blocks ? chunk.blocks[ chunk.get_index_from_coords( n_local.x, n_local.y, n_local.z ) ].id : LAST_BLOCK_ID;
                        } else {
                            const glm::ivec3 n = base + glm::ivec3( x, y, z ) + light_dirs[ d ];
                            nv = this->light_get( n, ch );
                            if ( nv < 0 || nv >= offered ) {
                                continue;
                            }
                            n_id = this->light_block_id_at( n );
                        }
                        if ( nv >= 0 && nv < offered && !light_opaque_id( n_id ) ) {
                            light_dbg_border_seed++;
                            this->light_add_seed( base + glm::ivec3( x, y, z ), ch, cv );
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
                            if ( !nb || nb->is_loading || !nb->light ) {
                                continue;
                            }
                            const glm::ivec3 n_local = w - nb->chunk_pos * CHUNK_SIZE_I;
                            if ( !light_local_ok( n_local ) ) {
                                continue; // slot recycled mid-write; torn chunk_pos
                            }
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
                        LightSeedGuard guard;
                        this->light_remove_queue.push_back( { w, static_cast<unsigned char>( ch ), static_cast<unsigned char>( rem_old[ ch ] ) } );
                        light_bfs_pending++;
                    }
                    if ( new_v <= 1 ) {
                        continue; // offered = new_v-1 can't light anything
                    }
                    // Seed only if the spread would actually go somewhere: a
                    // foreign-interior cell adjacent to w (across a face we
                    // sit on) that is under-lit and non-opaque. Mass load-in
                    // otherwise pushes thousands of no-op seeds per chunk —
                    // mirrors that needed syncing but whose interiors are
                    // already lit.
                    for ( int a = 0; a < 3; a++ ) {
                        const int off = local3[ a ] == 0 ? -1 : local3[ a ] == axis_size[ a ] - 1 ? 1 : 0;
                        if ( !off ) {
                            continue;
                        }
                        glm::ivec3 axo( 0 );
                        axo[ a ] = off;
                        const Chunk *nax = nb_cache[ ( axo.x + 1 ) * 9 + ( axo.y + 1 ) * 3 + ( axo.z + 1 ) ];
                        if ( !nax || nax->is_loading || !nax->light ) {
                            continue;
                        }
                        const glm::ivec3 n_pos = w + axo;
                        const glm::ivec3 n_loc = n_pos - nax->chunk_pos * CHUNK_SIZE_I;
                        if ( !light_local_ok( n_loc ) ||
                             n_loc.x < 0 || n_loc.x >= CHUNK_SIZE_X || n_loc.y < 0 || n_loc.y >= CHUNK_SIZE_Y || n_loc.z < 0 ||
                             n_loc.z >= CHUNK_SIZE_Z ) {
                            continue; // torn chunk_pos, or not nax's interior
                        }
                        const glm::ivec3 dir = light_dirs[ off > 0 ? a * 2 : a * 2 + 1 ];
                        const int offered = ( ch == LIGHT_CHANNEL_SKY && dir.y == -1 && new_v == LIGHT_MAX_LEVEL ) ? LIGHT_MAX_LEVEL : new_v - 1;
                        const int nv = ch == LIGHT_CHANNEL_BLOCK ? light_get_block( nax->light[ light_index( n_loc.x, n_loc.y, n_loc.z ) ] )
                                                                 : light_get_sky( nax->light[ light_index( n_loc.x, n_loc.y, n_loc.z ) ] );
                        if ( nv >= offered ) {
                            continue;
                        }
                        const BlockID n_id = nax->blocks ? nax->blocks[ nax->get_index_from_coords( n_loc.x, n_loc.y, n_loc.z ) ].id : LAST_BLOCK_ID;
                        if ( !light_opaque_id( n_id ) ) {
                            light_dbg_border_seed++;
                            this->light_add_seed( w, ch, new_v );
                            break;
                        }
                    }
                }
            }
        }
    }
}

void light_pending_enqueue( Chunk &chunk ) {
    {
        std::lock_guard<std::mutex> lock( light_work_mutex );
        if ( chunk.light_pending_listed ) {
            return;
        }
        chunk.light_pending_listed = 1;
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
    chunk.light_pending_listed = 0;
    if ( chunk.is_loading || !chunk.light ) {
        return; // loading chunks drain their queue in light_finalize_chunk
    }
    if ( chunk.light_reseed ) {
        // Full refill (light_pending overflowed). light_fill_chunk wiped
        // skylight and reset sky_open_above, so re-run the column
        // cascade to flip the flags and reseed, plus a border sync. The
        // whole volume was rewritten, so mark it all dirty for upload.
        light_fill_chunk( chunk );
        chunk.light_reseed = 0;
        this->light_cascade_columns( chunk );
        // Always scan — the refill's optimistic sky_open_above leaves the
        // cascade with nothing to fill, but shaded columns still need seeds.
        this->light_seed_interior_boundary( chunk );
        this->light_border_sync( chunk );
        ChunkLoader::light_mark_dirty_all( chunk );
    }
    for ( int p = 0; p < chunk.light_pending_count; p++ ) {
        int x, y, z;
        chunk.get_coords_from_index( chunk.light_pending[ p ], x, y, z );
        this->light_recheck_block( chunk.chunk_pos * CHUNK_SIZE_I + glm::ivec3( x, y, z ) );
    }
    chunk.light_pending_count = 0;
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
    if ( chunk.light_columns ) {
        // Optimistic skylight: assume open above so the chunk doesn't draw
        // black in the window between terrain upload and light finalize.
        // Cells above the topmost opaque cell get a direct sky=15 fill, and
        // sky_open_above=1 records the assumption — if above turns out
        // closed, the cascade flips the flag and the removal BFS clears it.
        unsigned char *flags = chunk.light_columns;
        for ( int x = 0; x < CHUNK_SIZE_X; x++ ) {
            for ( int z = 0; z < CHUNK_SIZE_Z; z++ ) {
                const int ci = LIGHT_COLUMN_INDEX( x, z );
                flags[ LIGHT_FLAGS_COUNT + ci ] = 1;
                const int fill_from = flags[ LIGHT_FILL_FROM_INDEX( x, z ) ];
                for ( int y = fill_from; y < CHUNK_SIZE_Y; y++ ) {
                    unsigned char &cell = chunk.light[ light_index( x, y, z ) ];
                    cell = static_cast<unsigned char>( ( cell & 0x0F ) | ( LIGHT_MAX_LEVEL << 4 ) );
                }
            }
        }
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
    const long long t0 = light_now_us( );
    glGenTextures( 1, &this->light_texture );
    glActiveTexture( GL_TEXTURE0 + light_texture_unit( ) );
    glBindTexture( GL_TEXTURE_3D, this->light_texture );
    // Full upload at creation: a chunk can be drawn before its first
    // light_upload_dirty pass, and must never sample an uninitialized volume.
    // Covers every queued dirty region, so clear the flag.
    // The block atlas (Texture::loadTexture) leaves UNPACK_ROW_LENGTH /
    // UNPACK_IMAGE_HEIGHT set, and the default UNPACK_ALIGNMENT=4 pads our
    // rows — set all three explicitly or the upload reads garbage.
    // Consume the dirty box BEFORE copying so marks landing mid-upload stay
    // pending (the chunk may not be in the list, but a mark sees listed==0
    // or the queued entry itself picks them up next pass).
    this->light_dirty_box.exchange( LIGHT_BOX_EMPTY, std::memory_order_acq_rel );
    for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
        light_expand_cell( this->light[ i ], s_light_upload_scratch + 2 * i );
    }
    glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
    glPixelStorei( GL_UNPACK_IMAGE_HEIGHT, 0 );
    glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
    glTexImage3D( GL_TEXTURE_3D, 0, GL_RG8, CHUNK_SIZE_INTERNAL_X, CHUNK_SIZE_INTERNAL_Y, CHUNK_SIZE_INTERNAL_Z, 0, GL_RG, GL_UNSIGNED_BYTE, s_light_upload_scratch );
    glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE );
    light_dbg_n_ensure++;
    light_dbg_us_ensure += light_now_us( ) - t0;
}

void ChunkLoader::light_upload_dirty( const int max_uploads ) {
    const long long t0 = light_now_us( );
    if ( light_dirty_list.empty( ) ) {
        light_dbg_us_upload += light_now_us( ) - t0;
        return;
    }
    struct LightUploadEntry {
        Chunk *chunk;
        uint64_t box;
    };
    static std::vector<LightUploadEntry> entries;
    entries.clear( );
    {
        // Clear listed AND snapshot the dirty box in the same critical
        // section that detaches the chunk from the list: a mark whose CAS
        // misses this exchange necessarily enters the mutex after us, sees
        // listed==0, and re-adds the chunk — the next frame's exchange picks
        // its texel up. Entries beyond the cap stay in the list untouched.
        std::lock_guard<std::mutex> lock( light_work_mutex );
        const size_t n = light_dirty_list.size( ) > static_cast<size_t>( max_uploads )
                             ? static_cast<size_t>( max_uploads )
                             : light_dirty_list.size( );
        entries.reserve( n );
        for ( size_t i = 0; i < n; i++ ) {
            Chunk *chunk = light_dirty_list[ i ];
            chunk->light_upload_listed.store( 0, std::memory_order_release );
            entries.push_back( { chunk, chunk->light_dirty_box.exchange( LIGHT_BOX_EMPTY, std::memory_order_acq_rel ) } );
        }
        light_dirty_list.erase( light_dirty_list.begin( ), light_dirty_list.begin( ) + n );
    }
    int uploads = 0;
    for ( const LightUploadEntry &e : entries ) {
        Chunk &chunk = *e.chunk;
        if ( e.box == LIGHT_BOX_EMPTY || chunk.is_loading || !chunk.light ) {
            continue;
        }
        // No texture yet: light_ensure_texture's full upload at first draw
        // already covers every dirty texel — skip the allocation here so
        // never-drawn (faceless) chunks don't pay 38KB of VRAM.
        if ( chunk.light_texture == 0 ) {
            continue;
        }
        uploads++;
        const glm::ivec3 lo = glm::clamp( light_box_min( e.box ), glm::ivec3( 0 ),
                                        glm::ivec3( CHUNK_SIZE_INTERNAL_X - 1, CHUNK_SIZE_INTERNAL_Y - 1, CHUNK_SIZE_INTERNAL_Z - 1 ) );
        const glm::ivec3 hi = glm::clamp( light_box_max( e.box ), glm::ivec3( 0 ),
                                        glm::ivec3( CHUNK_SIZE_INTERNAL_X - 1, CHUNK_SIZE_INTERNAL_Y - 1, CHUNK_SIZE_INTERNAL_Z - 1 ) );
        if ( hi.x < lo.x || hi.y < lo.y || hi.z < lo.z ) {
            continue;
        }
        const glm::ivec3 sz = hi - lo + glm::ivec3( 1 );
        glActiveTexture( GL_TEXTURE0 + light_texture_unit( ) );
        glBindTexture( GL_TEXTURE_3D, chunk.light_texture );
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
    }
    light_dbg_n_upload += uploads;
    light_dbg_us_upload += light_now_us( ) - t0;
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

// ---------------------------------------------------------------------------
// Dedicated lighting thread
// ---------------------------------------------------------------------------

bool ChunkLoader::light_async_active( ) {
    return light_thread_running != 0;
}

// Near-player radius in chunk columns for the front-of-queue lane. A chunk's
// lighting only converges once its neighborhood is processed, so a few chunks
// around the player jump the (possibly thousands-deep) backlog and their
// block ring settles together instead of in terrain-load order.
#define LIGHT_NEAR_COL_DIST 6

// Push where the claim scan finds it early: near-player columns to the front,
// everything else to the back. chunk_center is a racy render-thread read —
// fine for a priority hint, and stale ordering self-corrects as the player
// moves (only a snapshot of the backlog can be mis-ordered).
static void light_submit_job( const glm::ivec3 &center, const LightJob &job ) {
    const glm::ivec3 col = job.type == LIGHT_JOB_FINALIZE ? job.pos : light_chunk_pos_of( job.pos );
    const int dx = col.x - center.x;
    const int dz = col.z - center.z;
    const bool near = dx * dx + dz * dz <= LIGHT_NEAR_COL_DIST * LIGHT_NEAR_COL_DIST;
    if ( job.type == LIGHT_JOB_RECHECK || near ) {
        light_jobs.push_front( job );
    } else {
        light_jobs.push_back( job );
    }
}

void ChunkLoader::light_submit_finalize( Chunk &chunk ) {
    if ( !light_thread_running ) {
        this->light_finalize_chunk( chunk );
        return;
    }
    {
        std::lock_guard<std::mutex> lock( light_work_mutex );
        // pos identifies the column to claim AND validates the slot still
        // holds this chunk at execution time (slots recycle).
        light_submit_job( this->chunk_center, LightJob{ LIGHT_JOB_FINALIZE, &chunk, chunk.chunk_pos } );
    }
    light_work_cv.notify_one( );
}

void ChunkLoader::light_submit_recheck( const glm::ivec3 &block_pos ) {
    Chunk *chunk = this->get_chunk( light_chunk_pos_of( block_pos ) );
    if ( chunk == nullptr || chunk->is_loading || !chunk->light ) {
        return;
    }
    if ( !light_thread_running ) {
        this->light_recheck_block( block_pos );
        return;
    }
    {
        std::lock_guard<std::mutex> lock( light_work_mutex );
        // Edits are interactive — always the front lane.
        light_submit_job( this->chunk_center, LightJob{ LIGHT_JOB_RECHECK, chunk, block_pos } );
    }
    light_work_cv.notify_one( );
}

// Worker loop: claim a job's 5x5 column footprint, run it without the lock,
// release. Jobs whose columns are claimed by another worker are skipped in
// the scan; they become runnable when the claim releases (notify_all below).
// Pending-diff chunks are claimed the same way. The shared BFS queues run
// only while no claims are in flight (a popped seed writes anywhere).
void ChunkLoader::light_thread_loop( ) {
    for ( ;; ) {
        light_dbg_loop_top++;
        LightJob job;
        Chunk *pending_chunk = nullptr;
        glm::ivec3 claim_col( 0 );
        glm::ivec3 pending_expected( 0 );
        bool have_job = false;
        bool claimed = false;
        bool run_bfs = false;
        {
            std::unique_lock<std::mutex> lock( light_work_mutex );
            light_work_cv.wait( lock, [ this ] {
                if ( light_thread_stop_flag ) {
                    return true;
                }
                if ( light_bfs_pending.load( ) > LIGHT_BFS_PRI_HI ) {
                    light_bfs_priority = 1;
                }
                // Wake only for *runnable* work — a queue full of
                // claim-blocked jobs must not spin this loop.
                for ( const LightJob &j : light_jobs ) {
                    const glm::ivec3 col = j.type == LIGHT_JOB_FINALIZE ? j.pos : light_chunk_pos_of( j.pos );
                    if ( light_claim_free( col.x, col.z, j.type != LIGHT_JOB_FINALIZE ) ) {
                        return true;
                    }
                }
                for ( const Chunk *pc : light_pending_list ) {
                    if ( light_claim_free( pc->chunk_pos.x, pc->chunk_pos.z, true ) ) {
                        return true;
                    }
                }
                return !light_bfs_active && light_claims.empty( ) && light_bfs_pending.load( ) > 0;
            } );
            if ( light_thread_stop_flag ) {
                return;
            }
            for ( auto it = light_jobs.begin( ); it != light_jobs.end( ); ++it ) {
                const glm::ivec3 col = it->type == LIGHT_JOB_FINALIZE ? it->pos : light_chunk_pos_of( it->pos );
                if ( light_try_claim( col.x, col.z, it->type != LIGHT_JOB_FINALIZE ) ) {
                    job = *it;
                    light_jobs.erase( it );
                    claim_col = col;
                    have_job = claimed = true;
                    break;
                }
            }
            if ( !have_job ) {
                for ( auto it = light_pending_list.begin( ); it != light_pending_list.end( ); ) {
                    Chunk *pc = *it;
                    if ( pc->is_loading ) {
                        // Loading chunks drain pending in light_finalize_chunk.
                        pc->light_pending_listed = 0;
                        it = light_pending_list.erase( it );
                        continue;
                    }
                    const glm::ivec3 col = pc->chunk_pos;
                    if ( !light_try_claim( col.x, col.z, true ) ) {
                        ++it;
                        continue;
                    }
                    light_pending_list.erase( it );
                    // Clear listed under the lock so a producer writing
                    // pending fields now re-enqueues a fresh entry.
                    pc->light_pending_listed = 0;
                    pending_chunk = pc;
                    pending_expected = col;
                    claim_col = col;
                    claimed = true;
                    break;
                }
            }
            if ( !claimed && !light_bfs_active && light_claims.empty( ) && light_bfs_pending.load( ) > 0 ) {
                light_bfs_active = 1;
                run_bfs = true;
            }
        }
        if ( have_job ) {
            if ( job.type == LIGHT_JOB_FINALIZE ) {
                // Claim was staked on the pos captured at submit — the slot
                // must still hold that chunk or our writes aren't under the
                // claim. A recycled slot's fresh dequeue re-submits, so
                // skipping loses nothing.
                if ( job.chunk->chunk_pos == job.pos && !job.chunk->is_loading ) {
                    this->light_finalize_chunk( *job.chunk );
                }
            } else if ( this->get_chunk( light_chunk_pos_of( job.pos ) ) == job.chunk ) {
                this->light_recheck_block( job.pos );
            }
        } else if ( pending_chunk != nullptr ) {
            if ( pending_chunk->chunk_pos.x == pending_expected.x && pending_chunk->chunk_pos.z == pending_expected.z &&
                 !pending_chunk->is_loading && pending_chunk->light ) {
                this->light_process_pending_chunk( *pending_chunk );
            }
        }
        if ( claimed ) {
            {
                std::lock_guard<std::mutex> lock( light_work_mutex );
                light_release_claim( claim_col.x, claim_col.z );
            }
            light_work_cv.notify_all( );
        } else if ( run_bfs ) {
            // Bounded slices, but keep draining while we own the BFS slot and
            // no jobs/pending are waiting — yielding each slice would just
            // ping-pong the slot while other workers stand idle. In a
            // priority window jobs are claim-blocked anyway: drain to the low
            // watermark or the time cap, whichever first.
            const long long pri_start = light_now_us( );
            for ( ;; ) {
                this->light_process_queue( 2000 );
                {
                    std::lock_guard<std::mutex> lock( light_work_mutex );
                    if ( light_thread_stop_flag ) {
                        light_bfs_active = 0;
                        break;
                    }
                    if ( light_bfs_priority ) {
                        if ( light_bfs_pending.load( ) <= LIGHT_BFS_PRI_LO ||
                             light_now_us( ) - pri_start > LIGHT_BFS_PRI_MAX_US ) {
                            light_bfs_priority = 0;
                            light_bfs_active = 0;
                            break;
                        }
                    } else if ( light_bfs_pending.load( ) <= 0 || !light_jobs.empty( ) || !light_pending_list.empty( ) ) {
                        light_bfs_active = 0;
                        break;
                    }
                }
            }
            light_work_cv.notify_all( );
        }
    }
}

void ChunkLoader::light_thread_start( ) {
#if LIGHT_ON_THREAD
    if ( light_thread_running ) {
        return;
    }
    light_thread_stop_flag = 0;
    // Flag producers to enqueue BEFORE spawning — an inline-fallback call
    // racing a live worker would mutate light state concurrently.
    light_thread_running = 1;
    int spawned = 0;
    for ( int i = 0; i < LIGHT_WORKER_COUNT; i++ ) {
        try {
            light_threads[ i ] = std::thread( &ChunkLoader::light_thread_loop, this );
            spawned++;
        } catch ( ... ) {
            break;
        }
    }
    if ( spawned == 0 ) {
        light_thread_running = 0;
        // Drain anything enqueued during the spawn window.
        std::deque<LightJob> jobs;
        {
            std::lock_guard<std::mutex> lock( light_work_mutex );
            jobs.swap( light_jobs );
        }
        for ( LightJob &job : jobs ) {
            if ( job.type == LIGHT_JOB_FINALIZE ) {
                this->light_finalize_chunk( *job.chunk );
            } else if ( this->get_chunk( light_chunk_pos_of( job.pos ) ) == job.chunk ) {
                this->light_recheck_block( job.pos );
            }
        }
        return;
    }
    light_thread_running = spawned;
#endif
}

void ChunkLoader::light_thread_stop( ) {
#if LIGHT_ON_THREAD
    if ( !light_thread_running ) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock( light_work_mutex );
        light_thread_stop_flag = 1;
    }
    light_work_cv.notify_all( );
    for ( int i = 0; i < light_thread_running; i++ ) {
        light_threads[ i ].join( );
    }
    light_thread_running = 0;
    // Drop all queued work — stop() is only called from cleanup() right
    // before every chunk's light volume is freed, so processing leftover
    // finalizes/rechecks/seeds is wasted work (and takes seconds when quit
    // happens mid load-in). Clear the containers so a later init() in the
    // same process doesn't see stale chunk pointers or seeds.
    {
        std::lock_guard<std::mutex> lock( light_work_mutex );
        light_jobs.clear( );
        light_pending_list.clear( );
        light_dirty_list.clear( );
        light_claims.clear( );
        light_bfs_active = 0;
        light_bfs_priority = 0;
        for ( int ch = 0; ch < 2; ch++ ) {
            for ( int level = 0; level <= LIGHT_MAX_LEVEL; level++ ) {
                this->light_add_queue[ ch ][ level ].clear( );
            }
        }
        this->light_remove_queue.clear( );
        light_bfs_pending = 0;
    }
#endif
}

// Point-in-time snapshot for the debug overlay. Queue/container reads take the
// work mutex briefly; chunk/terrain counts are racy-but-harmless reads.
void ChunkLoader::debug_loader_stats( LoaderDebugStats *out ) const {
    memset( out, 0, sizeof( *out ) );
    this->terrain_loading_thread.queue_sizes( &out->terrain_queued, &out->terrain_results );
    out->chunks_drawable = this->num_drawable;
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        if ( this->chunkArray[ i ].is_loading ) {
            out->chunks_loading++;
        }
    }
    {
        std::lock_guard<std::mutex> lock( light_work_mutex );
        out->light_jobs = static_cast<int>( light_jobs.size( ) );
        out->light_pending = static_cast<int>( light_pending_list.size( ) );
        out->light_claims = static_cast<int>( light_claims.size( ) / 25 );
        out->light_dirty = static_cast<int>( light_dirty_list.size( ) );
    }
    out->light_bfs_active = light_bfs_active;
    out->light_seeds = light_bfs_pending.load( );
    out->light_dropped = light_dbg_dropped.load( );
}
