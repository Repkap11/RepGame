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
// Lighting worker (docs/lighting-thread-plan.md): job lanes, column claims,
// the lighting thread loop and lifecycle, heal passes and debug stats.
// Shared helpers/state live in common/light_internal.hpp.
// ---------------------------------------------------------------------------

static size_t light_jobs_depth( );

void ChunkLoader::light_dbg_stats( char *buf, size_t n ) {
    size_t jobs_now = light_jobs_depth( );
    int terrain_work = 0, terrain_results = 0;
    this->terrain_loading_thread.queue_sizes( &terrain_work, &terrain_results );
    snprintf( buf, n, "loop=%lld aj=%lld jn=%lld pq=%lld/%lld push=%lld pop=%lld fin=%lld casc=%lld bord=%lld rech=%lld step=%lld rem=%lld rq=%lld drop=%lld pd=%lld cap=%lld def=%lld jobs=%zu jlag=%lld jlate=%lld njlag=%lld syn=%lld rej=%lld tw=%d tr=%d"
                      " | us: fin=%lld(lfin=%lld[fill=%lld casc=%lld bscan=%lld border=%lld]) remesh=%lld drain=%lld bfs=%lld upl=%lld(n=%lld) bind=%lld ensure=%lld(n=%lld) fill=%lld(n=%lld) pool=%lld(n=%lld)",
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
              light_dbg_us_ensure, light_dbg_n_ensure,
              light_dbg_us_fill, light_dbg_n_fill,
              light_dbg_us_framepool, light_dbg_n_framepool );
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
                light_dbg_us_fin_casc = light_dbg_us_fin_bscan = light_dbg_us_fin_border =
                    light_dbg_us_fill = light_dbg_n_fill =
                        light_dbg_us_framepool = light_dbg_n_framepool = 0;
}

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
enum LightJobType { LIGHT_JOB_FINALIZE, LIGHT_JOB_RECHECK };
struct LightJob {
    LightJobType type;
    Chunk *chunk;
    glm::ivec3 pos;       // FINALIZE: expected chunk_pos; RECHECK: block pos
    long long submit_us;
    int first_run = 0;    // FINALIZE only: chunk had never been finalized at submit
};

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

// would make a thread re-acquire its own flag.

// REPGAME_LIGHT_CLAIM_RADIUS (1..4, default 1): Chebyshev center distance
// that counts as a conflict. 4 reproduces the old 5x5-key-overlap semantics
// exactly; 2 is the documented column model (no shared touched chunk);
// 1 relies on the disjoint-cell argument above. Resolved once per process.
static int light_claim_radius( ) {
    static const int r = []( ) {
        const int v = env_int( "REPGAME_LIGHT_CLAIM_RADIUS", 1 );
        return v < 1 ? 1 : ( v > 4 ? 4 : v );
    }( );
    return r;
}

// REPGAME_LIGHT_WORKERS (1..8, default 4): pool size. Claim capacity is
// sized for the max, so the env only changes how many threads spawn.
static int light_worker_count( ) {
    static const int n = []( ) {
        const int v = env_int( "REPGAME_LIGHT_WORKERS", 4 );
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
    if ( chunk.light.stale_get( ) ) {
        // Don't heal mid-flood: the re-finalize's own propagation seeds
        // would be refused above the cap and the flag would just re-set —
        // churn. Leave the flag set; the next shift retries. Share the
        // per-shift budget with the incident heals: a burst of flagged
        // chunks re-entering at once (parked slots reactivating) must not
        // all inline-finalize in one rescan — the flag survives until a
        // finalize actually lands.
        if ( light_bfs_pending.load( std::memory_order_relaxed ) > LIGHT_BFS_CAP_FLOOD ||
             this->light_heal_budget <= 0 ||
             !chunk.light.stale_take( ) ) {
            return;
        }
        this->light_heal_budget--;
        this->light_submit_finalize( chunk );
        return;
    }
    // Near chunks still catching up on queued seed work: re-finalizing the
    // chunk converges it deterministically (fill+cascade+border under one
    // claim) instead of waiting for its seeds to drain one by one —
    // measured ~2x faster tail convergence than relying on the per-column
    // drain order alone. Idempotent and bounded per shift.
    if ( !chunk.light.finalized.load( std::memory_order_relaxed ) || this->light_heal_budget <= 0 ) {
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
// The stale flags live in light_stale_bits (64 slots per word): the scan
// skips whole empty words instead of probing every Chunk record. A set bit
// on a loading chunk stays set, same as before.
void ChunkLoader::light_heal_idle( ) {
    if ( light_bfs_pending.load( std::memory_order_relaxed ) > LIGHT_HEAL_FEED ) {
        return;
    }
    int healed = 0;
    for ( int word_idx = 0; word_idx < LIGHT_STALE_WORDS && healed < LIGHT_HEAL_PER_FRAME; word_idx++ ) {
        uint64_t bits = this->light_stale_bits[ word_idx ].load( std::memory_order_relaxed );
        while ( bits && healed < LIGHT_HEAL_PER_FRAME ) {
            const int bit = __builtin_ctzll( bits );
            bits &= bits - 1;
            const int i = ( word_idx << 6 ) + bit;
            if ( i >= MAX_LOADED_CHUNKS ) {
                break;
            }
            Chunk &chunk = this->chunkArray[ i ];
            if ( !chunk.load_state && chunk.light.stale_take( ) ) {
                this->light_submit_finalize( chunk );
                healed++;
            }
        }
    }
}

// Fixed-point check over every interior cell of each settled chunk: a
// converged field has every non-opaque cell equal to the best offer from
// its step neighbors (or its own emission). Sources across a chunk face
// read the owner's canonical value and gates from the owner's cells[].
// Under-lit = some neighbor offers more than stored; over-lit = stored
// exceeds every offer (stale light nothing supports) — skipped where a
// source chunk is missing, since the offer is then only a lower bound.
// Also counts halo mirrors that disagree with their canonical owner.
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
                                    !chunk.light.finalized.load( std::memory_order_relaxed ) } );
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
        Chunk *stage_chunk = nullptr;
        glm::ivec3 claim_col( 0 );
        glm::ivec3 pending_expected( 0 );
        int claim_cols[ 9 ];
        int n_claim_cols = 0;
        bool have_job = false;
        bool claimed = false;
        bool want_drain = false;
        {
            std::unique_lock<std::mutex> lock( light_work_mutex );
            light_work_cv.wait( lock, [] {
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
                if ( !light_stage_list.empty( ) ) {
                    return true;
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
            // Stage work first: a staged fill only needs its GL upload slot
            // (bounded by the per-frame upload cap), so it can't starve jobs
            // — but leaving it behind job/pending selection lets a sustained
            // finalize backlog starve it instead, wedging every pool buffer.
            if ( !light_stage_list.empty( ) ) {
                stage_chunk = light_stage_list.front( );
                light_stage_list.pop_front( );
            }
            if ( stage_chunk == nullptr ) {
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
                // Same nearest-first policy for pending-diff chunks. Index
                // scan: stale entries drop via O(1) swap-with-back instead of
                // vector::erase (O(N) memmove per stale entry, O(N²) per pass
                // on a long list — list order is irrelevant since best is
                // picked across the whole scan).
                size_t best_idx = std::numeric_limits<size_t>::max( );
                int best_d2 = std::numeric_limits<int>::max( );
                for ( size_t i = 0; i < light_pending_list.size( ); ) {
                    Chunk *pc = light_pending_list[ i ];
                    if ( pc->load_state ) {
                        // Loading chunks drain pending in light_finalize_chunk.
                        pc->light.pending_listed = 0;
                        light_pending_list[ i ] = light_pending_list.back( );
                        light_pending_list.pop_back( );
                        continue;
                    }
                    if ( light_claim_free( pc->chunk_pos.x, pc->chunk_pos.z, true ) ) {
                        const glm::ivec3 d = pc->chunk_pos - this->chunk_center;
                        const int d2 = d.x * d.x + d.z * d.z;
                        if ( d2 < best_d2 ) {
                            best_d2 = d2;
                            best_idx = i;
                        }
                    }
                    i++;
                }
                if ( best_idx != std::numeric_limits<size_t>::max( ) ) {
                    Chunk *pc = light_pending_list[ best_idx ];
                    const glm::ivec3 col = pc->chunk_pos;
                    if ( light_try_claim( col.x, col.z, true ) ) {
                        light_pending_list[ best_idx ] = light_pending_list.back( );
                        light_pending_list.pop_back( );
                        // Clear listed under the lock so a producer writing
                        // pending fields now re-enqueues a fresh entry.
                        pc->light.pending_listed = 0;
                        pending_chunk = pc;
                        pending_expected = col;
                        claim_col = col;
                        claimed = true;
                    }
                }
            }
            }
            if ( !claimed && stage_chunk == nullptr && light_bfs_pending.load( ) > 0 ) {
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
                // claim. A recycled slot's fresh dequeue re-submits, and a
                // parked chunk reloads and re-finalizes on re-entry — so
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
        } else if ( stage_chunk != nullptr ) {
            // No column claim: staging only reads the field (the same
            // snapshot-torn reads the mark paths make) and writes a private
            // buffer.
            this->light_stage_chunk( *stage_chunk );
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
        light_stage_list.clear( );
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
    for ( int w = 0; w < LIGHT_STALE_WORDS; w++ ) {
        out->light_stale_count += __builtin_popcountll( this->light_stale_bits[ w ].load( std::memory_order_relaxed ) );
    }
    // Tail bits past MAX_LOADED_CHUNKS can't be set (no slot binds them),
    // so the raw popcount is exact.
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        if ( this->chunkArray[ i ].load_state == CHUNK_LOADING ) {
            out->chunks_loading++;
        }
    }
    // Count only chunks actually skipped this frame: drawable AND in the
    // frustum. Off-frustum chunks keep stale verdicts (reset on re-entry),
    // which would inflate the count with chunks nobody is hiding.
    for ( int i = 0; i < this->num_drawable; i++ ) {
        const Chunk &chunk = *this->drawable_chunks[ i ];
        if ( !chunk.cached_cull_normal && this->occl_hides( chunk ) ) {
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
    out->async_submitted = this->async_jobs_submitted;
    out->async_published = this->async_jobs_published;
    out->async_discarded = this->async_jobs_discarded;
    out->async_promoted = this->async_jobs_promoted;
    out->async_in_flight = static_cast<int>( this->async_in_flight_seqs.size( ) );
}
