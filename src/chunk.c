#include "chunk.h"
#include "RepGame.h"
#include "draw3d.h"
#include "map_gen.h"
#include <GL/gl.h>

extern inline int chunk_get_index_from_coords( int x, int y, int z ) {
    return ( y + 1 ) * CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL + ( x + 1 ) * CHUNK_SIZE_INTERNAL + ( z + 1 );
}

extern inline int chunk_get_coords_from_index( int index, int *out_x, int *out_y, int *out_z ) {
    int y = ( index / ( CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL ) ) - 1;
    int x = ( ( index / CHUNK_SIZE_INTERNAL ) % CHUNK_SIZE_INTERNAL ) - 1;
    int z = ( index % ( CHUNK_SIZE_INTERNAL ) ) - 1;
    // return 1;
    int result = x >= 0 && y >= 0 && z >= 0 && x < CHUNK_SIZE && y < CHUNK_SIZE && z < CHUNK_SIZE;
    *out_x = x;
    *out_y = y;
    *out_z = z;
    return result;
}

static inline Block *chunk_block( Chunk *chunk, int x, int y, int z ) {
    return &chunk->blocks[ chunk_get_index_from_coords( x, y, z ) ];
}

void chunk_calculate_sides( Chunk *chunk ) {
    for ( int index = CHUNK_BLOCK_DRAW_START; index < CHUNK_BLOCK_DRAW_STOP; index++ ) {
        int x, y, z;
        int drawn_block = chunk_get_coords_from_index( index, &x, &y, &z );
        if ( drawn_block ) { // Blocks at the edge of the chunk are not drawn, but only for edge checking
            Block *block = &chunk->blocks[ index ];
            if ( block->blockDef->alpha == 0.0f ) {
                // Transparent blocks don't get drawn
                block->draw_sides.top = 0;
                block->draw_sides.bottom = 0;
                block->draw_sides.left = 0;
                block->draw_sides.right = 0;
                block->draw_sides.front = 0;
                block->draw_sides.back = 0;
            } else {
                Block *topBlock = chunk_block( chunk, x + 0, y + 1, z + 0 );
                Block *bottomBlock = chunk_block( chunk, x + 0, y - 1, z + 0 );
                Block *leftBlock = chunk_block( chunk, x + 0, y + 0, z - 1 );
                Block *rightBlock = chunk_block( chunk, x + 0, y + 0, z + 1 );
                Block *frontBlock = chunk_block( chunk, x + 1, y + 0, z + 0 );
                Block *backBlock = chunk_block( chunk, x - 1, y + 0, z + 0 );
                // if ( block->blockDef->alpha == 1.0f ) {
                // Sufeaces of solid blocks get drawn if they are next to a transparent block.
                block->draw_sides.top = ( topBlock->blockDef->alpha < block->blockDef->alpha );
                block->draw_sides.bottom = ( bottomBlock->blockDef->alpha < block->blockDef->alpha );
                block->draw_sides.left = ( leftBlock->blockDef->alpha < block->blockDef->alpha );
                block->draw_sides.right = ( rightBlock->blockDef->alpha < block->blockDef->alpha );
                block->draw_sides.front = ( frontBlock->blockDef->alpha < block->blockDef->alpha );
                block->draw_sides.back = ( backBlock->blockDef->alpha < block->blockDef->alpha );
                // } else {
                //     // Sufeaces of translucent blocks get drawn unless they are next to a solid block
                //     block->draw_sides.top = ( topBlock->blockDef->alpha > block->blockDef->alpha );
                //     block->draw_sides.bottom = ( bottomBlock->blockDef->alpha != 1.0f );
                //     block->draw_sides.left = ( leftBlock->blockDef->alpha != 1.0f );
                //     block->draw_sides.right = ( rightBlock->blockDef->alpha != 1.0f );
                //     block->draw_sides.front = ( frontBlock->blockDef->alpha != 1.0f );
                //     block->draw_sides.back = ( backBlock->blockDef->alpha != 1.0f );
                // }
            }
        }
    }
}

void chunk_create_display_list( Chunk *chunk ) {

    chunk->displayListSolid = glGenLists( 1 );
    // compile the display list, store a triangle in it
    glNewList( chunk->displayListSolid, GL_COMPILE );
    glPushMatrix( );
    glTranslatef( chunk->chunk_x * CHUNK_SIZE, chunk->chunk_y * CHUNK_SIZE, chunk->chunk_z * CHUNK_SIZE );
    int chunk_contains_faces = 0;
    for ( int index = CHUNK_BLOCK_DRAW_START; index < CHUNK_BLOCK_DRAW_STOP; index++ ) {
        int x, y, z;
        int drawn_block = chunk_get_coords_from_index( index, &x, &y, &z );
        if ( !drawn_block ) { // Blocks at the edge of the chunk are not drawn, but only for edge checking
            continue;
        }
        Block *block = &chunk->blocks[ index ];
        if ( block->blockDef->alpha != 1.0f ) {
            // Quick exit for air and translucent
            continue;
        }
        if ( !block->draw_sides.top && !block->draw_sides.bottom && !block->draw_sides.left && !block->draw_sides.right && !block->draw_sides.front && !block->draw_sides.back ) {
            // Quick exit for not shown block
            continue;
        }
        chunk_contains_faces = 1;
        glPushMatrix( );
        glTranslatef( x, y, z );
        block_draw( block );
        glPopMatrix( );
    }
    glPopMatrix( );
    glEndList( );
    if ( !chunk_contains_faces ) {
        glDeleteLists( chunk->displayListSolid, 1 );
        chunk->displayListSolid = 0;
    }

    chunk->displayListTranslucent = glGenLists( 1 );
    // compile the display list, store a triangle in it
    glNewList( chunk->displayListTranslucent, GL_COMPILE );
    glPushMatrix( );
    chunk_contains_faces = 0;
    glTranslatef( chunk->chunk_x * CHUNK_SIZE, chunk->chunk_y * CHUNK_SIZE, chunk->chunk_z * CHUNK_SIZE );
    for ( int index = CHUNK_BLOCK_DRAW_START; index < CHUNK_BLOCK_DRAW_STOP; index++ ) {
        int x, y, z;
        int drawn_block = chunk_get_coords_from_index( index, &x, &y, &z );
        if ( !drawn_block ) { // Blocks at the edge of the chunk are not drawn, but only for edge checking
            continue;
        }
        Block *block = &chunk->blocks[ index ];
        if ( block->blockDef->alpha == 0.0f ) {
            // Quick exit for air
            continue;
        }
        if ( block->blockDef->alpha == 1.0f ) {
            // Don't draw solid blocks in the translucent display list
            continue;
        }
        if ( !block->draw_sides.top && !block->draw_sides.bottom && !block->draw_sides.left && !block->draw_sides.right && !block->draw_sides.front && !block->draw_sides.back ) {
            // Quick exit for not shown block
            continue;
        }
        chunk_contains_faces = 1;
        glPushMatrix( );
        glTranslatef( x, y, z );
        block_draw( block );
        glPopMatrix( );
    }
    glPopMatrix( );
    glEndList( );
    if ( !chunk_contains_faces ) {
        glDeleteLists( chunk->displayListTranslucent, 1 );
        chunk->displayListTranslucent = 0;
    }
}

extern inline void chunk_load_terrain( Chunk *chunk ) {
    // pr_debug( "Loading chunk terrain x:%d y:%d z:%d", chunk->chunk_x, chunk->chunk_y, chunk->chunk_z );
    map_gen_load_block( chunk );
}

void chunk_destroy_display_list( Chunk *chunk ) {
    if ( chunk->displayListSolid ) {
        glDeleteLists( chunk->displayListSolid, 1 );
        chunk->displayListSolid = 0;
    }
    if ( chunk->displayListTranslucent ) {
        glDeleteLists( chunk->displayListTranslucent, 1 );
        chunk->displayListTranslucent = 0;
    }
}

void chunk_draw( Chunk *chunk, int solid ) {
    if ( solid && chunk->displayListSolid ) {
        glCallList( chunk->displayListSolid );
    }
    if ( !solid && chunk->displayListTranslucent ) {
        glCallList( chunk->displayListTranslucent );
    }
}
void chunk_free_terrain( Chunk *chunk ) {
    // pr_debug( "Freeing chunk x:%d y:%d z:%d", chunk->chunk_x, chunk->chunk_y, chunk->chunk_z );
    if ( chunk->blocks ) {
        map_gen_free_block( chunk->blocks );
        chunk->blocks = 0;
    }
}
