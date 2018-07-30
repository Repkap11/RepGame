#include "RepGame.h"
#include "mouse_selection.h"
#include "abstract/renderer.h"
#include "block.h"

void mouse_selection_init( MouseSelection *mouseSelection, VertexBuffer *vb_block_solid, VertexBufferLayout *vbl_block, VertexBufferLayout *vbl_coords ) {
    int ib_size = 0;
    index_buffer_set_data( &mouseSelection->ib, ib_data_solid, IB_SOLID_SIZE );

    vertex_buffer_init( &mouseSelection->vb_coords );
    vertex_array_init( &mouseSelection->va );
    vertex_array_add_buffer( &mouseSelection->va, vb_block_solid, vbl_block, 0 );
    vertex_array_add_buffer( &mouseSelection->va, &mouseSelection->vb_coords, vbl_coords, 1 );

    mouseSelection->block.face_top = TNT;
    mouseSelection->block.face_bottom = TNT;
    mouseSelection->block.face_sides = TNT;
}
void mouse_selection_set_block( MouseSelection *mouseSelection, int x, int y, int z ) {
    mouseSelection->block.x = x;
    mouseSelection->block.y = y;
    mouseSelection->block.z = z;
    vertex_buffer_set_data( &mouseSelection->vb_coords, &mouseSelection->block, sizeof( BlockCoords ) * 1 );
}
void mouse_selection_draw( MouseSelection *mouseSelection, Renderer *renderer, Shader *shader ) {
    renderer_draw( renderer, &mouseSelection->va, &mouseSelection->ib, shader, 1 );
}