#ifndef HEADER_CHUNK_H
#define HEADER_CHUNK_H

#include "block.hpp"
#include "abstract/renderer.hpp"

#define CHUNK_SIZE_INTERNAL ( CHUNK_SIZE + 2 )
#define CHUNK_BLOCK_SIZE ( CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL )
#define CHUNK_BLOCK_DRAW_START ( CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL )
#define CHUNK_BLOCK_DRAW_STOP ( CHUNK_BLOCK_SIZE - CHUNK_BLOCK_DRAW_START )


typedef struct {
    float x;
    float y;
    float z;

    float mesh_x;
    float mesh_y;
    float mesh_z;

    float face[ NUM_FACES_IN_CUBE ];

    float packed_lighting[ NUM_FACES_IN_CUBE ];
} BlockCoords;

typedef struct {
    VertexArray va;
    IndexBuffer ib;
    int num_instances;
    VertexBuffer vb_coords;
    BlockCoords *populated_blocks;
} RenderLayer;

typedef struct {
    int is_loading;
    RenderLayer layers[ LAST_RENDER_ORDER ];
    BlockID *blocks;
    int chunk_x, chunk_y, chunk_z;
    int ditry;
    int should_render;
    int cached_cull;
    int chunk_mod_x, chunk_mod_y, chunk_mod_z;
} Chunk;

void chunk_init( Chunk *chunk, VertexBuffer *vb_block_solid, VertexBuffer *vb_block_water, VertexBufferLayout *vbl_block, VertexBufferLayout *vbl_coords );
void chunk_render( const Chunk *chunk, const Renderer *renderer, const Shader *shader, RenderOrder renderOrder );
void chunk_load_terrain( Chunk *chunk );      // Load from file or map gen
void chunk_program_terrain( Chunk *chunk );   // Program into GPU
void chunk_unprogram_terrain( Chunk *chunk ); // Remove from GPU
void chunk_calculate_sides( Chunk *chunk, int center_next_x, int center_next_y, int center_next_z );
int chunk_get_coords_from_index( int index, int *out_x, int *out_y, int *out_z );
int chunk_get_index_from_coords( int x, int y, int z );
// void chunk_draw( Chunk *chunk, int solid );
void chunk_persist( Chunk *chunk );
void chunk_destroy( Chunk *chunk );
void chunk_set_block( Chunk *chunk, int x, int y, int z, BlockID blockID );
BlockID chunk_get_block( Chunk *chunk, int x, int y, int z );
void chunk_calculate_popupated_blocks( Chunk *chunk );

inline int chunk_get_index_from_coords( int x, int y, int z ) {
    return ( y + 1 ) * CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL + ( x + 1 ) * CHUNK_SIZE_INTERNAL + ( z + 1 );
}

#endif