#include "common/RepGame.hpp"
#include "common/mouse_selection.hpp"
#include "common/abstract/renderer.hpp"
#include "common/block.hpp"

#define SELECTION_SIZE_OFFSET ( 0.0003f )

static CubeFace vd_data_selection[] = {
    // x=right/left, y=top/bottom, z=front/back : 1/0
    {0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 0, FACE_BACK, CORNER_OFFSET_bbl}, // 0
    {1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 0, FACE_BACK, CORNER_OFFSET_bbr}, // 1
    {1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 1, FACE_BACK, CORNER_OFFSET_tbr}, // 2
    {0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 1, FACE_BACK, CORNER_OFFSET_tbl}, // 3

    {0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 0, FACE_FRONT, CORNER_OFFSET_bfl}, // 4
    {1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 0, FACE_FRONT, CORNER_OFFSET_bfr}, // 5
    {1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 1, FACE_FRONT, CORNER_OFFSET_tfr}, // 6
    {0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 1, FACE_FRONT, CORNER_OFFSET_tfl}, // 7

    {0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 0, FACE_LEFT, CORNER_OFFSET_bbl},  // 8
    {1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 0, FACE_RIGHT, CORNER_OFFSET_bbr}, // 9
    {1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 1, FACE_RIGHT, CORNER_OFFSET_tbr}, // 10
    {0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 1, FACE_LEFT, CORNER_OFFSET_tbl},  // 11

    {0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 0, FACE_LEFT, CORNER_OFFSET_bfl},  // 12
    {1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 0, FACE_RIGHT, CORNER_OFFSET_bfr}, // 13
    {1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 1, FACE_RIGHT, CORNER_OFFSET_tfr}, // 14
    {0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 1, FACE_LEFT, CORNER_OFFSET_tfl},  // 15

    {0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 1, FACE_BOTTOM, CORNER_OFFSET_bbl}, // 16
    {1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 1, FACE_BOTTOM, CORNER_OFFSET_bbr}, // 17
    {1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 0, FACE_TOP, CORNER_OFFSET_tbr},    // 18
    {0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 0, FACE_TOP, CORNER_OFFSET_tbl},    // 19

    {0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 0, FACE_BOTTOM, CORNER_OFFSET_bfl}, // 20
    {1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 0, FACE_BOTTOM, CORNER_OFFSET_bfr}, // 21
    {1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 1, FACE_TOP, CORNER_OFFSET_tfr},    // 22
    {0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 1, FACE_TOP, CORNER_OFFSET_tfl},    // 23

    {0.5f, 1.0f + SELECTION_SIZE_OFFSET, 0.5f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_TOP, CORNER_OFFSET_c},    // 24
    {0.5f, 0.0f - SELECTION_SIZE_OFFSET, 0.5f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_BOTTOM, CORNER_OFFSET_c}, // 25
    {1.0f + SELECTION_SIZE_OFFSET, 0.5f, 0.5f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_RIGHT, CORNER_OFFSET_c},  // 26
    {0.5f, 0.5f, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_FRONT, CORNER_OFFSET_c},  // 27
    {0.0f - SELECTION_SIZE_OFFSET, 0.5f, 0.5f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_LEFT, CORNER_OFFSET_c},   // 28
    {0.5f, 0.5f, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_BACK, CORNER_OFFSET_c},   // 29
};
#define VB_DATA_SIZE_SELECTION ( 4 * 6 + 6 )

void mouse_selection_init( MouseSelection *mouseSelection, VertexBufferLayout *vbl_block, VertexBufferLayout *vbl_coords ) {
    int ib_size = 0;
    index_buffer_init( &mouseSelection->ib );
    index_buffer_set_data( &mouseSelection->ib, ib_data_solid, IB_SOLID_SIZE );

    vertex_buffer_init( &mouseSelection->vb_block );
    vertex_buffer_set_data( &mouseSelection->vb_block, vd_data_selection, sizeof( CubeFace ) * VB_DATA_SIZE_SELECTION );

    vertex_buffer_init( &mouseSelection->vb_coords );
    vertex_array_init( &mouseSelection->va );
    vertex_array_add_buffer( &mouseSelection->va, &mouseSelection->vb_block, vbl_block, 0, 0 );
    vertex_array_add_buffer( &mouseSelection->va, &mouseSelection->vb_coords, vbl_coords, 1, vbl_block->current_size );
    for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
        mouseSelection->block.face[ i ] = PLAYER_SELECTION - 1;
    }
    mouseSelection->block.x = 0;
    mouseSelection->block.y = 0;
    mouseSelection->block.z = 0;
    mouseSelection->block.mesh_x = 1;
    mouseSelection->block.mesh_y = 1;
    mouseSelection->block.mesh_z = 1;
    mouseSelection->shouldDraw = 0;
}
void mouse_selection_set_block( MouseSelection *mouseSelection, int x, int y, int z, int shouldDraw ) {
    // y += 1;
    mouseSelection->shouldDraw = shouldDraw;
    if ( mouseSelection->block.x != x || //
         mouseSelection->block.y != y || //
         mouseSelection->block.z != z    //
    ) {
        mouseSelection->block.x = x;
        mouseSelection->block.y = y;
        mouseSelection->block.z = z;
#if MOB_ROTATION
        mouseSelection->block.rotation = glm::mat4( 1 );
#endif

        vertex_buffer_set_data( &mouseSelection->vb_coords, &mouseSelection->block, sizeof( BlockCoords ) * 1 );
    }
}
void mouse_selection_draw( MouseSelection *mouseSelection, Renderer *renderer, Shader *shader ) {
    if ( mouseSelection->shouldDraw ) {
        renderer_draw_lines( renderer, &mouseSelection->va, &mouseSelection->ib, shader, 1 );
    }
}