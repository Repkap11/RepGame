#pragma once

#include "block.hpp"
#include "common/utils/map_storage.hpp"
#include "renderer/renderer.hpp"
#include "server/server.hpp"

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

    int can_be_seen;
    int visible;
    int has_been_drawn;
    int solid;
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

    int is_loading;
    RenderLayer layers[ LAST_RENDER_ORDER ];
    BlockState *blocks;
    glm::ivec3 chunk_pos;
    int dirty;
    int should_render;
    int cached_cull_normal;
    int cached_cull_reflect;
    int needs_repopulation;
    glm::ivec3 chunk_mod;

    int can_extend_rect( BlockState blockState, unsigned int *packed_lighting, WorkingSpace *workingSpace, const glm::ivec3 &starting, const glm::ivec3 &size, const glm::ivec3 &dir );

  public:
    void init( const VertexBuffer &vb_block_solid, const VertexBuffer &vb_block_water, const VertexBufferLayout &vbl_block, const VertexBufferLayout &vbl_coords );
    void draw( const Renderer &renderer, const Texture &texture, const Shader &shader, RenderOrder renderOrder, bool draw_reflect ) const;
    void load_terrain( MapStorage &map_storage ); // Load from file or map gen
    void program_terrain( );                      // Program into GPU
    void unprogram_terrain( );                    // Remove from GPU
    void calculate_sides( const glm::ivec3 &center_next );
    static int get_coords_from_index_todo( int index, glm::ivec3 &out_pos );
    static int get_coords_from_index( int index, int &out_x, int &out_y, int &out_z );

    void persist( MapStorage &map_storage );
    void destroy( );
    void set_block( const glm::ivec3 &pos, BlockState blockState );
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