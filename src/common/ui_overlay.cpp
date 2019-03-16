#include <math.h>

#include "common/RepGame.hpp"
#include "common/ui_overlay.hpp"
#include "common/abstract/shader.hpp"

#define WIDTH ( 1.5f )
#define SCALE ( 10.0f )

#define UI_OVERLAY_VERTEX_COUNT_CROSSHAIR 8
#define CROSSHAIR_COLOR 0, 0, 0, 0.5f
UIOverlayVertex vb_data_crosshair[] = {
    {-SCALE * WIDTH, -WIDTH, 0, CROSSHAIR_COLOR}, // 0
    {-SCALE * WIDTH, WIDTH, 0, CROSSHAIR_COLOR},  // 1
    {SCALE * WIDTH, -WIDTH, 0, CROSSHAIR_COLOR},  // 2
    {SCALE * WIDTH, WIDTH, 0, CROSSHAIR_COLOR},   // 3

    {-WIDTH, -SCALE *WIDTH, 0, CROSSHAIR_COLOR}, // 4
    {-WIDTH, SCALE *WIDTH, 0, CROSSHAIR_COLOR},  // 5
    {WIDTH, -SCALE *WIDTH, 0, CROSSHAIR_COLOR},  // 6
    {WIDTH, SCALE *WIDTH, 0, CROSSHAIR_COLOR},   // 7

};
#define UI_OVERLAY_INDEX_COUNT_CROSSHAIR ( 3 * 2 * 2 )
unsigned int ib_data_crosshair[] = {
    0, 3, 1, //
    3, 0, 2, //

    4, 7, 5, //
    7, 4, 6, //
};

void ui_overlay_init( UIOverlay *ui_overlay ) {
    // Calc The Vertices
    showErrors( );

    // These are from UIOverlayVertex
    vertex_buffer_layout_init( &ui_overlay->vbl );
    vertex_buffer_layout_push_float( &ui_overlay->vbl, 2 ); // UIOverlayVertex screen_position
    vertex_buffer_layout_push_float( &ui_overlay->vbl, 1 ); // UIOverlayVertex is_block
    vertex_buffer_layout_push_float( &ui_overlay->vbl, 4 ); // UIOverlayVertex tex_coord

    {
        index_buffer_init( &ui_overlay->draw_crosshair.ib );
        index_buffer_set_data( &ui_overlay->draw_crosshair.ib, ib_data_crosshair, UI_OVERLAY_INDEX_COUNT_CROSSHAIR );
        ui_overlay->draw_crosshair.vertex_size = UI_OVERLAY_VERTEX_COUNT_CROSSHAIR;
        vertex_buffer_init( &ui_overlay->draw_crosshair.vb );
        vertex_buffer_set_data( &ui_overlay->draw_crosshair.vb, vb_data_crosshair, sizeof( UIOverlayVertex ) * ui_overlay->draw_crosshair.vertex_size );
        vertex_array_init( &ui_overlay->draw_crosshair.va );
        showErrors( );
        vertex_array_add_buffer( &ui_overlay->draw_crosshair.va, &ui_overlay->draw_crosshair.vb, &ui_overlay->vbl, 0, 0 );
        showErrors( );
    }
    showErrors( );

    // {
    //     index_buffer_init( &ui_overlay->draw_inventory.ib );
    //     index_buffer_set_data( &ui_overlay->draw_inventory.ib, ib_data_crosshair, UI_OVERLAY_INDEX_COUNT_INVENTORY );
    //     ui_overlay->draw_inventory.vertex_size = UI_OVERLAY_VERTEX_COUNT_INVENTORY;
    //     vertex_buffer_init( &ui_overlay->draw_inventory.vb );
    //     vertex_buffer_set_data( &ui_overlay->draw_inventory.vb, vb_data_crosshair, sizeof( UIOverlayVertex ) * ui_overlay->draw_inventory.vertex_size );
    //     vertex_array_init( &ui_overlay->draw_inventory.va );
    //     vertex_array_add_buffer( &ui_overlay->draw_inventory.va, &ui_overlay->draw_inventory.vb, &ui_overlay->vbl, 0, 0 );
    // }

    shader_init( &ui_overlay->shader, "ui_overlay_vertex.glsl", "ui_overlay_fragment.glsl" );
    ui_overlay->state.inventoryOpen = 0;
}

void ui_overlay_update_state( UIOverlay *ui_overlay ) {
}

void ui_overlay_draw( UIOverlay *ui_overlay, Renderer *renderer, glm::mat4 &mvp_ui ) {
    shader_set_uniform_mat4f( &ui_overlay->shader, "u_MVP", mvp_ui );
    if ( ui_overlay->state.inventoryOpen ) {
        renderer_draw( renderer, &ui_overlay->draw_inventory.va, &ui_overlay->draw_inventory.ib, &ui_overlay->shader, 1 );
    } else {
        renderer_draw( renderer, &ui_overlay->draw_crosshair.va, &ui_overlay->draw_crosshair.ib, &ui_overlay->shader, 1 );
    }
}