#include "map_gen.h"
#include "RepGame.h"
#include <math.h>

#include <stdlib.h>

int perlin_noise( int x, int y, int z ) {
    if ( y == 0 ) {
        return 1;
    }
    if ( y < 0 ) {
        return 0;
    }
    if ( sin( ( double )( x + z ) / 10.0 ) > ( y ) + z / 10.0 ) {
        return 1;
    }
    return 0;
}

int *map_gen_load_block( Chunk *chunk, int draw_block ) {
    int chunk_offset_x = chunk->chunk_x * CHUNK_SIZE;
    int chunk_offset_y = chunk->chunk_y * CHUNK_SIZE;
    int chunk_offset_z = chunk->chunk_z * CHUNK_SIZE;

    int *blocks = malloc( CHUNK_BLOCK_SIZE * sizeof( int ) );
    for ( int index = 0; index < CHUNK_BLOCK_SIZE; index++ ) {
        int x, y, z;
        int value = 0;
        chunk_get_coords_from_index( index, &x, &y, &z );
        value = perlin_noise( chunk_offset_x + x, chunk_offset_y + y, chunk_offset_z + z );

        blocks[ index ] = value;
    }
    return blocks;
}

void map_gen_free_block( int *blocks ) {
    free( blocks );
}
