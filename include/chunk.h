#ifndef HEADER_CHUNK_H
#define HEADER_CHUNK_H

#include "block.h"
#include "abstract/renderer.h"

#define CHUNK_SIZE_INTERNAL ( CHUNK_SIZE + 2 )
#define CHUNK_BLOCK_SIZE ( CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL )
#define CHUNK_BLOCK_DRAW_START ( CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL )
#define CHUNK_BLOCK_DRAW_STOP ( CHUNK_BLOCK_SIZE - CHUNK_BLOCK_DRAW_START )

typedef struct {
    float x;
    float y;
    float z;
    unsigned int face_top;
    unsigned int face_sides;
    unsigned int face_bottom;
} BlockCoords;

typedef struct {
    int is_loading;
    IndexBuffer ib;
    VertexBuffer vb_coords;
    VertexArray va;
    VertexBufferLayout vbl_coords;
    BlockCoords *populated_blocks;
    Block *blocks;
    int num_instances;
    int chunk_x, chunk_y, chunk_z;
    int ditry;
    int should_render;
    int chunk_mod_x, chunk_mod_y, chunk_mod_z;
} Chunk;

void chunk_init( Chunk *chunk, VertexBuffer *vb_block, VertexBufferLayout *vbl_block );
void chunk_render( const Chunk *chunk, const Renderer *renderer, const Shader *shader );
void chunk_load_terrain( Chunk *chunk );      // Load from file or map gen
void chunk_program_terrain( Chunk *chunk );   // Program into GPU
void chunk_unprogram_terrain( Chunk *chunk ); // Remove from GPU
// void chunk_calculate_sides( Chunk *chunk );
int chunk_get_coords_from_index( int index, int *out_x, int *out_y, int *out_z );
// void chunk_draw( Chunk *chunk, int solid );
void chunk_destroy( Chunk *chunk );
void chunk_set_block( Chunk *chunk, int x, int y, int z, BlockID blockID );
Block *chunk_get_block( Chunk *chunk, int x, int y, int z );

#endif