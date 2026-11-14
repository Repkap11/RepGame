#pragma once

#include <climits>
#include <deque>
#include <set>
#include <vector>

#include "chunk.hpp"
#include "common/chunk_render.hpp"
#include "common/light.hpp"
#include "renderer/renderer.hpp"
#include "mouse_selection.hpp"
#include "sky_box.hpp"
#include "common/utils/terrain_loading_thread.hpp"
#include "common/utils/map_storage.hpp"
#include "common/utils/seed_fifo.hpp"
#include "common/chunk_sort.hpp"

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

// Per-column seed queues (see the queue comment in ChunkLoader below): one
// slot per loaded chunk-column position on the torus — the x/z part of
// chunk_slot_from_pos's mod ring. Seeds route by target column; drain
// workers claim the nearest non-empty slot.
#define LIGHT_COL_QUEUE_COUNT ( ( 2 * CHUNK_RADIUS_XZ + 1 ) * ( 2 * CHUNK_RADIUS_XZ + 1 ) )

#define MAX_LOADED_CHUNKS ( ( 2 * CHUNK_RADIUS_XZ + 1 ) * ( 2 * CHUNK_RADIUS_Y + 1 ) * ( 2 * CHUNK_RADIUS_XZ + 1 ) )

// Snapshot of chunk-loading and lighting work-queue depth for the debug
// overlay. All fields are point-in-time reads — the counts can race the
// worker pools by a frame, which is fine for a HUD.
struct LoaderDebugStats {
    int terrain_queued;    // chunks waiting for a terrain worker
    int terrain_results;   // generated chunks waiting for the render thread
    int chunks_loading;    // slots with a terrain work item in flight
    int chunks_drawable;
    int chunks_occluded;   // drawable chunks past the occlusion hide threshold
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
    // Async server-diff remesh counters (cumulative since init) + jobs
    // currently in flight — see docs/chunks.md "Async server-diff remesh".
    long long async_submitted;
    long long async_published;
    long long async_discarded;
    long long async_promoted;
    int async_in_flight;
};

class ChunkLoader {
    friend class World;
    friend void test_setup_world( World &world );
    friend int test_lighting( );
    friend int test_light_stale( );
    friend int test_falling_blocks( );
    friend int test_grass_persist( );
    friend void light_fill_chunk( Chunk &chunk );
    friend int test_async_remesh( );

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

    // AABB occlusion probing: minimal pos-only shader + unit-cube VBO used
    // to draw each chunk's bounding box inside GL_ANY_SAMPLES_PASSED queries
    // (ChunkLoader::draw, opaque order, normal pass). See ChunkOcclState.
    Shader occl_shader;
    VertexArray occl_va;
    VertexBuffer occl_vb;
    // Position + rotation generations and their last snapshots: verdicts
    // (ChunkOcclState::fails_gen/probe_gen, fails_rot/probe_rot) are tagged
    // with the generations they were earned under. A position bump
    // invalidates every verdict; a rotation bump only invalidates partial
    // (not fully in-frustum) ones — see chunk_occlusion.hpp. Were
    // function-statics in draw(); members now so the second VR eye can
    // reuse eye 0's generations (occl_probe_pass=false) instead of
    // invalidating them with a slightly different mvp.
    long occl_view_gen = 0;
    long occl_rot_gen = 0;
    glm::vec3 occl_last_cam = glm::vec3( 0.0f );
    glm::mat4 occl_last_rot_mvp = glm::mat4( 0.0f );
    bool occl_hides( const Chunk &c ) const {
        return c.occl.hides( this->occl_view_gen, this->occl_rot_gen );
    }
    // When false, draw() neither consumes probe results nor issues new
    // probes (and skips that block's GL state changes) — it only applies the
    // existing occl verdicts. Used for the second stereo eye.
    bool occl_probe_pass = true;

    // Compact list of chunks that are loaded and should be rendered, kept
    // sorted front-to-back by camera distance (chunk_pos tie-break). Iterated
    // by calculate_cull/draw instead of scanning all MAX_LOADED_CHUNKS slots
    // every pass. rebuild_drawable_list maintains it incrementally from the
    // slot_want/slot_listed byte mirror: entries [0,drawable_sorted_n) keep
    // the previous sorted order and newcomers append to the tail, so draw
    // merge-sorts instead of starting from slot order — see ChunkLoader::draw.
    Chunk **drawable_chunks;
    int num_drawable;
    int drawable_sorted_n = 0;
    // The drawable subset with any blended geometry (Translucent or Water
    // layer num_instances != 0), same incremental sort/gating scheme. Water
    // and Translucent passes iterate this back-to-front instead of the full
    // list: their draw order is the compositing order and must stay exact.
    Chunk **blended_chunks;
    int num_blended;
    int blended_sorted_n = 0;
    // Per-slot membership mirror: slot_want[i] is the SLOT_DRAWABLE/
    // SLOT_BLENDED bits recomputed by Chunk::refresh_slot_flags on every
    // write to load_state/should_render/num_instances (each chunk's
    // slot_flags points at its own byte); slot_listed[i] is what the lists
    // currently hold. rebuild_drawable_list diffs the two byte arrays, so a
    // no-change call never touches a Chunk record. A debug-build verify in
    // rebuild re-derives slot_want from the truth fields periodically.
    uint8_t *slot_want;
    uint8_t *slot_listed;
    int compact_slot_list( Chunk **list, int *n, int sorted_n, uint8_t bit );
    // Absolute camera position (render_origin + camera_pos) at the last sort
    // of each list; tracking absolute coords keeps a render-origin snap from
    // looking like movement.
    glm::vec3 last_full_sort_cam = glm::vec3( 0.0f );
    glm::vec3 last_blended_sort_cam = glm::vec3( 0.0f );
    // Precomputed-key scratch for sort_chunks_by_distance.
    std::vector<ChunkSortKey> sort_scratch;
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
    // Flagged drawable chunks the budgeted scan left for later frames —
    // counted during the scan itself and read by next frame's shared
    // frame-work budget so a backed-up remesh queue widens the pool.
    int remesh_flag_backlog = 0;
    // ---- Async server-diff remesh (docs/chunks.md "Async server-diff
    // remesh") — all fields render-thread only unless noted. ----
    // Chunks marked by apply_diff_block since the last submission.
    std::vector<Chunk *> async_wanted_list;
    // seqs of submitted jobs whose results haven't arrived yet — a marked
    // chunk joining a newer job just gets a new seq; publish compares
    // member.async_job_seq against the arriving job's seq.
    std::set<uint32_t> async_in_flight_seqs;
    uint32_t async_next_seq = 0;
    // Visit-stamp generation for the component floods (Chunk::async_visit).
    uint32_t async_visit_gen = 0;
    // Cumulative stats for LoaderDebugStats (ImGui overlay + results.jsonl).
    long long async_jobs_submitted = 0;
    long long async_jobs_published = 0;
    long long async_jobs_discarded = 0;
    long long async_jobs_promoted = 0;
    // -1 = auto (REPGAME_ASYNC_REMESH env && terrain workers exist), else a
    // forced on/off — tests force 1 and run the job inline (no GL, no
    // workers); the env killswitch reads 0.
    int async_remesh_override = -1;
    bool async_remesh_enabled( ) const;
    void async_remesh_mark( Chunk &chunk );
    bool async_member_in_flight( const Chunk &chunk ) const;
    // BFS over async_links edges to LIVE&&needs_repopulation neighbours
    // (seed pushed unconditionally): async_only restricts to async_wanted or
    // in-flight members — the submission flood must fold in-flight
    // members into the newer job so a superseded job can't split a seam.
    // `gen` is this flood's visit stamp (dedup + later link clearing).
    void async_dirty_component( Chunk *seed, bool async_only, uint32_t gen, std::vector<Chunk *> &out );
    // Clears link bits on `c` whose partner is in the just-remeshed group
    // (visit stamp `gen`) or no longer dirty — both directions.
    void async_clear_satisfied_links( Chunk &c, uint32_t gen );
    // budget_us limits render-thread time spent STARTING new components /
    // publishes (LLONG_MAX = unbounded — the test path).
    int async_submit_jobs( long long budget_us = LLONG_MAX );
    void async_publish_results( long long budget_us = LLONG_MAX );
    // Applies/discards one finished job — all-or-nothing: either every
    // still-valid member swaps mesh this frame or none does (the seam
    // invariant). gl_upload=false is the unit-test path (no GL context).
    void async_publish_job( MeshJob &job, bool gl_upload );
    void async_apply_member( Chunk &chunk, MeshJobMember &m );
    void async_upload_member( Chunk &chunk, bool gl_upload );
    // Path-1 deferral: a freshly finalized chunk whose pending diffs dirtied
    // it gets no synchronous upload — the stale worker mesh is dropped and
    // the chunk is marked for an async rebuild instead.
    void finalize_defer_mesh( Chunk &chunk );
    void remesh_now_body( Chunk &chunk, bool gl_upload );
    void remesh_now_impl( Chunk &chunk, bool gl_upload );
    // Set in init; cleared after the first render_chunks fires a box request
    // for the initial visible area. Lets us batch the initial chunk-diff
    // request into one frame instead of per-chunk as terrain gen finishes.
    bool initial_chunk_request_pending = false;
    // Per-slot record of the chunk-grid position the last sent
    // CHUNK_DIFF_REQUEST covered. Diffs are requested by position, but a
    // slot's *landing* position isn't always the position it was enqueued
    // for: TerrainLoadingThread retargets in-flight jobs at pick-up, and the
    // shift scan can't run loading slots through reload_if_out_of_bounds.
    // render_chunks prefetches targets that were never covered and
    // finalize_loaded_chunk re-requests any position still uncovered, so a
    // chunk can never go live as pure generated terrain while its
    // neighbours' halo mirrors hold the real border edits.
    glm::ivec3 slot_diff_req[ MAX_LOADED_CHUNKS ];
    // After a request box is sent, stamp slot_diff_req for every slot whose
    // current target position lies inside the box.
    void stamp_diff_request_box( const glm::ivec3 &center, const glm::ivec3 &box_min, const glm::ivec3 &box_size );
    // Near-heal promotions left this grid shift (light_heal_chunk): each
    // near chunk with queued incident seed work may take one near-lane
    // re-finalize so flying up to a half-lit region converges it promptly.
    int light_heal_budget = 0;
    // light_stale flags as a per-slot atomic bitset (one bit per chunkArray
    // slot; Chunk::light_stale_word/_bit bind into it in init). light_heal_
    // idle walks words and skips zeros instead of probing every Chunk
    // record (see light_heal_idle in light.cpp).
    static constexpr int LIGHT_STALE_WORDS = ( MAX_LOADED_CHUNKS + 63 ) / 64;
    std::atomic<uint64_t> light_stale_bits[ LIGHT_STALE_WORDS ]{};

    // Runtime render distance: horizontal (x/z) active radius in chunks,
    // 1..RENDER_RADIUS_MAX. The torus stays allocated at the compile-time
    // CHUNK_RADIUS_*; slots whose nearest position is outside the active
    // window park (CHUNK_PARKED) and are re-evaluated on each grid-shift
    // rescan. render_radius_applied tracks the radius the last rescan used
    // so a change triggers one even without a center move.
    int render_radius = CHUNK_RADIUS_XZ;
    int render_radius_applied = -1;

    // Deferred park releases (PARK_RELEASE_DEFER_FRAMES): parking flips
    // load_state immediately, but the buffer free waits so a light job
    // that passed its gate just before the park can finish touching the
    // chunk's arrays. park_release_gen invalidates entries for slots that
    // re-entered and re-parked before their defer elapsed.
    struct ParkedRelease {
        Chunk *chunk;
        unsigned int gen;
        long frame;
    };
    std::vector<ParkedRelease> parked_release;
    long park_frame = 0;
    void defer_release( Chunk &chunk );
    // Chunks that finished load+finalize since the last drain. World::render
    // hands them to MobManager::adopt_chunk so pending_mobs (loaded records
    // or natural-spawn roll results) become live mobs.
    std::vector<Chunk *> just_finalized;

    bool pos_in_window( const glm::ivec3 &center, const glm::ivec3 &pos ) const;
    int reload_if_out_of_bounds( Chunk &chunk, const glm::ivec3 &chunk_pos, glm::ivec3 *out_new_pos = nullptr );
    void finalize_loaded_chunk( Chunk &chunk, Multiplayer &multiplayer, const glm::ivec3 &center_next );
    inline int process_chunk_position( Chunk &chunk, const glm::ivec3 &center_next, int force_reload );
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
    // Queues are keyed by the chunk-torus column slot of the seed's target
    // cell (same mod ring as chunk_slot_from_pos): LIGHT_COL_QUEUE_COUNT
    // slots, one per loaded column position. A drainer claims the nearest
    // non-empty, unclaimed slot, so near-ness is evaluated at selection
    // time — seeds queued while their column was far resurface the moment
    // the column re-enters the near radius, with no re-promote pass.
    // Pushes take the slot's own spinlock, so producers and per-column
    // drainers never serialize globally.
    //
    // Buckets are SeedFifo (include/common/utils/seed_fifo.hpp): an empty
    // fifo allocates nothing — a std::deque allocates a map + block even
    // when empty, and the ~140k buckets here are almost all empty. Add
    // seeds store just the position; the channel is implied by the [ch]
    // index and re-attached at pop.
    SeedFifo<glm::ivec3> light_add_queue[ LIGHT_COL_QUEUE_COUNT ][ 2 ][ LIGHT_MAX_LEVEL + 1 ];
    SeedFifo<LightRemoveSeed> light_remove_queue[ LIGHT_COL_QUEUE_COUNT ];
    // push_back a remove seed under the column slot's queue lock.
    // internal=true marks flood-regen pushes (from add/remove step) which the
    // backlog cap may refuse; producer pushes leave it false.
    void light_push_remove_seed( const glm::ivec3 &block_pos, int channel, int old_value, bool internal = false );
    static int light_emit_at( const Chunk &chunk, int index );
    // Null-light (uniform volume) helpers — light.cpp.
    static inline unsigned short light_read_cell( const Chunk &chunk, int idx );
    static void light_materialize( Chunk &chunk );
    static inline void light_write_cell( Chunk &chunk, int idx, unsigned short nv );
    int light_get( const glm::ivec3 &block_pos, int channel ) const;
    void light_set( const glm::ivec3 &block_pos, int channel, int value );
    void light_set_nb( Chunk *const nb_cache[ 27 ], Chunk &owner, const glm::ivec3 &block_pos, int channel, int value );
    BlockID light_block_id_at( const glm::ivec3 &block_pos ) const;
    void light_mark_dirty( Chunk &chunk, const glm::ivec3 &local );
    void light_mark_texel( Chunk &chunk, const glm::ivec3 &local );
    void light_mark_dirty_now( Chunk &chunk, const glm::ivec3 &local );
    void light_flush_deferred_marks( );
    void light_upload_enqueue( Chunk &chunk );
    // Congestion/dead-target/flood-cap gate used by both seed push paths.
    int light_seed_gate( const glm::ivec3 &block_pos, bool near, bool internal );
    void light_add_seed( const glm::ivec3 &block_pos, int channel, int level, bool internal = false );
    void light_add_step( const LightSeed &seed );
    void light_remove_step( const LightRemoveSeed &seed );
    // Pop the next seed from column slot `col`'s queues under that slot's
    // queue lock: 1 = add, 2 = remove, 0 = empty. level records the origin
    // bucket for contention re-queueing.
    int light_pop_seed( int col, LightSeed &add_out, LightRemoveSeed &rem_out, int &level );
    void light_process_queue( long long budget_us, int col );
    void light_process_queue( long long budget_us ); // all columns, single-threaded callers
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
    // budget_us is the shared frame-work remainder; the pass still clamps
    // to its own cap and gets a small floor so uploads can't starve.
    void light_upload_dirty( int max_uploads, long long budget_us );
    // Worker-side upload staging: fills a pooled buffer with the chunk's
    // dirty texels (the expensive smoothed presentation fill) so
    // light_upload_dirty only pays the GL call. Runs on light workers.
    void light_stage_chunk( Chunk &chunk );
    // Shared box/mode computation and fill for the upload path — the render
    // thread (inline fills) and staging workers both go through these.
    // light_upload_box returns the presentation mode (0 = nothing to do)
    // with *uplo/*sz the texel box to fill.
    int light_upload_box( Chunk &chunk, uint64_t raw_box, glm::ivec3 *uplo, glm::ivec3 *sz, bool *self_smooth_out );
    void light_fill_upload_box( Chunk &chunk, const glm::ivec3 &uplo, const glm::ivec3 &sz, bool mipmaps, bool self_smooth, unsigned char *dst );

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
    void init( const glm::dvec3 &camera_pos, const VertexBufferLayout &vbl_block, const VertexBufferLayout &vbl_coords, MapStorage &map_storage, int render_radius );
    void render_chunks( Multiplayer &multiplayer, const glm::dvec3 &camera_pos, int limit_render );
    // Runtime render distance, in horizontal chunks [1, RENDER_RADIUS_MAX].
    // Applied on the next render_chunks rescan; shrink takes effect
    // immediately (slots park), grow streams chunks in like a fast move.
    void set_render_distance( int radius );
    int render_distance_blocks( ) const { return this->render_radius * CHUNK_SIZE_X; }
    void repopulate_blocks( );
    void calculate_cull( const glm::mat4 &mvp, bool saveAsReflection, const glm::ivec3 &renderOrigin ) const;
    void draw( const glm::mat4 &mvp, const Renderer &renderer, const Texture &texture, unsigned order_mask, bool draw_reflect, bool use_frame_buffer, const glm::vec3 &render_origin, const glm::vec3 &camera_pos );
    // Whether draw() runs the occlusion probe block. VR calls with false for
    // the second eye so both eyes share one verdict generation.
    void set_occlusion_probe_pass( bool probe ) { this->occl_probe_pass = probe; }
    Chunk *get_chunk( const glm::ivec3 &pointed ) const;
    // Block-body model instances drawn after each render order's terrain
    // (block_instance_batch.hpp); World fills it in BlockBodies::update_render.
    class BlockInstanceBatch *body_batch = nullptr;
    const glm::ivec3 &get_chunk_center( ) const { return this->chunk_center; }
    // Re-upload every live light texture under the current smoothing mode —
    // the "Shadows" quality toggle calls this (render thread).
    void light_shadow_refresh_all( );
    // Whole-volume dirty mark + upload enqueue (render thread); used by
    // Chunk::light_ensure_texture when a chunk's presentation mode drifts.
    void light_mark_dirty_all( Chunk &chunk );
    void light_mark_texel_box( Chunk &chunk, const glm::ivec3 &lo, const glm::ivec3 &hi );
    // Re-mark the ring texels `flipped` owns inside its neighbours' volumes
    // — needed when its upload presentation mode changes without any field
    // change (Shadows toggle / smoothing-radius crossing).
    void light_remark_owned_halos( Chunk &flipped );
    // Apply one chunk-diff cell (CHUNK_DIFF_RESULT / apply_pending_diffs):
    // writes the receiving chunk's copy, mirrors it into every loaded
    // neighbor that also stores that world cell, and enqueues the touched
    // chunks on repop_must so a modified seam remeshes in one pass.
    void apply_diff_block( Chunk &chunk, int index, const BlockState *blockState );
    // TEMP diagnostic for the flood-fill lighting bring-up. Reports display
    // levels (light_get's raw units / LIGHT_RAW_PER_LEVEL).
    int light_probe( const glm::ivec3 &pos, int channel ) {
        const int v = this->light_get( pos, channel );
        return v < 0 ? v : v / LIGHT_RAW_PER_LEVEL;
    }
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
        for ( int s = 0; s < LIGHT_COL_QUEUE_COUNT; s++ ) {
            total += this->light_remove_queue[ s ].size( );
        }
        return total;
    }
    void debug_loader_stats( LoaderDebugStats *out ) const;
    // Debug (REPGAME_LIGHT_VERIFY): scan settled chunks for cells whose
    // stored light disagrees with what their neighbors offer; logs counts
    // and the first few offenders. Only meaningful with the queues idle.
    void light_debug_verify( );
    void cleanup( MapStorage &map_storage );
};

// REPGAME_TRANSLUCENT_AFTER_WATER: where the translucent order draws
// relative to the water pass (world.cpp). Before = with the main Blocks
// call, After = own call after water, Split = per-fragment split at the
// sea-level water plane (u_SplitSign/u_SplitY in the chunk shaders; the
// default). Always Before when REPGAME_TRANSLUCENT_BLEND=legacy.
enum class TranslucentWaterOrder { Before, After, Split };
TranslucentWaterOrder translucent_water_order( );

// The chunk grid is a torus of size (2*R+1)^3. Each chunk's chunk_mod (which is
// invariant for its slot) uniquely maps to an array index, giving O(1) lookup.
static inline int chunk_slot_from_mod( const glm::ivec3 &chunk_mod ) {
    const int sx = 2 * CHUNK_RADIUS_XZ + 1;
    const int sz = 2 * CHUNK_RADIUS_XZ + 1;
    return chunk_mod.x + sx * ( chunk_mod.z + sz * chunk_mod.y );
}

// Chunk::async_links bit indexing over the 26 neighbour directions:
// (dx+1)+3*(dy+1)+9*(dz+1); the self direction (0,0,0) = index 13 is never
// a valid link.
static inline int async_dir_index( const glm::ivec3 &d ) {
    return ( d.x + 1 ) + 3 * ( ( d.y + 1 ) + 3 * ( d.z + 1 ) );
}
static inline glm::ivec3 async_dir_from_index( const int i ) {
    return glm::ivec3( ( i % 3 ) - 1, ( ( i / 3 ) % 3 ) - 1, ( i / 9 ) - 1 );
}

static inline int chunk_slot_from_pos( const glm::ivec3 &chunk_pos ) {
    const int sx = 2 * CHUNK_RADIUS_XZ + 1;
    const int sy = 2 * CHUNK_RADIUS_Y + 1;
    const int sz = 2 * CHUNK_RADIUS_XZ + 1;
    glm::ivec3 cmod;
    cmod.x = ( ( chunk_pos.x % sx ) + sx ) % sx;
    cmod.y = ( ( chunk_pos.y % sy ) + sy ) % sy;
    cmod.z = ( ( chunk_pos.z % sz ) + sz ) % sz;
    return chunk_slot_from_mod( cmod );
}