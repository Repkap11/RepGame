#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "block.hpp"
#include "common/chest.hpp"
#include "common/furnace.hpp"
#include "common/sign.hpp"
#include "common/chunk_occlusion.hpp"
#include "common/render_order.hpp"
#include "common/utils/map_storage.hpp"

class World;
class TntExplodeEvent;
class ChunkLoader;
struct LightSmoothCtx;
struct ChunkSortKey;
// Draw state lives in common/chunk_render.hpp — layers[] is a heap pointer
// so the renderer headers stay out of this header.
class RenderLayer;
class Renderer;
class Texture;
class Shader;
class VertexBuffer;
class VertexBufferLayout;

// One chunk's mesh built off the render thread (Chunk::build_mesh): per
// render-order arrays of BlockCoords, each malloc'd at exactly the used size
// or nullptr when empty. Ownership moves into layers[].populated_blocks on
// publish (program_terrain then frees it) or is free()d on discard.
struct ChunkMeshResult {
    BlockCoords *blocks[ LAST_RENDER_ORDER ];
    int num[ LAST_RENDER_ORDER ];
};

class WorkingSpace {
    friend class Chunk;

    bool can_be_seen;
    bool visible;
    bool has_been_drawn;
    bool solid;
    unsigned int packed_lighting[ NUM_FACES_IN_CUBE ];
};

// Empty sentinel for Chunk::light_dirty_box — min fields = 255 > max = 0, so
// any decoded box with min > max on an axis is "nothing dirty".
#define LIGHT_BOX_EMPTY 0xFFFFFFULL

// Fixed-size chunk-buffer pool (chunk.cpp). cells[]/extras[]/light[]/
// light_columns[] are always allocated at their full size and never resized,
// so freed buffers go into small per-thread free lists instead of
// round-tripping malloc — multi-megabyte allocations bypass glibc's arena
// and hit mmap/munmap every time, which is costly when slots recycle between
// terrain (allocated) and air (freed) chunks. All alloc functions return a
// buffer suitable for the matching free; _cells/_light_columns come back
// zeroed like calloc.
BlockCell *chunk_alloc_cells( );
void chunk_free_cells( BlockCell *p );
BlockExtra *chunk_alloc_extras( );
void chunk_free_extras( BlockExtra *p );
unsigned short *chunk_alloc_light( );
void chunk_free_light( unsigned short *p );
unsigned char *chunk_alloc_light_columns( );
void chunk_free_light_columns( unsigned char *p );
// Chunk::light_air_mask — CHUNK_BLOCK_SIZE/8 bytes, returned unzeroed (the
// builder writes every bit; the materialize path memsets).
unsigned char *chunk_alloc_air_mask( );
void chunk_free_air_mask( unsigned char *p );

// Chunk slot lifecycle. CHUNK_LIVE is the only state where terrain/mesh/
// light data is usable — every consumer that used to check !is_loading just
// keeps working since the parked state is also nonzero. Parking a slot
// (ChunkLoader::reload_if_out_of_bounds) persists dirty edits through the
// normal work queue then drops every owned buffer via
// Chunk::release_terrain — retained parked data let memory grow toward the
// whole torus instead of following the active window (the wasm32 heap can't
// even hold one full torus). Re-entry reloads the save or regenerates. No
// work item is in flight for parked slots, and the grid-shift rescan
// re-evaluates them when the window grows.
enum ChunkLoadState {
    CHUNK_LIVE = 0,     // loaded + finalized (the old is_loading=0)
    CHUNK_LOADING = 1,  // terrain work item in flight (old is_loading=1)
    CHUNK_PARKED = 2,   // out of window, no data retained — re-entry re-enqueues a load
};

// Per-slot draw-list membership bits for ChunkLoader's slot_want/slot_listed
// byte arrays (rebuild_drawable_list). SLOT_DRAWABLE = live + should_render;
// SLOT_BLENDED adds "has Water or Translucent instances". Chunk::slot_flags
// points at the chunk's slot_want byte; every write to a contributing field
// refreshes it so the loader only ever diffs the byte arrays.
#define SLOT_DRAWABLE 1
#define SLOT_BLENDED 2

// A chunk's 6-bit face-visibility mask relative to a camera chunk — which
// cube faces point toward the player. Chunk::calculate_sides selects each
// layer's shared-index-buffer slice from this. Each axis contributes at
// least one bit (equality sets both), so a real mask is never 0 — the
// calloc'd 0 doubles as "never computed" for Chunk::sides_mask.
static inline unsigned int chunk_face_mask( const glm::ivec3 &pos, const glm::ivec3 &center ) {
    return ( pos.z <= center.z ) | ( ( pos.x <= center.x ) << 1 ) | ( ( pos.z >= center.z ) << 2 ) | //
           ( ( pos.x >= center.x ) << 3 ) | ( ( pos.y <= center.y ) << 4 ) | ( ( pos.y >= center.y ) << 5 );
}

// ---- Flood-fill lighting (light.cpp) ----
// One chunk's whole light pipeline state: the packed volume, the masks the
// texture upload derives from it, and every flag/staging field the async
// workers and the render thread coordinate through. Plain struct — Chunk
// stays effectively public to its friends, so there's no extra
// encapsulation to declare here; grouping just names the subsystem.
// All fields must be safe under calloc zero-init (chunkArray never runs
// constructors): pointers zero, atomics zero, scalars zero.
struct ChunkLight {
    // Per-cell light volume covering the same internal region as blocks.
    // Each u16 packs one byte per channel — skylight high, block light low —
    // in raw units (LIGHT_RAW_PER_LEVEL per display level, see light.hpp).
    // Kept out of the mesh entirely so light changes never force a
    // remesh: the fragment shader samples the matching 3D texture instead.
    // nullptr means the whole volume is uniform: every cell reads
    // uniform (light.cpp reads via light_read_cell; writes materialize
    // the array only when the new value differs).
    unsigned short *volume;
    unsigned short uniform;
    // Per interior column (x,z) in [0,CHUNK_SIZE): two flag arrays packed into
    // one buffer — column_open (no opaque cell in this chunk's column) then
    // sky_open_above (no opaque cell anywhere above in the loaded grid).
    unsigned char *columns;
    // Air mask for the mipmapped light volume's B channel (see
    // light_mipmaps_enabled): 1 bit per cells[] entry — CHUNK_BLOCK_SIZE/8 =
    // 32KB — in the same z-fastest index space as cells[]
    // (get_index_from_coords). Bit set = the cell can hold light
    // (non-opaque, same predicate light_opaque_id uses). A derived cache of
    // cells[], never persisted: built by light_fill_chunk while cells[] is
    // already being streamed, restamped per cell by set_block /
    // set_block_by_index_if_different via light_air_mask_set. nullptr = not
    // built; the light-texture upload paths then fall back to walking
    // cells[] directly, which keeps every edge case correct (a mask is
    // only ever allocated alongside real cells[] data, and only when
    // light_mipmaps_enabled()). Mutable like cells: Chunk::set_block is
    // const but restamps it.
    mutable unsigned char *air_mask;
    // Shade (contact-shadow) mask alongside air_mask — same
    // layout/index space, same lifetime: 1 bit per cells[] entry, set iff
    // the cell is a light floor (blocks_light_down: slabs, path). The
    // light texture's A channel is written 0 for these cells so the shader
    // can re-darken the ground layer a floor sits in — restoring the soft
    // contact shadow the old opaque-cell light=0 blends produced, without
    // re-darkening wall faces beside it. Restamped per cell by
    // light_air_mask_set.
    mutable unsigned char *shade_mask;
    unsigned int texture; // GL_TEXTURE_3D, 0 = not created yet
    // Dirty texel box of volume[] not yet uploaded, in texel coords
    // [0,CHUNK_SIZE_INTERNAL_*): min.xyz in bits [0:7]/[8:15]/[16:23],
    // max.xyz in [24:31]/[32:39]/[40:47] (8 bits per coord). Marks CAS-merge
    // their texel into the box; the render
    // thread exchanges the whole word for LIGHT_BOX_EMPTY while popping the
    // upload list under the work mutex, so a racing mark is always either in
    // the upload's snapshot or visibly unlisted (never lost between a box
    // read and a flag clear).
    std::atomic<uint64_t> dirty_box;
    // Cell indices (into cells[]) changed by multiplayer diffs; converted to
    // light recheck seeds on the render thread. Overflow sets reseed.
    int pending[ 256 ];
    int pending_count;
    int reseed;
    // Set by a light worker when a seed targeting this chunk was refused
    // under the backlog cap (also sets reseed so the heal relights
    // fully); healed by a re-finalize on near-radius entry or the render
    // thread's idle scan (light_heal_* in light.cpp). The flag itself is one
    // bit in ChunkLoader::light_stale_bits (64 slots per word) — the heal
    // scan then skips empty words instead of probing every Chunk record.
    // Bound in ChunkLoader::init / test_setup_world; unbound chunks
    // (hand-built, TNT temps) no-op on set/clear and read false.
    std::atomic<uint64_t> *stale_word = nullptr;
    uint64_t stale_bit = 0;
    bool stale_get( ) const {
        return this->stale_word != nullptr && //
               ( this->stale_word->load( std::memory_order_relaxed ) & this->stale_bit ) != 0;
    }
    void stale_set( ) {
        if ( this->stale_word == nullptr ) {
            return;
        }
        // Load first: light_seed_gate is hot under flood, and a blind
        // fetch_or would pay an RMW on a word shared by 64 chunks.
        if ( this->stale_word->load( std::memory_order_relaxed ) & this->stale_bit ) {
            return;
        }
        this->stale_word->fetch_or( this->stale_bit, std::memory_order_relaxed );
    }
    bool stale_take( ) {
        return this->stale_word != nullptr && //
               ( this->stale_word->fetch_and( ~this->stale_bit, std::memory_order_relaxed ) & this->stale_bit ) != 0;
    }
    void stale_clear( ) {
        if ( this->stale_word != nullptr && //
             ( this->stale_word->load( std::memory_order_relaxed ) & this->stale_bit ) != 0 ) {
            this->stale_word->fetch_and( ~this->stale_bit, std::memory_order_relaxed );
        }
    }
    // Set while the chunk sits in the pending-light drain list (see
    // light_pending_enqueue) so producers don't enqueue it twice. Atomic
    // because light_process_pending_chunk clears it outside the work mutex.
    std::atomic<int> pending_listed;
    // Same, for the dirty-light-volume upload list. Atomic because
    // light_mark_dirty checks it lock-free on the hot path.
    std::atomic<int> upload_listed;
    // Game tick of the last visible light write (stamped by every
    // light_mark_dirty* call). light_upload_dirty defers chunks still being
    // written so the GPU never snapshots a mid-flood/mid-relight volume —
    // the correct -> floor -> correct swing of a passing remove front or a
    // reseed wipe showed up as a one-frame black flash.
    std::atomic<long> write_tick{ 0 };
    // Frames this chunk's upload was deferred for being write-hot; render
    // thread only (light_upload_dirty). A cap forces upload anyway so a
    // continuously-marched chunk can't starve forever.
    int upload_defers = 0;
    // Set once this chunk's light volume has been uploaded at least once.
    // Fresh chunks bypass the quiet/busy upload gates: the optimistic sky
    // fill is mostly correct and beats showing a black hole while seeds
    // are still pending. Render thread only; reset when the slot is reused.
    int uploaded_once = 0;
    // Presentation mode the texture was last fully filled under:
    // 0 = never filled, 1 = raw expand, 2 = smoothed. A mismatch vs the
    // current desired mode (Shadows toggle, or the chunk crossing
    // REPGAME_LIGHT_SMOOTH_RADIUS) upgrades the next upload to a
    // full-volume refill — partial fills under a new mode would leave a
    // mixed-mode texture. Atomic: written by render on upload completion,
    // read by staging workers computing the same check.
    std::atomic<int> tex_mode{ 0 };
    // The mode a flip was claimed for but has not landed yet (0 = none).
    // Keeps repeated ensure calls from re-running the flip marks every
    // frame while the queued refill is in flight.
    std::atomic<int> tex_flip{ 0 };
    // Worker-side upload staging: a light worker fills a pooled buffer with
    // the chunk's dirty texels so the render thread only pays the GL call.
    // stage_state packs a generation (high bits) and status (low 2):
    //   0 idle, 1 worker staging, 2 staged data ready to upload.
    // The CAS that publishes the buffer carries the generation the worker
    // claimed under, so a slot recycled mid-stage (release bumps the
    // generation) can never publish a stale buffer onto the new chunk.
    std::atomic<unsigned> stage_state{ 0 };
    unsigned char *stage_buf = nullptr; // pooled; valid while status==2
    uint64_t stage_box = 0;             // packed texel box, status==2
    int stage_mode = 0;                 // presentation mode, status==2
    // Set at the end of light_finalize_chunk — routes FINALIZE jobs for
    // never-finalized chunks (no real skylight yet, only the optimistic
    // fill) into the init job lane, ahead of far-lane work but behind
    // near-player work. Atomic: written by the job's worker, read by the
    // render thread at submit; a torn read just picks a lane.
    std::atomic<int> finalized{ 0 };
};

// Sidecar entity data staged by load_terrain on the worker and drained by
// MobManager::adopt_chunk / ItemEntities::adopt_chunk after finalize —
// see those callers. Cleared by release_terrain. Plain struct like
// ChunkLight: grouping only, same calloc zero-init contract.
struct ChunkSidecars {
    // Mob sidecar data: records loaded from the mobs file or produced by the
    // natural-spawn roll, whether the file existed, whether the spawn roll
    // produced any mobs, and the tombstoned natural-roll indices stored in
    // the file (mobs_removed).
    std::vector<MobFileRecord> mobs;
    int mobs_file_present;
    int mobs_spawned;
    std::vector<uint8_t> mobs_removed;
    // Dropped-item sidecar, same pattern as mobs: items_file_present tracks
    // whether an items file existed so the leave path rewrites it (even
    // empty) instead of reviving stale records.
    std::vector<ItemFileRecord> items;
    int items_file_present;
    // Minecart sidecar, same pattern as items.
    std::vector<CartFileRecord> carts;
    int carts_file_present;
    // A player edit touched this chunk's blocks: set by saved-edit file
    // hits, ChunkLoader::apply_diff_block and
    // World::set_loaded_block — never by random ticks or structure gen.
    // MobManager uses it to "touch" natural mobs standing in a pen.
    // Mutable like the other load-time fields (worker fills it).
    mutable uint8_t player_edited;
};

// Per-chunk container contents (chests/furnaces), keyed by in-chunk block
// index (same space as cells[]) and backed by the chunk's chest/furnace
// sidecar files. Plain struct like ChunkLight: grouping only, same calloc
// zero-init contract.
struct ChunkContainers {
    // Chest contents. Loaded from the chunk's chest file in load_terrain,
    // persisted in Chunk::persist; edits mark dirty. Mutable:
    // Chunk::set_block (a const path used by World::set_loaded_block)
    // erases entries when a chest block is overwritten.
    mutable std::map<int, ChestContents> chests;
    // Whether this chunk's chest file was read (or chest data created) during
    // load_terrain/afterwards — persist_chests only runs when set so a chunk
    // whose chest file failed to read doesn't get its file clobbered.
    mutable int chests_loaded;
    // Furnace contents — same pattern as chests above, backed by the
    // chunk's furnace sidecar file. FURNACE and LIT_FURNACE share one
    // entry; the lit swap never erases it.
    mutable std::map<int, FurnaceContents> furnaces;
    mutable int furnaces_loaded;
    // Sign front text — same sidecar pattern as chests, keyed by in-chunk
    // block index. Entries with empty() text are never stored.
    mutable std::map<int, SignText> signs;
    mutable int signs_loaded;
};

class Chunk {
    friend class ChunkLoader;
    friend class World;
    friend class MapGen;
    friend class StructureGen;
    friend class MapStorage;
    friend class MobManager;
    friend class ArrowEntities;
    friend class BlockBodies;
    friend class ItemEntities;
    friend class Minecarts;
    friend class NetEntities;
    friend class TerrainLoadingThread;
    friend class Multiplayer;
    friend class TntExplodeEvent;
    friend void test_setup_world( World &world );
    friend void light_fill_chunk( Chunk &chunk );
    friend void light_air_mask_set( const Chunk &chunk, int index, BlockID id );
    friend void light_reset_staging( Chunk *chunk );
    friend void light_pending_enqueue( Chunk &chunk );
    friend int test_lighting( );
    friend int test_structure_border( );
    friend int test_item_entities( );
    friend int test_minecarts( );
    friend int test_mobs( );
    friend int test_farming( );
    friend int test_doors( );
    friend int test_chunk_sort( );
    friend int test_light_stale( );
    friend int test_grass_persist( );
    friend int test_chunk_edits( );
    friend int test_async_remesh( );
    friend void sort_chunks_by_distance( Chunk **list, int n, int sorted_n, const glm::vec3 &render_origin, const glm::vec3 &cam, bool full_prefix, std::vector<ChunkSortKey> &scratch );

    int load_state;
    int gl_initialized;
    bool is_empty_chunk;
    // Per-layer draw state (chunk_render.hpp) — calloc'd on first use
    // (Chunk::init on real slots, the meshing entry points on hand-built
    // chunks) and freed by release_terrain. nullptr on chunks that never
    // meshed or were released; every access site must tolerate that.
    RenderLayer *layers = nullptr;
    // Split cell storage — see BlockCell/BlockExtra in block_definitions.hpp.
    // cells == nullptr means every cell is AIR (is_empty_chunk); extras ==
    // nullptr means every cell is canonical (power 0, display_id == id —
    // rotation and the powered flag live in the cell word itself; extras
    // hold the exact power value for gameplay, never rendered/uploaded).
    // Both are mutable so the const set_block can allocate on the first
    // real write.
    mutable BlockCell *cells;
    mutable BlockExtra *extras;
    glm::ivec3 chunk_pos;
    int dirty;
    // Bitset marking the cells runtime edits have touched (same cells[]
    // index space). Separate from `dirty`: mapgen, structure gen and
    // container fills write cells without setting these bits, and
    // MapStorage::persist_chunk saves only the marked cells as edit runs —
    // the rest regenerates. nullptr until the first edit. Lifetime is
    // separate from cells (free_block_storage leaves it alone — the
    // all-air fold still needs the bits); freed by load_terrain,
    // release_terrain, free_generated_buffers and inspect_load.
    mutable uint8_t *edited;
    // Set when a chunk file exists but fails to decode (or decodes to an
    // inconsistent run list): persist_chunk refuses to write so a corrupt
    // file isn't clobbered.
    mutable int edits_read_failed;
    // MPLOAD trace (REPGAME_MPLOAD_TRACE): timestamp (mpload_now_ms) of the
    // first live-applied diff cell since the last remesh, 0 when unset.
    // Read/reset by ChunkLoader::remesh_now; reset on reload/release.
    long long mpload_diff_ms = 0;
    // Set by load_terrain while it overlays saved edits — mark_edited no-ops
    // so the overlay can't mark its own cells.
    int applying_edits;
    // Chest/furnace contents keyed by in-chunk block index — see
    // ChunkContainers above.
    ChunkContainers containers;
    // Mob/item sidecar staging and the player-edited flag — see
    // ChunkSidecars above.
    ChunkSidecars sidecars;
    int should_render;
    // Points at this slot's byte in ChunkLoader::slot_want (set in
    // ChunkLoader::init); every write to load_state/should_render/num_
    // instances refreshes it via refresh_slot_flags so rebuild_drawable_list
    // can diff per-slot bytes instead of probing every Chunk. Null for
    // hand-built chunks (tests) — refresh_slot_flags then no-ops.
    uint8_t *slot_flags = nullptr;
    void refresh_slot_flags( );
    // Allocate layers[] on first use — see the member comment.
    void ensure_layers( );
    void set_load_state( int state ) {
        this->load_state = state;
        this->refresh_slot_flags( );
    }
    void set_should_render( int sr ) {
        this->should_render = sr;
        this->refresh_slot_flags( );
    }
    int cached_cull_normal;
    int cached_cull_reflect;
    // Park-release generation: bumped each time ChunkLoader defers this
    // slot's buffer release, so a stale deferred entry (slot re-entered
    // and re-parked before the old one fired) can't release fresh data.
    unsigned int park_release_gen;
    // AABB occlusion-probe state — see ChunkOcclState
    // (common/chunk_occlusion.hpp) for the position/rotation/full verdict
    // validity rules.
    ChunkOcclState occl;
    int needs_repopulation;
    // Set while this chunk sits in ChunkLoader::repop_must (dedupes the
    // enqueue). Cleared when the queue pops it, remeshed or not.
    int repop_must_queued;
    // Monotonic cell-content generation: bumped (relaxed) on every write to
    // cells[]/extras[] — store_state covers set_block /
    // set_block_by_index_if_different / the load-time edits overlay, while
    // load_terrain and release_terrain bump once at the end for the raw
    // array fills/frees. Async mesh jobs snapshot it; a mismatch at publish
    // discards the stale result. Never reset. Mutable because the
    // cell-writing const paths (set_block) go through it.
    mutable std::atomic<uint32_t> cell_version;
    // Async server-diff remesh state — render thread only, see the "Async
    // server-diff remesh" section of docs/chunks.md. async_wanted = marked
    // for the next submission; async_discards = consecutive version-mismatch
    // drops (3 promotes the component to a sync remesh); async_job_seq = id
    // of the newest submitted job containing this chunk (0 = none).
    uint8_t async_wanted;
    uint8_t async_discards;
    uint32_t async_job_seq;
    // Seam coupling edges, set only by apply_diff_block mirror writes (a diff
    // write that actually crossed a shared border): bit index d = the
    // neighbour in 26-direction d (index map: (dx+1)+3*(dy+1)+9*(dz+1); the
    // self index 13 is never set). Both directions are set by the mirror and
    // cleared once a publish/promotion satisfied the link.
    uint32_t async_links;
    // Visit stamp for the loader's component floods (ChunkLoader::
    // async_visit_gen) — O(1) dedup instead of per-member std::find.
    uint32_t async_visit;
    glm::ivec3 chunk_mod;
    // The chunk_face_mask the layers' ib slices were last built from
    // (Chunk::calculate_sides). process_chunk_position recomputes whenever
    // this differs from the mask the current camera chunk implies — a slot
    // can miss plane crossings while parked (the park window tests x/z
    // together and ignores y entirely) or during a multi-chunk move, and
    // the stale slice then skips the player-facing faces (see-through
    // block sides) until some later move happens to recalc.
    unsigned int sides_mask;

    // ---- Flood-fill lighting (light.cpp) — see ChunkLight above ----
    ChunkLight light;

    static int can_extend_rect( const BlockCell *cells, const BlockExtra *extras, const BlockState &blockState, const unsigned int *packed_lighting, const WorkingSpace *workingSpace, const glm::ivec3 &starting, const glm::ivec3 &size, const glm::ivec3 &dir );

    // Fills the 26-slot neighbour predicate table the upload-time light
    // smoother uses for boundary (halo) texels — light.cpp. Member function
    // because it reads other chunks' private light/cells fields; the
    // ChunkLoader friend calls it for the dirty-upload path too.
    static void light_nb_ctxs( const ChunkLoader &loader, const glm::ivec3 &pos, const glm::ivec3 &center,
                               LightSmoothCtx nb_ctxs[ 27 ], const LightSmoothCtx *nb_ptrs[ 27 ], unsigned *raw_owners );

  public:
    void init( const VertexBuffer &vb_block_solid, const VertexBuffer &vb_block_water, const VertexBufferLayout &vbl_block, const VertexBufferLayout &vbl_coords );
    void ensure_gl_init( );
    void light_ensure_texture( ChunkLoader *loader = nullptr ); // lazily create the 3D light texture (GL thread)
    void draw( const Renderer &renderer, const Texture &texture, Shader &shader, RenderOrder renderOrder, bool draw_reflect, const glm::vec3 &render_origin, int light_base_loc );
    void load_terrain( MapStorage &map_storage ); // Load from file or map gen
    void program_terrain( );                      // Program into GPU
    void unprogram_terrain( );                    // Remove from GPU
    // Drop every owned buffer (cells/extras, light volumes + mask, per-layer
    // GL objects + populated_blocks, light texture, occlusion query) so the
    // slot holds no terrain data — used when parking (chunk_loader.cpp) and
    // by destroy(). Render thread only: it deletes GL objects. Afterwards
    // the slot is equivalent to never-loaded.
    void release_terrain( );
    // Natural mob-spawn roll (load_terrain + multiplayer CHUNK_ROLE owner).
    // Deterministic per (chunk_pos, type, MAP_SEED): every emitted record gets
    // net_id = net_natural_mob_id(chunk_pos, index) + the NATURAL flag, where
    // index counts the roll's records in emission order. Indices in
    // `skip` (tombstoned naturals, ids already restored from file) are
    // omitted from the output but still consume an index.
    bool spawn_mobs( const std::set<uint8_t> *skip = nullptr );
    void calculate_sides( const glm::ivec3 &center_next );
    static int get_coords_from_index_todo( int index, glm::ivec3 &out_pos );
    static int get_coords_from_index( int index, int &out_x, int &out_y, int &out_z );

    void persist( MapStorage &map_storage ) const;
    void destroy( );
    // Offline inspection (harness/inspect): runs the whole load_terrain
    // pipeline for `pos` and copies the result out — `out` gets
    // CHUNK_BLOCK_SIZE states and `edited_bits` (may be null)
    // CHUNK_BLOCK_SIZE/8 bytes of edit bits. Returns 1 when a chunk file
    // existed. No GL objects are created.
    static int inspect_load( MapStorage &map_storage, const glm::ivec3 &pos, BlockState *out, uint8_t *edited_bits );
    void set_block( const glm::ivec3 &pos, const BlockState &blockState ) const;
    // Returns 1 if the cell actually changed, 0 for no-op writes (equal
    // state, AIR into an all-air chunk) and out-of-range indices.
    int set_block_by_index_if_different( int index, const BlockState *blockState );
    // Mark `index` as a runtime edit. Lazily allocates the bitset; no-ops
    // while `applying_edits` is set — load_terrain holds it for the whole
    // generate-plus-overlay pass so mapgen, structure gen and the overlay
    // itself never record their writes as edits. Index space is the
    // usual cells[] space.
    inline void mark_edited( const int index ) const {
        if ( this->applying_edits ) {
            return;
        }
        if ( this->edited == nullptr ) {
            this->edited = static_cast<uint8_t *>( calloc( CHUNK_BLOCK_SIZE / 8, 1 ) );
        }
        this->edited[ index >> 3 ] |= static_cast<uint8_t>( 1u << ( index & 7 ) );
    }
    inline int is_edited( const int index ) const {
        return this->edited != nullptr && ( ( this->edited[ index >> 3 ] >> ( index & 7 ) ) & 1 );
    }
    inline int has_edits( ) const {
        return this->edited != nullptr;
    }
    BlockState get_block( const glm::ivec3 &pos ) const;
    // Assemble the stored BlockState for `index`. Requires cells != nullptr;
    // index space is the usual get_index_from_coords (halo included).
    inline BlockState state_at( const int index ) const {
        return block_state_unpack( this->cells[ index ], this->extras ? &this->extras[ index ] : nullptr );
    }
    inline BlockID id_at( const int index ) const {
        return block_cell_id( this->cells[ index ] );
    }
    // Write `s` into the split arrays (cells must be allocated). Allocates
    // extras when the new state isn't canonical for its id. Inline so
    // map_storage.o (linked into the server without chunk.o) can use it.
    inline void store_state( const int index, const BlockState &s ) const {
        this->cell_version.fetch_add( 1u, std::memory_order_relaxed );
        this->cells[ index ] = block_cell_pack( s.id, s.waterlogged, s.rotation, s.current_redstone_power != 0 );
        if ( this->extras == nullptr && !block_state_is_canonical( s ) ) {
            this->materialize_extras( );
        }
        if ( this->extras != nullptr ) {
            this->extras[ index ] = block_state_pack_extra( s );
        }
    }
    // Realize extras (if absent) so every cell has an entry — used on the
    // first non-canonical write; canonical values are derived from cells[].
    inline void materialize_extras( ) const {
        if ( this->extras != nullptr ) {
            return;
        }
        this->extras = chunk_alloc_extras( );
        // Every existing cell is canonical by definition (extras was null),
        // so backfill each entry from its id. Callers overwrite the cell
        // they materialized for right after.
        for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
            const BlockID id = block_cell_id( this->cells[ i ] );
            this->extras[ i ] = { static_cast<uint16_t>( id ), 0 };
        }
    }
    // Pure meshing: reads only cells/extras/chunk_pos and writes `out` —
    // touches no Chunk state, so several worker threads can run it
    // concurrently (async server-diff remesh jobs). cells == nullptr meshes
    // an all-air chunk (every order gets num 0 / blocks nullptr).
    static void build_mesh( const BlockCell *cells, const BlockExtra *extras, const glm::ivec3 &chunk_pos, ChunkMeshResult &out );
    void free_block_storage( );
    // Free cells/extras/light_air_mask on a throwaway chunk generated by the
    // CPU terrain+structure pipeline (inverse-TNT restore). Does not touch GL
    // objects (a generated temp never had any). Callers allocate with
    // chunk_alloc_cells and set extras/light_air_mask to nullptr before gen.
    void free_generated_buffers( );
    void calculate_populated_blocks( );

    static inline int get_index_from_coords( const glm::ivec3 &pos ) {
        return ( pos.y + 1 ) * CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Z + ( pos.x + 1 ) * CHUNK_SIZE_INTERNAL_Z + ( pos.z + 1 );
    }

    static inline int get_index_from_coords( int x, int y, int z ) {
        return ( y + 1 ) * CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Z + ( x + 1 ) * CHUNK_SIZE_INTERNAL_Z + ( z + 1 );
    }
};

// Enqueue a chunk for lighting-thread draining (light_drain_pending).
// Called by Chunk::set_block_by_index_if_different from network/worker
// threads; no-ops when the chunk is already listed.
void light_pending_enqueue( Chunk &chunk );