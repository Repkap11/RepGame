#include "RepGame.h"
#include "mouse_selection.h"
#include "abstract/renderer.h"
#include "block.h"

#define SELECTION_SIZE_OFFSET ( 0.001f )

static CubeFace vd_data_selection[] = {
    {0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ !0, 0, FACE_FRONT}, // 0
    {1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ !1, 0, FACE_FRONT}, // 1
    {1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ !1, 1, FACE_FRONT}, // 2
    {0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ !0, 1, FACE_FRONT}, // 3

    {0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ !1, 0, FACE_BACK}, // 4
    {1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ !0, 0, FACE_BACK}, // 5
    {1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ !0, 1, FACE_BACK}, // 6
    {0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ !1, 1, FACE_BACK}, // 7

    {0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 0, FACE_LEFT},  // 8
    {1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 0, FACE_RIGHT}, // 9
    {1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 1, FACE_RIGHT}, // 10
    {0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 1, FACE_LEFT},  // 11

    {0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 0, FACE_LEFT},  // 12
    {1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 0, FACE_RIGHT}, // 13
    {1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 1, FACE_RIGHT}, // 14
    {0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 1, FACE_LEFT},  // 15

    {0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ !0, !0, FACE_BOTTOM}, // 16
    {1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ !1, !0, FACE_BOTTOM}, // 17
    {1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ !1, !1, FACE_TOP},    // 18
    {0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ !0, !1, FACE_TOP},    // 19

    {0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 0, FACE_BOTTOM}, // 20
    {1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 0, FACE_BOTTOM}, // 21
    {1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 1, FACE_TOP},    // 22
    {0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 1, FACE_TOP},    // 23
};
#define VB_DATA_SIZE_SELECTION ( 4 * 6 )

void mouse_selection_init( MouseSelection *mouseSelection, VertexBufferLayout *vbl_block, VertexBufferLayout *vbl_coords ) {
    int ib_size = 0;
    index_buffer_init( &mouseSelection->ib );
    index_buffer_set_data( &mouseSelection->ib, ib_data_solid, IB_SOLID_SIZE );

    vertex_buffer_init( &mouseSelection->vb_block );
    vertex_buffer_set_data( &mouseSelection->vb_block, vd_data_selection, sizeof( CubeFace ) * VB_DATA_SIZE_SELECTION );

    vertex_buffer_init( &mouseSelection->vb_coords );
    vertex_array_init( &mouseSelection->va );
    vertex_array_add_buffer( &mouseSelection->va, &mouseSelection->vb_block, vbl_block, 0 );
    vertex_array_add_buffer( &mouseSelection->va, &mouseSelection->vb_coords, vbl_coords, 1 );

    mouseSelection->block.face_top = WATER - 1;
    mouseSelection->block.face_bottom = WATER - 1;
    mouseSelection->block.face_sides = WATER - 1;
    mouseSelection->block.x = 0;
    mouseSelection->block.y = 0;
    mouseSelection->block.z = 0;
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
        vertex_buffer_set_data( &mouseSelection->vb_coords, &mouseSelection->block, sizeof( BlockCoords ) * 1 );
    }
}
void mouse_selection_draw( MouseSelection *mouseSelection, Renderer *renderer, Shader *shader ) {
    if ( mouseSelection->shouldDraw ) {
        renderer_draw( renderer, &mouseSelection->va, &mouseSelection->ib, shader, 1 );
    }
}