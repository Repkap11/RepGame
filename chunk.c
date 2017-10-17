#include "chunk.h"
#include "RepGame.h"
#include "draw3d.h"
#include "map_gen.h"
#include <GL/gl.h>

#define BLOCK_SCALE 1

int chunk_get_index_from_coords( int x, int y, int z ) {
    return ( y + 1 ) * CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL + ( x + 1 ) * CHUNK_SIZE_INTERNAL + ( z + 1 );
}

int chunk_get_coords_from_index( int index, int *out_x, int *out_y, int *out_z ) {
    *out_y = ( index / ( CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL ) ) - 1;
    *out_x = ( ( index / CHUNK_SIZE_INTERNAL ) % CHUNK_SIZE_INTERNAL ) - 1;
    *out_z = ( index % ( CHUNK_SIZE_INTERNAL ) ) - 1;
    // return 1;
    return *out_x >= 0 && *out_y >= 0 && *out_z >= 0 && *out_x < CHUNK_SIZE && *out_y < CHUNK_SIZE && *out_z < CHUNK_SIZE;
}

int chunk_block( Chunk *chunk, int x, int y, int z ) {
    if ( x < 0 || y < 0 || z < 0 ) {
        // pr_debug( "Block coord negitive should not be checked." );
        // return 0;
    }
    if ( x > CHUNK_SIZE || y > CHUNK_SIZE || z > CHUNK_SIZE ) {
        // pr_debug( "Block coord %d should not be checked.", CHUNK_SIZE_INTERNAL );
        // return 0;
    }
    return chunk->blocks[ chunk_get_index_from_coords( x, y, z ) ];
}

void chunk_create_display_list( Chunk *chunk ) {
    chunk->displayList = glGenLists( 1 );
    // compile the display list, store a triangle in it
    glNewList( chunk->displayList, GL_COMPILE );
    glPushMatrix( );
    glTranslatef( chunk->chunk_x * CHUNK_SIZE, chunk->chunk_y * CHUNK_SIZE, chunk->chunk_z * CHUNK_SIZE );
    for ( int index = 0; index < CHUNK_BLOCK_SIZE; index++ ) {
        int x, y, z;
        int drawn_block = chunk_get_coords_from_index( index, &x, &y, &z );
        int type = chunk->blocks[ index ];
        // drawn_block = 1;
        if ( drawn_block ) {
            if ( type > 0 ) {
                int upBlock = chunk_block( chunk, x + 0, y + 1, z + 0 ) == type;
                int downBlock = chunk_block( chunk, x + 0, y - 1, z + 0 ) == type;
                int leftBlock = chunk_block( chunk, x + 0, y + 0, z - 1 ) == type;
                int rightBlock = chunk_block( chunk, x + 0, y + 0, z + 1 ) == type;
                int frontBlock = chunk_block( chunk, x + 1, y + 0, z + 0 ) == type;
                int backBlock = chunk_block( chunk, x - 1, y + 0, z + 0 ) == type;

                if ( upBlock && downBlock && leftBlock && rightBlock && frontBlock && backBlock ) {
                    continue;
                }

                glPushMatrix( );
                glTranslatef( x, y, z );
                glScalef( BLOCK_SCALE, BLOCK_SCALE, BLOCK_SCALE );
                // draw3d_cube( );
                draw3d_cube_parts( !upBlock, !downBlock, !leftBlock, !rightBlock, !frontBlock, !backBlock, type );
                glPopMatrix( );
            } else {
                // pr_debug( "i:%d x%d y:%d z:%d", index, x, y, z );
            }
        }
    }
    glPopMatrix( );
    glEndList( );
}

void chunk_load( Chunk *chunk ) {
    // pr_debug( "Loading chunk x:%d y:%d z:%d", chunk->chunk_x, chunk->chunk_y, chunk->chunk_z );

    chunk->blocks = map_gen_load_block( chunk, chunk->chunk_y == 0 );
    chunk_create_display_list( chunk );
}

void chunk_destroy_display_list( Chunk *chunk ) {
    glDeleteLists( chunk->displayList, 1 );
}

void chunk_draw( Chunk *chunk ) {
    glCallList( chunk->displayList );
}
void chunk_free( Chunk *chunk ) {
    // pr_debug( "Freeing chunk x:%d y:%d z:%d", chunk->chunk_x, chunk->chunk_y, chunk->chunk_z );
    chunk_destroy_display_list( chunk );
    map_gen_free_block( chunk->blocks );
}
