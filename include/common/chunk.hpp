#pragma once

#include <atomic>
#include <cstdint>
#include <map>

#include "block.hpp"
#include "common/chest.hpp"
#include "common/utils/map_storage.hpp"
#include "renderer/renderer.hpp"
#include "server/server.hpp"

class World;

class RenderLayer {
    friend class Chunk;

    VertexArray va;
    int num_instances;
    VertexBuffer vb_coords;
    BlockCoords *populated_blocks;
    // Index data is a pure function of the chunk's 6-bit face-visibility mask,
    // so all variants live in one shared element buffer (chunk.cpp) and each
    // layer stores {byte offset, element count} per pass: [0]=normal,
    // [1]=reflect. count==0 draws nothing (e.g. water has no reflect variant).
    unsigned int ib_offset[ 2 ];
    unsigned int ib_count[ 2 ];
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

// Consecutive failed AABB probes before a chunk is treated as occluded (see
// Chunk::occl_fails). >1 absorbs a stale in-flight result read after slot
// reuse, at the cost of one extra frame of overdraw on real occlusion.
#define OCCL_HIDE_FRAMES 2

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
unsigned char *chunk_alloc_light( );
void chunk_free_light( unsigned char *p );
unsigned char *chunk_alloc_light_columns( );
void chunk_free_light_columns( unsigned char *p );
// Chunk::light_air_mask — CHUNK_BLOCK_SIZE/8 bytes, returned unzeroed (the
// builder writes every bit; the materialize path memsets).
unsigned char *chunk_alloc_air_mask( );
void chunk_free_air_mask( unsigned char *p );

// Chunk slot lifecycle. CHUNK_LIVE is the only state where terrain/mesh/
// light data is usable — every consumer that used to check !is_loading just
// keeps working since parked states are also nonzero. The parked states are
// render-distance slots outside the active window (ChunkLoader::
// render_radius): no work item is in flight for them, and the grid-shift
// rescan re-evaluates them when the window grows.
enum ChunkLoadState {
    CHUNK_LIVE = 0,               // loaded + finalized (the old is_loading=0)
    CHUNK_LOADING = 1,            // terrain work item in flight (old is_loading=1)
    CHUNK_PARKED = 2,             // out of window, finalized data retained — reactivates instantly
    CHUNK_PARKED_UNFINALIZED = 3, // out of window, never meshed/lit (cells==nullptr = never even loaded)
};

class Chunk {
    friend class ChunkLoader;
    friend class World;
    friend class MapGen;
    friend class StructureGen;
    friend class MapStorage;
    friend class TerrainLoadingThread;
    friend class Multiplayer;
    friend void test_setup_world( World &world );
    friend void light_fill_chunk( Chunk &chunk );
    friend void light_air_mask_set( const Chunk &chunk, int index, BlockID id );
    friend void light_pending_enqueue( Chunk &chunk );
    friend int test_lighting( );

    int load_state;
    int gl_initialized;
    bool is_empty_chunk;
    RenderLayer layers[ LAST_RENDER_ORDER ];
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
    // Container contents for chest blocks in this chunk, keyed by in-chunk
    // block index (same space as cells[]). Loaded from the chunk's chest
    // file in load_terrain, persisted in Chunk::persist; edits mark dirty.
    // Mutable: Chunk::set_block (a const path used by World::set_loaded_block)
    // erases entries when a chest block is overwritten.
    mutable std::map<int, ChestContents> chests;
    // Whether this chunk's chest file was read (or chest data created) during
    // load_terrain/afterwards — persist_chests only runs when set so a chunk
    // whose chest file failed to read doesn't get its file clobbered.
    mutable int chests_loaded;
    int should_render;
    int cached_cull_normal;
    int cached_cull_reflect;
    // AABB occlusion-probe state (ChunkLoader::draw, normal pass only). Each
    // frustum-visible chunk probes its [0,CHUNK_SIZE) bounding box against
    // the opaque depth buffer once per frame (GL_ANY_SAMPLES_PASSED).
    // occl_fails counts consecutive failed probes; at OCCL_HIDE_FRAMES every
    // render layer skips the chunk — conservative, since its content is
    // confined to the box. Hidden chunks keep probing, so a reveal un-hides
    // them the frame the box reappears. occl_query is lazily created on the
    // GL thread and persists across slot reuse.
    // Verdicts are view-relative: a probe only proves coverage against the
    // depth buffer of the view it was issued under. occl_probe_gen /
    // occl_fails_gen tag the ChunkLoader::draw view generation each piece of
    // state was earned under — after the camera moves, stale results are
    // discarded and stale fails stop hiding, so a fast turn re-tests
    // instead of leaving a see-through hole while verdicts drain.
    unsigned int occl_query;
    int occl_pending;
    int occl_fails;
    long occl_probe_gen;
    long occl_fails_gen;
    int needs_repopulation;
    // Set while this chunk sits in ChunkLoader::repop_must (dedupes the
    // enqueue). Cleared when the queue pops it, remeshed or not.
    int repop_must_queued;
    glm::ivec3 chunk_mod;

    // ---- Flood-fill lighting (light.cpp) ----
    // Per-cell light volume covering the same internal region as blocks.
    // Each byte packs skylight in the high nibble and block light in the low
    // nibble. Kept out of the mesh entirely so light changes never force a
    // remesh: the fragment shader samples the matching 3D texture instead.
    // nullptr means the whole volume is uniform: every cell reads
    // light_uniform (light.cpp reads via light_read_cell; writes materialize
    // the array only when the new value differs).
    unsigned char *light;
    unsigned char light_uniform;
    // Per interior column (x,z) in [0,CHUNK_SIZE): two flag arrays packed into
    // one buffer — column_open (no opaque cell in this chunk's column) then
    // sky_open_above (no opaque cell anywhere above in the loaded grid).
    unsigned char *light_columns;
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
    mutable unsigned char *light_air_mask;
    unsigned int light_texture; // GL_TEXTURE_3D, 0 = not created yet
    // Dirty texel box of light[] not yet uploaded, in texel coords
    // [0,CHUNK_SIZE_INTERNAL_*): min.xyz in bits [0:7]/[8:15]/[16:23],
    // max.xyz in [24:31]/[32:39]/[40:47] (8 bits per coord). Marks CAS-merge
    // their texel into the box; the render
    // thread exchanges the whole word for LIGHT_BOX_EMPTY while popping the
    // upload list under the work mutex, so a racing mark is always either in
    // the upload's snapshot or visibly unlisted (never lost between a box
    // read and a flag clear).
    std::atomic<uint64_t> light_dirty_box;
    // Cell indices (into cells[]) changed by multiplayer diffs; converted to
    // light recheck seeds on the render thread. Overflow sets light_reseed.
    int light_pending[ 256 ];
    int light_pending_count;
    int light_reseed;
    // Set by a light worker when a seed targeting this chunk was refused
    // under the backlog cap (also sets light_reseed so the heal relights
    // fully); healed by a re-finalize on near-radius entry or the render
    // thread's idle scan (light_heal_* in light.cpp).
    std::atomic<int> light_stale;
    // Set while the chunk sits in the pending-light drain list (see
    // light_pending_enqueue) so producers don't enqueue it twice.
    int light_pending_listed;
    // Same, for the dirty-light-volume upload list. Atomic because
    // light_mark_dirty checks it lock-free on the hot path.
    std::atomic<int> light_upload_listed;
    // Game tick of the last visible light write (stamped by every
    // light_mark_dirty* call). light_upload_dirty defers chunks still being
    // written so the GPU never snapshots a mid-flood/mid-relight volume —
    // the correct -> floor -> correct swing of a passing remove front or a
    // reseed wipe showed up as a one-frame black flash.
    std::atomic<long> light_write_tick{ 0 };
    // Frames this chunk's upload was deferred for being write-hot; render
    // thread only (light_upload_dirty). A cap forces upload anyway so a
    // continuously-marched chunk can't starve forever.
    int light_upload_defers = 0;
    // Set once this chunk's light volume has been uploaded at least once.
    // Fresh chunks bypass the quiet/busy upload gates: the optimistic sky
    // fill is mostly correct and beats showing a black hole while seeds
    // are still pending. Render thread only; reset when the slot is reused.
    int light_uploaded_once = 0;
    // Set at the end of light_finalize_chunk — routes FINALIZE jobs for
    // never-finalized chunks (no real skylight yet, only the optimistic
    // fill) into the init job lane, ahead of far-lane work but behind
    // near-player work. Atomic: written by the job's worker, read by the
    // render thread at submit; a torn read just picks a lane.
    std::atomic<int> light_finalized{ 0 };

    int can_extend_rect( const BlockState &blockState, const unsigned int *packed_lighting, const WorkingSpace *workingSpace, const glm::ivec3 &starting, const glm::ivec3 &size, const glm::ivec3 &dir ) const;

  public:
    void init( const VertexBuffer &vb_block_solid, const VertexBuffer &vb_block_water, const VertexBufferLayout &vbl_block, const VertexBufferLayout &vbl_coords );
    void ensure_gl_init( );
    void light_ensure_texture( ); // lazily create the 3D light texture (GL thread)
    void draw( const Renderer &renderer, const Texture &texture, Shader &shader, RenderOrder renderOrder, bool draw_reflect, const glm::vec3 &render_origin, int light_base_loc );
    void load_terrain( MapStorage &map_storage ); // Load from file or map gen
    void program_terrain( );                      // Program into GPU
    void unprogram_terrain( );                    // Remove from GPU
    void calculate_sides( const glm::ivec3 &center_next );
    static int get_coords_from_index_todo( int index, glm::ivec3 &out_pos );
    static int get_coords_from_index( int index, int &out_x, int &out_y, int &out_z );

    void persist( MapStorage &map_storage ) const;
    void destroy( );
    void set_block( const glm::ivec3 &pos, const BlockState &blockState ) const;
    // Returns 1 if the cell actually changed, 0 for no-op writes (equal
    // state, AIR into an all-air chunk) and out-of-range indices.
    int set_block_by_index_if_different( int index, const BlockState *blockState );
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
    void free_block_storage( );
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