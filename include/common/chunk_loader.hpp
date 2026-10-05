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
    int light_bfs_active;  // a worker is draining the seed queues
    long long light_seeds; // pending flood-fill seeds
    long long light_dropped; // seeds lost to missing/loading chunks (cumulative)
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
    int num_remeshed_this_frame;
    // Set in init; cleared after the first render_chunks fires a box request
    // for the initial visible area. Lets us batch the initial chunk-diff
    // request into one frame instead of per-chunk as terrain gen finishes.
    bool initial_chunk_request_pending = false;

    int reload_if_out_of_bounds( Chunk &chunk, const glm::ivec3 &chunk_pos, glm::ivec3 *out_new_pos = nullptr );
    static inline int process_chunk_position( Chunk &chunk, const glm::ivec3 &chunk_diff, const glm::ivec3 &center_previous, const glm::ivec3 &center_next, int force_reload );
    void process_random_ticks( );
    void rebuild_drawable_list( );

    // Flood-fill lighting (implemented in light.cpp). Light-state mutation is
    // owned by the lighting worker pool when it runs — workers process jobs
    // under 5x5 column claims (see light.cpp) — otherwise the calling thread
    // (tests, WASM without pthreads) does everything inline.
    //
    // Add seeds are bucketed by the light level written at push time and
    // popped highest-level-first (light_add_queue[channel][level]). Processing
    // strictly descending means a cell settles at its final value on first
    // visit — with plain FIFO a cell could be re-offered a better value later
    // and re-walk its neighbors, so the load-in flood never converged.
    std::deque<LightSeed> light_add_queue[ 2 ][ LIGHT_MAX_LEVEL + 1 ];
    std::deque<LightRemoveSeed> light_remove_queue;
    static int light_emit_at( const Chunk &chunk, int index );
    int light_get( const glm::ivec3 &block_pos, int channel ) const;
    void light_set( const glm::ivec3 &block_pos, int channel, int value );
    void light_set_nb( Chunk *const nb_cache[ 27 ], Chunk &owner, const glm::ivec3 &block_pos, int channel, int value );
    BlockID light_block_id_at( const glm::ivec3 &block_pos ) const;
    static void light_mark_dirty( Chunk &chunk, const glm::ivec3 &local );
    static void light_mark_dirty_all( Chunk &chunk );
    static void light_upload_enqueue( Chunk &chunk );
    void light_add_seed( const glm::ivec3 &block_pos, int channel, int level );
    void light_add_step( const LightSeed &seed );
    void light_remove_step( const LightRemoveSeed &seed );
    void light_process_queue( long long budget_us );
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
    void light_submit_recheck( const glm::ivec3 &block_pos );
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
    // TEMP diagnostic for the flood-fill lighting bring-up.
    int light_probe( const glm::ivec3 &pos, int channel ) { return this->light_get( pos, channel ); }
    BlockID light_probe_id( const glm::ivec3 &pos ) { return this->light_block_id_at( pos ); }
    size_t light_add_queue_size( ) const;
    void light_dbg_stats( char *buf, size_t n );
    size_t light_queue_sizes( ) { return this->light_add_queue_size( ) + this->light_remove_queue.size( ); }
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