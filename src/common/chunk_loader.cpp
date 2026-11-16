#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <algorithm>
#ifdef __GLIBC__
#include <malloc.h>
#endif
#include <chrono>
#include <limits>
#include <vector>

#include "common/Platforms.hpp" // IWYU pragma: keep — GL headers + platform macros
#include "common/Logging.hpp"
#include "common/chunk_loader.hpp"
#include "common/block_instance_batch.hpp"
#include "common/utils/env_config.hpp"
#include "common/multiplayer.hpp"
#include "common/net/mpload_trace.hpp"
#include "common/net/packet.hpp"
#include "common/utils/map_storage.hpp"
#include "common/utils/terrain_loading_thread.hpp"
#include "common/renderer/shader.hpp"
#include "common/constants.hpp"

MK_SHADER( chunk_vertex );
MK_SHADER( chunk_fragment );
MK_SHADER( occl_vertex );
MK_SHADER( occl_fragment );

// Unit cube for the AABB probes (position attr in [0,1]^3, scaled to
// CHUNK_SIZE_* in occl_vertex.glsl). Winding is irrelevant — face culling
// is disabled around the probe pass so a camera inside a box still
// rasterizes it.
static const float occl_box_verts[] = {
    // clang-format off
    0, 0, 0, 1, 0, 0, 1, 1, 0, /**/ 0, 0, 0, 1, 1, 0, 0, 1, 0, // -z
    0, 0, 1, 1, 1, 1, 1, 0, 1, /**/ 0, 0, 1, 0, 1, 1, 1, 1, 1, // +z
    0, 0, 0, 1, 0, 1, 1, 0, 0, /**/ 0, 0, 0, 0, 0, 1, 1, 0, 1, // -y
    0, 1, 0, 1, 1, 0, 1, 1, 1, /**/ 0, 1, 0, 1, 1, 1, 0, 1, 1, // +y
    0, 0, 0, 0, 1, 1, 0, 0, 1, /**/ 0, 0, 0, 0, 1, 0, 0, 1, 1, // -x
    1, 0, 0, 1, 0, 1, 1, 1, 1, /**/ 1, 0, 0, 1, 1, 1, 1, 1, 0, // +x
    // clang-format on
};

// When limit_render is set (WASM, no background loading thread), cap how long
// render_chunks spends generating/loading chunks per frame so the main thread
// keeps up a steady frame rate while still making progress every frame.
static constexpr long long WASM_LOAD_BUDGET_US = 4000; // 4 ms

// On native (workers do terrain gen off-thread), the dequeue loop below still
// runs per-chunk GL work on the render thread: glGen*/attrib setup in
// ensure_gl_init, index uploads in calculate_sides, and vertex uploads in
// program_terrain. Workers finish in bursts (initial load, fast flight), so
// draining the whole result queue in one frame causes hitches — most visibly
// on mobile GPUs where per-call driver overhead is high. Bound the finalize
// work per frame; undrained chunks stay in the result queue and stream in
// over the next frames (fog hides the pop-in).
// Kept tight enough that a burst of finished chunks can't eat a whole frame:
// the load-in tail streams in over a few extra frames instead of hitching.
static constexpr long long CHUNK_FINALIZE_BUDGET_US = 3000; // 3 ms

// Per grid shift, at most this many near chunks that still have queued
// incident seed work get a near-lane re-finalize (light_heal_chunk) —
// converges a just-arrived half-lit region in one pass instead of letting
// its seeds drain cell-by-cell.
static constexpr int LIGHT_HEAL_PER_SHIFT = 8;

// Similarly bound the needs_repopulation re-mesh loop: calculate_populated_blocks
// is full CPU meshing done on the render thread, and bursts of dirty chunks
// (multiplayer diffs, block edits) would otherwise all re-mesh in one frame.
static constexpr long long CHUNK_REMESH_BUDGET_US = 2000; // 2 ms

// The must-remesh queue (repop_must) gets its own, larger budget. Live edits
// are latency-critical and small — a boundary edit dirties at most 8 chunks
// (corner fixup) — so every mesh they touch should rebuild in the same pass;
// splitting them under the tighter budget showed a see-through hole while
// the neighbor's stale mesh lacked the newly exposed faces. Still bounded so
// a same-frame burst (TNT, fluids, piston cascades) can't hitch the render
// thread; overflow keeps needs_repopulation and drains via the scan below.
static constexpr long long REPOP_MUST_BUDGET_US = 8000; // 8 ms

// Bound the per-frame light propagation work: edit seeds are tiny, but chunk
// load-in border syncs can enqueue large BFS fronts — spreading them over a
// few frames keeps load-in smooth.
static constexpr long long LIGHT_PROPAGATE_BUDGET_US = 1000; // 1 ms
// Cap dirty light-volume uploads per frame; a first-draw upload covers the
// full volume anyway (Chunk::light_ensure_texture), so this only throttles
// propagation updates. Whole-volume updates (chunk re-finalizes) cost a
// 78KB expansion + glTexSubImage3D each, so keep the cap tight.
static constexpr int LIGHT_MAX_UPLOADS_PER_FRAME = 8;

// Shared per-frame allowance for the streaming phases in render_chunks:
// finalize dequeue, remesh submit/publish, repop_must, the dirty scan,
// and the light upload pass all draw from one wall-clock pool instead of
// each spending its own cap — the independent caps could all fire in one
// frame and stack past 15 ms. Each phase keeps its own cap as a maximum
// and gets min(cap, pool remaining); pool-empty phases only run their
// one-unit minimums.
//
// The allowance FLOATS with queue depth: quiet play holds FRAME_WORK_MIN
// (~half a 240 Hz frame) so crossing a chunk border stays invisible; a
// deep backlog (sprint flight, or a machine too slow to drain at MIN)
// grows it toward FRAME_WORK_MAX — throughput wins over smoothness only
// while the queues say the machine is actually behind. Each queued item
// adds 40us; ~120 queued items saturate the ceiling.
static constexpr long long FRAME_WORK_MIN_US = 2200;
static constexpr long long FRAME_WORK_MAX_US = 7000;
static constexpr int FRAME_WORK_BACKLOG_US = 40;

// CHUNK_SETTLE_WAIT fallback timings: a chunk whose covering diff answer
// never arrives re-asks once its deferral is this old, and force-reveals
// (with a warning) past the hard cap rather than leaving a hole in the world.
static constexpr long long SETTLE_REASK_US = 3000000;  // 3 s
static constexpr long long SETTLE_FORCE_US = 15000000; // 15 s

static inline long long now_us( ) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now( ).time_since_epoch( ) ).count( );
}

// TEMP diagnostics: phase timing accumulators (defined in light.cpp, printed
// by light_dbg_stats every ~2s from the LIGHT probe).
extern long long light_dbg_us_finalize, light_dbg_us_remesh, light_dbg_us_drain,
    light_dbg_us_bfs, light_dbg_us_upload, light_dbg_us_bind, light_dbg_us_ensure,
    light_dbg_n_upload, light_dbg_n_ensure, light_dbg_us_light_fin,
    light_dbg_us_framepool, light_dbg_n_framepool;

static int mod( const int x, const int N ) {
    const int result = ( x % N + N ) % N;
    if ( result < 0 ) {
        pr_debug( "Bad Mod:%d", result );
    }
    return result;
}
// The position congruent to chunk_mod nearest `center` — the same mapping
// TerrainLoadingThread::process_value retargets in-flight jobs by
// (terrain_slot_pos), so for a CHUNK_LOADING slot this is also "where it
// will land". No side effects, unlike reload_if_out_of_bounds.
static glm::ivec3 slot_target_pos( const glm::ivec3 &chunk_mod, const glm::ivec3 &center ) {
    return glm::ivec3(
        static_cast<int>( floorf( static_cast<float>( center.x - chunk_mod.x + CHUNK_RADIUS_XZ ) / static_cast<float>( ( CHUNK_RADIUS_XZ * 2 + 1 ) ) ) ) * ( CHUNK_RADIUS_XZ * 2 + 1 ) + chunk_mod.x,
        static_cast<int>( floorf( static_cast<float>( center.y - chunk_mod.y + CHUNK_RADIUS_Y ) / static_cast<float>( ( CHUNK_RADIUS_Y * 2 + 1 ) ) ) ) * ( CHUNK_RADIUS_Y * 2 + 1 ) + chunk_mod.y,
        static_cast<int>( floorf( static_cast<float>( center.z - chunk_mod.z + CHUNK_RADIUS_XZ ) / static_cast<float>( ( CHUNK_RADIUS_XZ * 2 + 1 ) ) ) ) * ( CHUNK_RADIUS_XZ * 2 + 1 ) + chunk_mod.z );
}
// The active render window is the chunk box centered on the camera chunk:
// render_radius on x/z (runtime-adjustable), the full compile-time
// CHUNK_RADIUS_Y vertically. Slots outside it park instead of loading.
bool ChunkLoader::pos_in_window( const glm::ivec3 &center, const glm::ivec3 &pos ) const {
    const glm::ivec3 d = pos - center;
    return d.x >= -this->render_radius && d.x <= this->render_radius && //
           d.z >= -this->render_radius && d.z <= this->render_radius;
}

void ChunkLoader::set_render_distance( int radius ) {
    if ( radius < 1 ) {
        radius = 1;
    } else if ( radius > RENDER_RADIUS_MAX ) {
        radius = RENDER_RADIUS_MAX;
    }
    this->render_radius = radius;
}

// Returns 0 when the slot stays put in-window (callers then do the usual
// recenter/heal, or the deferred-finalize path for a just-dequeued load),
// 1 when a (re)load was enqueued, and 2 when the slot parked because its
// nearest position is outside the active render window.
int ChunkLoader::reload_if_out_of_bounds( Chunk &chunk, const glm::ivec3 &chunk_pos, glm::ivec3 *out_new_pos ) {

    const glm::ivec3 new_chunk = slot_target_pos( chunk.chunk_mod, chunk_pos );
    if ( out_new_pos ) {
        *out_new_pos = new_chunk;
    }
    if ( !this->pos_in_window( chunk_pos, new_chunk ) ) {
        // Park the slot: any nonzero state keeps it out of the drawable
        // list, random ticks, and the light paths. No terrain data is
        // retained — a full torus of parked chunks would overflow the
        // wasm32 heap outright (and turn render distance into an unbounded
        // cache elsewhere) — so memory follows the active window and
        // re-entry reloads the save or regenerates. Dirty chunks persist
        // the same way a slot moving to a new position does: through the
        // work queue, where process_value writes the save off the render
        // thread before the dequeue-time bounds check releases the data.
        // The free itself is deferred (defer_release): a light worker can
        // still be mid-job on this chunk after its load_state gate, and
        // its buffers are pooled — freeing them now would recycle the
        // memory under the worker.
        if ( chunk.load_state == CHUNK_LIVE ) {
            if ( chunk.dirty ) {
                chunk.set_load_state( CHUNK_LOADING );
                this->terrain_loading_thread.enqueue( &chunk, chunk.chunk_pos, 1 );
            } else {
                chunk.set_load_state( CHUNK_PARKED );
                this->defer_release( chunk );
            }
        } else if ( chunk.load_state == CHUNK_LOADING || chunk.load_state == CHUNK_SETTLE_WAIT ) {
            // A just-dequeued item that landed out of window (or a
            // settle-wait slot whose window moved off it): its previous
            // position's edits already persisted on the worker (and a dirty
            // park's just did), while freshly generated terrain is
            // reproducible — drop everything.
            chunk.set_load_state( CHUNK_PARKED );
            this->defer_release( chunk );
        }
        return 2;
    }
    if ( new_chunk == chunk.chunk_pos && chunk.load_state != CHUNK_PARKED ) {
        // LIVE or LOADING at the right position already — including a dirty
        // park that rode the queue back in-window mid-persist: its data is
        // intact and it re-finalizes like any dequeued load (idempotent —
        // see program_terrain's populated_blocks guard).
        return 0;
    }
    // CHUNK_PARKED re-entering the window, or the slot's nearest position
    // moved: (re)load through the normal queue.
    chunk.unprogram_terrain( );
    chunk.set_load_state( CHUNK_LOADING );
    this->terrain_loading_thread.enqueue( &chunk, new_chunk, 1 );
    return 1;
}

// Queue a parked slot's buffers for release once light workers can no
// longer be mid-job on it (render_chunks drains parked_release after
// PARK_RELEASE_DEFER_FRAMES). gen guards against a stale entry releasing
// a slot that re-entered and re-parked in the meantime.
void ChunkLoader::defer_release( Chunk &chunk ) {
    this->parked_release.push_back( { &chunk, ++chunk.park_release_gen, this->park_frame } );
}

// The post-terrain-load steps shared by the dequeue path and parked-slot
// reactivation: GL init, mesh, pending diffs, light finalize, GPU upload.
void ChunkLoader::finalize_loaded_chunk( Chunk &chunk, Multiplayer &multiplayer, const glm::ivec3 &center_next, bool force_reveal ) {
    // Backstop for diff-request coverage: in-flight loads retarget to the
    // slot's current position at pick-up (terrain_slot_pos), so a chunk can
    // land on a position no request box ever covered. Without this it would
    // sit as pure generated terrain while neighbours' halo mirrors hold the
    // real border edits — mismatched seam mirrors, see-through faces, and
    // "reset" blocks.
    const int slot_i = static_cast<int>( &chunk - this->chunkArray );
    if ( this->slot_diff_req[ slot_i ] != chunk.chunk_pos && multiplayer.request_chunk( chunk.chunk_pos ) ) {
        this->slot_diff_req[ slot_i ] = chunk.chunk_pos;
        this->slot_diff_seq[ slot_i ] = multiplayer.diff_boxes_sent_count( );
    }
    // Diff-gated reveal (docs/multiplayer.md): with the server connected,
    // hold the whole finalize until the request covering this position is
    // DONE so the first live state already carries every applied diff —
    // no generated-terrain pop-in, no stale-edit seams, and every reader
    // (portals, collision, probes) sees LAST_BLOCK_ID meanwhile. Runs
    // before set_load_state/GL so a deferred slot touches nothing.
    if ( !force_reveal && !this->chunk_settled( slot_i, multiplayer ) ) {
        chunk.set_load_state( CHUNK_SETTLE_WAIT );
        // A slot re-deferred after retarget replaces its old entry so the
        // reask/force windows always start at the latest defer.
        for ( size_t k = 0; k < this->settle_wait_list.size( ); k++ ) {
            if ( this->settle_wait_list[ k ].slot == slot_i ) {
                this->settle_wait_list[ k ] = this->settle_wait_list.back( );
                this->settle_wait_list.pop_back( );
                break;
            }
        }
        this->settle_wait_list.push_back( { slot_i, now_us( ), false } );
        if ( mpload_trace_enabled( ) ) {
            mpload_tracef( "settle-wait chunk=%d,%d,%d seq=%u done=%u",
                           chunk.chunk_pos.x, chunk.chunk_pos.y, chunk.chunk_pos.z,
                           this->slot_diff_seq[ slot_i ], multiplayer.diff_boxes_done_count( ) );
        }
        return;
    }
    // Publish before the finalize work (the old dequeue path cleared
    // is_loading first): light jobs and seeds popped mid-finalize see the
    // chunk as loaded rather than missing.
    chunk.set_load_state( CHUNK_LIVE );
    this->live_finalize_count++;
    // Slot reuse: drop any async-remesh state the previous position left
    // behind (a stale in-flight seq would wrongly gate the new mesh).
    chunk.async_wanted = 0;
    chunk.async_discards = 0;
    chunk.async_job_seq = 0;
    chunk.async_links = 0;
    // Lazily create this chunk's GL objects on first load so the startup
    // loop doesn't block the first frame.
    chunk.ensure_gl_init( );
    process_chunk_position( chunk, center_next, 1 );
    // Apply any chunk diffs that arrived from the server before this chunk's
    // terrain gen finished (Phase 2 eager requests).
    const bool had_diffs = multiplayer.apply_pending_diffs( *this, chunk );
    // Staged SIGN_TEXT frames drain after the diffs so text only attaches
    // once the sign blocks exist.
    const bool had_pending = multiplayer.apply_pending_signs( chunk ) || had_diffs;
    if ( mpload_trace_enabled( ) ) {
        mpload_tracef( "finalize chunk=%d,%d,%d pending=%d",
                       chunk.chunk_pos.x, chunk.chunk_pos.y, chunk.chunk_pos.z, had_pending ? 1 : 0 );
    }
    // Blocks are final now: hand the chunk to the lighting thread
    // (cascade/border sync/propagation). Inline when no light thread is
    // running (tests, WASM).
    const long long t_lfin = now_us( );
    this->light_submit_finalize( chunk );
    light_dbg_us_light_fin += now_us( ) - t_lfin;
    // A recycled slot keeps its previous position's light texture (only
    // release_terrain frees it, and a fast park re-entry beats the deferred
    // release) while load_terrain flagged it tex_stale — draw shows the sky
    // fallback, but nothing would refresh the volume until the queued light
    // finalize happened to re-mark it. The field was just rewritten
    // wholesale, so mark it all: with uploaded_once reset the chunk counts
    // as fresh and the upload lands on the next pass (clearing tex_stale),
    // same as a first-draw create fill.
    if ( chunk.light.texture != 0 ) {
        this->light_mark_dirty_all( chunk );
    }
    if ( this->async_remesh_enabled( ) && chunk.needs_repopulation ) {
        // Pending diffs rewrote cells after the worker meshed — don't upload
        // the stale mesh. The chunk stays LIVE but undrawn until its async
        // remesh job publishes (publish does the equivalent of the skipped
        // program_terrain + light_ensure_texture).
        this->finalize_defer_mesh( chunk );
    } else {
        chunk.program_terrain( );
        // Create the light texture now rather than on first draw: the lazy path
        // (Chunk::draw -> light_ensure_texture) bunches a full-volume upload +
        // mipgen (~hundreds of us each) into whichever frame first reveals the
        // chunk, so the first camera pan after load-in pays it for the whole
        // never-visible half of the torus at once. Doing it here spreads that
        // cost across load-in under CHUNK_FINALIZE_BUDGET_US. Faceless chunks
        // still skip — only drawable chunks ever need the volume.
        if ( chunk.should_render ) {
            chunk.light_ensure_texture( this );
        }
    }
    // Mob ownership handoff: World::render drains this list into
    // MobManager::adopt_chunk so pending_mobs (loaded records or the
    // natural-spawn roll) become live mobs.
    this->just_finalized.push_back( &chunk );
}

void ChunkLoader::init( const glm::dvec3 &camera_pos, const VertexBufferLayout &vbl_block, const VertexBufferLayout &vbl_coords, MapStorage &map_storage, int render_radius ) {
    this->set_render_distance( render_radius );
    this->render_radius_applied = this->render_radius;
    if ( this->terrain_loading_thread.start( map_storage ) ) {
        pr_debug( "Terrain loading thread failed to start." );
    }
    this->chunkArray = static_cast<Chunk *>( calloc( MAX_LOADED_CHUNKS, sizeof( Chunk ) ) );
    this->drawable_chunks = static_cast<Chunk **>( calloc( MAX_LOADED_CHUNKS, sizeof( Chunk * ) ) );
    this->blended_chunks = static_cast<Chunk **>( calloc( MAX_LOADED_CHUNKS, sizeof( Chunk * ) ) );
    this->slot_want = static_cast<uint8_t *>( calloc( MAX_LOADED_CHUNKS, sizeof( uint8_t ) ) );
    this->slot_listed = static_cast<uint8_t *>( calloc( MAX_LOADED_CHUNKS, sizeof( uint8_t ) ) );
    this->repop_must = static_cast<Chunk **>( calloc( MAX_LOADED_CHUNKS, sizeof( Chunk * ) ) );
    this->num_drawable = 0;
    this->num_blended = 0;
    this->num_repop_must = 0;
    this->light_thread_start( );
    showErrors( );
    // pr_debug( "Num Total Chunks:%d", MAX_LOADED_CHUNKS );

    this->shader.init( &chunk_vertex, &chunk_fragment );
    if constexpr ( !DEBUG_CYCLE_AUTO_ROTATING_BLOCKS ) {
        this->shader.set_uniform1f( "u_ShowRotation", -2 );
    }

    this->solid.vb_block.init( );
    this->solid.vb_block.set_data( vd_data_solid, sizeof( CubeFace ) * VB_DATA_SIZE_SOLID );

    this->water.vb_block.init( );
    this->water.vb_block.set_data( vd_data_water, sizeof( CubeFace ) * VB_DATA_SIZE_WATER );

    this->occl_shader.init( &occl_vertex, &occl_fragment );
    this->occl_vb.init( );
    this->occl_vb.set_data( occl_box_verts, sizeof( occl_box_verts ) );
    this->occl_va.init( );
    VertexBufferLayout occl_vbl;
    occl_vbl.push_float( 3 );
    this->occl_va.add_buffer( this->occl_vb, occl_vbl );

    const VertexBuffer &vb_block_solid = this->solid.vb_block;
    const VertexBuffer &vb_block_water = this->water.vb_block;
    this->settle_wait_list.clear( );
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        this->slot_diff_req[ i ] = glm::ivec3( std::numeric_limits<int>::max( ) );
        this->slot_diff_seq[ i ] = 0;
        this->chunkArray[ i ].slot_flags = &this->slot_want[ i ];
        this->chunkArray[ i ].light.stale_word = &this->light_stale_bits[ i >> 6 ];
        this->chunkArray[ i ].light.stale_bit = 1ull << ( i & 63 );
        this->chunkArray[ i ].init( vb_block_solid, vb_block_water, vbl_block, vbl_coords );
    }

    showErrors( );

    glm::dvec3 chunk_size = glm::dvec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    glm::ivec3 camera_chuck = glm::floor( camera_pos / chunk_size );
    // render_chunks maintains chunk_center across moves; seed it here so the
    // skylight cascade (which scans center +/- CHUNK_RADIUS_Y) is correct from
    // the very first finalize.
    this->chunk_center = camera_chuck;

    int nextChunk = 0;
    for ( int i = 0; i <= CHUNK_RADIUS_XZ; i++ ) {
        for ( int j = 0; j <= CHUNK_RADIUS_Y; j++ ) {
            for ( int k = 0; k <= CHUNK_RADIUS_XZ; k++ ) {
                for ( int sign_i = -1; sign_i < 3; sign_i += 2 ) {
                    if ( sign_i == -1 && i == CHUNK_RADIUS_XZ ) {
                        continue;
                    }
                    for ( int sign_j = -1; sign_j < 3; sign_j += 2 ) {
                        if ( sign_j == -1 && j == CHUNK_RADIUS_Y ) {
                            continue;
                        }
                        for ( int sign_k = -1; sign_k < 3; sign_k += 2 ) {
                            if ( sign_k == -1 && k == CHUNK_RADIUS_XZ ) {
                                continue;
                            }
                            int new_i = ( i * sign_i ) - ( sign_i == -1 );
                            int new_j = ( j * sign_j ) - ( sign_j == -1 );
                            int new_k = ( k * sign_k ) - ( sign_k == -1 );
                            glm::ivec3 new_offset = glm::vec3( new_i, new_j, new_k );

                            glm::ivec3 chunk_pos = new_offset + camera_chuck;

                            glm::ivec3 chunk_mod;
                            chunk_mod.x = mod( chunk_pos.x, 2 * CHUNK_RADIUS_XZ + 1 );
                            chunk_mod.y = mod( chunk_pos.y, 2 * CHUNK_RADIUS_Y + 1 );
                            chunk_mod.z = mod( chunk_pos.z, 2 * CHUNK_RADIUS_XZ + 1 );

                            int slot = chunk_slot_from_mod( chunk_mod );
                            Chunk &chunk = this->chunkArray[ slot ];
                            chunk.chunk_pos = chunk_pos;
                            chunk.chunk_mod = chunk_mod;

                            if ( this->pos_in_window( camera_chuck, chunk_pos ) ) {
                                chunk.set_load_state( CHUNK_LOADING );
                                this->terrain_loading_thread.enqueue( &chunk, chunk.chunk_pos, 0 );
                            } else {
                                // Outside the active render window: park the
                                // slot until the radius grows over it. It has
                                // no cells, so re-entry enqueues a real load.
                                chunk.set_load_state( CHUNK_PARKED );
                            }
                            nextChunk = ( nextChunk + 1 );
                        }
                    }
                }
            }
        }
    }
    this->rebuild_drawable_list( );
    this->initial_chunk_request_pending = true;
}

void ChunkLoader::stamp_diff_request_box( const glm::ivec3 &center, const glm::ivec3 &box_min, const glm::ivec3 &box_size, uint32_t seq ) {
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        const glm::ivec3 np = slot_target_pos( this->chunkArray[ i ].chunk_mod, center );
        if ( np.x >= box_min.x && np.x < box_min.x + box_size.x && //
             np.y >= box_min.y && np.y < box_min.y + box_size.y && //
             np.z >= box_min.z && np.z < box_min.z + box_size.z ) {
            this->slot_diff_req[ i ] = np;
            this->slot_diff_seq[ i ] = seq;
        }
    }
}

// REPGAME_CHUNK_DIFF_GATE=0 restores pre-gate behaviour: chunks finalize as
// soon as terrain lands and late diffs apply on top of visible cells.
bool ChunkLoader::diff_gate_enabled( ) const {
    static const bool on = env_bool( "REPGAME_CHUNK_DIFF_GATE", true );
    return this->diff_gate_override >= 0 ? this->diff_gate_override != 0 : on;
}

// Is the diff request covering `slot`'s position fully answered? Offline
// (or with the gate off) everything is settled by definition; otherwise the
// slot must carry a stamp for its current position whose sequence the
// diff_boxes_done watermark has already passed.
bool ChunkLoader::chunk_settled( int slot_i, const Multiplayer &multiplayer ) const {
    if ( !this->diff_gate_enabled( ) || !multiplayer.is_active( ) ) {
        return true;
    }
    const Chunk &chunk = this->chunkArray[ slot_i ];
    return this->slot_diff_req[ slot_i ] == chunk.chunk_pos &&
           this->slot_diff_seq[ slot_i ] <= multiplayer.diff_boxes_done_count( );
}

// Bounded pass over the settle-wait list, run each frame after the terrain
// dequeue. A settled (or timed-out) entry re-runs the whole finalize; an
// unanswered one past SETTLE_REASK_US re-issues its request once (answer
// presumably lost); an entry whose slot moved on (retargeted LOADING,
// parked) is simply dropped. Entries that stay deferred cost only their
// scan slot — reveals are what the budget bounds.
void ChunkLoader::settle_scan( Multiplayer &multiplayer, const glm::ivec3 &center_next, long long budget_us ) {
    const long long t0 = now_us( );
    for ( size_t i = 0; i < this->settle_wait_list.size( ); ) {
        const SettleWait sw = this->settle_wait_list[ i ];
        Chunk &chunk = this->chunkArray[ sw.slot ];
        if ( chunk.load_state != CHUNK_SETTLE_WAIT ) {
            this->settle_wait_list[ i ] = this->settle_wait_list.back( );
            this->settle_wait_list.pop_back( );
            continue;
        }
        const long long deferred_for = t0 - sw.defer_us;
        const bool forced = deferred_for >= SETTLE_FORCE_US;
        const bool reveal = forced || this->chunk_settled( sw.slot, multiplayer );
        if ( !reveal ) {
            if ( !sw.reasked && deferred_for >= SETTLE_REASK_US &&
                 this->slot_diff_req[ sw.slot ] == chunk.chunk_pos &&
                 multiplayer.request_chunk( chunk.chunk_pos ) ) {
                this->slot_diff_seq[ sw.slot ] = multiplayer.diff_boxes_sent_count( );
                this->settle_wait_list[ i ].reasked = true;
            }
            i++;
            continue;
        }
        if ( forced ) {
            pr_debug( "Chunk (%d,%d,%d) settle wait timed out after %lldus; revealing with possibly missing diffs",
                      chunk.chunk_pos.x, chunk.chunk_pos.y, chunk.chunk_pos.z, deferred_for );
        }
        this->settle_wait_list[ i ] = this->settle_wait_list.back( );
        this->settle_wait_list.pop_back( );
        if ( mpload_trace_enabled( ) ) {
            mpload_tracef( "settle-reveal chunk=%d,%d,%d forced=%d",
                           chunk.chunk_pos.x, chunk.chunk_pos.y, chunk.chunk_pos.z, forced ? 1 : 0 );
        }
        this->finalize_loaded_chunk( chunk, multiplayer, center_next, true );
        if ( now_us( ) - t0 >= budget_us ) {
            break;
        }
    }
}

Chunk *ChunkLoader::get_chunk( const glm::ivec3 &chunk_pos ) const {
    const int slot = chunk_slot_from_pos( chunk_pos );
    Chunk &chunk = this->chunkArray[ slot ];
    if ( chunk.chunk_pos == chunk_pos ) {
        return &chunk;
    }
    return nullptr;
}

// A chunk's drawn face set is a pure function of (chunk_pos, center) via the
// 6-bit visibility mask, so the staleness test is the mask itself — not
// whether this move crossed the chunk's plane. The old prev/next-plane test
// missed crossings made while a slot was parked (the park window tests x/z
// together, so an x crossing slips by while z keeps the slot out; y is not
// window-tested at all) or during a move of more than one chunk. The stale
// slice then skipped the player-facing faces — see-through block sides —
// until a later move happened to land on the plane and recalc.
inline int ChunkLoader::process_chunk_position( Chunk &chunk, const glm::ivec3 &center_next, int force_reload ) {
    if ( force_reload || chunk_face_mask( chunk.chunk_pos, center_next ) != chunk.sides_mask ) {
        chunk.calculate_sides( center_next );
        return 1;
    }
    return 0;
}

void ChunkLoader::render_chunks( Multiplayer &multiplayer, const glm::dvec3 &camera_pos, int limit_render ) {
    this->num_remeshed_this_frame = 0;
    light_frame_begin( );
    // One wall-clock pool for the frame's streaming work — the budgeted
    // phases below each take min(their own cap, what remains). Sized by
    // queue depth (FRAME_WORK_*): idle frames stay tight for smoothness,
    // a backed-up machine widens the pool so deferred work keeps draining.
    int terrain_work_q = 0, terrain_results_q = 0, mesh_work_q = 0, mesh_results_q = 0;
    this->terrain_loading_thread.queue_sizes( &terrain_work_q, &terrain_results_q );
    this->terrain_loading_thread.mesh_queue_sizes( &mesh_work_q, &mesh_results_q );
    // Work-side queue depths count too: a deep work queue means results are
    // on the way, and the pool is only a ceiling — widening it costs
    // nothing when there is no work to spend it on.
    const int work_backlog = terrain_work_q + terrain_results_q + mesh_work_q + mesh_results_q +
                             this->num_repop_must + static_cast<int>( this->async_wanted_list.size( ) ) +
                             static_cast<int>( this->settle_wait_list.size( ) ) +
                             this->remesh_flag_backlog + light_upload_backlog( );
    const long long t_frame_work = now_us( );
    const long long frame_budget = std::min(
        FRAME_WORK_MAX_US, FRAME_WORK_MIN_US + FRAME_WORK_BACKLOG_US * static_cast<long long>( work_backlog ) );
    const auto frame_left = [ & ]( ) {
        return std::max<long long>( 0, frame_budget - ( now_us( ) - t_frame_work ) );
    };
    light_dbg_us_framepool += frame_budget;
    light_dbg_n_framepool++;
    glm::dvec3 chunk_size = glm::dvec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );

    glm::ivec3 chunk_pos = glm::floor( camera_pos / chunk_size );
    this->terrain_loading_thread.set_focus( chunk_pos, this->render_radius );
    glm::ivec3 &loaded_pos = this->chunk_center;
    glm::ivec3 chunk_diff = chunk_pos - loaded_pos;

    // Phase 2: fire one box request for the initial visible area instead of
    // per-chunk requests as terrain gen finishes. This overlaps the network
    // round-trip with GPU terrain gen.
    if ( this->initial_chunk_request_pending ) {
        const glm::ivec3 box_min = chunk_pos - glm::ivec3( this->render_radius, CHUNK_RADIUS_Y, this->render_radius );
        const glm::ivec3 box_size( 2 * this->render_radius + 1, 2 * CHUNK_RADIUS_Y + 1, 2 * this->render_radius + 1 );
        // Only consume the one-shot flag if the request actually went out.
        // On WASM the WebSocket may still be connecting when the first
        // frame renders; retry next frame rather than dropping the request.
        if ( multiplayer.request_chunks_box( box_min,
                                             static_cast<uint8_t>( box_size.x ),
                                             static_cast<uint8_t>( box_size.y ),
                                             static_cast<uint8_t>( box_size.z ),
                                             /* initial = */ true ) ) {
            this->initial_chunk_request_pending = false;
            this->stamp_diff_request_box( chunk_pos, box_min, box_size, multiplayer.diff_boxes_sent_count( ) );
        }
    }

    {
        Chunk *chunk_ptr;
        const long long t_budget_start = now_us( );
        const long long fin_limit = std::min<long long>(
            limit_render ? WASM_LOAD_BUDGET_US : CHUNK_FINALIZE_BUDGET_US, frame_left( ) );
        do {
            chunk_ptr = this->terrain_loading_thread.dequeue( );
            if ( chunk_ptr ) {
                Chunk &chunk = *chunk_ptr;
                // load_state stays CHUNK_LOADING through the bounds check —
                // reload_if_out_of_bounds owns all transitions (re-enqueue,
                // park as CHUNK_PARKED, or return for finalize).
                glm::ivec3 new_pos;
                int reloaded = reload_if_out_of_bounds( chunk, chunk_pos, &new_pos );
                if ( !reloaded ) {
                    this->finalize_loaded_chunk( chunk, multiplayer, chunk_pos );
                } else if ( reloaded == 1 ) {
                    // The re-enqueue target is the slot's current position —
                    // which may have entered the window while this slot was
                    // in-flight and so was never covered by a request box.
                    const int slot_i = static_cast<int>( &chunk - this->chunkArray );
                    if ( multiplayer.request_chunk( new_pos ) ) {
                        this->slot_diff_req[ slot_i ] = new_pos;
                        this->slot_diff_seq[ slot_i ] = multiplayer.diff_boxes_sent_count( );
                    }
                }
            }
            // When limit_render is true (WASM without pthreads), terrain is
            // generated inline in dequeue() on the main thread, so cap the work
            // per frame to a time budget. On native the budget bounds the
            // per-chunk GL finalize work so load-in bursts don't hitch frames.
        } while ( chunk_ptr && ( now_us( ) - t_budget_start < fin_limit ) );
        light_dbg_us_finalize += now_us( ) - t_budget_start;
    }
    // Reveal settle-wait chunks whose covering request finished since the
    // last frame (same budget class as the finalize dequeue above — a burst
    // of DONEs streams in over a few frames rather than hitching).
    if ( !this->settle_wait_list.empty( ) ) {
        this->settle_scan( multiplayer, chunk_pos, std::min<long long>( CHUNK_FINALIZE_BUDGET_US, frame_left( ) ) );
    }
    // Drain deferred park releases whose defer window elapsed (PARK_RELEASE_
    // DEFER_FRAMES). A stale entry (gen mismatch or no longer parked) means
    // the slot re-entered and re-parked — drop it, the newer entry owns the
    // release timing.
    this->park_frame++;
    // Consumed only under __GLIBC__ below for the malloc_trim throttle.
    size_t released_this_frame [[maybe_unused]] = 0;
    for ( size_t i = 0; i < this->parked_release.size( ); ) {
        const ParkedRelease &e = this->parked_release[ i ];
        if ( e.chunk->load_state == CHUNK_PARKED && e.gen == e.chunk->park_release_gen &&
             this->park_frame - e.frame >= PARK_RELEASE_DEFER_FRAMES ) {
            e.chunk->release_terrain( );
            released_this_frame++;
        }
        if ( e.chunk->load_state != CHUNK_PARKED || e.gen != e.chunk->park_release_gen ||
             this->park_frame - e.frame >= PARK_RELEASE_DEFER_FRAMES ) {
            this->parked_release[ i ] = this->parked_release.back( );
            this->parked_release.pop_back( );
        } else {
            i++;
        }
    }
#ifdef __GLIBC__
    // free() parks the released buffers in glibc's arenas — RSS only drops
    // via mmap munmaps or a trim. malloc_trim(0) madvises free pages back
    // to the kernel so render-distance shrinks are visible in system memory
    // rather than staying an internal high-water mark. It's a heap walk, so
    // throttle to once per second of frames when releases actually ran.
    static long last_trim_frame = 0;
    if ( released_this_frame && this->park_frame - last_trim_frame >= 60 ) {
        last_trim_frame = this->park_frame;
        malloc_trim( 0 );
    }
#endif
    const bool radius_changed = this->render_radius != this->render_radius_applied;
    if ( 0 != chunk_diff.x || 0 != chunk_diff.y || 0 != chunk_diff.z || radius_changed ) {
        // Collect new chunk positions from the reload loop so we can fire one
        // box request for all of them at once (Phase 2).
        std::vector<glm::ivec3> new_positions;
        this->light_heal_budget = LIGHT_HEAL_PER_SHIFT;
        for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
            Chunk &chunk = this->chunkArray[ i ];
            if ( chunk.load_state == CHUNK_SETTLE_WAIT ) {
                // A deferred slot tracks window moves like LOADING (retarget
                // re-enqueues, leaving the window parks) — the now-stale
                // settle_wait_list entry drops itself on the next scan.
                // Same-target slots only need their coverage kept fresh.
                glm::ivec3 new_pos;
                const int reloaded = reload_if_out_of_bounds( chunk, chunk_pos, &new_pos );
                if ( reloaded == 0 && this->slot_diff_req[ i ] != chunk.chunk_pos ) {
                    new_positions.push_back( chunk.chunk_pos );
                } else if ( reloaded == 1 ) {
                    new_positions.push_back( new_pos );
                }
                continue;
            }
            if ( chunk.load_state == CHUNK_LOADING ) {
                // A worker retargets an in-flight load to the slot's
                // CURRENT position at pick-up, so a chunk can land on a
                // position that entered the window while the slot was busy
                // — a position reload_if_out_of_bounds never sees. Keep its
                // target covered: join it to this shift's diff-request box
                // unless an earlier request already covered it.
                const glm::ivec3 wanted = slot_target_pos( chunk.chunk_mod, chunk_pos );
                if ( this->pos_in_window( chunk_pos, wanted ) && this->slot_diff_req[ i ] != wanted ) {
                    new_positions.push_back( wanted );
                }
                continue;
            }
            glm::ivec3 new_pos;
            int reloaded = reload_if_out_of_bounds( chunk, chunk_pos, &new_pos );
            if ( reloaded == 0 ) {
                process_chunk_position( chunk, chunk_pos, 0 );
                // Far-cap stale flag heals as the chunk re-enters near radius.
                this->light_heal_chunk( chunk, chunk_pos );
                // A live chunk at a position no request ever covered (a
                // dropped pre-connect box, or any coverage hole) re-asks on
                // the next shift instead of staying natural terrain forever.
                if ( this->slot_diff_req[ i ] != chunk.chunk_pos ) {
                    new_positions.push_back( chunk.chunk_pos );
                }
            } else if ( reloaded == 1 ) {
                new_positions.push_back( new_pos );
            }
        }
        // Fire one box request covering all newly loaded chunks. The box may
        // include chunks that don't need loading; the server just returns no
        // diffs for those, so it's harmless.
        if ( !new_positions.empty( ) ) {
            glm::ivec3 box_min = new_positions[ 0 ];
            glm::ivec3 box_max = new_positions[ 0 ];
            for ( const auto &p : new_positions ) {
                box_min = glm::min( box_min, p );
                box_max = glm::max( box_max, p );
            }
            glm::ivec3 box_size = box_max - box_min + glm::ivec3( 1 );
            // Clamp to NET_MAX_BOX_DIM per axis (shouldn't exceed on any platform,
            // but guard against pathological cases).
            uint8_t sx = static_cast<uint8_t>( std::min( box_size.x, static_cast<int>( NET_MAX_BOX_DIM ) ) );
            uint8_t sy = static_cast<uint8_t>( std::min( box_size.y, static_cast<int>( NET_MAX_BOX_DIM ) ) );
            uint8_t sz = static_cast<uint8_t>( std::min( box_size.z, static_cast<int>( NET_MAX_BOX_DIM ) ) );
            if ( multiplayer.request_chunks_box( box_min, sx, sy, sz ) ) {
                this->stamp_diff_request_box( chunk_pos, box_min, glm::ivec3( sx, sy, sz ), multiplayer.diff_boxes_sent_count( ) );
            }
        }
        this->chunk_center = chunk_pos;
        this->render_radius_applied = this->render_radius;
        // Jobs queued while this region was far must catch up with the
        // player — promote them into the near lane (bounded single pass).
        this->light_reprioritize_jobs( );
    }
    this->rebuild_drawable_list( );
    const long long t_remesh_start = now_us( );
    // Async server-diff remesh: submit newly marked chunks (snapshot copies
    // happen here on the render thread) and publish finished worker jobs
    // all-or-nothing per job. Both phases' render-thread cost counts toward
    // light_dbg_us_remesh with the sync remesh loops below.
    if ( this->async_remesh_enabled( ) ) {
        this->async_submit_jobs( std::min<long long>( CHUNK_REMESH_BUDGET_US, frame_left( ) ) );
        this->async_publish_results( std::min<long long>( CHUNK_REMESH_BUDGET_US, frame_left( ) ) );
    }
    // Drain the must-remesh queue first: edits enqueue every chunk they
    // dirty so a boundary edit's meshes update together in this pass. Past
    // its share of the frame pool leftovers are just popped —
    // needs_repopulation stays set and the budgeted scan below picks them
    // up like any backlog.
    const long long t_must_start = now_us( );
    const long long must_limit = std::min<long long>( REPOP_MUST_BUDGET_US, frame_left( ) );
    for ( int i = 0; i < this->num_repop_must; i++ ) {
        Chunk &chunk = *this->repop_must[ i ];
        chunk.repop_must_queued = 0;
        if ( chunk.needs_repopulation && !chunk.load_state && //
             !( chunk.cached_cull_normal && chunk.cached_cull_reflect ) && //
             now_us( ) - t_must_start < must_limit ) {
            this->remesh_group( chunk );
        }
    }
    this->num_repop_must = 0;
    // The backlog scan gets its own share: while the must-queue drains,
    // flag-only chunks (interior diff cells) would otherwise starve until
    // the queue empties.
    const long long t_scan_start = now_us( );
    const long long scan_limit = std::min<long long>( CHUNK_REMESH_BUDGET_US, frame_left( ) );
    int scan_stopped = 0;
    int flag_left = 0;
    for ( int i = 0; i < this->num_drawable; i++ ) {
        Chunk &chunk = *this->drawable_chunks[ i ];
        if ( chunk.needs_repopulation && !( chunk.cached_cull_normal && chunk.cached_cull_reflect ) ) {
            // Async-marked or in-flight chunks are owned by the async path —
            // the scan must not remesh them ahead of their job.
            if ( chunk.async_wanted || this->async_member_in_flight( chunk ) ) {
                continue;
            }
            // Past the remesh share the flagged remainder is only COUNTED
            // (cheap flag test, no remesh): remesh_flag_backlog feeds next
            // frame's pool size so a backed-up scan widens the allowance.
            if ( scan_stopped ) {
                flag_left++;
                continue;
            }
            this->remesh_group( chunk );
            // Remaining chunks keep needs_repopulation set and render their
            // old mesh until a later frame re-meshes them.
            if ( now_us( ) - t_scan_start >= scan_limit ) {
                scan_stopped = 1;
            }
        }
    }
    this->remesh_flag_backlog = flag_left;
    light_dbg_us_remesh += now_us( ) - t_remesh_start;
    // Light mutation lives on the lighting thread when one runs; the inline
    // path below is the no-thread fallback (tests, WASM without pthreads).
    if ( !ChunkLoader::light_async_active( ) ) {
        const long long t_drain = now_us( );
        this->light_drain_pending( );
        light_dbg_us_drain += now_us( ) - t_drain;
        // Scale the propagation cap with backlog: under-overhang/cave floods
        // legitimately cover every underground air cell within ~15 of lit
        // space, which is millions of steps during mass load-in. The cap is
        // per column slot, so each slot gets min(cap, pool remaining) —
        // passing the whole remainder to the all-columns variant would let
        // the phase spend it once per slot.
        const size_t light_backlog = this->light_queue_sizes( );
        const long long light_slot_cap =
            light_backlog > 200000 ? 8000 : light_backlog > 20000 ? 3000 : LIGHT_PROPAGATE_BUDGET_US;
        const long long t_bfs = now_us( );
        for ( int s = 0; s < LIGHT_COL_QUEUE_COUNT; s++ ) {
            this->light_process_queue( std::min<long long>( light_slot_cap, frame_left( ) ), s );
        }
        light_dbg_us_bfs += now_us( ) - t_bfs;
    }
    // GL uploads stay render-thread-owned regardless of who marked dirty.
    this->light_upload_dirty( LIGHT_MAX_UPLOADS_PER_FRAME, frame_left( ) );
    // Idle heal for far-cap stale flags; no-ops while the queue is busy.
    this->light_heal_idle( );
    this->rebuild_drawable_list( );

    // MPLOAD trace: per-second load-in summary while the gate is on.
    if ( mpload_trace_enabled( ) ) {
        static double last_stats_ms = 0.0;
        static long stats_frames = 0;
        stats_frames++;
        const double now_ms = mpload_now_ms( );
        if ( now_ms - last_stats_ms >= 1000.0 ) {
            int remesh_backlog = 0;
            for ( int i = 0; i < this->num_drawable; i++ ) {
                remesh_backlog += this->drawable_chunks[ i ]->needs_repopulation ? 1 : 0;
            }
            int loading = 0;
            for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
                loading += this->chunkArray[ i ].load_state == CHUNK_LOADING ? 1 : 0;
            }
            mpload_tracef( "frame_stats fps=%ld remesh_backlog=%d pending_diffs=%zu loading=%d settle_wait=%zu",
                           stats_frames, remesh_backlog, multiplayer.pending_diffs_count( ), loading,
                           this->settle_wait_list.size( ) );
            stats_frames = 0;
            last_stats_ms = now_ms;
        }
    }
}

void ChunkLoader::repop_must_enqueue( Chunk &chunk ) {
    // repop_must is null for hand-built loaders (tests) that never ran init;
    // those exercise the flag-driven scan path only.
    if ( this->repop_must == nullptr || chunk.repop_must_queued ) {
        return;
    }
    chunk.repop_must_queued = 1;
    this->repop_must[ this->num_repop_must++ ] = &chunk;
}

// The remesh_now work shared by the sync path and (steps identical, mesh
// pre-built) async_apply_member's GL-free twin. Render thread only.
// gl_upload=false is the unit-test path (no GL context): the CPU remesh and
// flags still run, the GPU upload is skipped.
void ChunkLoader::remesh_now_body( Chunk &chunk, bool gl_upload ) {
    if ( chunk.mpload_diff_ms != 0 ) {
        // Diff-to-remesh latency for the live-apply path (MPLOAD trace).
        mpload_tracef( "diff_remesh chunk=%d,%d,%d wait_ms=%lld",
                       chunk.chunk_pos.x, chunk.chunk_pos.y, chunk.chunk_pos.z,
                       mpload_now_ms_i( ) - chunk.mpload_diff_ms );
        chunk.mpload_diff_ms = 0;
    }
    chunk.unprogram_terrain( );
    chunk.calculate_populated_blocks( );
    if ( gl_upload ) {
        chunk.program_terrain( );
    }
    chunk.needs_repopulation = false;
    this->num_remeshed_this_frame++;
}

void ChunkLoader::remesh_now( Chunk &chunk ) {
    this->remesh_now_impl( chunk, true );
}

void ChunkLoader::remesh_now_impl( Chunk &chunk, bool gl_upload ) {
    // Seam safety vs the async path: a synchronous remesh (own edits,
    // BLOCK_UPDATE, promoted jobs) landing on a chunk that is async-marked
    // or part of an in-flight job must take the whole dirty async component
    // along — otherwise a chunk sharing an edited border could swap meshes
    // in a different frame and show a gap. Flood over link edges (a mirror
    // write is the only thing that couples two chunks' meshes), then remesh
    // them all in this same pass.
    if ( ( chunk.async_wanted || this->async_member_in_flight( chunk ) ) ) {
        const uint32_t gen = ++this->async_visit_gen;
        std::vector<Chunk *> group;
        this->async_dirty_component( &chunk, true, gen, group );
        for ( Chunk *c : group ) {
            // Clearing async_wanted keeps the submission pass from folding
            // the chunk into a new job; its in-flight job result then no-ops
            // at publish because needs_repopulation is already false.
            c->async_wanted = 0;
            if ( c->needs_repopulation ) {
                this->remesh_now_body( *c, gl_upload );
            }
        }
        for ( Chunk *c : group ) {
            this->async_clear_satisfied_links( *c, gen );
        }
        this->async_jobs_promoted++;
        return;
    }
    this->remesh_now_body( chunk, gl_upload );
}

// A chunk's mesh is only consistent with the chunk on the other side of
// each of its 6 faces: a boundary write dirties both arrays, so remeshing
// one side without the other leaves a see-through gap until the second
// catches up — which the remesh budget happily splits across frames when
// the dirty list is flooded (load-in diffs). Always pull dirty
// face-neighbors into the same pass; diagonal neighbors are skipped because
// no face spans a diagonal chunk boundary. Neighbors already remeshed via
// their own queue/scan entry just have needs_repopulation clear and skip.
void ChunkLoader::remesh_group( Chunk &chunk ) {
    this->remesh_now( chunk );
    static const glm::ivec3 face_dirs[ 6 ] = { glm::ivec3( 1, 0, 0 ),  glm::ivec3( -1, 0, 0 ), //
                                               glm::ivec3( 0, 1, 0 ),  glm::ivec3( 0, -1, 0 ), //
                                               glm::ivec3( 0, 0, 1 ),  glm::ivec3( 0, 0, -1 ) };
    for ( const glm::ivec3 &dir : face_dirs ) {
        Chunk *neighbor = this->get_chunk( chunk.chunk_pos + dir );
        if ( neighbor != nullptr && neighbor->needs_repopulation && !neighbor->load_state ) {
            this->remesh_now( *neighbor );
        }
    }
}

// ---- Async server-diff remesh ---------------------------------------------
// Server chunk diffs (CHUNK_DIFF_RESULT, pending diffs at finalize) mark
// their touched chunks via async_remesh_mark instead of repop_must, and
// each frame render_chunks (a) groups the marked chunks into link-connected
// components, snapshots cells/extras + cell_version and enqueues one MeshJob
// per component on the terrain workers (async_submit_jobs), then (b) applies
// finished jobs whose snapshots still match (async_publish_results). A job
// publishes all-or-nothing so two chunks sharing an edited border always
// swap meshes in the same frame — no seam gaps.

bool ChunkLoader::async_remesh_enabled( ) const {
    // Hot path: apply_diff_block calls this once per diff cell — read the
    // env killswitch once. Tests use async_remesh_override instead.
    static const bool env_enabled = env_bool( "REPGAME_ASYNC_REMESH", true );
    return env_enabled &&
           ( this->async_remesh_override >= 0 ? this->async_remesh_override != 0
                                             : this->terrain_loading_thread.has_workers( ) );
}

void ChunkLoader::async_remesh_mark( Chunk &chunk ) {
    if ( chunk.load_state != CHUNK_LIVE || chunk.async_wanted ) {
        return;
    }
    chunk.async_wanted = 1;
    this->async_wanted_list.push_back( &chunk );
}

bool ChunkLoader::async_member_in_flight( const Chunk &chunk ) const {
    return chunk.async_job_seq != 0 && this->async_in_flight_seqs.count( chunk.async_job_seq ) != 0;
}

// BFS over Chunk::async_links edges (mirror-write couplings only — plain
// adjacency must never group: interior-only diffs in neighbouring chunks
// share no seam). Neighbours need LIVE && needs_repopulation; async_only
// additionally requires async_wanted || async_member_in_flight — the
// submission flood must pull in-flight members too: folding them into the
// newer job (new seq) is what keeps a superseded job's discard from leaving
// a half-published seam. `gen` is the visit stamp (dedup; the callers also
// use it to recognize group members when clearing links).
void ChunkLoader::async_dirty_component( Chunk *seed, bool async_only, uint32_t gen, std::vector<Chunk *> &out ) {
    seed->async_visit = gen;
    out.push_back( seed );
    for ( size_t i = 0; i < out.size( ); i++ ) {
        uint32_t links = out[ i ]->async_links;
        while ( links ) {
            const int d = __builtin_ctz( links );
            links &= links - 1;
            if ( d == 13 ) {
                continue; // self index — never a link
            }
            Chunk *n = this->get_chunk( out[ i ]->chunk_pos + async_dir_from_index( d ) );
            if ( n == nullptr || n->async_visit == gen || n->load_state != CHUNK_LIVE || !n->needs_repopulation ) {
                continue;
            }
            if ( async_only && !( n->async_wanted || this->async_member_in_flight( *n ) ) ) {
                continue;
            }
            n->async_visit = gen;
            out.push_back( n );
        }
    }
}

// A publish/promotion satisfied the seam coupling: drop link bits on `c`
// (and the symmetric bits on partners) whose partner is in the same group
// (visit stamp `gen`) or no longer dirty. Links toward a partner that still
// needs_repopulation outside this group survive.
void ChunkLoader::async_clear_satisfied_links( Chunk &c, uint32_t gen ) {
    uint32_t links = c.async_links;
    while ( links ) {
        const int d = __builtin_ctz( links );
        links &= links - 1;
        if ( d == 13 ) {
            continue;
        }
        const glm::ivec3 dir = async_dir_from_index( d );
        Chunk *p = this->get_chunk( c.chunk_pos + dir );
        if ( p != nullptr && ( p->async_visit == gen || !p->needs_repopulation ) ) {
            c.async_links &= ~( 1u << d );
            p->async_links &= ~( 1u << async_dir_index( -dir ) );
        }
    }
}

// Group the marked chunks into link-connected components and enqueue one
// MeshJob per component. Render thread only — all snapshots (and their
// later frees) stay on this thread.
int ChunkLoader::async_submit_jobs( const long long budget_us ) {
    if ( this->async_wanted_list.empty( ) ) {
        return 0;
    }
    std::vector<Chunk *> wanted;
    wanted.swap( this->async_wanted_list );
    // Drop entries that are no longer live (parked/reloaded since the mark).
    for ( Chunk *c : wanted ) {
        if ( c->load_state != CHUNK_LIVE ) {
            c->async_wanted = 0;
        }
    }
    // Nearest first: the worker FIFO then meshes the player's surroundings
    // ahead of far chunks during load-in.
    const glm::ivec3 center = this->chunk_center;
    std::sort( wanted.begin( ), wanted.end( ), [ center ]( const Chunk *a, const Chunk *b ) {
        const glm::ivec3 da = a->chunk_pos - center;
        const glm::ivec3 db = b->chunk_pos - center;
        return da.x * da.x + da.y * da.y + da.z * da.z < db.x * db.x + db.y * db.y + db.z * db.z;
    } );
    int submitted = 0;
    const long long t_submit = now_us( );
    for ( Chunk *seed : wanted ) {
        if ( !seed->async_wanted || !seed->needs_repopulation ) {
            seed->async_wanted = 0;
            continue; // parked above, claimed by an earlier component, or no
            // longer dirty (a sync remesh beat the submission)
        }
        // A component is never split — the budget only stops STARTING new
        // ones; leftover seeds keep async_wanted and are re-listed below.
        if ( submitted >= 1 && now_us( ) - t_submit >= budget_us ) {
            break;
        }
        std::vector<Chunk *> component;
        this->async_dirty_component( seed, true, ++this->async_visit_gen, component );
        MeshJob job;
        job.seq = ++this->async_next_seq;
        for ( Chunk *c : component ) {
            MeshJobMember m = {};
            m.chunk = c;
            m.chunk_pos = c->chunk_pos;
            m.cell_version = c->cell_version.load( std::memory_order_relaxed );
            if ( c->cells != nullptr ) {
                m.cells = chunk_alloc_cells( );
                memcpy( m.cells, c->cells, CHUNK_BLOCK_SIZE * sizeof( BlockCell ) );
            }
            if ( c->extras != nullptr ) {
                m.extras = chunk_alloc_extras( );
                memcpy( m.extras, c->extras, CHUNK_BLOCK_SIZE * sizeof( BlockExtra ) );
            }
            c->async_job_seq = job.seq;
            c->async_wanted = 0;
            job.members.push_back( m );
        }
        this->async_in_flight_seqs.insert( job.seq );
        this->async_jobs_submitted++;
        this->terrain_loading_thread.mesh_enqueue( std::move( job ) );
        submitted++;
    }
    // Seeds not reached before the budget stop (or claimed by nothing) keep
    // their flags — re-list them for next frame.
    for ( Chunk *c : wanted ) {
        if ( c->async_wanted ) {
            this->async_wanted_list.push_back( c );
        }
    }
    return submitted;
}

void ChunkLoader::async_publish_results( const long long budget_us ) {
    const long long t_start = now_us( );
    MeshJob job;
    while ( this->terrain_loading_thread.mesh_pop_result( &job ) ) {
        this->async_in_flight_seqs.erase( job.seq );
        this->async_publish_job( job, true );
        // Never split a job, but stop STARTING new publishes once the remesh
        // share is spent — leftovers drain next frame.
        if ( now_us( ) - t_start >= budget_us ) {
            break;
        }
    }
}

// Publish phase 1: unprogram the old mesh and move the member's prebuilt
// result into its layers. Phase 2 (async_upload_member) then uploads — the
// split keeps the two phases batchable so a job swaps every member's mesh
// with no intermediate state visible (all-or-nothing in one frame).
void ChunkLoader::async_apply_member( Chunk &chunk, MeshJobMember &m ) {
    if ( chunk.mpload_diff_ms != 0 ) {
        mpload_tracef( "diff_remesh chunk=%d,%d,%d wait_ms=%lld",
                       chunk.chunk_pos.x, chunk.chunk_pos.y, chunk.chunk_pos.z,
                       mpload_now_ms_i( ) - chunk.mpload_diff_ms );
        chunk.mpload_diff_ms = 0;
    }
    chunk.unprogram_terrain( );
    chunk.ensure_layers( );
    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        chunk.layers[ renderOrder ].populated_blocks = m.result.blocks[ renderOrder ];
        chunk.layers[ renderOrder ].num_instances = m.result.num[ renderOrder ];
        m.result.blocks[ renderOrder ] = nullptr;
    }
    chunk.needs_repopulation = false;
    this->num_remeshed_this_frame++;
}

// Publish phase 2 — the same steps remesh_now_body performs around
// calculate_populated_blocks.
void ChunkLoader::async_upload_member( Chunk &chunk, bool gl_upload ) {
    if ( gl_upload ) {
        chunk.program_terrain( );
        // Finalize-deferred chunks (path 1) never got a light texture —
        // create it now that they are about to draw.
        if ( chunk.should_render && chunk.light.texture == 0 ) {
            chunk.light_ensure_texture( this );
        }
    } else {
        // GL-free publish (unit tests): program_terrain would also set
        // should_render for non-empty layers and free populated_blocks —
        // mirror the flag so callers can observe the outcome, keep the
        // arrays for the test to inspect/free.
        for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
            if ( chunk.layers[ renderOrder ].num_instances != 0 ) {
                chunk.set_should_render( 1 );
            }
        }
    }
}

void ChunkLoader::async_publish_job( MeshJob &job, bool gl_upload ) {
    // Valid only if EVERY member still matches its snapshot — a write to any
    // of them (bumped cell_version), a park, or a slot reuse discards the
    // whole job so no seam neighbour can desync.
    bool valid = true;
    for ( MeshJobMember &m : job.members ) {
        Chunk &c = *m.chunk;
        if ( c.load_state != CHUNK_LIVE || c.chunk_pos != m.chunk_pos ||
             c.cell_version.load( std::memory_order_relaxed ) != m.cell_version ) {
            valid = false;
            break;
        }
    }
    if ( valid ) {
        this->async_jobs_published++;
        // Phase 1: unprogram every member + move the built meshes into their
        // layers — nothing uploads yet, so no member is ever visible in a
        // state mid-swap.
        std::vector<Chunk *> applied;
        for ( MeshJobMember &m : job.members ) {
            Chunk &c = *m.chunk;
            if ( c.async_job_seq == job.seq && c.needs_repopulation ) {
                this->async_apply_member( c, m );
                c.async_discards = 0;
                applied.push_back( &c );
            }
        }
        // Phase 2: upload all members (GL) — same frame as phase 1, so every
        // seam pair flips together.
        for ( Chunk *c : applied ) {
            this->async_upload_member( *c, gl_upload );
        }
        // The publish satisfied the seam couplings: stamp the applied group
        // and drop link bits toward group members / non-dirty partners.
        const uint32_t gen = ++this->async_visit_gen;
        for ( Chunk *c : applied ) {
            c->async_visit = gen;
        }
        for ( Chunk *c : applied ) {
            this->async_clear_satisfied_links( *c, gen );
        }
        for ( MeshJobMember &m : job.members ) {
            // Skipped members (already current, or folded into a newer job)
            // free their unused result arrays here.
            for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
                free( m.result.blocks[ renderOrder ] );
                m.result.blocks[ renderOrder ] = nullptr;
            }
            chunk_free_cells( m.cells );
            chunk_free_extras( m.extras );
            m.cells = nullptr;
            m.extras = nullptr;
        }
        job.members.clear( );
        return;
    }
    this->async_jobs_discarded++;
    bool promote = false;
    for ( MeshJobMember &m : job.members ) {
        Chunk &c = *m.chunk;
        for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
            free( m.result.blocks[ renderOrder ] );
            m.result.blocks[ renderOrder ] = nullptr;
        }
        chunk_free_cells( m.cells );
        chunk_free_extras( m.extras );
        m.cells = nullptr;
        m.extras = nullptr;
        if ( c.async_job_seq == job.seq && c.load_state == CHUNK_LIVE && c.needs_repopulation ) {
            if ( ++c.async_discards >= 3 ) {
                // Kept losing snapshots — stop retrying async and remesh the
                // dirty component synchronously (all members swap in this
                // same pass below, seam-safe).
                promote = true;
            } else {
                this->async_remesh_mark( c );
            }
        }
    }
    if ( promote ) {
        // Remesh each promoted component synchronously right here — queueing
        // through repop_must would let the REPOP_MUST_BUDGET_US cap split a
        // component across frames (leftovers drain via the backlog scan),
        // which is exactly the seam gap the async path exists to prevent.
        // Members remeshed by an earlier component have needs_repopulation
        // clear and are skipped.
        for ( MeshJobMember &m : job.members ) {
            Chunk &c = *m.chunk;
            if ( c.load_state == CHUNK_LIVE && c.needs_repopulation && c.async_discards >= 3 ) {
                const uint32_t gen = ++this->async_visit_gen;
                std::vector<Chunk *> component;
                this->async_dirty_component( &c, false, gen, component );
                for ( Chunk *p : component ) {
                    p->async_discards = 0;
                    p->async_wanted = 0;
                    if ( p->needs_repopulation ) {
                        this->remesh_now_body( *p, gl_upload );
                    }
                }
                for ( Chunk *p : component ) {
                    this->async_clear_satisfied_links( *p, gen );
                }
                this->async_jobs_promoted++;
            }
        }
    }
}

// Path 1 (finalize): pending diffs dirtied a chunk whose worker just meshed
// it — that mesh is now stale. Discard it and hold the chunk out of the
// drawable list until its async job publishes (publish also creates the
// light texture the skipped ensure below would have made).
void ChunkLoader::finalize_defer_mesh( Chunk &chunk ) {
    chunk.unprogram_terrain( );
    // set_block_by_index_if_different flipped should_render on for the new
    // content; the chunk must stay undrawn until publish.
    chunk.set_should_render( 0 );
    this->async_remesh_mark( chunk );
}

void ChunkLoader::apply_diff_block( Chunk &chunk, int index, const BlockState *blockState ) {
    const bool async = this->async_remesh_enabled( );
    if ( !chunk.set_block_by_index_if_different( index, blockState ) ) {
        return;
    }
    // Async server-diff remesh: the write set needs_repopulation even for a
    // fully interior diff, so mark the chunk itself here — the sync path
    // only ever queues it when a border mirror dirtied a neighbour.
    if ( async ) {
        this->async_remesh_mark( chunk );
    }
    // A server diff (or queued local diff) is a player edit: mark the chunk
    // so natural mobs standing in it count as penned (MobManager::tick).
    chunk.sidecars.player_edited = 1;
    // Stamp the live-apply path for the diff→remesh latency trace
    // (mpload_diff_ms). First write wins; remesh_now reads and clears it.
    if ( mpload_trace_enabled( ) && chunk.mpload_diff_ms == 0 && chunk.load_state == CHUNK_LIVE ) {
        chunk.mpload_diff_ms = mpload_now_ms_i( );
    }
    // The same world cell is also stored in a neighbor chunk whenever pos
    // sits on a boundary: interior cells at 0/size-1 live in the neighbor's
    // halo, halo cells at -1/size live in the neighbor's interior. Diffs
    // arrive per chunk, so without this mirror each side of a modified seam
    // remeshed whenever its own diff landed under the remesh budget — the
    // stale side showed a see-through gap for those frames (the load-in
    // version of the boundary-edit artifact).
    int x, y, z;
    Chunk::get_coords_from_index( index, x, y, z );
    const glm::ivec3 pos( x, y, z );
    glm::ivec3 lo( 0 ), hi( 0 );
    lo.x = ( x <= 0 ) ? -1 : 0;
    hi.x = ( x >= CHUNK_SIZE_X - 1 ) ? 1 : 0;
    lo.y = ( y <= 0 ) ? -1 : 0;
    hi.y = ( y >= CHUNK_SIZE_Y - 1 ) ? 1 : 0;
    lo.z = ( z <= 0 ) ? -1 : 0;
    hi.z = ( z >= CHUNK_SIZE_Z - 1 ) ? 1 : 0;
    if ( lo == glm::ivec3( 0 ) && hi == glm::ivec3( 0 ) ) {
        return; // Fully interior — only this chunk's own mesh cares.
    }
    const glm::ivec3 sizes( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    int enqueued_neighbor = 0;
    for ( int i = lo.x; i <= hi.x; i++ ) {
        for ( int j = lo.y; j <= hi.y; j++ ) {
            for ( int k = lo.z; k <= hi.z; k++ ) {
                if ( i == 0 && j == 0 && k == 0 ) {
                    continue;
                }
                Chunk *neighbor = this->get_chunk( chunk.chunk_pos + glm::ivec3( i, j, k ) );
                if ( neighbor == nullptr || neighbor->load_state ) {
                    // Absent/loading chunks get the same cell from their own
                    // diff stream (the server mirrors boundary writes into
                    // every affected chunk's record).
                    continue;
                }
                const glm::ivec3 mirror = pos - glm::ivec3( i, j, k ) * sizes;
                if ( neighbor->set_block_by_index_if_different( Chunk::get_index_from_coords( mirror ), blockState ) ) {
                    // The diff write actually crossed this shared border —
                    // record the seam-coupling edge in both directions. Only
                    // mirror writes create links: interior-only diffs in
                    // adjacent chunks must NOT remesh as one component.
                    const glm::ivec3 dir( i, j, k );
                    chunk.async_links |= 1u << async_dir_index( dir );
                    neighbor->async_links |= 1u << async_dir_index( -dir );
                }
                if ( neighbor->needs_repopulation ) {
                    if ( async ) {
                        this->async_remesh_mark( *neighbor );
                    } else {
                        this->repop_must_enqueue( *neighbor );
                        enqueued_neighbor = 1;
                    }
                }
            }
        }
    }
    if ( enqueued_neighbor ) {
        this->repop_must_enqueue( chunk );
    }
}

void ChunkLoader::process_random_ticks( ) {
}

void ChunkLoader::cleanup( MapStorage &map_storage ) {
    this->terrain_loading_thread.stop( );
    // Join the light thread (draining queued work inline) before chunk.destroy
    // frees the light volumes it may still reference.
    this->light_thread_stop( );
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        Chunk &chunk = this->chunkArray[ i ];
        // The threads are joined, so every slot's chunk_pos/data pair is
        // stable — including dirty chunks parked for the queue ride whose
        // persist never ran. persist_chunk no-ops on clean/never-loaded
        // chunks.
        chunk.persist( map_storage );
        chunk.destroy( );
    }
    free( this->chunkArray );
    free( this->drawable_chunks );
    free( this->blended_chunks );
    free( this->slot_want );
    free( this->slot_listed );
    free( this->repop_must );
    this->occl_vb.destroy( );
    this->occl_va.destroy( );
    this->occl_shader.destroy( );
}