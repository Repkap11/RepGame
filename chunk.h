#ifndef HEADER_CHUNK_H
#define HEADER_CHUNK_H

#define CHUNK_SIZE 3
#define CHUNK_BLOCK_SIZE ( CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE )

typedef struct {
    int displayList;
    int *blocks;
    int chunk_x, chunk_y, chunk_z;
} Chunk;
void chunk_get_coords_from_index( int index, int *out_x, int *out_y, int *out_z );
void chunk_load( Chunk *chunk );
void chunk_draw( Chunk *chunk );
void chunk_free( Chunk *chunk );

#endif