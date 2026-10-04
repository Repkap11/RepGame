#pragma once

#include "block.hpp"
#include "common/utils/map_storage.hpp"
#include "renderer/renderer.hpp"
#include "server/server.hpp"

class World;

#define CHUNK_SIZE_INTERNAL_X ( CHUNK_SIZE_X + 2 )
#define CHUNK_SIZE_INTERNAL_Y ( CHUNK_SIZE_Y + 2 )
#define CHUNK_SIZE_INTERNAL_Z ( CHUNK_SIZE_Z + 2 )
#define CHUNK_BLOCK_SIZE ( CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Y * CHUNK_SIZE_INTERNAL_Z )
#define CHUNK_BLOCK_DRAW_START ( CHUNK_SIZE_INTERNAL_Z * CHUNK_SIZE_INTERNAL_X )
#define CHUNK_BLOCK_DRAW_STOP ( CHUNK_BLOCK_SIZE - CHUNK_BLOCK_DRAW_START )

class RenderLayer {
    friend class Chunk;

    VertexArray va;
    IndexBuffer ib;
    IndexBuffer ib_reflect;
    int num_instances;
    VertexBuffer vb_coords;
    BlockCoords *populated_blocks;
};

class WorkingSpace {
    friend class Chunk;

    bool can_be_seen;
    bool visible;
    bool has_been_drawn;
    bool solid;
    unsigned int packed_lighting[ NUM_FACES_IN_CUBE ];
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
    friend void light_pending_enqueue( Chunk &chunk );
    friend int test_lighting( );

    int is_loading;
    int gl_initialized;
    bool is_empty_chunk;
    RenderLayer layers[ LAST_RENDER_ORDER ];
    BlockState *blocks;
    glm::ivec3 chunk_pos;
    int dirty;
    int should_render;
    int cached_cull_normal;
    int cached_cull_reflect;
    int needs_repopulation;
    glm::ivec3 chunk_mod;

    // ---- Flood-fill lighting (light.cpp) ----
    // Per-cell light volume covering the same 34^3 internal region as blocks.
    // Each byte packs skylight in the high nibble and block light in the low
    // nibble. Kept out of the mesh entirely so light changes never force a
    // remesh: the fragment shader samples the matching 3D texture instead.
    unsigned char *light;
    // Per interior column (x,z) in [0,CHUNK_SIZE): two flag arrays packed into
    // one buffer — column_open (no opaque cell in this chunk's column) then
    // sky_open_above (no opaque cell anywhere above in the loaded grid).
    unsigned char *light_columns;
    unsigned int light_texture; // GL_TEXTURE_3D, 0 = not created yet
    // Dirty region of light[] not yet uploaded, in texel coords [0,34).
    // light_dirty == 0 means clean.
    int light_dirty;
    glm::ivec3 light_dirty_min;
    glm::ivec3 light_dirty_max;
    // Cell indices (into blocks[]) changed by multiplayer diffs; converted to
    // light recheck seeds on the render thread. Overflow sets light_reseed.
    int light_pending[ 256 ];
    int light_pending_count;
    int light_reseed;
    // Set while the chunk sits in the pending-light drain list (see
    // light_pending_enqueue) so producers don't enqueue it twice.
    int light_pending_listed;
    // Same, for the dirty-light-volume upload list (render thread only).
    int light_upload_listed;

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

// Enqueue a chunk for render-thread light draining (light_drain_pending).
// Called by Chunk::set_block_by_index_if_different from network/worker
// threads; no-ops when the chunk is already listed.
void light_pending_enqueue( Chunk &chunk );