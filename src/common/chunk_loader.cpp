#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include <chrono>
#include <vector>

#include "common/RepGame.hpp"
#include "common/chunk_loader.hpp"
#include "common/multiplayer.hpp"
#include "common/net/packet.hpp"
#include "common/utils/map_storage.hpp"
#include "common/utils/terrain_loading_thread.hpp"
#include "common/renderer/shader.hpp"
#include "common/constants.hpp"

MK_SHADER( chunk_vertex );
MK_SHADER( chunk_fragment );
MK_SHADER( occl_vertex );
MK_SHADER( occl_fragment );

// Hardware occlusion culling via per-chunk AABB queries. On by default on
// all platforms — the query API is GLES3/WebGL2 core — with
// REPGAME_OCCLUSION=0 as a kill switch where env vars exist. Mobile
// tile-based GPUs may stall on query boundaries; if that regresses, this
// needs a per-platform default again.
static int occlusion_enabled( ) {
    static int enabled = -1;
    if ( enabled < 0 ) {
        const char *env = getenv( "REPGAME_OCCLUSION" );
        enabled = env ? atoi( env ) != 0 : 1;
    }
    return enabled;
}

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

static inline long long now_us( ) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now( ).time_since_epoch( ) ).count( );
}

// TEMP diagnostics: phase timing accumulators (defined in light.cpp, printed
// by light_dbg_stats every ~2s from the LIGHT probe).
extern long long light_dbg_us_finalize, light_dbg_us_remesh, light_dbg_us_drain,
    light_dbg_us_bfs, light_dbg_us_upload, light_dbg_us_bind, light_dbg_us_ensure,
    light_dbg_n_upload, light_dbg_n_ensure, light_dbg_us_light_fin;

int mod( const int x, const int N ) {
    const int result = ( x % N + N ) % N;
    if ( result < 0 ) {
        pr_debug( "Bad Mod:%d", result );
    }
    return result;
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
// recenter/heal, or the deferred-finalize path for a reactivated
// CHUNK_PARKED_UNFINALIZED slot), 1 when a (re)load was enqueued, and 2 when
// the slot parked because its nearest position is outside the active
// render window.
int ChunkLoader::reload_if_out_of_bounds( Chunk &chunk, const glm::ivec3 &chunk_pos, glm::ivec3 *out_new_pos ) {

    const int new_chunk_x = static_cast<int>( floorf( static_cast<float>( chunk_pos.x - chunk.chunk_mod.x + CHUNK_RADIUS_XZ ) / static_cast<float>( ( CHUNK_RADIUS_XZ * 2 + 1 ) ) ) ) * ( CHUNK_RADIUS_XZ * 2 + 1 ) + chunk.chunk_mod.x;
    const int new_chunk_y = static_cast<int>( floorf( static_cast<float>( chunk_pos.y - chunk.chunk_mod.y + CHUNK_RADIUS_Y ) / static_cast<float>( ( CHUNK_RADIUS_Y * 2 + 1 ) ) ) ) * ( CHUNK_RADIUS_Y * 2 + 1 ) + chunk.chunk_mod.y;
    const int new_chunk_z = static_cast<int>( floorf( static_cast<float>( chunk_pos.z - chunk.chunk_mod.z + CHUNK_RADIUS_XZ ) / static_cast<float>( ( CHUNK_RADIUS_XZ * 2 + 1 ) ) ) ) * ( CHUNK_RADIUS_XZ * 2 + 1 ) + chunk.chunk_mod.z;
    const glm::ivec3 new_chunk = glm::ivec3( new_chunk_x, new_chunk_y, new_chunk_z );
    if ( out_new_pos ) {
        *out_new_pos = new_chunk;
    }
    if ( !this->pos_in_window( chunk_pos, new_chunk ) ) {
        // Park the slot: any nonzero state keeps it out of the drawable
        // list, random ticks, and the light paths, and retained terrain
        // stays for reactivation. A live chunk keeps its finalized mesh
        // (CHUNK_PARKED); a just-dequeued one needs mesh/light finalize on
        // re-entry (CHUNK_PARKED_UNFINALIZED). Parked states keep theirs.
        if ( chunk.load_state == CHUNK_LIVE ) {
            chunk.load_state = CHUNK_PARKED;
        } else if ( chunk.load_state == CHUNK_LOADING ) {
            chunk.load_state = CHUNK_PARKED_UNFINALIZED;
        }
        return 2;
    }
    if ( new_chunk == chunk.chunk_pos ) {
        if ( chunk.load_state == CHUNK_PARKED ) {
            // Parked with finalized data at this position: instant reuse.
            chunk.load_state = CHUNK_LIVE;
            return 0;
        }
        if ( chunk.load_state == CHUNK_PARKED_UNFINALIZED && chunk.cells != nullptr ) {
            // Terrain loaded but never meshed/lit — caller finalizes.
            return 0;
        }
        if ( chunk.load_state != CHUNK_PARKED_UNFINALIZED ) {
            // LIVE or LOADING at the right position already.
            return 0;
        }
        // PARKED_UNFINALIZED with cells==nullptr: never loaded — enqueue.
    }
    chunk.unprogram_terrain( );
    chunk.load_state = CHUNK_LOADING;
    this->terrain_loading_thread.enqueue( &chunk, new_chunk, 1 );
    return 1;
}

// The post-terrain-load steps shared by the dequeue path and parked-slot
// reactivation: GL init, mesh, pending diffs, light finalize, GPU upload.
void ChunkLoader::finalize_loaded_chunk( Chunk &chunk, Multiplayer &multiplayer, const glm::ivec3 &chunk_diff, const glm::ivec3 &center_previous, const glm::ivec3 &center_next ) {
    // Publish before the finalize work (the old dequeue path cleared
    // is_loading first): light jobs and seeds popped mid-finalize see the
    // chunk as loaded rather than missing.
    chunk.load_state = CHUNK_LIVE;
    // Lazily create this chunk's GL objects on first load so the startup
    // loop doesn't block the first frame.
    chunk.ensure_gl_init( );
    process_chunk_position( chunk, chunk_diff, center_previous, center_next, 1 );
    // Apply any chunk diffs that arrived from the server before this chunk's
    // terrain gen finished (Phase 2 eager requests).
    multiplayer.apply_pending_diffs( *this, chunk );
    // Blocks are final now: hand the chunk to the lighting thread
    // (cascade/border sync/propagation). Inline when no light thread is
    // running (tests, WASM).
    const long long t_lfin = now_us( );
    this->light_submit_finalize( chunk );
    light_dbg_us_light_fin += now_us( ) - t_lfin;
    chunk.program_terrain( );
}

void ChunkLoader::init( const glm::dvec3 &camera_pos, const VertexBufferLayout &vbl_block, const VertexBufferLayout &vbl_coords, MapStorage &map_storage, int render_radius ) {
    this->set_render_distance( render_radius );
    this->render_radius_applied = this->render_radius;
    if ( this->terrain_loading_thread.start( map_storage ) ) {
        pr_debug( "Terrain loading thread failed to start." );
    }
    this->chunkArray = static_cast<Chunk *>( calloc( MAX_LOADED_CHUNKS, sizeof( Chunk ) ) );
    this->drawable_chunks = static_cast<Chunk **>( calloc( MAX_LOADED_CHUNKS, sizeof( Chunk * ) ) );
    this->repop_must = static_cast<Chunk **>( calloc( MAX_LOADED_CHUNKS, sizeof( Chunk * ) ) );
    this->num_drawable = 0;
    this->num_repop_must = 0;
    this->light_thread_start( );
    showErrors( );
    // pr_debug( "Num Total Chunks:%d", MAX_LOADED_CHUNKS );

    Shader &shader = this->shader;
    shader.init( &chunk_vertex, &chunk_fragment );
    if constexpr ( !DEBUG_CYCLE_AUTO_ROTATING_BLOCKS ) {
        shader.set_uniform1f( "u_ShowRotation", -2 );
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
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
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
                                chunk.load_state = CHUNK_LOADING;
                                this->terrain_loading_thread.enqueue( &chunk, chunk.chunk_pos, 0 );
                            } else {
                                // Outside the active render window: park the
                                // slot until the radius grows over it. It has
                                // no cells, so re-entry enqueues a real load.
                                chunk.load_state = CHUNK_PARKED_UNFINALIZED;
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

Chunk *ChunkLoader::get_chunk( const glm::ivec3 &chunk_pos ) const {
    const int slot = chunk_slot_from_pos( chunk_pos );
    Chunk &chunk = this->chunkArray[ slot ];
    if ( chunk.chunk_pos == chunk_pos ) {
        return &chunk;
    }
    return nullptr;
}

inline int ChunkLoader::process_chunk_position( Chunk &chunk, const glm::ivec3 &chunk_diff, const glm::ivec3 &center_previous, const glm::ivec3 &center_next, int force_reload ) {

    int visible_changed_x = 0;
    int visible_changed_y = 0;
    int visible_changed_z = 0;

    if ( !force_reload ) {
        visible_changed_x = ( chunk_diff.x != 0 ) && ( chunk.chunk_pos.x == center_previous.x || chunk.chunk_pos.x == center_next.x );
        visible_changed_y = ( chunk_diff.y != 0 ) && ( chunk.chunk_pos.y == center_previous.y || chunk.chunk_pos.y == center_next.y );
        visible_changed_z = ( chunk_diff.z != 0 ) && ( chunk.chunk_pos.z == center_previous.z || chunk.chunk_pos.z == center_next.z );
    }
    if ( force_reload || visible_changed_x || visible_changed_y || visible_changed_z ) {
        // pr_debug( "Vis dir changed for %2d %2d %2d changed because of:%2d %2d %2d same:%2d %2d %2d", //
        //           TRIP_ARGS( chunk->chunk_ ),                                                        //
        //           TRIP_ARGS( chunk_diff_ ),                                                          //
        //           chunk->chunk_x == center_previous_x,                                               //
        //           chunk->chunk_y == center_previous_y,                                               //
        //           chunk->chunk_z == center_previous_z                                                //
        // );
        chunk.calculate_sides( center_next );
        return 1;
    }
    return 0;
}

void ChunkLoader::render_chunks( Multiplayer &multiplayer, const glm::dvec3 &camera_pos, int limit_render ) {
    this->num_remeshed_this_frame = 0;
    glm::dvec3 chunk_size = glm::dvec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );

    glm::ivec3 chunk_pos = glm::floor( camera_pos / chunk_size );
    this->terrain_loading_thread.set_focus( chunk_pos, this->render_radius );
    glm::ivec3 &loaded_pos = this->chunk_center;
    glm::ivec3 chunk_diff = chunk_pos - loaded_pos;

    // Phase 2: fire one box request for the initial visible area instead of
    // per-chunk requests as terrain gen finishes. This overlaps the network
    // round-trip with GPU terrain gen.
    if ( this->initial_chunk_request_pending ) {
        glm::ivec3 box_min = chunk_pos - glm::ivec3( this->render_radius, CHUNK_RADIUS_Y, this->render_radius );
        // Only consume the one-shot flag if the request actually went out.
        // On WASM the WebSocket may still be connecting when the first
        // frame renders; retry next frame rather than dropping the request.
        this->initial_chunk_request_pending = !multiplayer.request_chunks_box( box_min,
                                                                               static_cast<uint8_t>( 2 * this->render_radius + 1 ),
                                                                               static_cast<uint8_t>( 2 * CHUNK_RADIUS_Y + 1 ),
                                                                               static_cast<uint8_t>( 2 * this->render_radius + 1 ) );
    }

    {
        Chunk *chunk_ptr;
        const long long t_budget_start = now_us( );
        do {
            chunk_ptr = this->terrain_loading_thread.dequeue( );
            if ( chunk_ptr ) {
                Chunk &chunk = *chunk_ptr;
                // load_state stays CHUNK_LOADING through the bounds check —
                // reload_if_out_of_bounds owns all transitions (re-enqueue,
                // park as CHUNK_PARKED_UNFINALIZED, or return for finalize).
                int reloaded = reload_if_out_of_bounds( chunk, chunk_pos );
                if ( !reloaded ) {
                    this->finalize_loaded_chunk( chunk, multiplayer, chunk_diff, loaded_pos, chunk_pos );
                }
            }
            // When limit_render is true (WASM without pthreads), terrain is
            // generated inline in dequeue() on the main thread, so cap the work
            // per frame to a time budget. On native the budget bounds the
            // per-chunk GL finalize work so load-in bursts don't hitch frames.
        } while ( chunk_ptr && ( now_us( ) - t_budget_start < ( limit_render ? WASM_LOAD_BUDGET_US : CHUNK_FINALIZE_BUDGET_US ) ) );
        light_dbg_us_finalize += now_us( ) - t_budget_start;
    }
    const bool radius_changed = this->render_radius != this->render_radius_applied;
    if ( 0 != chunk_diff.x || 0 != chunk_diff.y || 0 != chunk_diff.z || radius_changed ) {
        // Collect new chunk positions from the reload loop so we can fire one
        // box request for all of them at once (Phase 2).
        std::vector<glm::ivec3> new_positions;
        this->light_heal_budget = LIGHT_HEAL_PER_SHIFT;
        for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
            Chunk &chunk = this->chunkArray[ i ];
            if ( chunk.load_state == CHUNK_LOADING ) {
                continue;
            }
            glm::ivec3 new_pos;
            int reloaded = reload_if_out_of_bounds( chunk, chunk_pos, &new_pos );
            if ( reloaded == 0 ) {
                if ( chunk.load_state == CHUNK_PARKED_UNFINALIZED ) {
                    // Parked slot re-entering with loaded-but-unfinalized
                    // terrain: mesh/light it now, no regeneration.
                    this->finalize_loaded_chunk( chunk, multiplayer, chunk_diff, loaded_pos, chunk_pos );
                } else {
                    process_chunk_position( chunk, chunk_diff, loaded_pos, chunk_pos, 0 );
                    // Far-cap stale flag heals as the chunk re-enters near radius.
                    this->light_heal_chunk( chunk, chunk_pos );
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
            multiplayer.request_chunks_box( box_min, sx, sy, sz );
        }
        this->chunk_center = chunk_pos;
        this->render_radius_applied = this->render_radius;
        // Jobs queued while this region was far must catch up with the
        // player — promote them into the near lane (bounded single pass).
        this->light_reprioritize_jobs( );
    }
    this->rebuild_drawable_list( );
    const long long t_remesh_start = now_us( );
    // Drain the must-remesh queue first: edits enqueue every chunk they
    // dirty so a boundary edit's meshes update together in this pass. Past
    // REPOP_MUST_BUDGET_US leftovers are just popped — needs_repopulation
    // stays set and the budgeted scan below picks them up like any backlog.
    for ( int i = 0; i < this->num_repop_must; i++ ) {
        Chunk &chunk = *this->repop_must[ i ];
        chunk.repop_must_queued = 0;
        if ( chunk.needs_repopulation && !chunk.load_state && //
             !( chunk.cached_cull_normal && chunk.cached_cull_reflect ) && //
             now_us( ) - t_remesh_start < REPOP_MUST_BUDGET_US ) {
            this->remesh_group( chunk );
        }
    }
    this->num_repop_must = 0;
    // The backlog scan gets its own budget: while the must-queue drains at
    // up to REPOP_MUST_BUDGET_US, flag-only chunks (interior diff cells)
    // would otherwise starve until the queue empties.
    const long long t_scan_start = now_us( );
    for ( int i = 0; i < this->num_drawable; i++ ) {
        Chunk &chunk = *this->drawable_chunks[ i ];
        if ( chunk.needs_repopulation && !( chunk.cached_cull_normal && chunk.cached_cull_reflect ) ) {
            this->remesh_group( chunk );
            // Stop once the remesh budget is spent; remaining chunks keep
            // needs_repopulation set and render their old mesh until a later
            // frame re-meshes them.
            if ( now_us( ) - t_scan_start >= CHUNK_REMESH_BUDGET_US ) {
                break;
            }
        }
    }
    light_dbg_us_remesh += now_us( ) - t_remesh_start;
    // Light mutation lives on the lighting thread when one runs; the inline
    // path below is the no-thread fallback (tests, WASM without pthreads).
    if ( !ChunkLoader::light_async_active( ) ) {
        const long long t_drain = now_us( );
        this->light_drain_pending( );
        light_dbg_us_drain += now_us( ) - t_drain;
        // Scale the propagation budget with backlog: under-overhang/cave floods
        // legitimately cover every underground air cell within ~15 of lit
        // space, which is millions of steps during mass load-in.
        const size_t light_backlog = this->light_queue_sizes( );
        const long long light_budget = light_backlog > 200000 ? 8000 : light_backlog > 20000 ? 3000 : LIGHT_PROPAGATE_BUDGET_US;
        const long long t_bfs = now_us( );
        this->light_process_queue( light_budget );
        light_dbg_us_bfs += now_us( ) - t_bfs;
    }
    // GL uploads stay render-thread-owned regardless of who marked dirty.
    this->light_upload_dirty( LIGHT_MAX_UPLOADS_PER_FRAME );
    // Idle heal for far-cap stale flags; no-ops while the queue is busy.
    this->light_heal_idle( );
    this->rebuild_drawable_list( );
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

void ChunkLoader::remesh_now( Chunk &chunk ) {
    chunk.unprogram_terrain( );
    chunk.calculate_populated_blocks( );
    chunk.program_terrain( );
    chunk.needs_repopulation = false;
    this->num_remeshed_this_frame++;
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

void ChunkLoader::apply_diff_block( Chunk &chunk, int index, const BlockState *blockState ) {
    if ( !chunk.set_block_by_index_if_different( index, blockState ) ) {
        return;
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
                neighbor->set_block_by_index_if_different( Chunk::get_index_from_coords( mirror ), blockState );
                if ( neighbor->needs_repopulation ) {
                    this->repop_must_enqueue( *neighbor );
                    enqueued_neighbor = 1;
                }
            }
        }
    }
    if ( enqueued_neighbor ) {
        this->repop_must_enqueue( chunk );
    }
}

void ChunkLoader::rebuild_drawable_list( ) {
    this->num_drawable = 0;
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        Chunk &chunk = this->chunkArray[ i ];
        if ( !chunk.load_state && chunk.should_render ) {
            this->drawable_chunks[ this->num_drawable++ ] = &chunk;
        }
    }
    this->drawable_generation++;
}
// Bounding-sphere radius for frustum culling: a chunk's mesh is confined to
// its own [0, CHUNK_SIZE) volume, so half the box diagonal plus one block of
// slack bounds every drawn vertex.
static constexpr float CHUNK_CULL_RADIUS = ( CHUNK_SIZE_X + 1 ) * 0.5f * 1.73205080757f; // sqrt(3)

void ChunkLoader::calculate_cull( const glm::mat4 &mvp, const bool saveAsReflection, const glm::ivec3 &renderOrigin ) const {
    // Extract the 6 frustum planes from the MVP rows (Gribb-Hartmann): a point
    // p is inside the clip volume iff (row3 +- row_i) . p >= 0 for each axis i.
    // GLM is column-major, so row i is (mvp[0][i], mvp[1][i], mvp[2][i], mvp[3][i]).
    // Testing the center's NDC coords against 1+r/w (the old approach) treats
    // the bounding sphere as isotropic in NDC, but perspective scales x by
    // f/aspect and y by f — with aspect < ~0.84 (narrow windows) the margin
    // under-estimates the sphere's x extent and visible chunks were culled.
    const glm::vec4 row_x( mvp[ 0 ][ 0 ], mvp[ 1 ][ 0 ], mvp[ 2 ][ 0 ], mvp[ 3 ][ 0 ] );
    const glm::vec4 row_y( mvp[ 0 ][ 1 ], mvp[ 1 ][ 1 ], mvp[ 2 ][ 1 ], mvp[ 3 ][ 1 ] );
    const glm::vec4 row_z( mvp[ 0 ][ 2 ], mvp[ 1 ][ 2 ], mvp[ 2 ][ 2 ], mvp[ 3 ][ 2 ] );
    const glm::vec4 row_w( mvp[ 0 ][ 3 ], mvp[ 1 ][ 3 ], mvp[ 2 ][ 3 ], mvp[ 3 ][ 3 ] );
    const glm::vec4 planes[ 6 ] = { row_w + row_x, row_w - row_x, //
                                    row_w + row_y, row_w - row_y, //
                                    row_w + row_z, row_w - row_z };
    // The plane normals aren't unit length; pre-scale the sphere radius by
    // |n.xyz| so the inner loop is a single dot-product compare per plane.
    float scaled_radius[ 6 ];
    for ( int p = 0; p < 6; p++ ) {
        scaled_radius[ p ] = CHUNK_CULL_RADIUS * glm::length( glm::vec3( planes[ p ] ) );
    }
    for ( int i = 0; i < this->num_drawable; i++ ) {
        int final_is_visible = 1;
        Chunk *chunk = this->drawable_chunks[ i ];
        if constexpr ( CULL_NON_VISIBLE ) {
            const glm::vec4 chunk_coords = glm::vec4( chunk->chunk_pos * CHUNK_SIZE_I + CHUNK_SIZE_I / 2 - renderOrigin, 1 );
            for ( int p = 0; p < 6; p++ ) {
                if ( glm::dot( chunk_coords, planes[ p ] ) < -scaled_radius[ p ] ) {
                    final_is_visible = 0;
                    break;
                }
            }
        }
        if ( saveAsReflection ) {
            chunk->cached_cull_reflect = !final_is_visible;
        } else {
            // Re-entering the frustum invalidates the stale occl verdict:
            // while the chunk was culled the camera may have moved, so a
            // still-hidden chunk leaves a hole until the fresh probe lands.
            // Draw it instead — overdraw at worst — and let this frame's
            // probe re-hide it if it's still occluded.
            if ( chunk->cached_cull_normal && final_is_visible ) {
                chunk->occl_fails = 0;
            }
            chunk->cached_cull_normal = !final_is_visible;
        }
    }
}

int shouldInc = 0;
int showRotation = 0;
void ChunkLoader::draw( const glm::mat4 &mvp, const Renderer &renderer, const Texture &texture, bool reflect_only, bool draw_reflect, bool use_frame_buffer, const glm::vec3 &render_origin, const glm::vec3 &camera_pos ) {
    Shader &shader = this->shader;
    shader.set_uniform_mat4f( "u_MVP", mvp );
    {
        GLint vp[ 4 ];
        glGetIntegerv( GL_VIEWPORT, vp );
        shader.set_uniform2f( "u_ViewportSize", (float)vp[ 2 ], (float)vp[ 3 ] );
    }
    // Screen-space corner grow (chunk_vertex.glsl), tunable via env.
    static const float face_grow_px = getenv( "REPGAME_FACE_GROW_PX" ) ? static_cast<float>( atof( getenv( "REPGAME_FACE_GROW_PX" ) ) ) : CHUNK_FACE_GROW_PX;
    // All per-chunk light volumes share one reserved texture unit; each chunk
    // binds its 3D texture + u_LightBase right before drawing (Chunk::draw).
    shader.set_uniform1i( "u_LightTex", light_texture_unit( ) );
    glActiveTexture( GL_TEXTURE0 + light_texture_unit( ) );
    // glGetUniformLocation once per draw instead of once per chunk — the
    // per-chunk set_uniform3f it replaced cost ~6000 lookups/frame.
    const int light_base_loc = shader.get_uniform_location( "u_LightBase" );

    // Front-to-back ordering: depth-writing layers draw nearest-first so
    // early-z kills occluded fragments (and, later, so occluders are in the
    // depth buffer before occlusion queries test farther chunks). Blended
    // layers iterate the same array in reverse (back-to-front) for correct
    // alpha compositing. Sorting by distance to the real camera is exact for
    // the reflected pass too — the mirror is an isometry. Re-sorted only
    // when rebuild_drawable_list ran (i.e. the camera's tick processed);
    // nothing else consumes the list's order.
    if ( this->sorted_generation != this->drawable_generation ) {
        this->sorted_generation = this->drawable_generation;
        const glm::vec3 ro = render_origin;
        std::sort( this->drawable_chunks, this->drawable_chunks + this->num_drawable, [ ro ]( const Chunk *a, const Chunk *b ) {
            const glm::vec3 da = glm::vec3( a->chunk_pos * CHUNK_SIZE_I + CHUNK_SIZE_I / 2 ) - ro;
            const glm::vec3 db = glm::vec3( b->chunk_pos * CHUNK_SIZE_I + CHUNK_SIZE_I / 2 ) - ro;
            return glm::dot( da, da ) < glm::dot( db, db );
        } );
    }

    // A chunk whose box contains the camera can never be occlusion-tested:
    // every in-view box face then sits BEHIND the chunk's own content, so
    // the probe self-fails even though the terrain is plainly visible
    // (looking straight down = the whole screen is the containing chunk).
    // That oscillates draw/hide every other frame. The margin covers the
    // near plane; the gate also un-hides instantly when the camera walks
    // into a hidden chunk (noclip into a sealed pocket draws correctly).
    const auto cam_in_box = [ &camera_pos, &render_origin ]( const Chunk &chunk ) {
        const glm::vec3 e = camera_pos - glm::vec3( chunk.chunk_pos * CHUNK_SIZE_I ) + render_origin;
        return e.x > -1.0f && e.x < CHUNK_SIZE_X + 1.0f && e.y > -1.0f && e.y < CHUNK_SIZE_Y + 1.0f && e.z > -1.0f && e.z < CHUNK_SIZE_Z + 1.0f;
    };

    // Occlusion verdicts are view-relative: a box probe measures coverage
    // against THIS frame's depth buffer, so a verdict earned under a
    // different camera view proves nothing (the covering occluder may have
    // left the frustum, or parallax may have uncovered the box). Bumping
    // occl_view_gen when the view moves invalidates per-chunk fails_gen /
    // probe_gen tagged with older generations — hidden chunks draw while
    // fresh probes re-verify them instead of trusting verdicts measured
    // under a view where they were occluded. That trades a few frames of
    // overdraw after a fast camera move for never leaving a see-through
    // hole while a stale "occluded" result drains through the query queue.
    // Rotation columns (0-2) carry no translation; column 3 carries it, so
    // a tighter epsilon there doubles as the ~0.3-block move threshold.
    long occl_view_gen = 0;
    if ( occlusion_enabled( ) && !draw_reflect ) {
        static long view_gen = 0;
        static glm::mat4 last_view_mvp( 0.0f );
        bool view_changed = false;
        for ( int c = 0; c < 3 && !view_changed; c++ ) {
            for ( int r = 0; r < 4 && !view_changed; r++ ) {
                view_changed = fabsf( mvp[ c ][ r ] - last_view_mvp[ c ][ r ] ) > 0.02f;
            }
        }
        for ( int r = 0; r < 4 && !view_changed; r++ ) {
            view_changed = fabsf( mvp[ 3 ][ r ] - last_view_mvp[ 3 ][ r ] ) > 0.4f;
        }
        if ( view_changed ) {
            last_view_mvp = mvp;
            view_gen++;
        }
        occl_view_gen = view_gen;
    }

    // pr_debug( "Drawing %d chunks", this->numLoadedChunks );
    for ( int renderOrder = LAST_RENDER_ORDER - 1; renderOrder > 0; renderOrder-- ) {
        if ( reflect_only == ( renderOrder == RenderOrder_Water ) ) {
            shader.set_uniform1f( "u_shouldDiscardAlpha", renderOrder != RenderOrder_Water && renderOrder != RenderOrder_Translucent);
            // Screen-space face grow, opaque only: water/translucent/flowers
            // are blended, so overlapping grown edges would blend twice and
            // show as dark dashed lines. Debug-flag bit 64 disables it.
            shader.set_uniform1f( "u_FaceGrowPx", ( renderOrder == RenderOrder_Opaque && ( this->debug_flags & 64 ) == 0 ) ? face_grow_px : 0.0f );
            // Set texture wrap mode once per render order instead of per chunk draw call.
            // glTexParameteri edits the texture bound on the *active* unit, so
            // re-bind the atlas first — the per-chunk loop leaves the light
            // unit active from the previous order.
            texture.bind( );
            if ( renderOrder == RenderOrder_Flowers ) {
                glTexParameteri( texture.target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
                glTexParameteri( texture.target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
            } else {
                glTexParameteri( texture.target, GL_TEXTURE_WRAP_S, GL_REPEAT );
                glTexParameteri( texture.target, GL_TEXTURE_WRAP_T, GL_REPEAT );
            }
            // Select the shared light unit once per pass — the glTexParameteri
            // calls above must run first, then per-chunk draws only need a
            // glBindTexture (Chunk::draw).
            glActiveTexture( GL_TEXTURE0 + light_texture_unit( ) );
            // Occlusion probes run only in the normal pass (the reflection
            // clears depth and rebuilds it from a mirrored view — its
            // visibility set is different, so it draws everything the
            // frustum keeps). The occl_fails gate still applies to every
            // order here and to the stencil-water pass: a chunk whose whole
            // box is occluded can't show water or flowers either.
            const bool occl = occlusion_enabled( ) && !draw_reflect;
            // Blended layers walk the sorted list backwards (back-to-front).
            const bool back_to_front = renderOrder == RenderOrder_Water || renderOrder == RenderOrder_Translucent;
            if ( occl && renderOrder == RenderOrder_Opaque ) {
                // Two sub-passes over the front-to-back list. (1) Consume
                // last frame's probe result (non-blocking) and draw chunks
                // still believed visible — this builds the opaque depth
                // buffer. (2) Probe every frustum-visible chunk's box
                // against that depth with all writes masked. A failed probe
                // means the content (confined to [0,CHUNK_SIZE)) can't be
                // visible; occl_fails >= OCCL_HIDE_FRAMES gates it out.
                for ( int i = 0; i < this->num_drawable; i++ ) {
                    Chunk &chunk = *this->drawable_chunks[ i ];
                    if ( chunk.cached_cull_normal ) {
                        continue;
                    }
                    if ( chunk.occl_pending ) {
                        GLuint available = 0;
                        glGetQueryObjectuiv( chunk.occl_query, GL_QUERY_RESULT_AVAILABLE, &available );
                        if ( available ) {
                            GLuint passed = 0;
                            glGetQueryObjectuiv( chunk.occl_query, GL_QUERY_RESULT, &passed );
                            chunk.occl_pending = 0;
                            // Retire every finished query, but only trust the
                            // verdict if it was issued under the current view:
                            // a result from before the camera moved describes
                            // depth that may no longer exist.
                            if ( chunk.occl_probe_gen == occl_view_gen ) {
                                if ( chunk.occl_fails_gen != occl_view_gen ) {
                                    chunk.occl_fails = 0;
                                    chunk.occl_fails_gen = occl_view_gen;
                                }
                                chunk.occl_fails = passed ? 0 : chunk.occl_fails + 1;
                            }
                        }
                    }
                    if ( cam_in_box( chunk ) ) {
                        chunk.occl_fails = 0;
                    }
                    if ( chunk.occl_fails_gen != occl_view_gen || chunk.occl_fails < OCCL_HIDE_FRAMES ) {
                        chunk.draw( renderer, texture, shader, static_cast<RenderOrder>( renderOrder ), draw_reflect, render_origin, light_base_loc );
                    }
                }
                this->occl_shader.bind( );
                this->occl_shader.set_uniform_mat4f( "u_MVP", mvp );
                const int box_min_loc = this->occl_shader.get_uniform_location( "u_BoxMin" );
                this->occl_va.bind( );
                // LEQUAL, not LESS: a chunk's boundary surface is coplanar
                // with its own box face, so under LESS a fully walled chunk
                // would self-occlude and flicker draw/hide each frame. Ties
                // only ever occur against the chunk's own geometry
                // (neighbors sit a whole block away), so passing them is
                // conservative — overdraw at worst, never a hole. Cull face
                // off so a camera inside a box still rasterizes it.
                glDisable( GL_CULL_FACE );
                glDepthFunc( GL_LEQUAL );
                glColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
                glDepthMask( GL_FALSE );
                for ( int i = 0; i < this->num_drawable; i++ ) {
                    Chunk &chunk = *this->drawable_chunks[ i ];
                    if ( chunk.cached_cull_normal || chunk.occl_pending || cam_in_box( chunk ) ) {
                        continue;
                    }
                    if ( chunk.occl_query == 0 ) {
                        glGenQueries( 1, &chunk.occl_query );
                    }
                    const glm::vec3 box_min = glm::vec3( chunk.chunk_pos * CHUNK_SIZE_I ) - render_origin;
                    glUniform3f( box_min_loc, box_min.x, box_min.y, box_min.z );
                    glBeginQuery( GL_ANY_SAMPLES_PASSED, chunk.occl_query );
                    glDrawArrays( GL_TRIANGLES, 0, 36 );
                    glEndQuery( GL_ANY_SAMPLES_PASSED );
                    chunk.occl_pending = 1;
                    chunk.occl_probe_gen = occl_view_gen;
                }
                glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
#if ( SUPPORTS_FRAME_BUFFER )
                if ( use_frame_buffer ) {
                    // world.cpp masks the sky-color attachment (3) off after
                    // the sky pass so terrain draws can't clobber the
                    // captured sky — the blanket unmask above re-enabled it,
                    // so put it back or fog compositing blends terrain pixels
                    // toward whatever garbage lands there.
                    glColorMaski( 3, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
                }
#endif
                glDepthMask( GL_TRUE );
                glDepthFunc( GL_LESS );
                glEnable( GL_CULL_FACE );
            } else {
                for ( int i = 0; i < this->num_drawable; i++ ) {
                    Chunk &chunk = *this->drawable_chunks[ back_to_front ? this->num_drawable - 1 - i : i ];
                    if ( draw_reflect ) {
                        if ( !chunk.cached_cull_reflect ) {
                            chunk.draw( renderer, texture, shader, static_cast<RenderOrder>( renderOrder ), draw_reflect, render_origin, light_base_loc );
                        }
                    } else {
                        if ( chunk.cached_cull_normal ||
                             ( occl && chunk.occl_fails_gen == occl_view_gen && chunk.occl_fails >= OCCL_HIDE_FRAMES && !cam_in_box( chunk ) ) ) {
                            continue;
                        }
                        chunk.draw( renderer, texture, shader, static_cast<RenderOrder>( renderOrder ), draw_reflect, render_origin, light_base_loc );
                    }
                }
            }
        }
    }
    if constexpr ( DEBUG_CYCLE_AUTO_ROTATING_BLOCKS ) {
        shouldInc++;
        if ( shouldInc > 100 ) {
            shouldInc = 0;
            showRotation++;
            if ( showRotation >= 8 ) {
                showRotation = 0;
            }
        }
        shader.set_uniform1f( "u_ShowRotation", showRotation );
    }
    shader.set_uniform1f( "u_shouldDiscardAlpha", 1 );
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
        // Parked slots still hold dirty data — persist_chunk no-ops on
        // clean/never-loaded chunks.
        if ( chunk.load_state != CHUNK_LOADING ) {
            chunk.persist( map_storage );
        }
        chunk.destroy( );
    }
    free( this->chunkArray );
    free( this->drawable_chunks );
    free( this->repop_must );
    this->occl_vb.destroy( );
    this->occl_va.destroy( );
    this->occl_shader.destroy( );
}