#include <algorithm>
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

#include "common/RepGame.hpp" // GL headers + platform macros
#include "common/light.hpp"
#include "common/chunk_loader.hpp"
#include "common/block_definitions.hpp"
#include "common/renderer/texture.hpp"

// ---------------------------------------------------------------------------
// Flood-fill lighting engine.
//
// Chunk::light[] is a byte per cell over the same 34^3 internal volume as
// cells[]: high nibble = skylight, low nibble = block (emissive) light.
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
    if ( !chunk.cells ) {
        return 0;
    }
    return block_definition_get_definition( block_cell_id( chunk.cells[ index ] ) )->emits_light;
}

static inline bool light_opaque_id( BlockID id ) {
    if ( id == LAST_BLOCK_ID ) {
        return true;
    }
    return block_definition_get_definition( id )->casts_shadow;
}

// Vertical skylight fill test: a column is "blocked" by opaque cells AND by
// absorbing ones. The BFS can still permeate absorbing cells (it charges
// their extra cost per entry), but the direct 15-strength descent only runs
// through cells that take nothing.
static inline bool light_column_blocked_id( BlockID id ) {
    if ( id == LAST_BLOCK_ID ) {
        return true;
    }
    const Block *def = block_definition_get_definition( id );
    return def->casts_shadow || def->light_absorb > 0;
}

// What a cell holding `v` offers neighbor cell `n_id` in direction `d`.
// Entering costs 1 + the target's light_absorb; the straight-down 15 skylight
// privilege only applies through non-absorbing cells.
static inline int light_offered( const int v, const int channel, const int d, const BlockID n_id ) {
    const int absorb = block_definition_get_definition( n_id )->light_absorb;
    if ( channel == LIGHT_CHANNEL_SKY && d == LIGHT_DIR_DOWN && v == LIGHT_MAX_LEVEL && absorb == 0 ) {
        return LIGHT_MAX_LEVEL;
    }
    return v - 1 - absorb;
}

// Bit i of chunk.light_air_mask mirrors the opacity of cells[i]: set = the
// cell can hold light. light_fill_chunk writes every bit; this restamps one
// cell after a post-fill cells[] write so the upload paths never have to
// touch the cells[] array again. A missing mask is always legal —
// uploads fall back to the cells[] walk.
// The bitmap is z-fastest (cells[] index space), so each x column occupies
// CHUNK_SIZE_INTERNAL_Z/8 bytes — the layout requires Z % 8 == 0.
static_assert( CHUNK_SIZE_INTERNAL_Z % 8 == 0,
               "light_air_mask packs cells[] z-fastest 8/byte; CHUNK_SIZE_INTERNAL_Z must be a multiple of 8" );
void light_air_mask_set( const Chunk &chunk, const int index, const BlockID id ) {
    if ( chunk.light_air_mask == nullptr ) {
        return;
    }
    if ( light_opaque_id( id ) ) {
        chunk.light_air_mask[ index >> 3 ] &= static_cast<unsigned char>( ~( 1u << ( index & 7 ) ) );
    } else {
        chunk.light_air_mask[ index >> 3 ] |= static_cast<unsigned char>( 1u << ( index & 7 ) );
    }
}

// TEMP diagnostics: which producer feeds the queue.
static long long light_dbg_pq_calls = 0, light_dbg_pq_nonempty = 0;
static long long light_dbg_loop_top = 0, light_dbg_after_jobs = 0, light_dbg_jobs_n = 0;
static std::atomic<long long> light_dbg_pushes{ 0 }, light_dbg_pops{ 0 };
static long long light_dbg_finalize = 0,
                 light_dbg_cascade_seed = 0, light_dbg_border_seed = 0,
                 light_dbg_recheck_seed = 0;
static std::atomic<long long> light_dbg_step_push{ 0 }, light_dbg_remove_seed{ 0 };
// Pops that hit a contested column and were re-queued unprocessed — the
// measure of lock-contention churn in the parallel drain.
static std::atomic<long long> light_dbg_requeued{ 0 };
// Seeds that died because their cell's chunk was missing/still loading when
// popped. The receiving chunk's own finalize normally re-seeds coverage, so a
// nonzero count is not itself a bug — but a high count plus persistent dark
// areas means dropped work that nothing re-covered.
static std::atomic<long long> light_dbg_dropped{ 0 };
// Same, but refused at push time by the gate in light_seed_gate — these
// never occupy the queue, so a high count is strictly cheaper than the same
// drops landing at pop. Folded into `drop`.
static std::atomic<long long> light_dbg_push_drops{ 0 };
// Seeds refused by the far-lane backlog cap — each flagged its target chunk
// light_stale for lazy re-finalize rather than queueing.
static std::atomic<long long> light_dbg_capped{ 0 };
// Dirty-upload list entries deferred this interval for being write-hot
// (light_write_tick within LIGHT_UPLOAD_QUIET_TICKS) — the anti-torn-upload
// debounce at work; spikes while a flood marches through visible chunks.
static std::atomic<long long> light_dbg_upload_deferred{ 0 };
// Per-interval max age of a job when it actually ran (submit -> execute), and
// how many jobs waited >1s — exposes priority inversions where a visible
// chunk's finalize sits behind a deep queue.
static std::atomic<long long> light_dbg_max_lag_us{ 0 };
static std::atomic<long long> light_dbg_late_jobs{ 0 };
static std::atomic<long long> light_dbg_max_lag_near_us{ 0 }; // lag of jobs that were near the player AT EXECUTION
static std::atomic<long long> light_dbg_sync{ 0 };    // rechecks run synchronously on the calling thread
static std::atomic<long long> light_dbg_rej{ 0 };     // rechecks that failed the sync claim and went async
// TEMP frame-phase timing (µs accumulated between probes) + call counts.
long long light_dbg_us_finalize = 0, light_dbg_us_remesh = 0, light_dbg_us_drain = 0,
          light_dbg_us_bfs = 0, light_dbg_us_upload = 0, light_dbg_us_bind = 0,
          light_dbg_us_ensure = 0, light_dbg_n_upload = 0, light_dbg_n_ensure = 0,
          light_dbg_us_light_fin = 0, light_dbg_us_fin_fill = 0, light_dbg_us_fin_casc = 0,
          light_dbg_us_fin_bscan = 0, light_dbg_us_fin_border = 0;
static size_t light_jobs_depth( );

void ChunkLoader::light_dbg_stats( char *buf, size_t n ) {
    size_t jobs_now = light_jobs_depth( );
    int terrain_work = 0, terrain_results = 0;
    this->terrain_loading_thread.queue_sizes( &terrain_work, &terrain_results );
    snprintf( buf, n, "loop=%lld aj=%lld jn=%lld pq=%lld/%lld push=%lld pop=%lld fin=%lld casc=%lld bord=%lld rech=%lld step=%lld rem=%lld rq=%lld drop=%lld pd=%lld cap=%lld def=%lld jobs=%zu jlag=%lld jlate=%lld njlag=%lld syn=%lld rej=%lld tw=%d tr=%d"
                      " | us: fin=%lld(lfin=%lld[fill=%lld casc=%lld bscan=%lld border=%lld]) remesh=%lld drain=%lld bfs=%lld upl=%lld(n=%lld) bind=%lld ensure=%lld(n=%lld)",
              light_dbg_loop_top, light_dbg_after_jobs, light_dbg_jobs_n,
              light_dbg_pq_calls, light_dbg_pq_nonempty,
              light_dbg_pushes.load( ), light_dbg_pops.load( ), light_dbg_finalize, light_dbg_cascade_seed,
              light_dbg_border_seed, light_dbg_recheck_seed, light_dbg_step_push.load( ), light_dbg_remove_seed.load( ),
              light_dbg_requeued.load( ),
              light_dbg_dropped.load( ), light_dbg_push_drops.load( ), light_dbg_capped.load( ),
              light_dbg_upload_deferred.load( ), jobs_now,
              light_dbg_max_lag_us.load( ), light_dbg_late_jobs.load( ), light_dbg_max_lag_near_us.load( ),
              light_dbg_sync.load( ), light_dbg_rej.load( ),
              terrain_work, terrain_results,
              light_dbg_us_finalize, light_dbg_us_light_fin, light_dbg_us_fin_fill,
              light_dbg_us_fin_casc, light_dbg_us_fin_bscan, light_dbg_us_fin_border,
              light_dbg_us_remesh, light_dbg_us_drain,
              light_dbg_us_bfs, light_dbg_us_upload, light_dbg_n_upload, light_dbg_us_bind,
              light_dbg_us_ensure, light_dbg_n_ensure );
    light_dbg_max_lag_us = 0;
    light_dbg_late_jobs = 0;
    light_dbg_max_lag_near_us = 0;
    light_dbg_sync = 0;
    light_dbg_rej = 0;
    light_dbg_pushes = light_dbg_pops = light_dbg_step_push = light_dbg_remove_seed = light_dbg_requeued = 0;
    light_dbg_finalize = light_dbg_cascade_seed = light_dbg_border_seed = light_dbg_recheck_seed = 0;
    light_dbg_capped = 0;
    light_dbg_upload_deferred = 0;
    // loop/aj/jn/pq are cumulative (not reset) — reset races with the light
    // thread would otherwise print impossible combinations.
    light_dbg_us_finalize = light_dbg_us_remesh = light_dbg_us_drain = light_dbg_us_bfs =
        light_dbg_us_upload = light_dbg_us_bind = light_dbg_us_ensure = light_dbg_n_upload =
            light_dbg_n_ensure = light_dbg_us_light_fin = light_dbg_us_fin_fill =
                light_dbg_us_fin_casc = light_dbg_us_fin_bscan = light_dbg_us_fin_border = 0;
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

// Approximate count of queued BFS seeds, kept atomic so light workers can
// test "queue nonempty" from the cv predicate without touching the deques
// (a claimed job may be pushing while another worker waits). ++ on every
// push_back, -- on every pop_front below.
static std::atomic<long long> light_bfs_pending{ 0 };

// Starve-window thresholds (the window machinery lives with the claim code
// below): a backlog over STARVE_MIN for more than STARVE_TICKS interrupts
// claims. Backlog age is measured in game ticks fed by RepGame::tick
// (light_note_tick) so the seed push/pop hot paths never read steady_clock —
// everything below is plain atomic loads.
static std::atomic<long> light_tick_counter{ 0 };
#define LIGHT_BFS_STARVE_MIN 2048                // backlog worth interrupting claims for
#define LIGHT_BFS_STARVE_TICKS ( UPS_RATE / 10 ) // ~100ms of game ticks
// Flood-regen cap: past this, pushes spawned BY pops (the add/remove march's
// own expansion) are refused and their target chunk flagged light_stale — a
// marching flood can no longer regenerate the backlog for tens of seconds
// after it formed; flagged chunks re-finalize lazily (light_heal_*). Seeds
// pushed by producers (finalize/cascade/border sync/recheck) still land, so
// fresh chunk light keeps converging during streaming.
#define LIGHT_BFS_CAP_FLOOD ( 1 << 20 )
// Absolute bound: even external far-lane pushes are refused past this.
#define LIGHT_BFS_CAP_ALL ( 4 << 20 )
// Chunks re-finalized per frame by the idle stale-heal scan, and the backlog
// under which heals may feed. Heal seeds are external pushes (they always
// land), so the feed can run well above STARVE_MIN — keeping the pile fed
// while flagged chunks still exist is what shortens the post-flight tail.
#define LIGHT_HEAL_PER_FRAME 8
#define LIGHT_HEAL_FEED ( 256 << 10 )
// Upload debounce: a chunk whose light was written within the last
// LIGHT_UPLOAD_QUIET_TICKS game ticks waits — a mid-march/mid-relight
// snapshot shows the transient floor state as a one-frame black flash.
// Chunks under a sustained march stay hot, so LIGHT_UPLOAD_MAX_DEFERS frames
// forces an upload anyway (~190ms starvation bound at 240fps).
#define LIGHT_UPLOAD_QUIET_TICKS 2
// Force-upload caps (frames an entry may be skipped before it goes anyway).
// The short cap bounds stale-but-converged uploads. The long cap applies
// while the column is provably mid-flood (incident seeds / held lock) —
// uploading then is guaranteed to snapshot a transient, so a torn frame a
// few times a second beats a black flash per wave, but starvation still
// bounds the wait if a column somehow stays busy forever.
#define LIGHT_UPLOAD_MAX_DEFERS 45
#define LIGHT_UPLOAD_MAX_DEFERS_BUSY 480
// Tick at which light_bfs_pending last rose above LIGHT_BFS_STARVE_MIN.
// Stamped only by the crossing push, so a backlog that never drains under
// the line keeps accruing age no matter how often drains run.
static std::atomic<long> light_bfs_backlog_tick{ 0 };

static inline void light_bfs_pending_inc( ) {
    if ( light_bfs_pending.fetch_add( 1, std::memory_order_relaxed ) == LIGHT_BFS_STARVE_MIN ) {
        light_bfs_backlog_tick.store( light_tick_counter.load( std::memory_order_relaxed ), std::memory_order_relaxed );
    }
}

// Seed deque serialization: claimed jobs push while drain workers pop
// concurrently, so both sides take the column slot's guard. Tiny critical
// sections (one deque op plus a pending-count update) → spinlock. Same
// representation as light_col_locks (plain int CAS) since C++17
// atomic_flag arrays can't be fully list-initialized.
static std::atomic<int> light_colq_locks[ LIGHT_COL_QUEUE_COUNT ];
struct LightSeedGuard {
    explicit LightSeedGuard( const int col ) : col( col ) {
        for ( ;; ) {
            while ( light_colq_locks[ col ].load( std::memory_order_relaxed ) != 0 ) {
            }
            int expected = 0;
            if ( light_colq_locks[ col ].compare_exchange_weak( expected, 1, std::memory_order_acquire, std::memory_order_relaxed ) ) {
                return;
            }
        }
    }
    ~LightSeedGuard( ) {
        light_colq_locks[ col ].store( 0, std::memory_order_release );
    }
    const int col;
};

// Column-slot routing: the same mod ring the chunk torus uses for x/z —
// light_colq_* arrays have one slot per loaded column position. A seed for
// a column outside the window lands in whatever slot the ring maps it to;
// it drains at that slot's priority and dies at pop when get_chunk fails —
// harmless, and it keeps the table bounded with no allocation.
static inline int light_col_slot_at( const int cx, const int cz ) {
    const int sx = 2 * CHUNK_RADIUS_XZ + 1;
    const int sz = 2 * CHUNK_RADIUS_XZ + 1;
    return ( ( cx % sx ) + sx ) % sx + sx * ( ( ( cz % sz ) + sz ) % sz );
}
static inline int light_col_slot( const glm::ivec3 &block_pos ) {
    const glm::ivec3 cp = light_chunk_pos_of( block_pos );
    return light_col_slot_at( cp.x, cp.z );
}
// Inverse for distance ordering: slot (mx,mz) currently holds the column
// center + wrapped offset (the window is centered on chunk_center). This
// is also the right priority for out-of-window seeds squatting in the
// slot — doomed seeds should drain late, not ahead of live columns.
static inline glm::ivec2 light_col_slot_pos( const int slot, const glm::ivec3 &center ) {
    const int sx = 2 * CHUNK_RADIUS_XZ + 1;
    const int sz = 2 * CHUNK_RADIUS_XZ + 1;
    const int mx = slot % sx, mz = slot / sx;
    int ox = mx - ( ( center.x % sx ) + sx ) % sx;
    ox += ox > sx / 2 ? -sx : ( ox < -sx / 2 ? sx : 0 );
    int oz = mz - ( ( center.z % sz ) + sz ) % sz;
    oz += oz > sz / 2 ? -sz : ( oz < -sz / 2 ? sz : 0 );
    return glm::ivec2( center.x + ox, center.z + oz );
}

// Pending-seeds-per-column accounting for the upload in-progress gate —
// defined with the column-lock machinery below.
static void light_col_incident_inc( const glm::ivec3 &block_pos );
static void light_col_incident_dec( const glm::ivec3 &block_pos );

// Per-column-slot pending counts and drain ownership. A slot's queues are
// popped only by the worker that CAS-claims its owned flag; selection picks
// the NEAREST claimable slot to the player, so distance priority is
// continuous — a column the player flies up to drains next regardless of
// when its seeds were pushed.
static std::atomic<long long> light_colq_pending[ LIGHT_COL_QUEUE_COUNT ];
static std::atomic<int> light_colq_owned[ LIGHT_COL_QUEUE_COUNT ];
static int light_claim_column( const glm::ivec3 &center ) {
    for ( ;; ) {
        int best = -1, best_d2 = std::numeric_limits<int>::max( );
        for ( int s = 0; s < LIGHT_COL_QUEUE_COUNT; s++ ) {
            if ( light_colq_pending[ s ].load( std::memory_order_relaxed ) <= 0 ||
                 light_colq_owned[ s ].load( std::memory_order_relaxed ) != 0 ) {
                continue;
            }
            const glm::ivec2 col = light_col_slot_pos( s, center );
            const glm::ivec2 d = col - glm::ivec2( center.x, center.z );
            const int d2 = d.x * d.x + d.y * d.y;
            if ( d2 < best_d2 ) {
                best_d2 = d2;
                best = s;
            }
        }
        if ( best < 0 ) {
            return -1;
        }
        int expected = 0;
        if ( light_colq_owned[ best ].compare_exchange_strong( expected, 1, std::memory_order_acquire, std::memory_order_relaxed ) ) {
            return best;
        }
    }
}
static void light_release_column( const int col ) {
    light_colq_owned[ col ].store( 0, std::memory_order_release );
    // Callers notify light_work_cv — waiters re-check pending/owned slots.
}

// Near-player radius in chunk columns for the priority lanes (seeds below)
// and the front-of-queue job lane (light_submit_job). A chunk's lighting only
// converges once its neighborhood is processed, so a few chunks around the
// player jump the backlog and their block ring settles together instead of
// in terrain-load order.
#define LIGHT_NEAR_COL_DIST 6

// Is chunk_col within the near-player radius of center?
// chunk_center is a racy render-thread read — fine for a priority hint, and
// stale routing self-corrects as the player moves.
static inline bool light_near_col( const glm::ivec3 &center, const glm::ivec3 &chunk_col ) {
    const glm::ivec3 d = chunk_col - center;
    return d.x * d.x + d.z * d.z <= LIGHT_NEAR_COL_DIST * LIGHT_NEAR_COL_DIST;
}
static inline bool light_near_center( const glm::ivec3 &center, const glm::ivec3 &block_pos ) {
    return light_near_col( center, light_chunk_pos_of( block_pos ) );
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
// (light_heal_* below). light_reseed is set alongside the flag so the heal
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
        c->light_stale.store( 1, std::memory_order_relaxed );
        // Set at flag time (not heal time) so ANY finalize landing before
        // the heal scan — e.g. a pending-diff drain — already does the full
        // wipe + emitter refill. Benign racy int write: producers on worker
        // threads store 1 here, the finalize's own store of 0 clears it.
        c->light_reseed = 1;
        light_dbg_capped++;
        return 1;
    }
    return 0;
}

// level is the light value the cell holds at push time; it only determines
// which bucket the seed lands in (higher first), not what gets propagated.
// The seed routes to its target column's slot; drainers claim the nearest
// non-empty slot, so work for the column under the player drains first no
// matter when it was queued.
void ChunkLoader::light_add_seed( const glm::ivec3 &block_pos, const int channel, const int level, const bool internal ) {
    const bool near = light_near_center( this->chunk_center, block_pos );
    if ( this->light_seed_gate( block_pos, near, internal ) ) {
        return;
    }
    const int clamped = level < 0 ? 0 : level > LIGHT_MAX_LEVEL ? LIGHT_MAX_LEVEL : level;
    light_dbg_pushes++;
    const int col = light_col_slot( block_pos );
    {
        LightSeedGuard guard( col );
        this->light_add_queue[ col ][ channel ][ clamped ].push_back( { block_pos, static_cast<unsigned char>( channel ) } );
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

// Internal coords -1..CHUNK_SIZE_* index into the 34^3 volume. A cached Chunk*
// can refer to a slot recycled mid-access (chunk_pos torn), producing a
// garbage local — guard before indexing or it writes/reads out of bounds.
static inline bool light_local_ok( const glm::ivec3 &l ) {
    return l.x >= -1 && l.x <= CHUNK_SIZE_X && l.y >= -1 && l.y <= CHUNK_SIZE_Y && l.z >= -1 && l.z <= CHUNK_SIZE_Z;
}

// A chunk with light == nullptr has a uniform volume: every cell holds
// light_uniform (all-air chunks fold to optimistic sky=15; fully-dark fills
// fold to 0). Reads return the uniform value; a write that differs
// materializes the array first.
unsigned char ChunkLoader::light_read_cell( const Chunk &chunk, const int idx ) {
    return chunk.light ? chunk.light[ idx ] : chunk.light_uniform;
}

void ChunkLoader::light_materialize( Chunk &chunk ) {
    if ( chunk.light ) {
        return;
    }
    chunk.light = chunk_alloc_light( );
    memset( chunk.light, chunk.light_uniform, CHUNK_BLOCK_SIZE );
}

// Write packed value to cell idx. Materializes only when the new packed byte
// actually differs from the uniform value — writing the uniform value into a
// uniform volume is a no-op.
void ChunkLoader::light_write_cell( Chunk &chunk, const int idx, const unsigned char nv ) {
    if ( !chunk.light ) {
        if ( nv == chunk.light_uniform ) {
            return;
        }
        light_materialize( chunk );
    }
    chunk.light[ idx ] = nv;
}

// Read the canonical (interior-owner) light value. Returns -1 when the cell's
// owner chunk isn't loaded / is still generating — the propagation treats it
// as out-of-bounds. A null light[] volume returns the uniform value instead.
int ChunkLoader::light_get( const glm::ivec3 &block_pos, const int channel ) const {
    const Chunk *chunk = this->get_chunk( light_chunk_pos_of( block_pos ) );
    if ( !chunk || chunk->load_state ) {
        return -1;
    }
    const glm::ivec3 local = block_pos - chunk->chunk_pos * CHUNK_SIZE_I;
    if ( !light_local_ok( local ) ) {
        return -1; // chunk_pos raced with slot reuse; treat as out-of-bounds
    }
    const unsigned char v = light_read_cell( *chunk, light_index( local.x, local.y, local.z ) );
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

// Trilinear sample of the light volume for non-terrain draws (player avatars,
// block-break debris) — those draw all instances in one call and can't bind a
// per-chunk 3D texture, so they bake this CPU sample into instance data.
// Light cells sit at block centers (integer+0.5), matching the texel-center
// convention the chunk shader samples the GPU volume with. Returns
// normalized (sky, block) in [0,1]; missing/unloaded cells contribute 0.
glm::vec2 ChunkLoader::light_sample( const glm::vec3 &pos ) const {
    const glm::vec3 q = pos - 0.5f;
    const glm::ivec3 base = glm::ivec3( glm::floor( q ) );
    const glm::vec3 f = q - glm::vec3( base );
    glm::vec2 accum( 0.0f );
    for ( int dz = 0; dz <= 1; dz++ ) {
        for ( int dy = 0; dy <= 1; dy++ ) {
            for ( int dx = 0; dx <= 1; dx++ ) {
                const float w = ( dx ? f.x : 1.0f - f.x ) * ( dy ? f.y : 1.0f - f.y ) * ( dz ? f.z : 1.0f - f.z );
                const glm::ivec3 cell = base + glm::ivec3( dx, dy, dz );
                const int sky = this->light_get( cell, LIGHT_CHANNEL_SKY );
                const int blk = this->light_get( cell, LIGHT_CHANNEL_BLOCK );
                if ( sky > 0 ) {
                    accum.x += w * static_cast<float>( sky );
                }
                if ( blk > 0 ) {
                    accum.y += w * static_cast<float>( blk );
                }
            }
        }
    }
    return accum / static_cast<float>( LIGHT_MAX_LEVEL );
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
    glm::ivec3 pos;       // FINALIZE: expected chunk_pos; RECHECK: block pos
    long long submit_us;
    int first_run = 0;    // FINALIZE only: chunk had never been finalized at submit
};

static std::mutex light_work_mutex;
static std::condition_variable light_work_cv;
// Three FIFO lanes: near-player jobs run first, then init jobs (FINALIZEs
// for chunks that have never been finalized — they still show only the
// optimistic fill), then far ones, preserving submit order within each
// lane. A single deque + push_front near-lane inverted the order (LIFO) —
// during load-in the ~2900 near jobs pushed the earliest (and most
// visible) chunks to the back of the near segment, so light next to the
// player was the LAST to converge.
static std::deque<LightJob> light_jobs_near;
static std::deque<LightJob> light_jobs_init;
static std::deque<LightJob> light_jobs_far;
static size_t light_jobs_depth( ) {
    std::lock_guard<std::mutex> lock( light_work_mutex );
    return light_jobs_near.size( ) + light_jobs_init.size( ) + light_jobs_far.size( );
}
static std::vector<Chunk *> light_pending_list;
// Dirty light volumes awaiting GPU upload, split by distance to the player —
// uploads are capped per frame so a near chunk marked during load-in must not
// sit behind thousands of far entries (that alone added seconds of stale
// lighting next to the player).
static std::vector<Chunk *> light_dirty_list_near;
static std::vector<Chunk *> light_dirty_list_far;

// Parallel workers (docs/lighting-thread-plan.md). A claimed job records
// only its center column; two jobs conflict iff their centers are <= claim
// radius apart in x AND z (Chebyshev). Halo mirrors reach +-1 chunk, so a
// radius of 2 keeps concurrent jobs off shared chunk memory entirely;
// radius 1 still keeps distance-2 jobs' touches in the shared middle chunk
// ~58 cells apart (max light spread ~15) — same chunk, disjoint cells.
// All-or-nothing under the mutex; one claim per worker bounds the array.
// Claims and BFS drains used to be globally exclusive (light_bfs_active):
// a popped seed can write cells anywhere, so the drain ran single-threaded
// and ping-ponged with claims. They now exclude per-column instead —
// light_col_locks below — so drains in disjoint regions run in parallel and
// claims coexist with pops that don't touch their footprint.
#define LIGHT_MAX_WORKERS 8
static glm::ivec2 light_claim_centers[ LIGHT_MAX_WORKERS ]; // in-flight job columns
static int light_claim_count = 0;
static std::atomic<int> light_drain_count{ 0 }; // workers currently in a BFS slice

// Per-column spinlock array: the mutual-exclusion unit for all light[]
// writes. A popped seed reads its own cell + 6 neighbors and writes cells
// whose halo mirrors reach +-1 chunk per axis — bounded by the 3x3 columns
// around the target column (y-mirrors stay inside the column). Claimed
// jobs (finalize/recheck/pending-diff) have the same +-1 bound. Pops take
// 1 lock when strictly interior (all six x/z neighbors off the chunk faces)
// else the 3x3 set; claims take the 3x3 set. Every lock is acquired in
// ascending index order and pop locks are try-locked (requeue on failure),
// so there is no lock-ordering cycle: pops never hold-and-wait.
#define LIGHT_COL_LOCK_COUNT 4096
static std::atomic<int> light_col_locks[ LIGHT_COL_LOCK_COUNT ];
static inline int light_col_lock_index( const int cx, const int cz ) {
    // Odd multipliers spread adjacent columns to distant slots — the common
    // contention case is neighboring columns.
    return ( cx * 0x9E3779B1 + cz * 0x85EBCA77 ) & ( LIGHT_COL_LOCK_COUNT - 1 );
}
static bool light_col_lock_try( const int idx ) {
    int expected = 0;
    return light_col_locks[ idx ].load( std::memory_order_relaxed ) == 0 &&
           light_col_locks[ idx ].compare_exchange_strong( expected, 1, std::memory_order_acquire, std::memory_order_relaxed );
}
// Bounded spin: pop-vs-pop holds last one step (~0.3-1µs), so ~32 tries
// acquire the lock inline instead of paying a requeue round-trip. Claim
// holds are ~ms and never resolve inside any reasonable spin — timing out
// fast bounds the wasted CAS work per requeue.
static bool light_col_lock_spin( const int idx ) {
    for ( int i = 0; i < 32; i++ ) {
        if ( light_col_lock_try( idx ) ) {
            return true;
        }
    }
    return false;
}
static void light_col_lock_acquire( const int idx ) {
    for ( ;; ) {
        while ( light_col_locks[ idx ].load( std::memory_order_relaxed ) != 0 ) {
        }
        int expected = 0;
        if ( light_col_locks[ idx ].compare_exchange_weak( expected, 1, std::memory_order_acquire, std::memory_order_relaxed ) ) {
            return;
        }
    }
}
static void light_col_lock_release( const int idx ) {
    light_col_locks[ idx ].store( 0, std::memory_order_release );
}

// Queued BFS seeds per column (same hash domain as the locks — collisions
// just over-count, which is conservative). Incremented when a seed enters
// a column queue, decremented when one is popped. A column with incident
// seeds is mid-flood: the upload debounce holds its chunks back entirely
// (a floor-write → respread gap under a deep backlog is far longer than
// the write-quiet window, so counting pending work is what actually
// suppresses the correct→dark→correct flash).
static std::atomic<int> light_col_incident[ LIGHT_COL_LOCK_COUNT ];
static void light_col_incident_inc( const glm::ivec3 &block_pos ) {
    const glm::ivec3 cp = light_chunk_pos_of( block_pos );
    light_col_incident[ light_col_lock_index( cp.x, cp.z ) ].fetch_add( 1, std::memory_order_relaxed );
}
static void light_col_incident_dec( const glm::ivec3 &block_pos ) {
    const glm::ivec3 cp = light_chunk_pos_of( block_pos );
    light_col_incident[ light_col_lock_index( cp.x, cp.z ) ].fetch_sub( 1, std::memory_order_relaxed );
}

// Sorted+deduped lock indices for the 3x3 columns around (cx,cz). Sorting
// keeps every multi-lock acquirer on the same global order (claims spin,
// so an unordered pair could deadlock); dedup removes hash collisions that
// would make a thread re-acquire its own flag.
static int light_claim_cols( const int cx, const int cz, int out[ 9 ] ) {
    int n = 0;
    for ( int dz = -1; dz <= 1; dz++ ) {
        for ( int dx = -1; dx <= 1; dx++ ) {
            out[ n++ ] = light_col_lock_index( cx + dx, cz + dz );
        }
    }
    // n == 9: hand-rolled insertion sort + dedup (same rationale as
    // light_pop_cols: fixed small buffer trips GCC's -Warray-bounds under
    // -Werror on some targets).
    for ( int i = 1; i < n; i++ ) {
        const int v = out[ i ];
        int j = i;
        while ( j > 0 && out[ j - 1 ] > v ) {
            out[ j ] = out[ j - 1 ];
            j--;
        }
        out[ j ] = v;
    }
    int m = 0;
    for ( int i = 0; i < n; i++ ) {
        if ( m == 0 || out[ m - 1 ] != out[ i ] ) {
            out[ m++ ] = out[ i ];
        }
    }
    return m;
}

// Lock set for one BFS seed — the columns the step can read or write. The
// step reads pos's own cell and inspects/writes its 6 neighbors; each
// neighbor write goes through light_set, whose mirror matrix reaches the
// cell's own column +/-1 on each axis where the cell sits on a border —
// the cartesian product of its border-axis offsets. Face-edge seeds take
// 2-4 columns; only degenerate corners approach the 3x3 bound (mirrors
// only extend toward the border the cell sits on, so the footprint can
// never leave pos's column neighborhood). Vertical neighbors/mirrors stay
// inside the column (same cx,cz).
// Entries: hashed lock index + column coords. Sorted by index so
// multi-lock acquires share one global order; coord lookups stay aligned.
struct LightPopCol {
    int idx;
    int cx;
    int cz;
};
static int light_pop_cols( const glm::ivec3 &block_pos, LightPopCol out[ 9 ] ) {
    const glm::ivec3 cp = light_chunk_pos_of( block_pos );
    const glm::ivec3 local = block_pos - cp * CHUNK_SIZE_I;
    if ( local.x >= 2 && local.x <= CHUNK_SIZE_X - 3 && local.z >= 2 && local.z <= CHUNK_SIZE_Z - 3 ) {
        out[ 0 ] = { light_col_lock_index( cp.x, cp.z ), cp.x, cp.z };
        return 1;
    }
    int n = 0;
    glm::ivec2 seen[ 9 ];
    seen[ n++ ] = glm::ivec2( cp.x, cp.z );
    for ( const glm::ivec3 &d : light_dirs ) {
        const glm::ivec3 p = block_pos + d;
        const glm::ivec3 c = light_chunk_pos_of( p );
        const glm::ivec3 l = p - c * CHUNK_SIZE_I;
        const int xo = l.x == 0 ? -1 : ( l.x == CHUNK_SIZE_X - 1 ? 1 : 0 );
        const int zo = l.z == 0 ? -1 : ( l.z == CHUNK_SIZE_Z - 1 ? 1 : 0 );
        const int xopts[ 2 ] = { 0, xo };
        const int zopts[ 2 ] = { 0, zo };
        for ( const int dx : xopts ) {
            for ( const int dz : zopts ) {
                const glm::ivec2 col( c.x + dx, c.z + dz );
                bool dup = false;
                for ( int i = 0; i < n; i++ ) {
                    if ( seen[ i ] == col ) {
                        dup = true;
                        break;
                    }
                }
                if ( !dup ) {
                    seen[ n++ ] = col;
                }
            }
        }
    }
    for ( int i = 0; i < n; i++ ) {
        out[ i ] = { light_col_lock_index( seen[ i ].x, seen[ i ].y ), seen[ i ].x, seen[ i ].y };
    }
    // n <= 9: hand-rolled insertion sort + dedup. Avoids std::sort's
    // introsort dispatch and GCC 13's -Warray-bounds false positive on the
    // small fixed buffer (mingw -Werror).
    for ( int i = 1; i < n; i++ ) {
        const LightPopCol v = out[ i ];
        int j = i;
        while ( j > 0 && out[ j - 1 ].idx > v.idx ) {
            out[ j ] = out[ j - 1 ];
            j--;
        }
        out[ j ] = v;
    }
    int m = 0;
    for ( int i = 0; i < n; i++ ) {
        if ( m == 0 || out[ m - 1 ].idx != out[ i ].idx ) {
            out[ m++ ] = out[ i ];
        }
    }
    return m;
}

// Spin-acquire a sorted lock set, then run and release. Pops use this; the
// set is tiny (1-9 flags) and hold times are a single step (~hundreds of
// ns), so contention resolves quickly.
static void light_cols_lock( const int *cols, const int n ) {
    for ( int i = 0; i < n; i++ ) {
        light_col_lock_acquire( cols[ i ] );
    }
}
static void light_cols_unlock( const int *cols, const int n ) {
    for ( int i = 0; i < n; i++ ) {
        light_col_lock_release( cols[ i ] );
    }
}

// REPGAME_LIGHT_CLAIM_RADIUS (1..4, default 1): Chebyshev center distance
// that counts as a conflict. 4 reproduces the old 5x5-key-overlap semantics
// exactly; 2 is the documented column model (no shared touched chunk);
// 1 relies on the disjoint-cell argument above. Resolved once per process.
static int light_claim_radius( ) {
    static const int r = []( ) {
        const char *env = getenv( "REPGAME_LIGHT_CLAIM_RADIUS" );
        const int v = env ? atoi( env ) : 1;
        return v < 1 ? 1 : ( v > 4 ? 4 : v );
    }( );
    return r;
}

// REPGAME_LIGHT_WORKERS (1..8, default 4): pool size. Claim capacity is
// sized for the max, so the env only changes how many threads spawn.
static int light_worker_count( ) {
    static const int n = []( ) {
        const char *env = getenv( "REPGAME_LIGHT_WORKERS" );
        const int v = env ? atoi( env ) : 4;
        return v < 1 ? 1 : ( v > LIGHT_MAX_WORKERS ? LIGHT_MAX_WORKERS : v );
    }( );
    return n;
}
// Backlog-priority window: while set, no NEW finalize claims are granted so
// in-flight claims drain and a BFS drain can run; interactive work (rechecks,
// pending diffs) still claims so block edits stay responsive. Hysteresis
// (HI/LO) plus a time cap keep the window from starving finalizes forever.
static volatile int light_bfs_priority = 0;
#define LIGHT_BFS_PRI_HI 3000000
#define LIGHT_BFS_PRI_LO 400000
#define LIGHT_BFS_PRI_MAX_US 100000

// Starvation window: a drain also needs no claims in flight, which during
// continuous streaming effectively never happens — the backlog then sits
// (pop=0 for whole seconds) and even near-player light only converges when
// streaming stops. The trigger is backlog AGE (light_bfs_backlog_tick), not
// time-since-last-drain: the ~2ms scraps a worker grabs whenever claims
// momentarily idle would otherwise keep refreshing "last drain" and defer
// the window forever. Once the backlog has stayed over LIGHT_BFS_STARVE_MIN
// for STARVE_TICKS, open the same priority window: finalize claims pause,
// in-flight claims finish, the drainer runs until the backlog empties or
// the cap.
static volatile int light_bfs_starve = 0; // priority window was age-triggered

// A recheck's own writes (the emit cell, mirror cells, cascade clears) mark
// dirty the moment they run, but their propagation spread only writes when a
// BFS drain pops the seeds — during load-in that is frames later, so the
// torch cell appears lit while the halo around it is still dark. While
// light_defer_marks > 0, light_mark_dirty appends to this list instead; the
// marks flush at the start of the next drain so the emit cell and its spread
// upload in the same pass. Guarded by light_work_mutex.
static std::atomic<int> light_defer_marks{ 0 };
static std::vector<std::pair<Chunk *, glm::ivec3>> light_deferred_marks;
#define LIGHT_BFS_STARVE_MAX_US 25000   // drain cap inside a starve window (at STARVE_MIN)
#define LIGHT_BFS_STARVE_HARD_US 150000 // ceiling for the backlog-scaled starve cap

// Footprint is free (no claimed center within the conflict radius of cx,cz)?
// Also requires, unless bypass_priority, that no backlog-priority window is
// in effect. Drains no longer block claims globally — pops that overlap a
// claim's columns serialize on light_col_locks instead.
static bool light_claim_free( int cx, int cz, const bool bypass_priority ) {
    if ( light_bfs_priority && !bypass_priority ) {
        return false;
    }
    const int r = light_claim_radius( );
    for ( int i = 0; i < light_claim_count; i++ ) {
        const int dx = light_claim_centers[ i ].x - cx;
        const int dz = light_claim_centers[ i ].y - cz;
        if ( dx >= -r && dx <= r && dz >= -r && dz <= r ) {
            return false;
        }
    }
    return true;
}

// Record the center column. Caller holds light_work_mutex; the test-then-push
// is atomic under it, so no partial-claim rollback is needed.
static bool light_try_claim( int cx, int cz, const bool bypass_priority ) {
    if ( light_claim_count >= light_worker_count( ) || !light_claim_free( cx, cz, bypass_priority ) ) {
        return false;
    }
    light_claim_centers[ light_claim_count++ ] = glm::ivec2( cx, cz );
    return true;
}

static void light_release_claim( int cx, int cz ) {
    for ( int i = 0; i < light_claim_count; i++ ) {
        if ( light_claim_centers[ i ].x == cx && light_claim_centers[ i ].y == cz ) {
            light_claim_centers[ i ] = light_claim_centers[ --light_claim_count ];
            return;
        }
    }
}

static std::thread light_threads[ LIGHT_MAX_WORKERS ];
static volatile int light_thread_running = 0;
static volatile int light_thread_stop_flag = 0;

// Pack/unpack helpers for Chunk::light_dirty_box (layout in chunk.hpp).
// 8 bits per component limits texel coords to [0,255], so the light volume
// can be at most 256^3 internal — a larger CHUNK_SIZE would silently wrap
// dirty-box coordinates and scramble uploads. LIGHT_BOX_EMPTY has
// min=255 > max=0.
static_assert( CHUNK_SIZE_INTERNAL_X <= 256 && CHUNK_SIZE_INTERNAL_Y <= 256 && CHUNK_SIZE_INTERNAL_Z <= 256,
               "light_dirty_box packs texel coords as 8-bit fields; CHUNK_SIZE_INTERNAL_* must be <= 256" );
static inline uint64_t light_box_pack( const glm::ivec3 &mn, const glm::ivec3 &mx ) {
    return static_cast<uint64_t>( mn.x & 0xFF ) | ( static_cast<uint64_t>( mn.y & 0xFF ) << 8 ) |
           ( static_cast<uint64_t>( mn.z & 0xFF ) << 16 ) | ( static_cast<uint64_t>( mx.x & 0xFF ) << 24 ) |
           ( static_cast<uint64_t>( mx.y & 0xFF ) << 32 ) | ( static_cast<uint64_t>( mx.z & 0xFF ) << 40 );
}

static inline glm::ivec3 light_box_min( const uint64_t box ) {
    return glm::ivec3( static_cast<int>( box & 0xFF ), static_cast<int>( ( box >> 8 ) & 0xFF ),
                       static_cast<int>( ( box >> 16 ) & 0xFF ) );
}

static inline glm::ivec3 light_box_max( const uint64_t box ) {
    return glm::ivec3( static_cast<int>( ( box >> 24 ) & 0xFF ), static_cast<int>( ( box >> 32 ) & 0xFF ),
                       static_cast<int>( ( box >> 40 ) & 0xFF ) );
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
        const bool near = light_near_col( this->chunk_center, chunk.chunk_pos );
        std::lock_guard<std::mutex> lock( light_work_mutex );
        if ( !chunk.light_upload_listed ) {
            chunk.light_upload_listed = 1;
            ( near ? light_dirty_list_near : light_dirty_list_far ).push_back( &chunk );
        }
    }
}

void ChunkLoader::light_mark_dirty_now( Chunk &chunk, const glm::ivec3 &local ) {
    chunk.light_write_tick.store( light_tick_counter.load( std::memory_order_relaxed ), std::memory_order_relaxed );
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

void ChunkLoader::light_mark_dirty( Chunk &chunk, const glm::ivec3 &local ) {
    // Stamp BEFORE the defer check: the write is happening now regardless of
    // when its texel mark lands, and the upload-quiet window measures writes.
    chunk.light_write_tick.store( light_tick_counter.load( std::memory_order_relaxed ), std::memory_order_relaxed );
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
        if ( e.first->light && !e.first->load_state ) {
            light_mark_dirty_now( *e.first, e.second );
        }
    }
}

// Whole-volume variant for finalize/reseed paths that rewrite everything.
// Dirty coords are texel-space (internal+1), so the full 34^3 texture is
// 0..CHUNK_SIZE_INTERNAL_-1 inclusive.
void ChunkLoader::light_mark_dirty_all( Chunk &chunk ) {
    chunk.light_write_tick.store( light_tick_counter.load( std::memory_order_relaxed ), std::memory_order_relaxed );
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
                const unsigned char cell = light_read_cell( *c, idx );
                const unsigned char nv = channel == LIGHT_CHANNEL_BLOCK //
                    ? static_cast<unsigned char>( ( cell & 0xF0 ) | value )
                    : static_cast<unsigned char>( ( cell & 0x0F ) | ( value << 4 ) );
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
                const unsigned char cell = light_read_cell( *c, idx );
                const unsigned char nv = channel == LIGHT_CHANNEL_BLOCK //
                    ? static_cast<unsigned char>( ( cell & 0xF0 ) | value )
                    : static_cast<unsigned char>( ( cell & 0x0F ) | ( value << 4 ) );
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
    const unsigned char cell = light_read_cell( *chunk, light_index( local.x, local.y, local.z ) );
    const int v = seed.channel == LIGHT_CHANNEL_BLOCK ? light_get_block( cell ) : light_get_sky( cell );
    if ( v <= 0 ) {
        return;
    }
    const bool sky = seed.channel == LIGHT_CHANNEL_SKY;
    for ( int d = 0; d < 6; d++ ) {
        const glm::ivec3 n_local = local + light_dirs[ d ];
        // Best possible offer this direction (target's absorb can only lower
        // it): cells at or above it can't gain light from this step.
        const int best = ( sky && d == LIGHT_DIR_DOWN && v == LIGHT_MAX_LEVEL ) ? LIGHT_MAX_LEVEL : v - 1;
        // Strictly-interior neighbor: same owner, no halo mirrors — direct
        // array ops only, no get_chunk/light_set lookups.
        if ( n_local.x > 0 && n_local.x < CHUNK_SIZE_X - 1 && n_local.y > 0 && n_local.y < CHUNK_SIZE_Y - 1 && n_local.z > 0 && n_local.z < CHUNK_SIZE_Z - 1 ) {
            const int nidx = light_index( n_local.x, n_local.y, n_local.z );
            const unsigned char ncell = light_read_cell( *chunk, nidx );
            const int nv = sky ? light_get_sky( ncell ) : light_get_block( ncell );
            if ( best <= nv ) {
                continue;
            }
            // A null cells[] volume is all-air: nothing is opaque.
            const BlockID n_id = chunk->cells ? block_cell_id( chunk->cells[ chunk->get_index_from_coords( n_local.x, n_local.y, n_local.z ) ] ) : AIR;
            if ( light_opaque_id( n_id ) ) {
                continue;
            }
            const int offered = light_offered( v, seed.channel, d, n_id );
            if ( offered <= nv ) {
                continue;
            }
            light_write_cell( *chunk, nidx, sky ? static_cast<unsigned char>( ( ncell & 0x0F ) | ( offered << 4 ) ) : static_cast<unsigned char>( ( ncell & 0xF0 ) | offered ) );
            light_mark_dirty( *chunk, n_local );
            light_dbg_step_push++;
            this->light_add_seed( seed.pos + light_dirs[ d ], seed.channel, offered, true );
            continue;
        }
        const glm::ivec3 n = seed.pos + light_dirs[ d ];
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
    for ( int d = 0; d < 6; d++ ) {
        const glm::ivec3 n = seed.pos + light_dirs[ d ];
        const glm::ivec3 n_local = local + light_dirs[ d ];
        const bool interior = n_local.x >= 0 && n_local.x < CHUNK_SIZE_X && n_local.y >= 0 && n_local.y < CHUNK_SIZE_Y && n_local.z >= 0 && n_local.z < CHUNK_SIZE_Z;
        int nv;
        BlockID n_id;
        if ( interior ) {
            const unsigned char ncell = light_read_cell( *chunk, light_index( n_local.x, n_local.y, n_local.z ) );
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
            const int floor_v = seed.channel == LIGHT_CHANNEL_BLOCK ? block_definition_get_definition( n_id )->emits_light : 0;
            // Strictly interior (no halo mirrors): write the floor directly
            // instead of going through light_set's mirror matrix.
            const bool strict = n_local.x > 0 && n_local.x < CHUNK_SIZE_X - 1 && n_local.y > 0 && n_local.y < CHUNK_SIZE_Y - 1 && n_local.z > 0 && n_local.z < CHUNK_SIZE_Z - 1;
            if ( strict ) {
                const int widx = light_index( n_local.x, n_local.y, n_local.z );
                const unsigned char wcell = light_read_cell( *chunk, widx );
                light_write_cell( *chunk, widx, seed.channel == LIGHT_CHANNEL_BLOCK ? static_cast<unsigned char>( ( wcell & 0xF0 ) | floor_v )
                                                                                  : static_cast<unsigned char>( ( wcell & 0x0F ) | ( floor_v << 4 ) ) );
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
            std::deque<LightSeed> &bucket = this->light_add_queue[ col ][ ch ][ level ];
            if ( !bucket.empty( ) ) {
                add_out = bucket.front( );
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
        LightPopCol cols[ 9 ];
        const int ncols = light_pop_cols( pos, cols );
        int held = 0;
        for ( ; held < ncols; held++ ) {
            if ( !light_col_lock_spin( cols[ held ].idx ) ) {
                break;
            }
        }
        if ( held < ncols ) {
            for ( int i = 0; i < held; i++ ) {
                light_col_lock_release( cols[ i ].idx );
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
                    this->light_add_queue[ col ][ add_seed.channel ][ level ].push_back( add_seed );
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
            light_col_lock_release( cols[ i ].idx );
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

void ChunkLoader::light_ensure_columns( Chunk &chunk ) {
    if ( !chunk.light_columns ) {
        chunk.light_columns = chunk_alloc_light_columns( );
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
    chunk.light_columns[ ci ] = open;
    chunk.light_columns[ LIGHT_FILL_FROM_INDEX( x, z ) ] = static_cast<unsigned char>( fill_from );
}

// column_open[x][z] == 1 iff no column-blocking (opaque or absorbing) cell
// exists anywhere in this chunk's interior column. Called by the worker in
// load_terrain and lazily by light_ensure_columns on the render thread.
void ChunkLoader::light_compute_column_open( Chunk &chunk ) {
    if ( !chunk.light_columns ) {
        return;
    }
    if ( !chunk.cells ) {
        // All-air: every column open, fill reaches y=0.
        memset( chunk.light_columns, 1, LIGHT_FLAGS_COUNT );
        memset( chunk.light_columns + LIGHT_FILL_FROM_INDEX( 0, 0 ), 0, LIGHT_FLAGS_COUNT );
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
        if ( !chunk || chunk->load_state ) {
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
                // needs no per-cell cells[] reads.
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
                        this->light_push_remove_seed( pos, LIGHT_CHANNEL_SKY, v );
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
        if ( !chunk || chunk->load_state ) {
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
                                light_write_cell( *chunk, idx, static_cast<unsigned char>( ( light_read_cell( *chunk, idx ) & 0x0F ) | ( LIGHT_MAX_LEVEL << 4 ) ) );
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
                                    light_write_cell( *chunk, idx, static_cast<unsigned char>( light_read_cell( *chunk, idx ) & 0x0F ) );
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
                this->light_push_remove_seed( block_pos, ch, old_v );
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

    light_defer_marks--;
    // No seeds -> no drain is coming to flush the deferred marks; do it now.
    // (Pops on other columns can run concurrently now, so pending may have
    // grown for unrelated reasons too — either way a drainer will flush.)
    if ( light_bfs_pending.load( std::memory_order_relaxed ) <= pend_before ) {
        this->light_flush_deferred_marks( );
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
    if ( chunk.load_state ) {
        return;
    }
    light_dbg_finalize++;
    const long long t0 = light_now_us( );
    if ( chunk.light_reseed ) {
        light_fill_chunk( chunk ); // refill emitters + column_open
        chunk.light_reseed = 0;
        // The wipe+refill already re-derived the whole volume — a stale flag
        // left over from cap refusals is superseded (racy refusals during
        // this run re-flag it, which is fine: the next heal re-derives).
        chunk.light_stale.store( 0, std::memory_order_relaxed );
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
    // Real skylight is derived now — future finalizes route to the far lane.
    chunk.light_finalized.store( 1, std::memory_order_relaxed );
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
                    const int cv = light_get_sky( light_read_cell( chunk, light_index( x, y, z ) ) );
                    if ( light_get_sky( light_read_cell( chunk, light_index( nx, y, nz ) ) ) >= cv - 1 ) {
                        continue;
                    }
                    // A null cells[] volume is all-air: nothing is opaque.
                    if ( chunk.cells && light_opaque_id( block_cell_id( chunk.cells[ chunk.get_index_from_coords( nx, y, nz ) ] ) ) ) {
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
                if ( !nchunk || nchunk->load_state ) {
                    continue;
                }
                const int cv = light_get_sky( light_read_cell( chunk, light_index( x, y, z ) ) );
                const int offered = ( d == LIGHT_DIR_DOWN && cv == LIGHT_MAX_LEVEL ) ? LIGHT_MAX_LEVEL : cv - 1;
                const int ny = y + dir.y;
                const int nwy = ny < 0 ? CHUNK_SIZE_Y - 1 : ny >= CHUNK_SIZE_Y ? 0 : ny;
                const int nv = light_get_sky( light_read_cell( *nchunk, light_index( x, nwy, z ) ) );
                if ( cv > 1 && offered > 0 && nv < offered ) {
                    const BlockID n_id = nchunk->cells ? block_cell_id( nchunk->cells[ nchunk->get_index_from_coords( x, nwy, z ) ] ) : AIR;
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
                if ( !nchunk || nchunk->load_state ) {
                    continue;
                }
                const int nx = x + dir2.x;
                const int nz = z + dir2.y;
                const int nwx = nx < 0 ? CHUNK_SIZE_X - 1 : nx >= CHUNK_SIZE_X ? 0 : nx;
                const int nwz = nz < 0 ? CHUNK_SIZE_Z - 1 : nz >= CHUNK_SIZE_Z ? 0 : nz;
                for ( int y = lit_top; y < CHUNK_SIZE_Y; y++ ) {
                    const int cv = light_get_sky( light_read_cell( chunk, light_index( x, y, z ) ) );
                    if ( cv <= 1 ) {
                        continue;
                    }
                    const int nv = light_get_sky( light_read_cell( *nchunk, light_index( nwx, y, nwz ) ) );
                    if ( nv < cv - 1 ) {
                        const BlockID n_id = nchunk->cells ? block_cell_id( nchunk->cells[ nchunk->get_index_from_coords( nwx, y, nwz ) ] ) : AIR;
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
        if ( !chunk || chunk->load_state ) {
            continue;
        }
        for ( int y = 0; y < CHUNK_SIZE_Y; y++ ) {
            const unsigned char cell = light_read_cell( *chunk, light_index( lx, y, lz ) );
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
    // chunk.light may be null (uniform volume): light_read_cell/write_cell
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
        const unsigned char canon = light_read_cell( *owner, light_index( x - ox * CHUNK_SIZE_X, y - oy * CHUNK_SIZE_Y, z - oz * CHUNK_SIZE_Z ) );
        const int midx = light_index( x, y, z );
        if ( light_read_cell( chunk, midx ) != canon ) {
            light_write_cell( chunk, midx, canon );
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
                    // cells[] is only consulted when the neighbor can
                    // actually gain light.
                    nv = ch == LIGHT_CHANNEL_BLOCK ? light_get_block( light_read_cell( chunk, light_index( n_local.x, n_local.y, n_local.z ) ) )
                                                   : light_get_sky( light_read_cell( chunk, light_index( n_local.x, n_local.y, n_local.z ) ) );
                    if ( nv >= offered ) {
                        continue;
                    }
                    n_id = chunk.cells ? block_cell_id( chunk.cells[ chunk.get_index_from_coords( n_local.x, n_local.y, n_local.z ) ] ) : AIR;
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
        const unsigned char canon = light_read_cell( chunk, light_index( x, y, z ) );
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
                    const unsigned char old_mirror = light_read_cell( *nb, midx );
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
                if ( !nax || nax->load_state ) {
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
                const int nv = ch == LIGHT_CHANNEL_BLOCK ? light_get_block( light_read_cell( *nax, light_index( n_loc.x, n_loc.y, n_loc.z ) ) )
                                                         : light_get_sky( light_read_cell( *nax, light_index( n_loc.x, n_loc.y, n_loc.z ) ) );
                if ( nv >= offered ) {
                    continue;
                }
                const BlockID n_id = nax->cells ? block_cell_id( nax->cells[ nax->get_index_from_coords( n_loc.x, n_loc.y, n_loc.z ) ] ) : AIR;
                if ( !light_opaque_id( n_id ) ) {
                    light_dbg_border_seed++;
                    this->light_add_seed( w, ch, new_v );
                    break;
                }
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
    if ( chunk.load_state ) {
        return; // loading chunks drain their queue in light_finalize_chunk
    }
    if ( chunk.light_reseed ) {
        // Full refill (light_pending overflowed). light_fill_chunk wiped
        // skylight and reset sky_open_above, so re-run the column
        // cascade to flip the flags and reseed, plus a border sync. The
        // whole volume was rewritten, so mark it all dirty for upload.
        light_fill_chunk( chunk );
        chunk.light_reseed = 0;
        chunk.light_stale.store( 0, std::memory_order_relaxed );
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
    // The memset below is a write the upload-quiet window must see: without
    // this stamp an already-listed chunk could upload mid-wipe, before the
    // cascade/marks restamp it — a one-frame full-volume black flash.
    chunk.light_write_tick.store( light_tick_counter.load( std::memory_order_relaxed ), std::memory_order_relaxed );
    if ( !chunk.cells ) {
        // All-air chunk: no light volume at all. light==nullptr means every
        // cell holds light_uniform — set it to the same optimistic sky=15 the
        // volume path's prefill would produce, so propagation reads it without
        // the array ever existing. Column flags still get computed (all-open
        // + optimistic sky_open_above) so the cascade's flag bookkeeping
        // stays correct.
        if ( chunk.light ) {
            chunk_free_light( chunk.light );
            chunk.light = nullptr;
        }
        // cells[] is gone, so its derived air mask must be too (a stale
        // mask would describe the previous occupancy of this chunk slot).
        if ( chunk.light_air_mask ) {
            chunk_free_air_mask( chunk.light_air_mask );
            chunk.light_air_mask = nullptr;
        }
        chunk.light_uniform = static_cast<unsigned char>( LIGHT_MAX_LEVEL << 4 );
        ChunkLoader::light_compute_column_open( chunk );
        if ( chunk.light_columns ) {
            memset( chunk.light_columns + LIGHT_FLAGS_COUNT, 1, LIGHT_FLAGS_COUNT );
        }
        return;
    }
    if ( !chunk.light ) {
        chunk.light = chunk_alloc_light( );
    }
    memset( chunk.light, 0, CHUNK_BLOCK_SIZE );
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
    if ( light_mipmaps_enabled( ) ) {
        if ( chunk.light_air_mask == nullptr ) {
            chunk.light_air_mask = chunk_alloc_air_mask( );
        }
        // Rewritten wholesale below (this can re-run via light_reseed).
        memset( chunk.light_air_mask, 0, CHUNK_BLOCK_SIZE / 8 );
        air_mask = chunk.light_air_mask;
    } else if ( chunk.light_air_mask ) {
        // Can't happen (mipmaps is process-lifetime constant), but never
        // leave a mask around that upload would wrongly trust.
        chunk_free_air_mask( chunk.light_air_mask );
        chunk.light_air_mask = nullptr;
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
        const int emit = def->emits_light;
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
        if ( v <= 1 ) {
            continue; // best possible offer (v-1) can't light anything
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
            if ( v - 1 <= ( chunk.light[ nidx ] & 0x0F ) ) {
                continue;
            }
            const BlockID n_id = block_cell_id( chunk.cells[ chunk.get_index_from_coords( nx, ny, nz ) ] );
            if ( light_opaque_id( n_id ) ) {
                continue;
            }
            const int offered = v - 1 - block_definition_get_definition( n_id )->light_absorb;
            if ( offered <= ( chunk.light[ nidx ] & 0x0F ) ) {
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
        // The fill covers the 1-cell halo shell too (meshes sample it at
        // chunk faces): the halo blocks are real terrain data generated and
        // persisted with the chunk, so the edge lights to its own surface
        // line instead of the memset-0 dark edge it had until the
        // neighbors' border sync converged. A wrong guess self-corrects —
        // the owner chunk's border sync rewrites the mirror and pushes a
        // removal when it loads.
        unsigned char *flags = chunk.light_columns;
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
                    unsigned char &cell = chunk.light[ light_index( x, y, z ) ];
                    cell = static_cast<unsigned char>( ( cell & 0x0F ) | ( LIGHT_MAX_LEVEL << 4 ) );
                }
            }
        }
    }
    // Fold constant fills back to the uniform representation — a fully-dark
    // underground chunk (no emitters, all columns closed) or a fully-lit one
    // keeps just the byte instead of the 262K array.
    {
        const unsigned char first = chunk.light[ 0 ];
        bool uniform = true;
        for ( int i = 1; i < CHUNK_BLOCK_SIZE; i++ ) {
            if ( chunk.light[ i ] != first ) {
                uniform = false;
                break;
            }
        }
        if ( uniform ) {
            chunk.light_uniform = first;
            chunk_free_light( chunk.light );
            chunk.light = nullptr;
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
// The light-mipmap mode additionally carries B = 255 for cells that can
// hold light (non-opaque per the same light_opaque_id the BFS uses — water
// counts as air since it doesn't cast a shadow), 0 for solid, plus an
// unused alpha (RGBA8 avoids the driver's RGB->RGBA repack on upload). The
// mipmapped volume then recovers the average light over the *air* cells in
// a footprint as rg / b (see chunk_fragment.glsl). It must be a real air
// mask, not a lit-cell mask: solid cells always hold light 0, so "has
// light" conflates solid with merely-dark air — pitch-black air (deep
// caves) has to sit IN the denominator, otherwise rg / b divides the few
// lit cells in a coarse texel up to full brightness. Opacity comes from
// Chunk::light_air_mask (a 32KB bitmap built by light_fill_chunk while
// cells[] is already streamed, restamped on block edits); the per-cell
// cells[] walk remains only as the fallback for chunks whose mask was
// never built.
bool light_mipmaps_enabled( ) {
    // Resolved once and constant for the process lifetime — the texture
    // format and mip chain are decided by it.
    static const bool enabled = []( ) {
        const char *env = getenv( "REPGAME_LIGHT_MIPMAPS" );
        // Default on everywhere; the env var is the escape hatch if a driver
        // makes 3D mipmap generation expensive on the upload path.
        const bool e = env ? atoi( env ) != 0 : true;
        pr_debug( "Light mipmaps: %s", e ? "on" : "off" );
        return e;
    }( );
    return enabled;
}

// Scratch is 4 bytes/texel: mip mode writes a uint32_t texel per cell, the
// RG8 mode writes 2 bytes per cell through the unsigned char* view.
static uint32_t s_light_upload_scratch[ CHUNK_BLOCK_SIZE ];

// BlockID -> air mask (255 = can hold light, 0 = opaque) using the same
// light_opaque_id predicate the propagation BFS uses, so the mask agrees
// with what the light values actually reached. Built on first call: it
// needs block definitions initialised, and file-scope static init order
// across TUs is not guaranteed (same pattern as light_mipmaps_enabled).
static const unsigned char *light_air_lut( ) {
    static unsigned char lut[ LAST_BLOCK_ID + 1 ];
    static const bool init = []( ) {
        for ( int i = 0; i <= LAST_BLOCK_ID; i++ ) {
            lut[ i ] = light_opaque_id( static_cast<BlockID>( i ) ) ? 0 : 255;
        }
        return true;
    }( );
    ( void ) init;
    return lut;
}

static inline void light_expand_cell( const unsigned char packed, unsigned char *out ) {
    out[ 0 ] = static_cast<unsigned char>( ( packed >> 4 ) * 17 ); // sky
    out[ 1 ] = static_cast<unsigned char>( ( packed & 0x0F ) * 17 ); // block
}

// packed light byte -> (sky*17) | (block*17 << 8): the RG half of the RGBA8
// texel in one u32, so the mipmapped fill can store a whole texel per cell
// (little-endian byte order, matching the byte-wise path).
static const uint32_t *light_rg_lut( ) {
    static uint32_t lut[ 256 ];
    static const bool init = []( ) {
        for ( int i = 0; i < 256; i++ ) {
            lut[ i ] = static_cast<uint32_t>( ( i >> 4 ) * 17 ) | ( static_cast<uint32_t>( ( i & 0x0F ) * 17 ) << 8 );
        }
        return true;
    }( );
    ( void ) init;
    return lut;
}

void Chunk::light_ensure_texture( ) {
    if ( this->light_texture != 0 ) {
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
    const bool mipmaps = light_mipmaps_enabled( );
    if ( mipmaps ) {
        unsigned char *out8 = reinterpret_cast<unsigned char *>( s_light_upload_scratch );
        // B is a per-cell air mask. It normally comes straight out of
        // chunk.light_air_mask — the 32KB bitmap light_fill_chunk built while
        // streaming cells[] and edits keep stamped — so the common case
        // iterates in texel order and never touches the 3.7MB cells[] array
        // (nor even strides light[]: both stream linearly). When the mask is
        // missing the fallback still walks cells[] (BlockState is 14 packed
        // bytes) in z-inner order so the cells[] side streams. Two fast
        // paths skip the general case entirely:
        //  - blocks == null: the whole volume is air, B = 255.
        //  - light == null && light_uniform != 0: every cell holds light, so
        //    every cell is non-opaque by definition — B = 255, no lookup.
        //    (Uniform light alone is NOT enough: an all-stone chunk packs
        //    light_uniform == 0 just like an unlit air pocket.)
        if ( this->light == nullptr && ( this->cells == nullptr || this->light_uniform != 0 ) ) {
            const uint32_t cell = light_rg_lut( )[ this->light_uniform ] | 0xFFFF0000u; // B=255, A=255
            uint32_t *out32 = s_light_upload_scratch;
            for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
                out32[ i ] = cell;
            }
        } else if ( this->cells == nullptr ) {
            // All-air but non-uniform light: still expand light[] per cell.
            const uint32_t *rg_lut = light_rg_lut( );
            uint32_t *out32 = s_light_upload_scratch;
            for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
                out32[ i ] = rg_lut[ this->light[ i ] ] | 0xFFFF0000u;
            }
        } else if ( this->light_air_mask != nullptr ) {
            // Mask built: iterate in texel order (x fastest) so light[] reads
            // and scratch writes are both linear streams. The mask lives in
            // cells[] index space (z-fastest): mask byte index is
            // (tz>>3) + (Z_INT/8)*tx + (X_INT*Z_INT/8)*ty and the tested bit
            // (tz&7) is invariant across the plane, so the same
            // X_INT*Z_INT/8 mask bytes serve 8 consecutive tz planes.
            const unsigned char *air_mask = this->light_air_mask;
            const uint32_t *rg_lut = light_rg_lut( );
            uint32_t *out32 = s_light_upload_scratch;
            if ( this->light == nullptr ) {
                // Uniform-dark volume (light_uniform==0 — nonzero went to the
                // fast path above): every cell's RG is the same constant and
                // B is exactly the mask bit, so no per-cell light read.
                const uint32_t base = rg_lut[ this->light_uniform ] | ( 255u << 24 );
                for ( int tz = 0; tz < CHUNK_SIZE_INTERNAL_Z; tz++ ) {
                    const unsigned char *mrow = air_mask + ( tz >> 3 );
                    const int shift = tz & 7;
                    for ( int ty = 0; ty < CHUNK_SIZE_INTERNAL_Y; ty++ ) {
                        const unsigned char *mty = mrow + ty * ( CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Z / 8 );
                        for ( int tx = 0; tx < CHUNK_SIZE_INTERNAL_X; tx++, out32++ ) {
                            const uint32_t air = ( static_cast<uint32_t>( mty[ tx * ( CHUNK_SIZE_INTERNAL_Z / 8 ) ] ) >> shift ) & 1u;
                            *out32 = base | ( air * 255u << 16 );
                        }
                    }
                }
            } else {
                for ( int tz = 0; tz < CHUNK_SIZE_INTERNAL_Z; tz++ ) {
                    const unsigned char *mrow = air_mask + ( tz >> 3 );
                    const int shift = tz & 7;
                    for ( int ty = 0; ty < CHUNK_SIZE_INTERNAL_Y; ty++ ) {
                        const unsigned char *lrow = this->light + ( tz * CHUNK_SIZE_INTERNAL_Y + ty ) * CHUNK_SIZE_INTERNAL_X;
                        const unsigned char *mty = mrow + ty * ( CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Z / 8 );
                        for ( int tx = 0; tx < CHUNK_SIZE_INTERNAL_X; tx++, out32++ ) {
                            const unsigned char packed = lrow[ tx ];
                            // The mask is authoritative for opacity — a lit
                            // cell is provably non-opaque (its bit is set),
                            // and a solid cell reads B=0 even while its light
                            // is still propagating out.
                            const uint32_t air = ( static_cast<uint32_t>( mty[ tx * ( CHUNK_SIZE_INTERNAL_Z / 8 ) ] ) >> shift ) & 1u;
                            *out32 = rg_lut[ packed ] | ( air * 255u << 16 ) | ( 255u << 24 );
                        }
                    }
                }
            }
        } else {
            // Fallback for a chunk whose mask was never built: walk cells[]
            // alongside light[]. The two arrays transpose (light[] x-fastest,
            // cells[] z-fastest), so iterate z innermost — cells[] reads
            // stream sequentially while the strided accesses move to the
            // small cache-resident arrays (light[] is 256KB, scratch 1MB).
            const unsigned char *air_lut = light_air_lut( );
            int tx = 0, ty = 0, tz = 0;
            for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
                const int lidx = ( tz * CHUNK_SIZE_INTERNAL_Y + ty ) * CHUNK_SIZE_INTERNAL_X + tx;
                const unsigned char packed = this->light ? this->light[ lidx ] : this->light_uniform;
                unsigned char *out = out8 + 4 * lidx;
                light_expand_cell( packed, out );
                out[ 2 ] = packed != 0 ? 255 : air_lut[ block_cell_id( this->cells[ i ] ) ];
                out[ 3 ] = 255;
                if ( ++tz == CHUNK_SIZE_INTERNAL_Z ) {
                    tz = 0;
                    if ( ++tx == CHUNK_SIZE_INTERNAL_X ) {
                        tx = 0;
                        ty++;
                    }
                }
            }
        }
    } else {
        unsigned char *out8 = reinterpret_cast<unsigned char *>( s_light_upload_scratch );
        if ( this->light ) {
            for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
                light_expand_cell( this->light[ i ], out8 + 2 * i );
            }
        } else {
            // Uniform volume (null light[]): the whole texture is one value.
            unsigned char cell[ 2 ];
            light_expand_cell( this->light_uniform, cell );
            for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
                out8[ 2 * i ] = cell[ 0 ];
                out8[ 2 * i + 1 ] = cell[ 1 ];
            }
        }
    }
    glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
    glPixelStorei( GL_UNPACK_IMAGE_HEIGHT, 0 );
    glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
    glTexImage3D( GL_TEXTURE_3D, 0, mipmaps ? GL_RGBA8 : GL_RG8, CHUNK_SIZE_INTERNAL_X, CHUNK_SIZE_INTERNAL_Y, CHUNK_SIZE_INTERNAL_Z, 0, mipmaps ? GL_RGBA : GL_RG, GL_UNSIGNED_BYTE, s_light_upload_scratch );
    glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
    if ( mipmaps ) {
        // LIGHT_TEX_MAX_MIP mirrors the shader's LIGHT_LOD_MAX cap: levels
        // deeper than 1 are never sampled, so cap the chain and let
        // glGenerateMipmap build only level 1 (32^3) instead of all six.
        glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, LIGHT_TEX_MAX_MIP );
        glGenerateMipmap( GL_TEXTURE_3D );
        glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
    } else {
        glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    }
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE );
    light_dbg_n_ensure++;
    light_dbg_us_ensure += light_now_us( ) - t0;
}

void ChunkLoader::light_upload_dirty( const int max_uploads ) {
    const long long t0 = light_now_us( );
    if ( light_dirty_list_near.empty( ) && light_dirty_list_far.empty( ) ) {
        light_dbg_us_upload += light_now_us( ) - t0;
        return;
    }
    struct LightUploadEntry {
        Chunk *chunk;
        uint64_t box;
    };
    static std::vector<LightUploadEntry> entries;
    entries.clear( );
    const long tick_now = light_tick_counter.load( std::memory_order_relaxed );
    static std::vector<Chunk *> eligible;
    {
        // Clear listed AND snapshot the dirty box in the same critical
        // section that detaches the chunk from the list: a mark whose CAS
        // misses this exchange necessarily enters the mutex after us, sees
        // listed==0, and re-adds the chunk — the next frame's exchange picks
        // its texel up. Entries beyond the cap stay in the list untouched.
        // Eligibility = write-quiet debounce + in-progress gate. Quiet:
        // chunks written within LIGHT_UPLOAD_QUIET_TICKS stay listed so the
        // GPU never snapshots a mid-march volume. In-progress: a column with
        // pending seeds or a held lock is still mid-flood — its floor-write
        // to respread-write gap under a deep backlog is far longer than any
        // tick window, so counting the work itself is what suppresses the
        // correct→dark→correct flash. Defers stay capped so a column under
        // a sustained march can't starve its upload forever.
        std::lock_guard<std::mutex> lock( light_work_mutex );
        size_t n = max_uploads;
        // Near uploads first — the visible region must not wait behind the
        // far backlog when the per-frame cap is binding.
        for ( std::vector<Chunk *> *list : { &light_dirty_list_near, &light_dirty_list_far } ) {
            eligible.clear( );
            for ( Chunk *chunk : *list ) {
                // Fresh chunks bypass both gates: the optimistic sky fill
                // is mostly correct and beats a black hole while seeds are
                // still queued — later uploads still wait for the flood.
                const bool fresh = !chunk->light_uploaded_once;
                const int cidx = light_col_lock_index( chunk->chunk_pos.x, chunk->chunk_pos.z );
                const bool busy = light_col_incident[ cidx ].load( std::memory_order_relaxed ) > 0 ||
                                  light_col_locks[ cidx ].load( std::memory_order_relaxed ) != 0;
                const bool quiet = tick_now - chunk->light_write_tick.load( std::memory_order_relaxed ) >= LIGHT_UPLOAD_QUIET_TICKS;
                const int cap = busy ? LIGHT_UPLOAD_MAX_DEFERS_BUSY : LIGHT_UPLOAD_MAX_DEFERS;
                if ( fresh || ( quiet && !busy ) || chunk->light_upload_defers >= cap ) {
                    eligible.push_back( chunk );
                } else {
                    chunk->light_upload_defers++;
                    light_dbg_upload_deferred++;
                }
            }
            const size_t take = eligible.size( ) > n ? n : eligible.size( );
            // Nearest of the eligible backlog first: selection-sort just the
            // `take` prefix (take is bounded by the small per-frame cap) so
            // the upload budget lands on the visible region first.
            for ( size_t i = 0; i < take; i++ ) {
                size_t best = i;
                const glm::ivec3 bd = eligible[ i ]->chunk_pos - this->chunk_center;
                int best_d2 = bd.x * bd.x + bd.z * bd.z;
                for ( size_t j = i + 1; j < eligible.size( ); j++ ) {
                    const glm::ivec3 d = eligible[ j ]->chunk_pos - this->chunk_center;
                    const int d2 = d.x * d.x + d.z * d.z;
                    if ( d2 < best_d2 ) {
                        best_d2 = d2;
                        best = j;
                    }
                }
                std::swap( eligible[ i ], eligible[ best ] );
            }
            entries.reserve( entries.size( ) + take );
            for ( size_t i = 0; i < take; i++ ) {
                Chunk *chunk = eligible[ i ];
                chunk->light_upload_listed.store( 0, std::memory_order_release );
                chunk->light_upload_defers = 0;
                chunk->light_uploaded_once = 1;
                entries.push_back( { chunk, chunk->light_dirty_box.exchange( LIGHT_BOX_EMPTY, std::memory_order_acq_rel ) } );
            }
            // Rebuild minus taken entries (they're the ones now listed==0).
            std::vector<Chunk *> keep;
            keep.reserve( list->size( ) - take );
            for ( Chunk *chunk : *list ) {
                if ( chunk->light_upload_listed.load( std::memory_order_relaxed ) ) {
                    keep.push_back( chunk );
                }
            }
            list->swap( keep );
            n -= take;
            if ( n == 0 ) {
                break;
            }
        }
    }
    int uploads = 0;
    for ( const LightUploadEntry &e : entries ) {
        Chunk &chunk = *e.chunk;
        if ( e.box == LIGHT_BOX_EMPTY || chunk.load_state ) {
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
        // Expand the packed (sky<<4|block) dirty region so the two channels
        // filter independently — see light_expand_cell / light_air_lut. The
        // scratch is tightly packed so the default unpack stride applies.
        const bool mipmaps = light_mipmaps_enabled( );
        // Opacity lookups for B are skipped wherever the answer is already
        // known: null cells[] means the whole volume is air, and a lit
        // cell (packed != 0) provably can hold light — both give B = 255.
        // Only dark cells (solid vs. merely-dark air) need the mask — read
        // from chunk.light_air_mask (bit index = cells[] index space) when
        // it exists, else the cells[]/air_lut fallback. Hoisted per chunk
        // (not per row).
        const bool all_air = mipmaps && chunk.cells == nullptr;
        const unsigned char *air_mask = mipmaps && !all_air ? chunk.light_air_mask : nullptr;
        const unsigned char *air_lut = mipmaps && chunk.cells && air_mask == nullptr ? light_air_lut( ) : nullptr;
        if ( mipmaps && air_mask ) {
            // Mask path: texel order (z, y, x — x fastest) so the light[]
            // rows and the scratch both stream linearly; the mask bit index
            // steps +CHUNK_SIZE_INTERNAL_Z per x (see light_ensure_texture —
            // byte index (z>>3) + (Z_INT/8)*x + (X_INT*Z_INT/8)*y, tested bit z&7).
            const uint32_t *rg_lut = light_rg_lut( );
            uint32_t *out = s_light_upload_scratch;
            for ( int z = 0; z < sz.z; z++ ) {
                const unsigned char *mrow = air_mask + ( ( lo.z + z ) >> 3 );
                const int shift = ( lo.z + z ) & 7;
                for ( int y = 0; y < sz.y; y++ ) {
                    const unsigned char *lrow = chunk.light ? chunk.light + ( ( lo.z + z ) * CHUNK_SIZE_INTERNAL_Y + ( lo.y + y ) ) * CHUNK_SIZE_INTERNAL_X + lo.x : nullptr;
                    const unsigned char *mty = mrow + ( lo.y + y ) * ( CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Z / 8 ) + lo.x * ( CHUNK_SIZE_INTERNAL_Z / 8 );
                    for ( int x = 0; x < sz.x; x++, out++ ) {
                        const unsigned char packed = lrow ? lrow[ x ] : chunk.light_uniform;
                        const uint32_t air = ( static_cast<uint32_t>( mty[ x * ( CHUNK_SIZE_INTERNAL_Z / 8 ) ] ) >> shift ) & 1u;
                        *out = rg_lut[ packed ] | ( air * 255u << 16 ) | ( 255u << 24 );
                    }
                }
            }
        } else {
            for ( int y = 0; y < sz.y; y++ ) {
                if ( mipmaps ) {
                    // Fallback (no mask): z-inner order as in
                    // light_ensure_texture — cells[] reads (+1 index per z
                    // step) stream sequentially while light[] and the
                    // scratch take the strided steps.
                    for ( int x = 0; x < sz.x; x++ ) {
                        unsigned char *out = reinterpret_cast<unsigned char *>( s_light_upload_scratch ) + 4 * ( y * sz.x + x );
                        const unsigned char *lrow = chunk.light ? chunk.light + ( ( lo.z ) * CHUNK_SIZE_INTERNAL_Y + ( lo.y + y ) ) * CHUNK_SIZE_INTERNAL_X + lo.x + x : nullptr;
                        int bidx = ( lo.y + y ) * CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Z + ( lo.x + x ) * CHUNK_SIZE_INTERNAL_Z + lo.z;
                        for ( int z = 0; z < sz.z; z++, out += 4 * sz.y * sz.x, bidx++ ) {
                            const unsigned char packed = lrow ? lrow[ z * CHUNK_SIZE_INTERNAL_Y * CHUNK_SIZE_INTERNAL_X ] : chunk.light_uniform;
                            light_expand_cell( packed, out );
                            out[ 2 ] = all_air || packed != 0 ? 255 : air_lut[ block_cell_id( chunk.cells[ bidx ] ) ];
                            out[ 3 ] = 255;
                        }
                    }
                } else {
                    for ( int z = 0; z < sz.z; z++ ) {
                        unsigned char *out = reinterpret_cast<unsigned char *>( s_light_upload_scratch ) + 2 * ( z * sz.y + y ) * sz.x;
                        if ( chunk.light ) {
                            const unsigned char *row = chunk.light + ( ( lo.z + z ) * CHUNK_SIZE_INTERNAL_Y + ( lo.y + y ) ) * CHUNK_SIZE_INTERNAL_X + lo.x;
                            for ( int x = 0; x < sz.x; x++ ) {
                                light_expand_cell( row[ x ], out + 2 * x );
                            }
                        } else {
                            // Uniform volume: the whole dirty box reads light_uniform.
                            for ( int x = 0; x < sz.x; x++ ) {
                                light_expand_cell( chunk.light_uniform, out + 2 * x );
                            }
                        }
                    }
                }
            }
        }
        glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
        glPixelStorei( GL_UNPACK_IMAGE_HEIGHT, 0 );
        glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
        glTexSubImage3D( GL_TEXTURE_3D, 0, lo.x, lo.y, lo.z, sz.x, sz.y, sz.z, mipmaps ? GL_RGBA : GL_RG, GL_UNSIGNED_BYTE, s_light_upload_scratch );
        glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
        if ( mipmaps ) {
            glGenerateMipmap( GL_TEXTURE_3D );
        }
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
    // Broad daylight plateau, smooth dawn/dusk, down to 0 at midnight. The
    // actual floor lives in the caller: RepGame.cpp max()es this with
    // night_ambient (moonlight) and the shaders floor everything with
    // u_MinAmbient (caves). A baked floor here would beat both.
    return glm::clamp( ( sun + 0.06f ) * 4.0f, 0.0f, 1.0f );
}

// ---------------------------------------------------------------------------
// Dedicated lighting thread
// ---------------------------------------------------------------------------

bool ChunkLoader::light_async_active( ) {
    return light_thread_running != 0;
}

// Route by priority tier: near columns append to the near lane (FIFO —
// preserves submit order), FINALIZEs for never-finalized chunks go to the
// init lane (still showing only the optimistic sky fill — the real
// skylight is worth landing before background far work), everything else
// to the far lane. Rechecks are interactive edits so they jump the near
// lane's own backlog too. chunk_center is a racy render-thread read —
// fine for a priority hint; jobs submitted while far get re-promoted by
// light_reprioritize_jobs on moves.
static void light_submit_job( const glm::ivec3 &center, const LightJob &job_in ) {
    LightJob job = job_in;
    job.submit_us = light_now_us( );
    const glm::ivec3 col = job.type == LIGHT_JOB_FINALIZE ? job.pos : light_chunk_pos_of( job.pos );
    const bool near = light_near_col( center, col );
    if ( job.type == LIGHT_JOB_RECHECK ) {
        light_jobs_near.push_front( job );
    } else if ( near ) {
        light_jobs_near.push_back( job );
    } else if ( job.first_run ) {
        light_jobs_init.push_back( job );
    } else {
        light_jobs_far.push_back( job );
    }
}

// Called when chunk_center moves: promote queued far-lane jobs (and dirty
// uploads) that are now near the player so the visible ring converges first
// even when the player outran the queue. Jobs never demote — stale near work
// is harmless.
void ChunkLoader::light_reprioritize_jobs( ) {
    if ( !light_thread_running ) {
        return;
    }
    std::lock_guard<std::mutex> lock( light_work_mutex );
    const glm::ivec3 center = this->chunk_center;
    for ( std::deque<LightJob> *lane : { &light_jobs_init, &light_jobs_far } ) {
        std::deque<LightJob> old;
        old.swap( *lane );
        for ( LightJob &j : old ) {
            const glm::ivec3 col = j.type == LIGHT_JOB_FINALIZE ? j.pos : light_chunk_pos_of( j.pos );
            std::deque<LightJob> *dst = light_near_col( center, col ) ? &light_jobs_near
                                        : j.first_run                 ? &light_jobs_init
                                                                      : &light_jobs_far;
            dst->push_back( j );
        }
    }
    std::vector<Chunk *> still_dirty;
    still_dirty.swap( light_dirty_list_far );
    for ( Chunk *c : still_dirty ) {
        ( light_near_col( center, c->chunk_pos ) ? light_dirty_list_near : light_dirty_list_far ).push_back( c );
    }
}

// Heal for light_stale chunks (flagged by cap refusals in light_seed_gate):
// the refusal also set light_reseed, so the re-finalize is a full relight —
// light_fill_chunk wipes the volume and re-seeds every emitter, then
// cascade + border sync + interior boundary re-derive it against current
// neighbor state. Any mix of dropped add/remove work converges cleanly.
// Called from the grid-shift slot scan in chunk_loader.cpp so stale far
// chunks heal as they re-enter the near radius.
void ChunkLoader::light_heal_chunk( Chunk &chunk, const glm::ivec3 &center ) {
    if ( chunk.load_state || !light_near_col( center, chunk.chunk_pos ) ) {
        return;
    }
    if ( chunk.light_stale.load( std::memory_order_relaxed ) ) {
        // Don't heal mid-flood: the re-finalize's own propagation seeds
        // would be refused above the cap and the flag would just re-set —
        // churn. Leave the flag set; the next shift retries.
        if ( light_bfs_pending.load( std::memory_order_relaxed ) > LIGHT_BFS_CAP_FLOOD ||
             !chunk.light_stale.exchange( 0, std::memory_order_relaxed ) ) {
            return;
        }
        this->light_submit_finalize( chunk );
        return;
    }
    // Near chunks still catching up on queued seed work: re-finalizing the
    // chunk converges it deterministically (fill+cascade+border under one
    // claim) instead of waiting for its seeds to drain one by one —
    // measured ~2x faster tail convergence than relying on the per-column
    // drain order alone. Idempotent and bounded per shift.
    if ( !chunk.light_finalized.load( std::memory_order_relaxed ) || this->light_heal_budget <= 0 ) {
        return;
    }
    const int cidx = light_col_lock_index( chunk.chunk_pos.x, chunk.chunk_pos.z );
    if ( light_col_incident[ cidx ].load( std::memory_order_relaxed ) <= 0 ) {
        return;
    }
    this->light_heal_budget--;
    this->light_submit_finalize( chunk );
}

// Render-thread idle heal, once per frame from loadChunks: while the queue
// is nearly drained, re-finalize a few flagged chunks per frame. Each heal
// pushes seeds back over the gate, so this self-throttles — leftover far
// light converges in small waves instead of one monolithic plateau, and
// flagged chunks the player never re-approaches still heal eventually.
void ChunkLoader::light_heal_idle( ) {
    if ( light_bfs_pending.load( std::memory_order_relaxed ) > LIGHT_HEAL_FEED ) {
        return;
    }
    int healed = 0;
    for ( int i = 0; i < MAX_LOADED_CHUNKS && healed < LIGHT_HEAL_PER_FRAME; i++ ) {
        Chunk &chunk = this->chunkArray[ i ];
        // Load first: the common case is flag=0, and an unconditional exchange
        // would pay an atomic RMW per slot per frame for nothing.
        if ( !chunk.load_state && chunk.light_stale.load( std::memory_order_relaxed ) &&
             chunk.light_stale.exchange( 0, std::memory_order_relaxed ) ) {
            this->light_submit_finalize( chunk );
            healed++;
        }
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
        light_submit_job( this->chunk_center,
                          LightJob{ LIGHT_JOB_FINALIZE, &chunk, chunk.chunk_pos, 0,
                                    !chunk.light_finalized.load( std::memory_order_relaxed ) } );
    }
    light_work_cv.notify_one( );
}

void ChunkLoader::light_submit_recheck( const glm::ivec3 &block_pos ) {
    Chunk *chunk = this->get_chunk( light_chunk_pos_of( block_pos ) );
    if ( chunk == nullptr || chunk->load_state ) {
        return;
    }
    if ( !light_thread_running ) {
        this->light_recheck_block( block_pos );
        return;
    }
    // Fast path for interactive edits: if this column's footprint is claimable
    // right now (no conflicting claim — light_try_claim enforces that), run
    // the recheck on the calling thread and drain the seeds it produced
    // immediately. A torch's spread is a few hundred cells, so this finishes
    // in well under the 2 ms slice cap, and the dirty marks it leaves get
    // picked up by this frame's light_upload_dirty — the edit's light lands
    // in the SAME frame instead of waiting out the
    // submit → wake → claim → BFS chain (~1-2 frames). If the footprint is
    // busy (load-in, another worker's job), fall back to the async lane —
    // correctness is identical, just slower.
    const glm::ivec3 col = light_chunk_pos_of( block_pos );
    bool claimed = false;
    {
        std::lock_guard<std::mutex> lock( light_work_mutex );
        claimed = light_try_claim( col.x, col.z, true );
        if ( !claimed ) {
            light_submit_job( this->chunk_center, LightJob{ LIGHT_JOB_RECHECK, chunk, block_pos, 0 } );
        }
    }
    if ( !claimed ) {
        light_dbg_rej++;
        light_work_cv.notify_one( );
        return;
    }
    light_dbg_sync++;
    int cols[ 9 ];
    const int ncols = light_claim_cols( col.x, col.z, cols );
    light_cols_lock( cols, ncols );
    this->light_recheck_block( block_pos );
    light_cols_unlock( cols, ncols );
    {
        std::lock_guard<std::mutex> lock( light_work_mutex );
        light_release_claim( col.x, col.z );
    }
    if ( light_bfs_pending.load( ) > 0 ) {
        // Drain our own seeds inline — they mostly land in the recheck's own
        // column slot. If a worker already owns it, it will handle them; pops
        // also serialize per-column against claims, so this is safe either
        // way.
        const int col_slot = light_col_slot( block_pos );
        int expected = 0;
        if ( light_colq_owned[ col_slot ].compare_exchange_strong( expected, 1, std::memory_order_acquire, std::memory_order_relaxed ) ) {
            light_drain_count++;
            this->light_process_queue( 2000, col_slot );
            light_drain_count--;
            light_release_column( col_slot );
            {
                std::lock_guard<std::mutex> lock( light_work_mutex );
                // Clear any priority window too: it exists to get a drainer
                // running, and our drain just handled that — the wait
                // predicate re-opens it if a real backlog remains.
                light_bfs_priority = 0;
                light_bfs_starve = 0;
            }
        }
    }
    light_work_cv.notify_all( );
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
        int claim_cols[ 9 ];
        int n_claim_cols = 0;
        bool have_job = false;
        bool claimed = false;
        bool want_drain = false;
        {
            std::unique_lock<std::mutex> lock( light_work_mutex );
            light_work_cv.wait( lock, [ this ] {
                if ( light_thread_stop_flag ) {
                    return true;
                }
                const long long pend = light_bfs_pending.load( );
                if ( pend > LIGHT_BFS_PRI_HI ) {
                    light_bfs_priority = 1;
                    light_bfs_starve = 0;
                } else if ( pend > LIGHT_BFS_STARVE_MIN &&
                            light_tick_counter.load( std::memory_order_relaxed ) -
                                        light_bfs_backlog_tick.load( std::memory_order_relaxed ) >
                                    LIGHT_BFS_STARVE_TICKS ) {
                    light_bfs_priority = 1;
                    light_bfs_starve = 1;
                }
                // Wake only for *runnable* work — a queue full of
                // claim-blocked jobs must not spin this loop. Lanes in
                // priority order: near, then init, then far.
                for ( const LightJob &j : light_jobs_near ) {
                    const glm::ivec3 col = j.type == LIGHT_JOB_FINALIZE ? j.pos : light_chunk_pos_of( j.pos );
                    if ( light_claim_free( col.x, col.z, j.type != LIGHT_JOB_FINALIZE ) ) {
                        return true;
                    }
                }
                for ( const LightJob &j : light_jobs_init ) {
                    const glm::ivec3 col = j.type == LIGHT_JOB_FINALIZE ? j.pos : light_chunk_pos_of( j.pos );
                    if ( light_claim_free( col.x, col.z, j.type != LIGHT_JOB_FINALIZE ) ) {
                        return true;
                    }
                }
                for ( const LightJob &j : light_jobs_far ) {
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
                // Runnable seed work = a nonempty column slot nobody owns
                // yet. 289 relaxed loads in the wait predicate is fine —
                // it only runs when a worker actually wakes.
                for ( int s = 0; s < LIGHT_COL_QUEUE_COUNT; s++ ) {
                    if ( light_colq_pending[ s ].load( std::memory_order_relaxed ) > 0 &&
                         light_colq_owned[ s ].load( std::memory_order_relaxed ) == 0 ) {
                        return true;
                    }
                }
                return false;
            } );
            if ( light_thread_stop_flag ) {
                return;
            }
            for ( std::deque<LightJob> *jobs : { &light_jobs_near, &light_jobs_init, &light_jobs_far } ) {
                // Nearest claimable job to the player wins. claim_free is a
                // few int compares since the centers refactor, so the
                // whole-lane distance pass stays cheap even at load-in
                // depths — the claim itself happens once, on the argmin.
                auto best = jobs->end( );
                int best_d2 = std::numeric_limits<int>::max( );
                for ( auto it = jobs->begin( ); it != jobs->end( ); ++it ) {
                    const glm::ivec3 col = it->type == LIGHT_JOB_FINALIZE ? it->pos : light_chunk_pos_of( it->pos );
                    if ( !light_claim_free( col.x, col.z, it->type != LIGHT_JOB_FINALIZE ) ) {
                        continue;
                    }
                    const glm::ivec3 d = col - this->chunk_center;
                    const int d2 = d.x * d.x + d.z * d.z;
                    if ( d2 < best_d2 ) {
                        best_d2 = d2;
                        best = it;
                    }
                }
                if ( best != jobs->end( ) ) {
                    const glm::ivec3 col = best->type == LIGHT_JOB_FINALIZE ? best->pos : light_chunk_pos_of( best->pos );
                    if ( light_try_claim( col.x, col.z, best->type != LIGHT_JOB_FINALIZE ) ) {
                        job = *best;
                        jobs->erase( best );
                        claim_col = col;
                        have_job = claimed = true;
                        break;
                    }
                }
            }
            if ( !have_job ) {
                // Same nearest-first policy for pending-diff chunks.
                auto best = light_pending_list.end( );
                int best_d2 = std::numeric_limits<int>::max( );
                for ( auto it = light_pending_list.begin( ); it != light_pending_list.end( ); ) {
                    Chunk *pc = *it;
                    if ( pc->load_state ) {
                        // Loading chunks drain pending in light_finalize_chunk.
                        pc->light_pending_listed = 0;
                        it = light_pending_list.erase( it );
                        continue;
                    }
                    if ( light_claim_free( pc->chunk_pos.x, pc->chunk_pos.z, true ) ) {
                        const glm::ivec3 d = pc->chunk_pos - this->chunk_center;
                        const int d2 = d.x * d.x + d.z * d.z;
                        if ( d2 < best_d2 ) {
                            best_d2 = d2;
                            best = it;
                        }
                    }
                    ++it;
                }
                if ( best != light_pending_list.end( ) ) {
                    Chunk *pc = *best;
                    const glm::ivec3 col = pc->chunk_pos;
                    if ( light_try_claim( col.x, col.z, true ) ) {
                        light_pending_list.erase( best );
                        // Clear listed under the lock so a producer writing
                        // pending fields now re-enqueues a fresh entry.
                        pc->light_pending_listed = 0;
                        pending_chunk = pc;
                        pending_expected = col;
                        claim_col = col;
                        claimed = true;
                    }
                }
            }
            if ( !claimed && light_bfs_pending.load( ) > 0 ) {
                want_drain = true;
            }
            if ( claimed ) {
                n_claim_cols = light_claim_cols( claim_col.x, claim_col.z, claim_cols );
            }
        }
        // Column locks serialize us against BFS pops and other jobs touching
        // our 3x3 footprint. Acquired AFTER the work mutex is released: a pop
        // holding a column can block on that mutex (dirty-mark enqueue), so
        // spinning inside it could deadlock. Hold times are bounded by a
        // single step/job, so the spin is short.
        if ( claimed ) {
            light_cols_lock( claim_cols, n_claim_cols );
        }
        if ( have_job ) {
            const long long lag = light_now_us( ) - job.submit_us;
            long long cur_lag = light_dbg_max_lag_us.load( std::memory_order_relaxed );
            while ( lag > cur_lag &&
                    !light_dbg_max_lag_us.compare_exchange_weak( cur_lag, lag, std::memory_order_relaxed ) ) {
            }
            const glm::ivec3 jcol = job.type == LIGHT_JOB_FINALIZE ? job.pos : light_chunk_pos_of( job.pos );
            const glm::ivec3 jd = jcol - this->chunk_center;
            if ( jd.x * jd.x + jd.z * jd.z <= LIGHT_NEAR_COL_DIST * LIGHT_NEAR_COL_DIST ) {
                long long cn = light_dbg_max_lag_near_us.load( std::memory_order_relaxed );
                while ( lag > cn &&
                        !light_dbg_max_lag_near_us.compare_exchange_weak( cn, lag, std::memory_order_relaxed ) ) {
                }
            }
            if ( lag > 1000000 ) {
                light_dbg_late_jobs++;
            }
            if ( job.type == LIGHT_JOB_FINALIZE ) {
                // Claim was staked on the pos captured at submit — the slot
                // must still hold that chunk or our writes aren't under the
                // claim. A recycled slot's fresh dequeue re-submits, so
                // skipping loses nothing.
                if ( job.chunk->chunk_pos == job.pos && !job.chunk->load_state ) {
                    this->light_finalize_chunk( *job.chunk );
                }
            } else if ( this->get_chunk( light_chunk_pos_of( job.pos ) ) == job.chunk ) {
                this->light_recheck_block( job.pos );
            }
        } else if ( pending_chunk != nullptr ) {
            if ( pending_chunk->chunk_pos.x == pending_expected.x && pending_chunk->chunk_pos.z == pending_expected.z &&
                 !pending_chunk->load_state ) {
                this->light_process_pending_chunk( *pending_chunk );
            }
        }
        if ( claimed ) {
            light_cols_unlock( claim_cols, n_claim_cols );
            {
                std::lock_guard<std::mutex> lock( light_work_mutex );
                light_release_claim( claim_col.x, claim_col.z );
            }
            light_work_cv.notify_all( );
        } else if ( want_drain ) {
            // Bounded slices on one claimed column slot at a time; keep
            // draining while no jobs/pending are waiting — other workers
            // drain other columns concurrently. light_claim_column picks
            // the nearest pending slot each slice, so the region around the
            // player converges first and reprioritization on movement is
            // free. In a priority window jobs are claim-blocked anyway:
            // drain to the low watermark or the time cap, whichever first.
            // A starve-triggered window drains to empty (bounded by a
            // shorter cap) — the backlog it interrupts is old, not small,
            // so the cap bounds the hitch.
            light_drain_count++;
            const long long pri_start = light_now_us( );
            for ( ;; ) {
                const int col_slot = light_claim_column( this->chunk_center );
                if ( col_slot < 0 ) {
                    break;
                }
                this->light_process_queue( 2000, col_slot );
                light_release_column( col_slot );
                bool stop_drain = false;
                {
                    std::lock_guard<std::mutex> lock( light_work_mutex );
                    if ( light_thread_stop_flag ) {
                        stop_drain = true;
                    } else if ( light_bfs_priority ) {
                        const long long floor = light_bfs_starve ? 0 : LIGHT_BFS_PRI_LO;
                        // A starve window's slice scales with the backlog it
                        // interrupted: ~25ms at STARVE_MIN growing ~+25ms per
                        // PRI_LO of backlog, to a 150ms ceiling. A million-
                        // seed queue gets real progress per window instead of
                        // a token slice it outgrows before the next one opens.
                        const long long cap =
                            light_bfs_starve ? std::min<long long>( LIGHT_BFS_STARVE_MAX_US * ( 1 + light_bfs_pending.load( ) / LIGHT_BFS_PRI_LO ), (long long)LIGHT_BFS_STARVE_HARD_US )
                                             : LIGHT_BFS_PRI_MAX_US;
                        if ( light_bfs_pending.load( ) <= floor || light_now_us( ) - pri_start > cap ) {
                            light_bfs_priority = 0;
                            light_bfs_starve = 0;
                            // Re-arm the backlog-age timer so claimed work gets
                            // a turn before the next starve window can open.
                            light_bfs_backlog_tick.store( light_tick_counter.load( std::memory_order_relaxed ),
                                                          std::memory_order_relaxed );
                            stop_drain = true;
                        } else {
                            // Rechecks bypass the priority window at claim
                            // time — if one is queued, yield so it runs now
                            // instead of waiting out the whole drain (~25ms).
                            // The window stays set: drainers resume next pass.
                            for ( const LightJob &j : light_jobs_near ) {
                                if ( j.type == LIGHT_JOB_RECHECK ) {
                                    stop_drain = true;
                                    break;
                                }
                            }
                        }
                    } else if ( light_bfs_pending.load( ) <= 0 ||
                                ( !light_jobs_near.empty( ) || !light_jobs_init.empty( ) || !light_jobs_far.empty( ) ) ||
                                !light_pending_list.empty( ) ) {
                        stop_drain = true;
                    }
                }
                if ( stop_drain ) {
                    break;
                }
            }
            light_drain_count--;
            light_work_cv.notify_all( );
        }
    }
}

// Game-tick feed for light_bfs_backlog_tick — RepGame::tick calls this once
// per tick so the starve-window predicate can measure backlog age with an
// atomic load instead of a clock read.
void ChunkLoader::light_note_tick( const long tick ) {
    light_tick_counter.store( tick, std::memory_order_relaxed );
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
    for ( int i = 0; i < light_worker_count( ); i++ ) {
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
            jobs.swap( light_jobs_near );
            for ( LightJob &j : light_jobs_init ) {
                jobs.push_back( j );
            }
            light_jobs_init.clear( );
            for ( LightJob &j : light_jobs_far ) {
                jobs.push_back( j );
            }
            light_jobs_far.clear( );
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
        light_jobs_near.clear( );
        light_jobs_init.clear( );
        light_jobs_far.clear( );
        light_pending_list.clear( );
        light_dirty_list_near.clear( );
        light_dirty_list_far.clear( );
        light_claim_count = 0;
        light_drain_count = 0;
        for ( int i = 0; i < LIGHT_COL_LOCK_COUNT; i++ ) {
            light_col_locks[ i ].store( 0, std::memory_order_relaxed );
            light_col_incident[ i ].store( 0, std::memory_order_relaxed );
        }
        light_bfs_priority = 0;
        light_bfs_starve = 0;
        light_bfs_backlog_tick = 0;
        light_dbg_max_lag_us = 0;
        light_dbg_late_jobs = 0;
        light_dbg_max_lag_near_us = 0;
        for ( int s = 0; s < LIGHT_COL_QUEUE_COUNT; s++ ) {
            for ( int ch = 0; ch < 2; ch++ ) {
                for ( int level = 0; level <= LIGHT_MAX_LEVEL; level++ ) {
                    this->light_add_queue[ s ][ ch ][ level ].clear( );
                }
            }
            this->light_remove_queue[ s ].clear( );
            light_colq_pending[ s ] = 0;
            light_colq_owned[ s ] = 0;
            light_colq_locks[ s ] = 0;
        }
        light_bfs_pending = 0;
        light_deferred_marks.clear( );
        light_defer_marks = 0;
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
        if ( this->chunkArray[ i ].load_state == CHUNK_LOADING ) {
            out->chunks_loading++;
        }
        if ( this->chunkArray[ i ].light_stale.load( std::memory_order_relaxed ) ) {
            out->light_stale_count++;
        }
        if ( this->chunkArray[ i ].occl_fails >= OCCL_HIDE_FRAMES ) {
            out->chunks_occluded++;
        }
    }
    {
        std::lock_guard<std::mutex> lock( light_work_mutex );
        out->light_jobs = static_cast<int>( light_jobs_near.size( ) + light_jobs_init.size( ) + light_jobs_far.size( ) );
        out->light_pending = static_cast<int>( light_pending_list.size( ) );
        out->light_claims = light_claim_count;
        out->light_dirty = static_cast<int>( light_dirty_list_near.size( ) + light_dirty_list_far.size( ) );
    }
    out->light_bfs_active = light_drain_count.load( std::memory_order_relaxed );
    out->light_bfs_priority = light_bfs_priority;
    out->light_bfs_starve = light_bfs_starve;
    out->light_seeds = light_bfs_pending.load( );
    out->light_dropped = light_dbg_dropped.load( );
    // Running maxima for the current probe interval — the dbg_stats print
    // owns the reset, so these are reads only.
    out->light_jlag_us = light_dbg_max_lag_us.load( );
    out->light_njlag_us = light_dbg_max_lag_near_us.load( );
}
