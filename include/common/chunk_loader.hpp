#pragma once

#include <deque>

#include "chunk.hpp"
#include "common/light.hpp"
#include "renderer/renderer.hpp"
#include "mouse_selection.hpp"
#include "sky_box.hpp"
#include "common/utils/terrain_loading_thread.hpp"
#include "common/utils/map_storage.hpp"

class Multiplayer;
class World;

// Light propagation queue entries. A LightSeed spreads the cell's stored
// value to neighbors; a LightRemoveSeed clears light that had been fed
// through the cell when it held old_value. Channels: LIGHT_CHANNEL_BLOCK /
// LIGHT_CHANNEL_SKY (light.hpp).
struct LightSeed {
    glm::ivec3 pos;
    unsigned char channel;
};
struct LightRemoveSeed {
    glm::ivec3 pos;
    unsigned char channel;
    unsigned char old_value;
};

// Region shards for the BFS seed queues (see the queue comment in
// ChunkLoader below): seeds route by target column so each drain worker
// owns a disjoint region of the flood.
#define LIGHT_BFS_SHARDS 4

// Snapshot of chunk-loading and lighting work-queue depth for the debug
// overlay. All fields are point-in-time reads — the counts can race the
// worker pools by a frame, which is fine for a HUD.
struct LoaderDebugStats {
    int terrain_queued;    // chunks waiting for a terrain worker
    int terrain_results;   // generated chunks waiting for the render thread
    int chunks_loading;    // slots with is_loading set
    int chunks_drawable;
    int light_jobs;        // queued finalize/recheck jobs
    int light_pending;     // chunks with recorded block diffs awaiting drain
    int light_claims;      // column-claimed jobs in flight on light workers
    int light_dirty;       // light volumes waiting for GPU upload
    int light_bfs_active;   // workers currently draining a seed shard
    int light_bfs_priority; // backlog-priority window open (finalize claims paused)
    int light_bfs_starve;   // that window was opened by backlog age
    long long light_seeds;  // pending flood-fill seeds
    long long light_dropped; // seeds lost to missing/loading chunks (cumulative)
    int light_stale_count;  // chunks flagged light_stale awaiting heal
    long long light_jlag_us;  // max submit->execute age of a job this probe interval
    long long light_njlag_us; // same, for jobs near the player at execution
};

class ChunkLoader {
    friend class World;
    friend void test_setup_world( World &world );
    friend int test_lighting( );
    friend void light_fill_chunk( Chunk &chunk );

    TerrainLoadingThread terrain_loading_thread;
    glm::ivec3 chunk_center;
    Chunk *chunkArray;
    Shader shader;
    // Capture-mode u_DebugFlags bitmask (World::set_chunk_debug_flags) —
    // vertex-side bits like 64 (face grow off) are applied in the draw loop.
    int debug_flags = 0;
    struct {
        VertexBuffer vb_block;
    } solid;
    struct {
        VertexBuffer vb_block;
    } water;

    // Compact list of chunks that are loaded and should be rendered.
    // Rebuilt once per frame in render_chunks; iterated by calculate_cull/draw
    // instead of scanning all MAX_LOADED_CHUNKS slots every pass.
    Chunk **drawable_chunks;
    int num_drawable;
    // Must-remesh queue: chunks dirtied by live edits (World::set_loaded_block
    // and its boundary fixup writes) are enqueued here so every mesh touched
    // by one edit is rebuilt in the same render pass — splitting them across
    // frames under the remesh budget showed the neighbor's stale mesh, a
    // see-through hole at chunk boundaries. Drained at the top of the remesh
    // phase in render_chunks, ahead of the flag-driven budgeted scan which
    // still owns bursts (multiplayer diffs, overflows past the must budget).
    Chunk **repop_must;
    int num_repop_must;
    int num_remeshed_this_frame;
    // Set in init; cleared after the first render_chunks fires a box request
    // for the initial visible area. Lets us batch the initial chunk-diff
    // request into one frame instead of per-chunk as terrain gen finishes.
    bool initial_chunk_request_pending = false;

    int reload_if_out_of_bounds( Chunk &chunk, const glm::ivec3 &chunk_pos, glm::ivec3 *out_new_pos = nullptr );
    static inline int process_chunk_position( Chunk &chunk, const glm::ivec3 &chunk_diff, const glm::ivec3 &center_previous, const glm::ivec3 &center_next, int force_reload );
    void process_random_ticks( );
    void rebuild_drawable_list( );
    void repop_must_enqueue( Chunk &chunk );
    void remesh_now( Chunk &chunk );
    void remesh_group( Chunk &chunk );

    // Flood-fill lighting (implemented in light.cpp). Light-state mutation is
    // owned by the lighting worker pool when it runs — workers process jobs
    // under 5x5 column claims (see light.cpp) — otherwise the calling thread
    // (tests, WASM without pthreads) does everything inline.
    //
    // Add seeds are bucketed by the light level written at push time and
    // popped highest-level-first (light_add_queue[shard][channel][level]).
    // Processing strictly descending means a cell settles at its final value
    // on first visit — with plain FIFO a cell could be re-offered a better
    // value later and re-walk its neighbors, so the load-in flood never
    // converged.
    //
    // Queues are sharded LIGHT_BFS_SHARDS ways by the target cell's column
    // (parity of 4x4 column blocks — a local flood scatters across all
    // shards, so every drainer gets a share). Each shard is drained
    // exclusively by whichever worker CAS-claims it; pushes route by shard
    // and take the shard's own seed lock, so producers and per-shard
    // drainers never serialize globally.
    std::deque<LightSeed> light_add_queue[ LIGHT_BFS_SHARDS ][ 2 ][ LIGHT_MAX_LEVEL + 1 ];
    std::deque<LightRemoveSeed> light_remove_queue[ LIGHT_BFS_SHARDS ];
    // Near-player lanes (see LIGHT_NEAR_COL_DIST in light.cpp): seeds whose
    // target column sits near chunk_center are pushed here instead and
    // drained before the shared queues, so the region around the player
    // converges during streaming rather than when the whole backlog empties.
    // Same bucketing as the normal queues; light_bfs_pending covers all.
    std::deque<LightSeed> light_add_queue_pri[ LIGHT_BFS_SHARDS ][ 2 ][ LIGHT_MAX_LEVEL + 1 ];
    std::deque<LightRemoveSeed> light_remove_queue_pri[ LIGHT_BFS_SHARDS ];
    // push_back a remove seed under light_seed_lock, routed by distance.
    // internal=true marks flood-regen pushes (from add/remove step) which the
    // backlog cap may refuse; producer pushes leave it false.
    void light_push_remove_seed( const glm::ivec3 &block_pos, int channel, int old_value, bool internal = false );
    static int light_emit_at( const Chunk &chunk, int index );
    // Null-light (uniform volume) helpers — light.cpp.
    static inline unsigned char light_read_cell( const Chunk &chunk, int idx );
    static void light_materialize( Chunk &chunk );
    static inline void light_write_cell( Chunk &chunk, int idx, unsigned char nv );
    int light_get( const glm::ivec3 &block_pos, int channel ) const;
    void light_set( const glm::ivec3 &block_pos, int channel, int value );
    void light_set_nb( Chunk *const nb_cache[ 27 ], Chunk &owner, const glm::ivec3 &block_pos, int channel, int value );
    BlockID light_block_id_at( const glm::ivec3 &block_pos ) const;
    void light_mark_dirty( Chunk &chunk, const glm::ivec3 &local );
    void light_mark_dirty_now( Chunk &chunk, const glm::ivec3 &local );
    void light_flush_deferred_marks( );
    void light_mark_dirty_all( Chunk &chunk );
    void light_upload_enqueue( Chunk &chunk );
    // Congestion/dead-target/flood-cap gate used by both seed push paths.
    int light_seed_gate( const glm::ivec3 &block_pos, bool near, bool internal );
    void light_add_seed( const glm::ivec3 &block_pos, int channel, int level, bool internal = false );
    void light_add_step( const LightSeed &seed );
    void light_remove_step( const LightRemoveSeed &seed );
    // Pop the next seed from `shard`'s queues under that shard's seed lock:
    // 1 = add, 2 = remove, 0 = empty. level/pri record the origin bucket for
    // contention re-queueing.
    int light_pop_seed( int shard, LightSeed &add_out, LightRemoveSeed &rem_out, int &level, bool &pri );
    void light_process_queue( long long budget_us, int shard );
    void light_process_queue( long long budget_us ); // all shards, single-threaded callers
    static void light_ensure_columns( Chunk &chunk );
    static void light_compute_column_open( Chunk &chunk );
    static void light_compute_one_column( Chunk &chunk, int x, int z );
    void light_cascade_column( int world_x, int world_z );
    // Returns the number of cells skylight was written into (0 => the chunk's
    // interior has no skylight, so callers can skip the boundary scan).
    int light_cascade_columns( Chunk &chunk );
    void light_seed_column_boundary( int world_x, int world_z );
    void light_seed_interior_boundary( Chunk &chunk );
    void light_recheck_block( const glm::ivec3 &block_pos );
    void light_finalize_chunk( Chunk &chunk );
    void light_border_sync( Chunk &chunk );
    void light_process_pending_chunk( Chunk &chunk );
    void light_drain_pending( );
    void light_upload_dirty( int max_uploads );

    // Lighting-thread plumbing (docs/lighting-thread-plan.md). When the
    // thread is running, finalize/recheck calls enqueue instead of running
    // inline; without it (tests, WASM without pthreads) everything stays
    // synchronous on the calling thread.
    void light_submit_finalize( Chunk &chunk );
    // Stale-flag heals: near-radius entry (grid-shift scan) and per-frame idle.
    void light_heal_chunk( Chunk &chunk, const glm::ivec3 &center );
    void light_heal_idle( );
    void light_submit_recheck( const glm::ivec3 &block_pos );
    // Called after chunk_center moves: promotes far-lane jobs that are now
    // near so the visible ring converges first even when the queue outran
    // the player. Jobs never demote (stale near work is harmless).
    void light_reprioritize_jobs( );
    static bool light_async_active( );
    void light_thread_start( );
    void light_thread_stop( );
    void light_thread_loop( );

  public:
    void init( const glm::dvec3 &camera_pos, const VertexBufferLayout &vbl_block, const VertexBufferLayout &vbl_coords, MapStorage &map_storage );
    void render_chunks( Multiplayer &multiplayer, const glm::dvec3 &camera_pos, int limit_render );
    void repopulate_blocks( );
    void calculate_cull( const glm::mat4 &mvp, bool saveAsReflection, const glm::ivec3 &renderOrigin ) const;
    void draw( const glm::mat4 &mvp, const Renderer &renderer, const Texture &texture, bool reflect_only, bool draw_reflect, bool use_frame_buffer, const glm::vec3 &render_origin );
    Chunk *get_chunk( const glm::ivec3 &pointed ) const;
    // Apply one chunk-diff cell (CHUNK_DIFF_RESULT / apply_pending_diffs):
    // writes the receiving chunk's copy, mirrors it into every loaded
    // neighbor that also stores that world cell, and enqueues the touched
    // chunks on repop_must so a modified seam remeshes in one pass.
    void apply_diff_block( Chunk &chunk, int index, const BlockState *blockState );
    // TEMP diagnostic for the flood-fill lighting bring-up.
    int light_probe( const glm::ivec3 &pos, int channel ) { return this->light_get( pos, channel ); }
    BlockID light_probe_id( const glm::ivec3 &pos ) { return this->light_block_id_at( pos ); }
    // Trilinear (sky, block) light sample at a world position, normalized to
    // [0,1]. For dynamic objects baked into instance data (avatars, debris).
    glm::vec2 light_sample( const glm::vec3 &pos ) const;
    size_t light_add_queue_size( ) const;
    void light_dbg_stats( char *buf, size_t n );
    // RepGame::tick feeds the game tick here once per tick so the light
    // workers' starve-window predicate can measure BFS-backlog age without
    // clock reads on the seed push/pop hot paths.
    void light_note_tick( long tick );
    size_t light_queue_sizes( ) {
        size_t total = this->light_add_queue_size( );
        for ( int s = 0; s < LIGHT_BFS_SHARDS; s++ ) {
            total += this->light_remove_queue[ s ].size( ) + this->light_remove_queue_pri[ s ].size( );
        }
        return total;
    }
    void debug_loader_stats( LoaderDebugStats *out ) const;
    void cleanup( MapStorage &map_storage );
};

#define MAX_LOADED_CHUNKS ( ( 2 * CHUNK_RADIUS_X + 1 ) * ( 2 * CHUNK_RADIUS_Y + 1 ) * ( 2 * CHUNK_RADIUS_Z + 1 ) )

// The chunk grid is a torus of size (2*R+1)^3. Each chunk's chunk_mod (which is
// invariant for its slot) uniquely maps to an array index, giving O(1) lookup.
static inline int chunk_slot_from_mod( const glm::ivec3 &chunk_mod ) {
    const int sx = 2 * CHUNK_RADIUS_X + 1;
    const int sz = 2 * CHUNK_RADIUS_Z + 1;
    return chunk_mod.x + sx * ( chunk_mod.z + sz * chunk_mod.y );
}

static inline int chunk_slot_from_pos( const glm::ivec3 &chunk_pos ) {
    const int sx = 2 * CHUNK_RADIUS_X + 1;
    const int sy = 2 * CHUNK_RADIUS_Y + 1;
    const int sz = 2 * CHUNK_RADIUS_Z + 1;
    glm::ivec3 cmod;
    cmod.x = ( ( chunk_pos.x % sx ) + sx ) % sx;
    cmod.y = ( ( chunk_pos.y % sy ) + sy ) % sy;
    cmod.z = ( ( chunk_pos.z % sz ) + sz ) % sz;
    return chunk_slot_from_mod( cmod );
}