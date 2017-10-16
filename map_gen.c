#include "map_gen.h"
#include "RepGame.h"
#include "chunk.h"
#include <stdlib.h>

int *map_gen_load_block( int draw_block ) {
    int *blocks = malloc( CHUNK_BLOCK_SIZE * sizeof( int ) );
    for ( int index = 0; index < CHUNK_BLOCK_SIZE; index++ ) {
        int x, y, z;
        int value = 0;
        chunk_get_coords_from_index( index, &x, &y, &z );
        if ( y == 0 && draw_block ) { //&& ( abs( x + z ) > y ) ) {
            value = 1;
        }
        blocks[ index ] = value;
    }
    return blocks;
}

void map_gen_free_block( int *blocks ) {
    free( blocks );
}