#ifndef HEADER_CHUNK_H
#define HEADER_CHUNK_H

#include "block.h"

#define CHUNK_SIZE_INTERNAL ( CHUNK_SIZE + 2 )
#define CHUNK_BLOCK_SIZE ( CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL )
#define CHUNK_BLOCK_DRAW_START ( CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL )
#define CHUNK_BLOCK_DRAW_STOP ( CHUNK_BLOCK_SIZE - CHUNK_BLOCK_DRAW_START )

typedef struct {
    int displayListSolid;
    int displayListTranslucent;
    Block *blocks;
    int chunk_x, chunk_y, chunk_z;
    int ditry;
} Chunk;

void chunk_calculate_sides( Chunk *chunk );
int chunk_get_coords_from_index( int index, int *out_x, int *out_y, int *out_z );
void chunk_load_terrain( Chunk *chunk );
void chunk_create_display_list( Chunk *chunk );
void chunk_draw( Chunk *chunk, int solid );
void chunk_destroy_display_list( Chunk *chunk );
void chunk_free_terrain( Chunk *chunk );
void chunk_set_block( Chunk *chunk, int x, int y, int z, BlockID blockID);
void chunk_persist( Chunk *chunk );
Block *chunk_get_block( Chunk *chunk, int x, int y, int z );

#endif