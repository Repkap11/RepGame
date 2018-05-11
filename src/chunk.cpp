#include "chunk.h"
#include "RepGame.h"
#include "draw3d.h"
#include "map_gen.h"
#include "map_storage.h"
#include <GL/gl.h>

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

void chunk_init( Chunk *chunk, VertexBuffer *vb_block, VertexBufferLayout *vbl_block ) {
    chunk->blocks = ( Block * )calloc( CHUNK_BLOCK_SIZE, sizeof( Block ) );
    chunk->populated_blocks = ( BlockCoords * )calloc( CHUNK_BLOCK_SIZE, sizeof( BlockCoords ) );
    index_buffer_init( &chunk->ib, ib_data, 3 * 2 * 6 );
    index_buffer_bind( &chunk->ib );

    vertex_buffer_layout_init( &chunk->vbl_coords );
    vertex_buffer_layout_bind( &chunk->vbl_coords );
    // The sum of these must be elements_per_vertex
    vertex_buffer_layout_push_float( &chunk->vbl_coords, 3 );        // block 3d world coords
    vertex_buffer_layout_push_unsigned_int( &chunk->vbl_coords, 3 ); // which texture (block type)

    unsigned int elements_per_vertex_coords = 6;
    vertex_buffer_init( &chunk->vb_coords );

    vertex_array_init( &chunk->va );
    vertex_array_add_buffer( &chunk->va, vb_block, vbl_block, 0 );
    vertex_array_add_buffer( &chunk->va, &chunk->vb_coords, &chunk->vbl_coords, 1 );
}

void chunk_destroy( Chunk *chunk ) {
    free( chunk->blocks );
    free( chunk->populated_blocks );
}

void chunk_render( const Chunk *chunk, const Renderer *renderer, const Shader *shader ) {
    if ( chunk->should_render && chunk->num_instances != 0 ) {
        if (chunk->is_loading){
            pr_debug("Error, attempting to render loading chunk");
        }
        renderer_draw( renderer, &chunk->va, &chunk->ib, shader, chunk->num_instances );
    }
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
    int which_block_coord = 0;
    for ( int index = CHUNK_BLOCK_DRAW_START; index < CHUNK_BLOCK_DRAW_STOP; index++ ) {
        int x, y, z;
        int drawn_block = chunk_get_coords_from_index( index, &x, &y, &z );
        if ( drawn_block ) {
            BlockDefinition *blockDef = chunk->blocks[ index ].blockDef;
            int visiable_block = blockDef->alpha != 0.0f;
            if ( visiable_block ) {
                int visiable_from_top = chunk->blocks[ chunk_get_index_from_coords( x + 0, y + 1, z + 0 ) ].blockDef->alpha < blockDef->alpha;
                int visiable_from_bottom = chunk->blocks[ chunk_get_index_from_coords( x + 0, y - 1, z + 0 ) ].blockDef->alpha < blockDef->alpha;
                int visiable_from_left = chunk->blocks[ chunk_get_index_from_coords( x + 0, y + 0, z - 1 ) ].blockDef->alpha < blockDef->alpha;
                int visiable_from_right = chunk->blocks[ chunk_get_index_from_coords( x + 0, y + 0, z + 1 ) ].blockDef->alpha < blockDef->alpha;
                int visiable_from_front = chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 0, z + 0 ) ].blockDef->alpha < blockDef->alpha;
                int visiable_from_back = chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 0, z + 0 ) ].blockDef->alpha < blockDef->alpha;

                int block_could_be_visiable = visiable_from_top || visiable_from_bottom || visiable_from_left || visiable_from_right || visiable_from_front || visiable_from_back;

                if ( block_could_be_visiable ) {
                    BlockCoords *blockCoord = &chunk->populated_blocks[ which_block_coord ];
                    blockCoord->x = chunk->chunk_x * CHUNK_SIZE + x;
                    blockCoord->y = chunk->chunk_y * CHUNK_SIZE + y;
                    blockCoord->z = chunk->chunk_z * CHUNK_SIZE + z;
                    blockCoord->face_top = blockDef->textures.top - 1;
                    blockCoord->face_sides = blockDef->textures.side - 1;
                    blockCoord->face_bottom = blockDef->textures.bottom - 1;
                    which_block_coord += 1;
                }
            }
        }
    }
    chunk->num_instances = which_block_coord;
}

void chunk_program_terrain( Chunk *chunk ) {
    if ( chunk->num_instances != 0 ) {
        vertex_buffer_set_data( &chunk->vb_coords, chunk->populated_blocks, sizeof( BlockCoords ) * chunk->num_instances );
        chunk->should_render = 1;
    }
}

void chunk_unprogram_terrain( Chunk *chunk ) {
    chunk->should_render = 0;
}