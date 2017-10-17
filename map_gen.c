#include "map_gen.h"
#include "RepGame.h"
#include "perlin_noise.h"

#include <stdlib.h>

int *map_gen_load_block( Chunk *chunk ) {
    int chunk_offset_x = chunk->chunk_x * CHUNK_SIZE;
    int chunk_offset_y = chunk->chunk_y * CHUNK_SIZE;
    int chunk_offset_z = chunk->chunk_z * CHUNK_SIZE;

    int *blocks = malloc( CHUNK_BLOCK_SIZE * sizeof( int ) );
    for ( int index = 0; index < CHUNK_BLOCK_SIZE; index++ ) {
        int x, y, z;
        int value = 0;
        chunk_get_coords_from_index( index, &x, &y, &z );
        // value = 1;
        value = perlin_noise( chunk_offset_x + x, chunk_offset_y + y, chunk_offset_z + z );

        if ( value == 0 && chunk_offset_y + y < 30 ) {
            value = 2;
        }

        blocks[ index ] = value;
    }
    return blocks;
}

void map_gen_free_block( int *blocks ) {
    free( blocks );
}
