#include "map_gen.h"
#include "RepGame.h"
#include "block_definitions.h"
#include "perlin_noise.h"

#include <stdlib.h>

void map_gen_load_block( Chunk *chunk ) {
    int chunk_offset_x = chunk->chunk_x * CHUNK_SIZE;
    int chunk_offset_y = chunk->chunk_y * CHUNK_SIZE;
    int chunk_offset_z = chunk->chunk_z * CHUNK_SIZE;
    if ( chunk->blocks == NULL ) {
        chunk->blocks = calloc( CHUNK_BLOCK_SIZE, sizeof( Block ) );
    }
    for ( int index = 0; index < CHUNK_BLOCK_SIZE; index++ ) {
        int x, y, z;
        int value = 0;
        chunk_get_coords_from_index( index, &x, &y, &z );
        // value = 1;
        value = perlin_noise( chunk_offset_x + x, chunk_offset_y + y, chunk_offset_z + z );

        if ( value == 0 && chunk_offset_y + y < 30 ) {
            value = 2;
        }

        chunk->blocks[ index ].blockDef = block_definition_get_definition( value );
    }
}

void map_gen_free_block( Block *blocks ) {
    free( blocks );
}
