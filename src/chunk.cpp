#include "chunk.h"
#include "RepGame.h"
#include "draw3d.h"
#include "map_gen.h"
#include "map_storage.h"
#include <GL/gl.h>

#define FACE_TOP 0
#define FACE_BOTTOM 2
#define FACE_FRONT 1
#define FACE_BACK 1
#define FACE_LECT 1
#define FACE_RIGHT 1

typedef struct {
    float x;
    float y;
    float z;
    unsigned int tex_coord_x;
    unsigned int tex_coord_y;
    unsigned int which_face;
} CubeFace;

CubeFace vd_data[] = {
    {0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !0, 0, FACE_FRONT}, // 0
    {1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !1, 0, FACE_FRONT}, // 1
    {1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !1, 1, FACE_FRONT}, // 2
    {0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !0, 1, FACE_FRONT}, // 3

    {0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !1, 0, FACE_BACK}, // 4
    {1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !0, 0, FACE_BACK}, // 5
    {1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !0, 1, FACE_BACK}, // 6
    {0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !1, 1, FACE_BACK}, // 7

    {0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 0, 0, FACE_LECT},  // 8
    {1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 1, 0, FACE_RIGHT}, // 9
    {1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 1, 1, FACE_RIGHT}, // 10
    {0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 0, 1, FACE_LECT},  // 11

    {0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 1, 0, FACE_LECT},  // 12
    {1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 0, 0, FACE_RIGHT}, // 13
    {1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 0, 1, FACE_RIGHT}, // 14
    {0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 1, 1, FACE_LECT},  // 15

    {0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !0, !0, FACE_BOTTOM}, // 16
    {1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !1, !0, FACE_BOTTOM}, // 17
    {1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !1, !1, FACE_TOP},    // 18
    {0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !0, !1, FACE_TOP},    // 19

    {0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 1, 0, FACE_BOTTOM}, // 20
    {1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 0, 0, FACE_BOTTOM}, // 21
    {1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 0, 1, FACE_TOP},    // 6
    {0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 1, 1, FACE_TOP},    // 7
};
unsigned int ib_data[] = {
    2,  1,  0, // Front
    0,  3,  2, //

    14, 13, 9,  // Right
    9,  10, 14, //

    7,  4,  5, // Back
    5,  6,  7, //

    11, 8,  12, // Left
    12, 15, 11, //

    22, 18, 19, // Top
    19, 23, 22, //

    17, 21, 20, // Bottom
    20, 16, 17, //
};

int chunk_get_index_from_coords( int x, int y, int z ) {
    return ( y + 1 ) * CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL + ( x + 1 ) * CHUNK_SIZE_INTERNAL + ( z + 1 );
}

int chunk_get_coords_from_index( int index, int *out_x, int *out_y, int *out_z ) {
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

#define num_blocks CHUNK_SIZE *CHUNK_SIZE *CHUNK_SIZE

void chunk_init( Chunk *chunk ) {
    chunk->num_instances = num_blocks;

    vertex_buffer_init( &chunk->vb_block, vd_data, sizeof( CubeFace ) * 4 * 6 );

    index_buffer_init( &chunk->ib, ib_data, 3 * 2 * 6 );
    index_buffer_bind( &chunk->ib );

    vertex_buffer_layout_init( &chunk->vbl_block );
    vertex_buffer_layout_bind( &chunk->vbl_block );
    // The sum of these must be elements_per_vertex
    vertex_buffer_layout_push_float( &chunk->vbl_block, 3 ); // Coords
    vertex_buffer_layout_push_float( &chunk->vbl_block, 2 ); // Texture coords
    vertex_buffer_layout_push_float( &chunk->vbl_block, 1 ); // Face type (top, sides, bottom)

    vertex_buffer_layout_init( &chunk->vbl_coords );
    vertex_buffer_layout_bind( &chunk->vbl_coords );
    // The sum of these must be elements_per_vertex
    vertex_buffer_layout_push_float( &chunk->vbl_coords, 3 );        // block 3d world coords
    vertex_buffer_layout_push_unsigned_int( &chunk->vbl_coords, 3 ); // which texture (block type)

    unsigned int elements_per_vertex_coords = 6;
    vertex_buffer_init( &chunk->vb_coords, chunk->populated_blocks, sizeof( BlockCoords ) * chunk->num_instances );

    vertex_array_init( &chunk->va );
    vertex_array_add_buffer( &chunk->va, &chunk->vb_block, &chunk->vbl_block, 0 );
    vertex_array_add_buffer( &chunk->va, &chunk->vb_coords, &chunk->vbl_coords, 1 );

    texture_init_blocks( &chunk->blocksTexture );
    unsigned int textureSlot = 0;
    texture_bind( &chunk->blocksTexture, textureSlot );
}

void chunk_destroy( Chunk *chunk ) {
    texture_destroy( &chunk->blocksTexture );
}

void chunk_render( const Chunk *chunk, const Renderer *renderer, const Shader *shader ) {
    renderer_draw( renderer, &chunk->va, &chunk->ib, shader, chunk->num_instances );
}

Block *chunk_get_block( Chunk *chunk, int x, int y, int z ) {
    if ( chunk->blocks == NULL ) {
        // pr_debug("Chunk has no blocks");
        return NULL;
    }
    if ( x > CHUNK_SIZE + 1 || //
         y > CHUNK_SIZE + 1 || //
         z > CHUNK_SIZE + 1 ) {
        return NULL;
    }
    if ( x < -1 || //
         y < -1 || //
         z < -1 ) {
        return NULL;
    }
    return &chunk->blocks[ chunk_get_index_from_coords( x, y, z ) ];
}

void chunk_set_block( Chunk *chunk, int x, int y, int z, BlockID blockID ) {
    Block *block = chunk_get_block( chunk, x, y, z );
    block->blockDef = block_definition_get_definition( blockID );
}

// void chunk_calculate_sides( Chunk *chunk ) {
//     for ( int index = CHUNK_BLOCK_DRAW_START; index < CHUNK_BLOCK_DRAW_STOP; index++ ) {
//         int x, y, z;
//         int drawn_block = chunk_get_coords_from_index( index, &x, &y, &z );
//         if ( drawn_block ) { // Blocks at the edge of the chunk are not drawn, but only for edge checking
//             Block *block = &chunk->blocks[ index ];
//             if ( block->blockDef->alpha == 0.0f ) {
//                 // Transparent blocks don't get drawn
//                 block->draw_sides.top = 0;
//                 block->draw_sides.bottom = 0;
//                 block->draw_sides.left = 0;
//                 block->draw_sides.right = 0;
//                 block->draw_sides.front = 0;
//                 block->draw_sides.back = 0;
//             } else {
//                 Block *topBlock = chunk_get_block( chunk, x + 0, y + 1, z + 0 );
//                 Block *bottomBlock = chunk_get_block( chunk, x + 0, y - 1, z + 0 );
//                 Block *leftBlock = chunk_get_block( chunk, x + 0, y + 0, z - 1 );
//                 Block *rightBlock = chunk_get_block( chunk, x + 0, y + 0, z + 1 );
//                 Block *frontBlock = chunk_get_block( chunk, x + 1, y + 0, z + 0 );
//                 Block *backBlock = chunk_get_block( chunk, x - 1, y + 0, z + 0 );
//                 // if ( block->blockDef->alpha == 1.0f ) {
//                 // Sufeaces of solid blocks get drawn if they are next to a transparent block.
//                 block->draw_sides.top = ( topBlock->blockDef->alpha < block->blockDef->alpha );
//                 block->draw_sides.bottom = ( bottomBlock->blockDef->alpha < block->blockDef->alpha );
//                 block->draw_sides.left = ( leftBlock->blockDef->alpha < block->blockDef->alpha );
//                 block->draw_sides.right = ( rightBlock->blockDef->alpha < block->blockDef->alpha );
//                 block->draw_sides.front = ( frontBlock->blockDef->alpha < block->blockDef->alpha );
//                 block->draw_sides.back = ( backBlock->blockDef->alpha < block->blockDef->alpha );
//                 // } else {
//                 //     // Sufeaces of translucent blocks get drawn unless they are next to a solid block
//                 //     block->draw_sides.top = ( topBlock->blockDef->alpha > block->blockDef->alpha );
//                 //     block->draw_sides.bottom = ( bottomBlock->blockDef->alpha != 1.0f );
//                 //     block->draw_sides.left = ( leftBlock->blockDef->alpha != 1.0f );
//                 //     block->draw_sides.right = ( rightBlock->blockDef->alpha != 1.0f );
//                 //     block->draw_sides.front = ( frontBlock->blockDef->alpha != 1.0f );
//                 //     block->draw_sides.back = ( backBlock->blockDef->alpha != 1.0f );
//                 // }
//             }
//         }
//     }
// }

// void chunk_create_display_list( Chunk *chunk ) {

//     chunk->displayListSolid = glGenLists( 1 );
//     // compile the display list, store a triangle in it
//     glNewList( chunk->displayListSolid, GL_COMPILE );
//     glPushMatrix( );
//     glTranslatef( chunk->chunk_x * CHUNK_SIZE, chunk->chunk_y * CHUNK_SIZE, chunk->chunk_z * CHUNK_SIZE );
//     int chunk_contains_faces = 0;
//     for ( int index = CHUNK_BLOCK_DRAW_START; index < CHUNK_BLOCK_DRAW_STOP; index++ ) {
//         int x, y, z;
//         int drawn_block = chunk_get_coords_from_index( index, &x, &y, &z );
//         if ( !drawn_block ) { // Blocks at the edge of the chunk are not drawn, but only for edge checking
//             continue;
//         }
//         Block *block = &chunk->blocks[ index ];
//         if ( block->blockDef->alpha != 1.0f ) {
//             // Quick exit for air and translucent
//             continue;
//         }
//         if ( !block->draw_sides.top && !block->draw_sides.bottom && !block->draw_sides.left && !block->draw_sides.right && !block->draw_sides.front && !block->draw_sides.back ) {
//             // Quick exit for not shown block
//             continue;
//         }
//         chunk_contains_faces = 1;
//         glPushMatrix( );
//         glTranslatef( x, y, z );
//         //renderShaders(chunk->chunk_x * CHUNK_SIZE+x,chunk->chunk_y * CHUNK_SIZE+y,chunk->chunk_z * CHUNK_SIZE+z);
//         block_draw( block );
//         glPopMatrix( );
//     }
//     glPopMatrix( );
//     glEndList( );
//     if ( !chunk_contains_faces ) {
//         glDeleteLists( chunk->displayListSolid, 1 );
//         chunk->displayListSolid = 0;
//     }

//     chunk->displayListTranslucent = glGenLists( 1 );
//     // compile the display list, store a triangle in it
//     glNewList( chunk->displayListTranslucent, GL_COMPILE );
//     glPushMatrix( );
//     chunk_contains_faces = 0;
//     glTranslatef( TRIP_ARGS( CHUNK_SIZE * chunk->chunk_ ) );
//     for ( int index = CHUNK_BLOCK_DRAW_START; index < CHUNK_BLOCK_DRAW_STOP; index++ ) {
//         TRIP_STATE( int val_ );
//         int drawn_block = chunk_get_coords_from_index( index, TRIP_ARGS( &val_ ) );
//         if ( !drawn_block ) { // Blocks at the edge of the chunk are not drawn, but only for edge checking
//             continue;
//         }
//         Block block = chunk->blocks[ index ];
//         float alpha = block.blockDef->alpha;
//         if ( alpha == 0.0f ) {
//             // Quick exit for air
//             continue;
//         }
//         if ( alpha == 1.0f ) {
//             // Don't draw solid blocks in the translucent display list
//             continue;
//         }
//         if ( !block.draw_sides.top && !block.draw_sides.bottom && !block.draw_sides.left && !block.draw_sides.right && !block.draw_sides.front && !block.draw_sides.back ) {
//             // Quick exit for not shown block
//             continue;
//         }
//         chunk_contains_faces = 1;
//         glPushMatrix( );
//         glTranslatef( TRIP_ARGS( val_ ) );
//         block_draw( &chunk->blocks[ index ] );
//         glPopMatrix( );
//     }
//     glPopMatrix( );
//     glEndList( );
//     if ( !chunk_contains_faces ) {
//         glDeleteLists( chunk->displayListTranslucent, 1 );
//         chunk->displayListTranslucent = 0;
//     }
// }

void chunk_load_terrain( Chunk *chunk ) {
    // pr_debug( "Loading chunk terrain x:%d y:%d z:%d", chunk->chunk_x, chunk->chunk_y, chunk->chunk_z );
    int loaded = map_storage_load( chunk );
    if ( !loaded ) {
        // We havn't loaded this chunk before, map gen it.
        map_gen_load_block( chunk );
    }
    // chunk_blocks_to_render_blocks();
    // delete chunk->blocks;
}

void chunk_program_terrain( Chunk *chunk ) {
    // Just for lolz, re gen the terrain every time we program it!
    // This sort of thing shoudl really go in chunk_load_terrain
    for ( int i = 0; i < chunk->num_instances; i++ ) {
        BlockCoords *block = &chunk->populated_blocks[ i ];

        int y = ( int )( i / ( CHUNK_SIZE * CHUNK_SIZE ) );
        int x = ( int )( ( i / CHUNK_SIZE ) % CHUNK_SIZE );
        int z = ( int )( i % CHUNK_SIZE );
        // int x = ( rand( ) % 128 );
        // int y = ( rand( ) % 128 );
        // int z = ( rand( ) % 128 );
        block->x = x; // * 4;
        block->y = y; // * 4;
        block->z = z; // * 4;

        // block->face_top = 9;
        // block->face_sides = 8;
        // block->face_bottom = 10;
        unsigned int face = rand( ) % 128;

        block->face_top = face;
        block->face_sides = face;
        block->face_bottom = face;
    }
    vertex_buffer_set_data( &chunk->vb_coords, chunk->populated_blocks, sizeof( BlockCoords ) * chunk->num_instances );
}

void chunk_persist( Chunk *chunk ) {
    map_storage_persist( chunk );
}

void chunk_free_terrain( Chunk *chunk ) {
    // pr_debug( "Freeing chunk x:%d y:%d z:%d", chunk->chunk_x, chunk->chunk_y, chunk->chunk_z );
    if ( chunk->blocks ) {
        map_gen_free_block( chunk->blocks );
        chunk->blocks = 0;
    }
}
