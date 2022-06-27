#ifndef HEADER_CHUNK_H
#define HEADER_CHUNK_H

#include "block.hpp"
#include "abstract/renderer.hpp"
#include "server/server.hpp"

#define CHUNK_SIZE_INTERNAL ( CHUNK_SIZE + 2 )
#define CHUNK_BLOCK_SIZE ( CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL )
#define CHUNK_BLOCK_DRAW_START ( CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL )
#define CHUNK_BLOCK_DRAW_STOP ( CHUNK_BLOCK_SIZE - CHUNK_BLOCK_DRAW_START )

typedef struct {
    VertexArray va;
    IndexBuffer ib;
    IndexBuffer ib_reflect;
    int num_instances;
    VertexBuffer vb_coords;
    BlockCoords *populated_blocks;
} RenderLayer;

// This size must match, update the size in server.hpp if it doesn't
static_assert( sizeof( BlockState ) == SERVER_BLOCK_DATA_SIZE, "Size of BlockState doesn't match server packet size." );

typedef struct {
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
} Chunk;

void chunk_init( Chunk *chunk, VertexBuffer *vb_block_solid, VertexBuffer *vb_block_water, VertexBufferLayout *vbl_block, VertexBufferLayout *vbl_coords );
void chunk_render( const Chunk *chunk, const Renderer *renderer, const Shader *shader, RenderOrder renderOrder, bool draw_reflect );
void chunk_load_terrain( Chunk *chunk );      // Load from file or map gen
void chunk_program_terrain( Chunk *chunk );   // Program into GPU
void chunk_unprogram_terrain( Chunk *chunk ); // Remove from GPU
void chunk_calculate_sides( Chunk *chunk, const glm::ivec3 &center_next );
int chunk_get_coords_from_index( int index, glm::ivec3 &out_pos );
int chunk_get_index_from_coords( const glm::ivec3 &pos );
// void chunk_draw( Chunk *chunk, int solid );
void chunk_persist( Chunk *chunk );
void chunk_destroy( Chunk *chunk );
void chunk_set_block( Chunk *chunk, const glm::ivec3 &pos, BlockState blockState );
BlockState chunk_get_block( Chunk *chunk, const glm::ivec3 &pos );
void chunk_calculate_popupated_blocks( Chunk *chunk );

inline int chunk_get_index_from_coords( const glm::ivec3 &pos ) {
    return ( pos.y + 1 ) * CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL + ( pos.x + 1 ) * CHUNK_SIZE_INTERNAL + ( pos.z + 1 );
}

inline int chunk_get_index_from_coords( int x, int y, int z ) {
    return ( y + 1 ) * CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL + ( x + 1 ) * CHUNK_SIZE_INTERNAL + ( z + 1 );
}

#endif