#include "RepGame.h"
#include "mouse_selection.h"
#include "abstract/renderer.h"
#include "block.h"

void mouse_selection_init( MouseSelection *mouseSelection, VertexBuffer *vb_block_solid, VertexBufferLayout *vbl_block, VertexBufferLayout *vbl_coords ) {
    int ib_size = 0;
    index_buffer_init( &mouseSelection->ib );
    index_buffer_set_data( &mouseSelection->ib, ib_data_solid, IB_SOLID_SIZE );

    vertex_buffer_init( &mouseSelection->vb_coords );
    vertex_array_init( &mouseSelection->va );
    vertex_array_add_buffer( &mouseSelection->va, vb_block_solid, vbl_block, 0 );
    vertex_array_add_buffer( &mouseSelection->va, &mouseSelection->vb_coords, vbl_coords, 1 );

    mouseSelection->block.face_top = TNT;
    mouseSelection->block.face_bottom = TNT;
    mouseSelection->block.face_sides = TNT;
    mouseSelection->block.x = 0;
    mouseSelection->block.y = 0;
    mouseSelection->block.z = 0;
    mouseSelection->shouldDraw = 0;
}
void mouse_selection_set_block( MouseSelection *mouseSelection, int x, int y, int z, int shouldDraw ) {
    mouseSelection->block.x = x;
    mouseSelection->block.y = y + 1;
    mouseSelection->block.z = z;
    mouseSelection->shouldDraw = shouldDraw;
    vertex_buffer_set_data( &mouseSelection->vb_coords, &mouseSelection->block, sizeof( BlockCoords ) * 1 );
}
void mouse_selection_draw( MouseSelection *mouseSelection, Renderer *renderer, Shader *shader ) {
    if ( mouseSelection->shouldDraw ) {
        renderer_draw( renderer, &mouseSelection->va, &mouseSelection->ib, shader, 1 );
    }
}