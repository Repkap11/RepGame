#include "chunk.h"
#include "RepGame.h"
#include "draw3d.h"
#include <GL/gl.h>

#define BLOCK_SCALE 0.8

int chunk_get_index( int x, int y, int z ) {
    return y * CHUNK_SIZE * CHUNK_SIZE + x * CHUNK_SIZE + z;
}

void chunk_get_coords( int index, int *out_x, int *out_y, int *out_z ) {
    *out_y = index / ( CHUNK_SIZE * CHUNK_SIZE );
    *out_x = ( index / CHUNK_SIZE ) % CHUNK_SIZE;
    *out_z = index % ( CHUNK_SIZE );
}

void chunk_create_display_list( Chunk *chunk ) {
    glPushMatrix( );
    chunk->displayList = glGenLists( 1 );
    // compile the display list, store a triangle in it
    glNewList( chunk->displayList, GL_COMPILE );

    for ( int index = 0; index < CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE; index++ ) {
        int x, y, z;
        chunk_get_coords( index, &x, &y, &z );
        // pr_debug( "x%d y:%d z:%d", x, y, z );
        glPushMatrix( );
        glTranslatef( x, y, z );
        glScalef( BLOCK_SCALE, BLOCK_SCALE, BLOCK_SCALE );
        draw3d_cube( );
        glPopMatrix( );
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
