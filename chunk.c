#include "chunk.h"
#include "RepGame.h"
#include "draw3d.h"
#include <GL/gl.h>

#define BLOCK_SCALE 0.95

int chunk_get_index_from_coords( int x, int y, int z ) {
    return y * CHUNK_SIZE * CHUNK_SIZE + x * CHUNK_SIZE + z;
}

void chunk_get_coords_from_index( int index, int *out_x, int *out_y, int *out_z ) {
    *out_y = index / ( CHUNK_SIZE * CHUNK_SIZE );
    *out_x = ( index / CHUNK_SIZE ) % CHUNK_SIZE;
    *out_z = index % ( CHUNK_SIZE );
}

int chunk_block( Chunk *chunk, int x, int y, int z ) {
    if ( x < 0 || y < 0 || z < 0 ) {
        return 0;
    }
    if ( x >= CHUNK_SIZE || y >= CHUNK_SIZE || z >= CHUNK_SIZE ) {
        return 0;
    }
    return chunk->blocks[ chunk_get_index_from_coords( x, y, z ) ];
}

void chunk_create_display_list( Chunk *chunk ) {
    glPushMatrix( );
    chunk->displayList = glGenLists( 1 );
    // compile the display list, store a triangle in it
    glNewList( chunk->displayList, GL_COMPILE );

    for ( int index = 0; index < CHUNK_BLOCK_SIZE; index++ ) {
        int x, y, z;
        if ( chunk->blocks[ index ] ) {
            chunk_get_coords_from_index( index, &x, &y, &z );
            int upBlock = chunk_block( chunk, x + 0, y + 1, z + 0 );
            int downBlock = chunk_block( chunk, x + 0, y - 1, z + 0 );
            int leftBlock = chunk_block( chunk, x + 1, y + 0, z + 0 );
            int rightBlock = chunk_block( chunk, x - 1, y + 0, z + 0 );
            int frontBlock = chunk_block( chunk, x + 0, y + 0, z + 1 );
            int backBlock = chunk_block( chunk, x + 0, y + 0, z - 1 );

            if ( upBlock && downBlock && leftBlock && rightBlock && frontBlock && backBlock ) {
                continue;
            }

            glPushMatrix( );
            glTranslatef( x, y, z );
            glScalef( BLOCK_SCALE, BLOCK_SCALE, BLOCK_SCALE );
            draw3d_cube( );
            glPopMatrix( );
        } else {
            // pr_debug( "i:%d x%d y:%d z:%d", index, x, y, z );
        }
    }
    glEndList( );
    glPopMatrix( );
}
void chunk_destroy_display_list( Chunk *chunk ) {
    glDeleteLists( chunk->displayList, 1 );
}

void chunk_draw( Chunk *chunk ) {
    glCallList( chunk->displayList );
}
