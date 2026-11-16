#pragma once

// Shared internals for the lighting implementation, split across
// src/common/light.cpp (cell access, seed queues, BFS propagation), the
// chunk-side pipeline, GPU upload, and the lighting worker. Everything in
// this header was file-local to light.cpp before the split; it is private
// to those translation units — do not include it elsewhere.
//
// Hot-path helpers keep the same shape they had as file-statics: inline
// functions and `inline` (single-instance, shared) tables and counters.
// `inline` variables give every TU the same instance a file-static had,
// with identical codegen.
//
// Member functions (ChunkLoader::light_*) are declared in
// common/chunk_loader.hpp and defined across the split .cpp files as usual;
// the two bodies below are `inline` members that must be visible in every
// TU using them.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

#if defined( _MSC_VER ) && !defined( __clang__ )
#include <intrin.h>
#endif

#include "common/light.hpp"
#include "common/chunk_loader.hpp"
#include "common/block_definitions.hpp"

// Defined with the upload-smoothing machinery; used by the dirty-mark walk
// to skip the smoothing-only neighbour marks when off.
bool light_smooth_enabled( );
bool light_smooth_in_radius( const glm::ivec3 &center, const glm::ivec3 &chunk_pos );

inline void light_cpu_relax( ) {
#if defined( __x86_64__ ) || defined( __i386__ )
    __builtin_ia32_pause( );
#elif defined( __aarch64__ ) || defined( __arm__ )
    __asm__ __volatile__( "yield" );
#endif
}
constexpr glm::ivec3 light_dirs[ 6 ] = {
    glm::ivec3( 1, 0, 0 ),  // +x
    glm::ivec3( -1, 0, 0 ), // -x
    glm::ivec3( 0, 1, 0 ),  // +y (up)
    glm::ivec3( 0, -1, 0 ), // -y (down)
    glm::ivec3( 0, 0, 1 ),  // +z
    glm::ivec3( 0, 0, -1 ), // -z
};
constexpr int LIGHT_DIR_DOWN = 3;

// Chamfer propagation directions: the 26-cell neighborhood in one table.
// Faces come first in light_dirs order so LIGHT_DIR_DOWN stays index 3.
// A diagonal step additionally needs an open shared-face route (no corner
// cutting): gate[] lists the face offsets that can form one — a step is
// allowed when some gate cell admits light and neither sub-hop is sealed by
// a light floor (a floor cell never spreads light downward, so a sub-hop
// with dy<0 is sealed when its SOURCE cell blocks_light_down).
struct LightStepDir {
    glm::ivec3 d;
    signed char cost;
    signed char ngates;
    glm::ivec3 gate[ 3 ];
};
// Faces (6, light_dirs order) then edge diagonals (12). Corner diagonals
// are not steps: corner-to-corner light arrives via edge + face at cost
// 41 (17+24) instead of the old 29 corner step — they were dropped for
// costing propagation work while being visually indistinguishable.
inline const std::array<LightStepDir, 18> light_step_dirs = []( ) {
    std::array<LightStepDir, 18> t{};
    int n = 0;
    for ( int i = 0; i < 6; i++ ) {
        t[ n++ ] = LightStepDir{ light_dirs[ i ], LIGHT_STEP_FACE, 0, {} };
    }
    for ( int dx = -1; dx <= 1; dx++ ) {
        for ( int dy = -1; dy <= 1; dy++ ) {
            for ( int dz = -1; dz <= 1; dz++ ) {
                if ( ( dx != 0 ) + ( dy != 0 ) + ( dz != 0 ) != 2 ) {
                    continue;
                }
                LightStepDir &e = t[ n++ ];
                e.d = glm::ivec3( dx, dy, dz );
                e.cost = LIGHT_STEP_EDGE;
                int g = 0;
                if ( dx ) {
                    e.gate[ g++ ] = glm::ivec3( dx, 0, 0 );
                }
                if ( dy ) {
                    e.gate[ g++ ] = glm::ivec3( 0, dy, 0 );
                }
                if ( dz ) {
                    e.gate[ g++ ] = glm::ivec3( 0, 0, dz );
                }
                e.ngates = static_cast<signed char>( g );
            }
        }
    }
    return t;
}( );

constexpr int LIGHT_STEP_COUNT = 18;

// Per-position direction masks for the boundary scans below. Which step
// dirs can pass a loop's geometric test depends only on where the source
// cell sits in the local volume, so iterating just those dirs — in the
// same ascending table order — is output-identical to scanning all 18
// and skipping: a rejected dir never reaches light_add_seed, and the
// seed written on a hit doesn't depend on which dir triggered it.
//
// Axis class of a local coordinate c (boundary scans see -1..32):
// 0: c<0  1: c==0  2: interior  3: c==last  4: c>last. A cell's class
// triple indexes LIGHT_STEP_MASKS as cx*25+cy*5+cz.
static_assert( CHUNK_SIZE_X == CHUNK_SIZE_Y && CHUNK_SIZE_Y == CHUNK_SIZE_Z,
               "light_step_masks assumes a cubic interior" );

inline int light_axis_class( const int c ) {
    return c < 0 ? 0 : c == 0 ? 1 : c <= CHUNK_SIZE_X - 2 ? 2 : c == CHUNK_SIZE_X - 1 ? 3 : 4;
}
inline int light_cell_class( const int x, const int y, const int z ) {
    return light_axis_class( x ) * 25 + light_axis_class( y ) * 5 + light_axis_class( z );
}

struct LightStepMasks {
    uint32_t in;     // target c+d stays inside the [0,CHUNK_SIZE) interior
    uint32_t out_xz; // target leaves through an x or z face
    uint32_t out_y;  // target leaves through a y face
    uint32_t out;    // target leaves the interior on any axis
};

inline const std::array<LightStepMasks, 125> light_step_masks = []( ) {
    std::array<LightStepMasks, 125> t{};
    for ( int cx = 0; cx < 5; cx++ ) {
        for ( int cy = 0; cy < 5; cy++ ) {
            for ( int cz = 0; cz < 5; cz++ ) {
                // Any coordinate in the class gives the same masks; use a
                // representative per class.
                const glm::ivec3 c( cx == 0 ? -1 : cx == 1 ? 0 : cx == 2 ? 1 : cx == 3 ? CHUNK_SIZE_X - 1 : CHUNK_SIZE_X,
                                    cy == 0 ? -1 : cy == 1 ? 0 : cy == 2 ? 1 : cy == 3 ? CHUNK_SIZE_Y - 1 : CHUNK_SIZE_Y,
                                    cz == 0 ? -1 : cz == 1 ? 0 : cz == 2 ? 1 : cz == 3 ? CHUNK_SIZE_Z - 1 : CHUNK_SIZE_Z );
                LightStepMasks m{};
                for ( int d = 0; d < LIGHT_STEP_COUNT; d++ ) {
                    const glm::ivec3 n = c + light_step_dirs[ d ].d;
                    const glm::ivec3 o( n.x < 0 ? -1 : n.x >= CHUNK_SIZE_X ? 1 : 0,
                                        n.y < 0 ? -1 : n.y >= CHUNK_SIZE_Y ? 1 : 0,
                                        n.z < 0 ? -1 : n.z >= CHUNK_SIZE_Z ? 1 : 0 );
                    const uint32_t bit = 1u << d;
                    if ( o == glm::ivec3( 0 ) ) {
                        m.in |= bit;
                    } else {
                        m.out |= bit;
                        if ( o.x != 0 || o.z != 0 ) {
                            m.out_xz |= bit;
                        }
                        if ( o.y != 0 ) {
                            m.out_y |= bit;
                        }
                    }
                }
                t[ cx * 25 + cy * 5 + cz ] = m;
            }
        }
    }
    return t;
}( );

inline const uint32_t light_step_down_mask = []( ) {
    uint32_t m = 0;
    for ( int d = 0; d < LIGHT_STEP_COUNT; d++ ) {
        if ( light_step_dirs[ d ].d.y < 0 ) {
            m |= 1u << d;
        }
    }
    return m;
}( );

inline int light_ctz32( const uint32_t v ) {
#if defined( _MSC_VER ) && !defined( __clang__ )
    unsigned long i;
    _BitScanForward( &i, v );
    return static_cast<int>( i );
#else
    return __builtin_ctz( v );
#endif
}

// BlockID of the cell at internal coord `local` in a cells[] volume (halo
// cells carry real neighbor data, so a ±1 neighborhood of an interior
// source is always in-volume). Null volume -> AIR. `cells` is passed
// explicitly: it's a private Chunk field only members/friends may name.
// Defined after light_opaque_id below; declared for the step table users.
inline BlockID light_cell_id_at( const BlockCell *cells, const glm::ivec3 &local );
inline bool light_diag_open( const BlockCell *cells, const glm::ivec3 &local, const LightStepDir &sd, const bool src_floor );

inline long long light_now_us( ) {
    return std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now( ).time_since_epoch( ) ).count( );
}

inline glm::ivec3 light_chunk_pos_of( const glm::ivec3 &block_pos ) {
    return glm::ivec3( glm::floor( glm::vec3( block_pos ) / CHUNK_SIZE_F ) );
}
inline bool light_opaque_id( BlockID id ) {
    if ( id == LAST_BLOCK_ID ) {
        return true;
    }
    return block_definition_get_definition( id )->casts_shadow;
}

// Vertical skylight fill test: a column is "blocked" by opaque cells AND by
// absorbing ones AND by light-floor cells (blocks_light_down: slabs, path —
// light-holding, but the direct descent stops at them so they shade like a
// roof; fill_from = y+1 leaves the floor cell itself for the BFS to light).
// The BFS can still permeate absorbing cells (it charges their extra cost
// per entry), but the direct 15-strength descent only runs through cells
// that take nothing.
inline bool light_column_blocked_id( BlockID id ) {
    if ( id == LAST_BLOCK_ID ) {
        return true;
    }
    const Block *def = block_definition_get_definition( id );
    return def->casts_shadow || def->light_absorb > 0 || def->blocks_light_down;
}

// What a cell holding `v` offers neighbor cell `n_id` in step direction `d`
// (index into light_step_dirs). Entering costs the step's chamfer cost plus
// the target's light_absorb (absorb counts display levels, so it scales by
// LIGHT_RAW_PER_LEVEL); the straight-down full-strength skylight privilege
// only applies through non-absorbing cells.
inline int light_offered( const int v, const int channel, const int d, const BlockID n_id ) {
    const int absorb = block_definition_get_definition( n_id )->light_absorb;
    if ( channel == LIGHT_CHANNEL_SKY && d == LIGHT_DIR_DOWN && v == LIGHT_RAW_MAX && absorb == 0 ) {
        return LIGHT_RAW_MAX;
    }
    return v - light_step_dirs[ d ].cost - absorb * LIGHT_RAW_PER_LEVEL;
}

inline BlockID light_cell_id_at( const BlockCell *cells, const glm::ivec3 &local ) {
    return cells ? block_cell_id( cells[ Chunk::get_index_from_coords( local ) ] ) : AIR;
}
// The powered bit of the cell at `local` in a cells[] volume (see
// light_cell_id_at). Null volume -> false.
inline bool light_cell_powered_at( const BlockCell *cells, const glm::ivec3 &local ) {
    return cells && block_cell_powered( cells[ Chunk::get_index_from_coords( local ) ] );
}
// A cell's block-light emission floor in display levels: the def's
// emits_light, or the lit variant's while the cell is powered — a powered
// mechanism's "on" state is the cell's power bit on the same id, not a
// block swap (block_adjust_coord_based_on_state does the same swap for the
// redstone lamp's texture).
inline int light_emit_level( const Block *def, const bool powered ) {
    if ( powered && def->lit_variant != LAST_BLOCK_ID ) {
        return block_definition_get_definition( def->lit_variant )->emits_light;
    }
    return def->emits_light;
}

// Gate check for a diagonal step local -> local+sd.d. Returns true when a
// shared-face route is open: some gate face M is non-opaque, C->M isn't a
// downward hop sealed by a floor source, and M->D isn't a downward hop
// sealed by a floor M. Ids come from the chunk's own cells[] — gate cells
// are always within the internal volume for an interior source.
inline bool light_diag_open( const BlockCell *cells, const glm::ivec3 &local, const LightStepDir &sd, const bool src_floor ) {
    for ( int i = 0; i < sd.ngates; i++ ) {
        const glm::ivec3 &f = sd.gate[ i ];
        if ( f.y < 0 && src_floor ) {
            continue;
        }
        const BlockID m_id = light_cell_id_at( cells, local + f );
        if ( light_opaque_id( m_id ) ) {
            continue;
        }
        if ( sd.d.y - f.y < 0 && block_definition_get_definition( m_id )->blocks_light_down ) {
            continue;
        }
        return true;
    }
    return false;
}
// TEMP diagnostics: which producer feeds the queue.
inline long long light_dbg_pq_calls = 0, light_dbg_pq_nonempty = 0;
inline long long light_dbg_loop_top = 0, light_dbg_after_jobs = 0, light_dbg_jobs_n = 0;
inline std::atomic<long long> light_dbg_pushes{ 0 }, light_dbg_pops{ 0 };
inline long long light_dbg_finalize = 0,
                 light_dbg_cascade_seed = 0, light_dbg_border_seed = 0,
                 light_dbg_recheck_seed = 0;
inline std::atomic<long long> light_dbg_step_push{ 0 }, light_dbg_remove_seed{ 0 };
// Pops that hit a contested column and were re-queued unprocessed — the
// measure of lock-contention churn in the parallel drain.
inline std::atomic<long long> light_dbg_requeued{ 0 };
// Seeds that died because their cell's chunk was missing/still loading when
// popped. The receiving chunk's own finalize normally re-seeds coverage, so a
// nonzero count is not itself a bug — but a high count plus persistent dark
// areas means dropped work that nothing re-covered.
inline std::atomic<long long> light_dbg_dropped{ 0 };
// Same, but refused at push time by the gate in light_seed_gate — these
// never occupy the queue, so a high count is strictly cheaper than the same
// drops landing at pop. Folded into `drop`.
inline std::atomic<long long> light_dbg_push_drops{ 0 };
// Seeds refused by the far-lane backlog cap — each flagged its target chunk
// light_stale for lazy re-finalize rather than queueing.
inline std::atomic<long long> light_dbg_capped{ 0 };
// Dirty-upload list entries deferred this interval for being write-hot
// (light_write_tick within LIGHT_UPLOAD_QUIET_TICKS) — the anti-torn-upload
// debounce at work; spikes while a flood marches through visible chunks.
inline std::atomic<long long> light_dbg_upload_deferred{ 0 };
// Per-interval max age of a job when it actually ran (submit -> execute), and
// how many jobs waited >1s — exposes priority inversions where a visible
// chunk's finalize sits behind a deep queue.
inline std::atomic<long long> light_dbg_max_lag_us{ 0 };
inline std::atomic<long long> light_dbg_late_jobs{ 0 };
inline std::atomic<long long> light_dbg_max_lag_near_us{ 0 }; // lag of jobs that were near the player AT EXECUTION
inline std::atomic<long long> light_dbg_sync{ 0 };    // rechecks run synchronously on the calling thread
inline std::atomic<long long> light_dbg_rej{ 0 };     // rechecks that failed the sync claim and went async
// TEMP frame-phase timing (µs accumulated between probes) + call counts.
inline long long light_dbg_us_finalize = 0, light_dbg_us_remesh = 0, light_dbg_us_drain = 0,
          light_dbg_us_bfs = 0, light_dbg_us_upload = 0, light_dbg_us_bind = 0,
          light_dbg_us_ensure = 0, light_dbg_n_upload = 0, light_dbg_n_ensure = 0,
          light_dbg_us_light_fin = 0, light_dbg_us_fin_fill = 0, light_dbg_us_fin_casc = 0,
          light_dbg_us_fin_bscan = 0, light_dbg_us_fin_border = 0,
          light_dbg_us_fill = 0, light_dbg_n_fill = 0,
          light_dbg_us_framepool = 0, light_dbg_n_framepool = 0;
// Approximate count of queued BFS seeds, kept atomic so light workers can
// test "queue nonempty" from the cv predicate without touching the deques
// (a claimed job may be pushing while another worker waits). ++ on every
// push_back, -- on every pop_front below.
inline std::atomic<long long> light_bfs_pending{ 0 };

// Starve-window thresholds (the window machinery lives with the claim code
// below): a backlog over STARVE_MIN for more than STARVE_TICKS interrupts
// claims. Backlog age is measured in game ticks fed by RepGame::tick
// (light_note_tick) so the seed push/pop hot paths never read steady_clock —
// everything below is plain atomic loads.
inline std::atomic<long> light_tick_counter{ 0 };
#define LIGHT_BFS_STARVE_MIN 2048                // backlog worth interrupting claims for
#define LIGHT_BFS_STARVE_TICKS ( UPS_RATE / 10 ) // ~100ms of game ticks
// Flood-regen cap: past this, pushes spawned BY pops (the add/remove march's
// own expansion) are refused and their target chunk flagged light_stale — a
// marching flood can no longer regenerate the backlog for tens of seconds
// after it formed; flagged chunks re-finalize lazily (light_heal_*). Seeds
// pushed by producers (finalize/cascade/border sync/recheck) still land, so
// fresh chunk light keeps converging during streaming.
// Every refusal costs a full relight of its chunk later, which re-runs that
// chunk's share of the flood — so a cap below the legitimate peak backlog
// multiplies work instead of bounding it. Large shadowed volumes (flat-world
// voxelized models: the whole space under a car is one remove/respread
// flood) peak at ~2.3M seeds; at the old 1M cap the stationary car world did
// ~10x the BFS work and took ~50s to converge instead of ~5s. Seeds are ~16
// bytes, so 8M is ~128MB of transient queue at worst.
#define LIGHT_BFS_CAP_FLOOD ( 1 << 23 )
// Absolute bound: even external far-lane pushes are refused past this.
#define LIGHT_BFS_CAP_ALL ( 1 << 24 )
// Chunks re-finalized per frame by the idle stale-heal scan, and the backlog
// under which heals may feed. Heal seeds are external pushes (they always
// land), so the feed can run well above STARVE_MIN — keeping the pile fed
// while flagged chunks still exist is what shortens the post-flight tail.
// The feed must stay far below LIGHT_BFS_CAP_FLOOD: each healed chunk
// re-emits thousands of cascade/border seeds, and a heal burst that lifts
// the backlog past the cap flags more chunks stale than 8/frame can heal —
// a self-sustaining loop (the car world livelocked on it with the old 1M cap).
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
// Wall-clock budget for the whole per-frame upload pass. Staged READY
// buffers bypass the max_uploads count (claim ordering needs them to
// drain), so a worker burst could publish a dozen fills between passes
// and one frame would run every glTexSubImage3D + glGenerateMipmap back
// to back. Time-boxing the pass spreads that drain; entries the budget
// skips re-queue — staged buffers stay READY on the chunk and inline
// boxes merge back into dirty_box, so nothing is lost.
// LIGHT_UPLOAD_BUDGET_US caps the pass; LIGHT_UPLOAD_FLOOR_US is the slice
// it still gets when the shared frame-work budget is already spent —
// without it a saturated frame could leave dirty texels unrefreshed
// indefinitely (visibly stale light) during sustained sprint streaming.
#define LIGHT_UPLOAD_BUDGET_US 1200
#define LIGHT_UPLOAD_FLOOR_US 400
// Tick at which light_bfs_pending last rose above LIGHT_BFS_STARVE_MIN.
// Stamped only by the crossing push, so a backlog that never drains under
// the line keeps accruing age no matter how often drains run.
inline std::atomic<long> light_bfs_backlog_tick{ 0 };

inline void light_bfs_pending_inc( ) {
    if ( light_bfs_pending.fetch_add( 1, std::memory_order_relaxed ) == LIGHT_BFS_STARVE_MIN ) {
        light_bfs_backlog_tick.store( light_tick_counter.load( std::memory_order_relaxed ), std::memory_order_relaxed );
    }
}
// Seed deque serialization: claimed jobs push while drain workers pop
// concurrently, so both sides take the column slot's guard. Tiny critical
// sections (one deque op plus a pending-count update) → spinlock. Same
// representation as light_col_locks (plain int CAS) since C++17
// atomic_flag arrays can't be fully list-initialized.
inline std::atomic<int> light_colq_locks[ LIGHT_COL_QUEUE_COUNT ];
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
inline int light_col_slot_at( const int cx, const int cz ) {
    const int sx = 2 * CHUNK_RADIUS_XZ + 1;
    const int sz = 2 * CHUNK_RADIUS_XZ + 1;
    return ( ( cx % sx ) + sx ) % sx + sx * ( ( ( cz % sz ) + sz ) % sz );
}
inline int light_col_slot( const glm::ivec3 &block_pos ) {
    const glm::ivec3 cp = light_chunk_pos_of( block_pos );
    return light_col_slot_at( cp.x, cp.z );
}
// Inverse for distance ordering: slot (mx,mz) currently holds the column
// center + wrapped offset (the window is centered on chunk_center). This
// is also the right priority for out-of-window seeds squatting in the
// slot — doomed seeds should drain late, not ahead of live columns.
inline glm::ivec2 light_col_slot_pos( const int slot, const glm::ivec3 &center ) {
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

// Per-column-slot pending counts and drain ownership. A slot's queues are
// popped only by the worker that CAS-claims its owned flag; selection picks
// the NEAREST claimable slot to the player, so distance priority is
// continuous — a column the player flies up to drains next regardless of
// when its seeds were pushed.
inline std::atomic<long long> light_colq_pending[ LIGHT_COL_QUEUE_COUNT ];
inline std::atomic<int> light_colq_owned[ LIGHT_COL_QUEUE_COUNT ];
// Near-player radius in chunk columns for the priority lanes (seeds below)
// and the front-of-queue job lane (light_submit_job). A chunk's lighting only
// converges once its neighborhood is processed, so a few chunks around the
// player jump the backlog and their block ring settles together instead of
// in terrain-load order.
#define LIGHT_NEAR_COL_DIST 6

// Is chunk_col within the near-player radius of center?
// chunk_center is a racy render-thread read — fine for a priority hint, and
// stale routing self-corrects as the player moves.
inline bool light_near_col( const glm::ivec3 &center, const glm::ivec3 &chunk_col ) {
    const glm::ivec3 d = chunk_col - center;
    return d.x * d.x + d.z * d.z <= LIGHT_NEAR_COL_DIST * LIGHT_NEAR_COL_DIST;
}
inline bool light_near_center( const glm::ivec3 &center, const glm::ivec3 &block_pos ) {
    return light_near_col( center, light_chunk_pos_of( block_pos ) );
}
// ---------------------------------------------------------------------------
// Cell access
// ---------------------------------------------------------------------------

// Internal coords -1..CHUNK_SIZE_* index into the 34^3 volume. A cached Chunk*
// can refer to a slot recycled mid-access (chunk_pos torn), producing a
// garbage local — guard before indexing or it writes/reads out of bounds.
inline bool light_local_ok( const glm::ivec3 &l ) {
    return l.x >= -1 && l.x <= CHUNK_SIZE_X && l.y >= -1 && l.y <= CHUNK_SIZE_Y && l.z >= -1 && l.z <= CHUNK_SIZE_Z;
}
// A chunk with light == nullptr has a uniform volume: every cell holds
// light_uniform (all-air chunks fold to optimistic sky=15; fully-dark fills
// fold to 0). Reads return the uniform value; a write that differs
// materializes the array first.
inline unsigned short ChunkLoader::light_read_cell( const Chunk &chunk, const int idx ) {
    return chunk.light.volume ? chunk.light.volume[ idx ] : chunk.light.uniform;
}
// Write packed value to cell idx. Materializes only when the new packed u16
// actually differs from the uniform value — writing the uniform value into a
// uniform volume is a no-op.
inline void ChunkLoader::light_write_cell( Chunk &chunk, const int idx, const unsigned short nv ) {
    if ( !chunk.light.volume ) {
        if ( nv == chunk.light.uniform ) {
            return;
        }
        light_materialize( chunk );
    }
    chunk.light.volume[ idx ] = nv;
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
inline std::mutex light_work_mutex;
inline std::condition_variable light_work_cv;
inline std::vector<Chunk *> light_pending_list;
// Dirty light volumes awaiting GPU upload, split by distance to the player —
// uploads are capped per frame so a near chunk marked during load-in must not
// sit behind thousands of far entries (that alone added seconds of stale
// lighting next to the player).
inline std::vector<Chunk *> light_dirty_list_near;
inline std::vector<Chunk *> light_dirty_list_far;
// ---------------------------------------------------------------------------
// Worker-side upload staging
// ---------------------------------------------------------------------------
// The smoothed presentation fill costs ~25x the raw expand per texel, and
// doing it synchronously in light_upload_dirty hitches the render thread.
// Staging moves the fill to the light workers: the render thread claims a
// chunk (light_stage_state IDLE -> BUSY) and queues it; a worker exchanges
// the dirty box, fills a pooled buffer and publishes (BUSY -> READY); the
// next upload pass just does the GL call and returns the buffer (READY ->
// IDLE). Fast/raw-mode chunks keep the inline fill — their expand is
// already cheap, so a round trip would only add latency.
//
// light_stage_state packs a generation counter above a 2-bit status. The
// publish CAS carries the generation the worker claimed under, so a slot
// recycled mid-stage (light_reset_staging bumps the generation) can never
// publish stale texels onto the new chunk — the CAS just fails and the
// worker frees its own buffer. Chunk::light itself can be freed mid-stage
// the same way: the buffer pools keep stale reads memory-safe, and the
// generation keeps the RESULT from landing.
#define LIGHT_STAGE_IDLE 0u
#define LIGHT_STAGE_BUSY 1u
#define LIGHT_STAGE_READY 2u
#define LIGHT_STAGE_STATUS( s ) ( ( s ) & 3u )
// Upload texel buffer: RGBA8 worst case, full internal volume.
#define LIGHT_STAGE_BUF_SIZE ( CHUNK_BLOCK_SIZE * 4 )
// Live staging buffers: one per worker plus a few published-but-not-yet-
// uploaded (LIGHT_MAX_WORKERS is 8, defined below). Empty pool = worker
// aborts the stage and re-queues the chunk.
#define LIGHT_STAGE_MAX 12
inline std::deque<Chunk *> light_stage_list;
inline unsigned char *light_stage_pool[ LIGHT_STAGE_MAX ];
inline int light_stage_pool_n = 0;
inline int light_stage_live = 0;
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
#define LIGHT_COL_SPIN_TRIES 1024
inline std::atomic<int> light_col_locks[ LIGHT_COL_LOCK_COUNT ];
inline int light_col_lock_index( const int cx, const int cz ) {
    // Odd multipliers spread adjacent columns to distant slots — the common
    // contention case is neighboring columns.
    return ( cx * 0x9E3779B1 + cz * 0x85EBCA77 ) & ( LIGHT_COL_LOCK_COUNT - 1 );
}
inline bool light_col_lock_try( const int idx ) {
    int expected = 0;
    return light_col_locks[ idx ].load( std::memory_order_relaxed ) == 0 &&
           light_col_locks[ idx ].compare_exchange_strong( expected, 1, std::memory_order_acquire, std::memory_order_relaxed );
}
// Bounded spin: pop-vs-pop holds last one BFS step (~1µs) — 32 bare tries
// timed out before the holder released, so most contested pops requeued
// needlessly. ~1024 tries with a pause hint covers a step-length hold;
// claim holds (~ms) still time out and the seed requeues.
inline bool light_col_lock_spin( const int idx ) {
    for ( int i = 0; i < LIGHT_COL_SPIN_TRIES; i++ ) {
        if ( light_col_lock_try( idx ) ) {
            return true;
        }
        light_cpu_relax( );
    }
    return false;
}
inline void light_col_lock_acquire( const int idx ) {
    for ( ;; ) {
        while ( light_col_locks[ idx ].load( std::memory_order_relaxed ) != 0 ) {
            light_cpu_relax( );
        }
        int expected = 0;
        if ( light_col_locks[ idx ].compare_exchange_weak( expected, 1, std::memory_order_acquire, std::memory_order_relaxed ) ) {
            return;
        }
    }
}
inline void light_col_lock_release( const int idx ) {
    light_col_locks[ idx ].store( 0, std::memory_order_release );
}

// Queued BFS seeds per column (same hash domain as the locks — collisions
// just over-count, which is conservative). Incremented when a seed enters
// a column queue, decremented when one is popped. A column with incident
// seeds is mid-flood: the upload debounce holds its chunks back entirely
// (a floor-write → respread gap under a deep backlog is far longer than
// the write-quiet window, so counting pending work is what actually
// suppresses the correct→dark→correct flash).
inline std::atomic<int> light_col_incident[ LIGHT_COL_LOCK_COUNT ];
inline void light_col_incident_inc( const glm::ivec3 &block_pos ) {
    const glm::ivec3 cp = light_chunk_pos_of( block_pos );
    light_col_incident[ light_col_lock_index( cp.x, cp.z ) ].fetch_add( 1, std::memory_order_relaxed );
}
inline void light_col_incident_dec( const glm::ivec3 &block_pos ) {
    const glm::ivec3 cp = light_chunk_pos_of( block_pos );
    light_col_incident[ light_col_lock_index( cp.x, cp.z ) ].fetch_sub( 1, std::memory_order_relaxed );
}

// Sorted+deduped lock indices for the 3x3 columns around (cx,cz). Sorting
// keeps every multi-lock acquirer on the same global order (claims spin,
// so an unordered pair could deadlock); dedup removes hash collisions that
inline int light_claim_cols( const int cx, const int cz, int out[ 9 ] ) {
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
// step reads pos's own cell and inspects/writes its 18 step neighbors; each
// neighbor write goes through light_set, whose mirror matrix reaches the
// cell's own column +/-1 on each axis where the cell sits on a border —
// the cartesian product of its border-axis offsets. Face-edge seeds take
// 2-4 columns; only degenerate corners approach the 3x3 bound (mirrors
// only extend toward the border the cell sits on, so the footprint can
// never leave pos's column neighborhood). Vertical neighbors/mirrors stay
// inside the column (same cx,cz).
// Entries: hashed lock indices, sorted so multi-lock acquires share one
// global order.
inline int light_pop_cols( const glm::ivec3 &block_pos, int out[ 9 ] ) {
    const glm::ivec3 cp = light_chunk_pos_of( block_pos );
    const glm::ivec3 local = block_pos - cp * CHUNK_SIZE_I;
    if ( local.x >= 2 && local.x <= CHUNK_SIZE_X - 3 && local.z >= 2 && local.z <= CHUNK_SIZE_Z - 3 ) {
        out[ 0 ] = light_col_lock_index( cp.x, cp.z );
        return 1;
    }
    int n = 0;
    glm::ivec2 seen[ 9 ];
    seen[ n++ ] = glm::ivec2( cp.x, cp.z );
    // Every step dir, not just faces: an xz edge diagonal from a cell one
    // in from a chunk corner writes the corner cell, whose mirrors reach
    // the diagonal chunk — a column no face neighbor's offsets cover.
    for ( const LightStepDir &sd : light_step_dirs ) {
        const glm::ivec3 p = block_pos + sd.d;
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
        out[ i ] = light_col_lock_index( seen[ i ].x, seen[ i ].y );
    }
    // n <= 9: hand-rolled insertion sort + dedup. Avoids std::sort's
    // introsort dispatch and GCC 13's -Warray-bounds false positive on the
    // small fixed buffer (mingw -Werror).
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

// Spin-acquire a sorted lock set, then run and release. Pops use this; the
// set is tiny (1-9 flags) and hold times are a single step (~hundreds of
// ns), so contention resolves quickly.
inline void light_cols_lock( const int *cols, const int n ) {
    for ( int i = 0; i < n; i++ ) {
        light_col_lock_acquire( cols[ i ] );
    }
}
inline void light_cols_unlock( const int *cols, const int n ) {
    for ( int i = 0; i < n; i++ ) {
        light_col_lock_release( cols[ i ] );
    }
}
// A recheck's own writes (the emit cell, mirror cells, cascade clears) mark
// dirty the moment they run, but their propagation spread only writes when a
// BFS drain pops the seeds — during load-in that is frames later, so the
// torch cell appears lit while the halo around it is still dark. While
// light_defer_marks > 0, light_mark_dirty appends to this list instead; the
// marks flush at the start of the next drain so the emit cell and its spread
// upload in the same pass. Guarded by light_work_mutex.
inline std::atomic<int> light_defer_marks{ 0 };
inline std::vector<std::pair<Chunk *, glm::ivec3>> light_deferred_marks;
// Pack/unpack helpers for Chunk::light_dirty_box (layout in chunk.hpp).
// 8 bits per component limits texel coords to [0,255], so the light volume
// can be at most 256^3 internal — a larger CHUNK_SIZE would silently wrap
// dirty-box coordinates and scramble uploads. LIGHT_BOX_EMPTY has
// min=255 > max=0.
static_assert( CHUNK_SIZE_INTERNAL_X <= 256 && CHUNK_SIZE_INTERNAL_Y <= 256 && CHUNK_SIZE_INTERNAL_Z <= 256,
               "light_dirty_box packs texel coords as 8-bit fields; CHUNK_SIZE_INTERNAL_* must be <= 256" );
inline uint64_t light_box_pack( const glm::ivec3 &mn, const glm::ivec3 &mx ) {
    return static_cast<uint64_t>( mn.x & 0xFF ) | ( static_cast<uint64_t>( mn.y & 0xFF ) << 8 ) |
           ( static_cast<uint64_t>( mn.z & 0xFF ) << 16 ) | ( static_cast<uint64_t>( mx.x & 0xFF ) << 24 ) |
           ( static_cast<uint64_t>( mx.y & 0xFF ) << 32 ) | ( static_cast<uint64_t>( mx.z & 0xFF ) << 40 );
}

inline glm::ivec3 light_box_min( const uint64_t box ) {
    return glm::ivec3( static_cast<int>( box & 0xFF ), static_cast<int>( ( box >> 8 ) & 0xFF ),
                       static_cast<int>( ( box >> 16 ) & 0xFF ) );
}

inline glm::ivec3 light_box_max( const uint64_t box ) {
    return glm::ivec3( static_cast<int>( ( box >> 24 ) & 0xFF ), static_cast<int>( ( box >> 32 ) & 0xFF ),
                       static_cast<int>( ( box >> 40 ) & 0xFF ) );
}

inline uint64_t light_box_merge( const uint64_t box, const glm::ivec3 &texel ) {
    const glm::ivec3 mn = light_box_min( box );
    if ( mn.x > light_box_max( box ).x ) {
        return light_box_pack( texel, texel ); // EMPTY
    }
    return light_box_pack( glm::min( mn, texel ), glm::max( light_box_max( box ), texel ) );
}

inline uint64_t light_box_union( const uint64_t a, const uint64_t b ) {
    if ( light_box_min( a ).x > light_box_max( a ).x ) {
        return b; // EMPTY
    }
    return light_box_pack( glm::min( light_box_min( a ), light_box_min( b ) ),
                           glm::max( light_box_max( a ), light_box_max( b ) ) );
}
