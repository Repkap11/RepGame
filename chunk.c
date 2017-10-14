#include "chunk.h"
#include "draw3d.h"
#include <GL/gl.h>

void chunk_create_display_list( Chunk *chunk ) {
    glPushMatrix( );
    chunk->displayList = glGenLists( 1 );
    // compile the display list, store a triangle in it
    glNewList( chunk->displayList, GL_COMPILE );
    for ( int i = 0; i < CHUNK_SIZE; i++ ) {
        for ( int j = 0; j < CHUNK_SIZE; j++ ) {
            for ( int k = 0; k < CHUNK_SIZE; k++ ) {
                // if ( i + k > j ) {
                glPushMatrix( );
                glTranslatef( i, j, k );
                draw3d_cube( );
                glPopMatrix( );
                //}
            }
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
