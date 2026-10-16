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

// Empty sentinel for Chunk::light_dirty_box — min fields = 63 > max = 0, so
// any decoded box with min > max on an axis is "nothing dirty".
#define LIGHT_BOX_EMPTY 0x3FFFFULL

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
    friend void light_pending_enqueue( Chunk &chunk );
    friend int test_lighting( );

    int is_loading;
    int gl_initialized;
    bool is_empty_chunk;
    RenderLayer layers[ LAST_RENDER_ORDER ];
    // nullptr means every cell is AIR (is_empty_chunk). Mutable so the const
    // set_block can allocate the array on the first real write.
    mutable BlockState *blocks;
    glm::ivec3 chunk_pos;
    int dirty;
    // Container contents for chest blocks in this chunk, keyed by in-chunk
    // block index (same space as blocks[]). Loaded from the chunk's chest
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
    int needs_repopulation;
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
    unsigned int light_texture; // GL_TEXTURE_3D, 0 = not created yet
    // Dirty texel box of light[] not yet uploaded, in texel coords [0,34):
    // min.xyz in bits [0:5]/[6:11]/[12:17], max.xyz in [18:23]/[24:29]/[30:35]
    // (6 bits per coord). Marks CAS-merge their texel into the box; the render
    // thread exchanges the whole word for LIGHT_BOX_EMPTY while popping the
    // upload list under the work mutex, so a racing mark is always either in
    // the upload's snapshot or visibly unlisted (never lost between a box
    // read and a flag clear).
    std::atomic<uint64_t> light_dirty_box;
    // Cell indices (into blocks[]) changed by multiplayer diffs; converted to
    // light recheck seeds on the render thread. Overflow sets light_reseed.
    int light_pending[ 256 ];
    int light_pending_count;
    int light_reseed;
    // Set while the chunk sits in the pending-light drain list (see
    // light_pending_enqueue) so producers don't enqueue it twice.
    int light_pending_listed;
    // Same, for the dirty-light-volume upload list. Atomic because
    // light_mark_dirty checks it lock-free on the hot path.
    std::atomic<int> light_upload_listed;

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
    void set_block_by_index_if_different( int index, const BlockState *blockState );
    BlockState get_block( const glm::ivec3 &pos ) const;
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

// Fixed-size chunk-buffer pool (chunk.cpp). blocks[]/light[]/light_columns[]
// are always allocated at their full size and never resized, so freed buffers
// go into small per-thread free lists instead of round-tripping malloc —
// multi-megabyte allocations bypass glibc's arena and hit mmap/munmap every
// time, which is costly when slots recycle between terrain (allocated) and
// air (freed) chunks. All alloc functions return a buffer suitable for the
// matching free; _blocks/_light_columns come back zeroed like calloc.
BlockState *chunk_alloc_blocks( );
void chunk_free_blocks( BlockState *p );
unsigned char *chunk_alloc_light( );
void chunk_free_light( unsigned char *p );
unsigned char *chunk_alloc_light_columns( );
void chunk_free_light_columns( unsigned char *p );