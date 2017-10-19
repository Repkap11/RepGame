#ifndef HEADER_CHUNK_H
#define HEADER_CHUNK_H

#include "block.h"

#define CHUNK_SIZE_INTERNAL ( CHUNK_SIZE + 2 )
#define CHUNK_BLOCK_SIZE ( CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL )

typedef struct {
    int loaded;
    int displayListSolid;
    int displayListTranslucent;
    Block *blocks;
    int chunk_x, chunk_y, chunk_z;
} Chunk;

void chunk_calculate_sides( Chunk *chunk );
int chunk_get_coords_from_index( int index, int *out_x, int *out_y, int *out_z );
void chunk_load_terrain( Chunk *chunk );
void chunk_create_display_list( Chunk *chunk );
void chunk_draw( Chunk *chunk, int solid );
void chunk_destroy_display_list( Chunk *chunk );
void chunk_free_terrain( Chunk *chunk );

#endif