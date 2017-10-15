#ifndef HEADER_CHUNK_H
#define HEADER_CHUNK_H

#define CHUNK_SIZE 8
#define CHUNK_BLOCK_SIZE ( CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE )

typedef struct {
    int displayList;
    int *blocks;
} Chunk;
void chunk_get_coords_from_index( int index, int *out_x, int *out_y, int *out_z );
void chunk_create_display_list( Chunk *chunk );

void chunk_draw( Chunk *chunk );

#endif