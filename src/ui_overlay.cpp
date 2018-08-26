#include <math.h>

#include "RepGame.hpp"
#include "ui_overlay.hpp"
#include "abstract/shader.hpp"

#define UI_OVERLAY_VERTEX_COUNT 8

#define WIDTH ( 1.5f )
#define SCALE ( 10.0f )
UIOverlayVertex vb_data[] = {
    {-SCALE * WIDTH, -WIDTH}, // 0
    {-SCALE * WIDTH, WIDTH},  // 1
    {SCALE * WIDTH, -WIDTH},  // 2
    {SCALE * WIDTH, WIDTH},   // 3

    {-WIDTH, -SCALE *WIDTH}, // 4
    {-WIDTH, SCALE *WIDTH},  // 5
    {WIDTH, -SCALE *WIDTH},  // 6
    {WIDTH, SCALE *WIDTH},   // 7

};

#define UI_OVERLAY_INDEX_COUNT ( 3 * 2 * 2 )
unsigned int ib_data[] = {
    0, 3, 1, //
    3, 0, 2, //

    4, 7, 5, //
    7, 4, 6, //
};
void ui_overlay_init( UIOverlay *ui_overlay ) {
    // Calc The Vertices
    index_buffer_init( &ui_overlay->ib );
    index_buffer_set_data( &ui_overlay->ib, ib_data, UI_OVERLAY_INDEX_COUNT );

    // These are from UIOverlayVertex
    vertex_buffer_layout_init( &ui_overlay->vbl );
    vertex_buffer_layout_push_float( &ui_overlay->vbl, 2 ); // Coords

    ui_overlay->vertex_size = UI_OVERLAY_VERTEX_COUNT;

    vertex_buffer_init( &ui_overlay->vb );
    vertex_buffer_set_data( &ui_overlay->vb, vb_data, sizeof( UIOverlayVertex ) * ui_overlay->vertex_size );

    vertex_array_init( &ui_overlay->va );

    vertex_array_add_buffer( &ui_overlay->va, &ui_overlay->vb, &ui_overlay->vbl, 0, 0 );
    shader_init( &ui_overlay->shader, "ui_overlay_vertex.glsl", "ui_overlay_fragment.glsl" );
    shader_set_uniform4f( &ui_overlay->shader, "u_Color", 0, 0, 0, 0.5f );
}

void ui_overlay_draw( UIOverlay *ui_overlay, Renderer *renderer, glm::mat4 &mvp_ui ) {
    shader_set_uniform_mat4f( &ui_overlay->shader, "u_MVP", mvp_ui );
    renderer_draw( renderer, &ui_overlay->va, &ui_overlay->ib, &ui_overlay->shader, 1 );
}