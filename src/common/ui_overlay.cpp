#include <math.h>

#include "common/RepGame.hpp"
#include "common/ui_overlay.hpp"
#include "common/abstract/shader.hpp"

#define WIDTH ( 1.5f )
#define SCALE ( 10.0f )

#define UI_OVERLAY_VERTEX_COUNT_CROSSHAIR 8
#define CROSSHAIR_COLOR                                                                                                                                                                                                                        \
    {                                                                                                                                                                                                                                          \
        { 1, 1, 1, 0.5f }                                                                                                                                                                                                                      \
    }
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

MK_SHADER( ui_overlay_vertex );
MK_SHADER( ui_overlay_fragment );

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
        vertex_buffer_init( &ui_overlay->draw_crosshair.vb );
        vertex_buffer_set_data( &ui_overlay->draw_crosshair.vb, vb_data_crosshair, sizeof( UIOverlayVertex ) * UI_OVERLAY_VERTEX_COUNT_CROSSHAIR );
        vertex_array_init( &ui_overlay->draw_crosshair.va );
        showErrors( );
        vertex_array_add_buffer( &ui_overlay->draw_crosshair.va, &ui_overlay->draw_crosshair.vb, &ui_overlay->vbl, 0, 0 );
        showErrors( );
    }
    showErrors( );

    {
        int inventory_offset_x = -400;
        int inventory_offset_y = -400;

        int block_per_row = 20;

        int num_blocks = 200;
        int num_points_per_block = 4;
        int num_index_per_block = 6;
        int block_size = 50;
        int block_spaceing = 60;

        int vb_size = num_blocks * num_points_per_block;
        int ib_size = num_blocks * num_index_per_block;

        unsigned int *ib_data_inventory = ( unsigned int * )calloc( ib_size, sizeof( unsigned int ) );
        UIOverlayVertex *vb_data_inventory = ( UIOverlayVertex * )calloc( vb_size, sizeof( UIOverlayVertex ) );

        for ( int i_block = 0; i_block < num_blocks; i_block++ ) {
            int block_grid_coord_x = i_block % block_per_row;
            int block_grid_coord_y = i_block / block_per_row;
            int block_corner_x = inventory_offset_x + block_grid_coord_x * block_spaceing;
            int block_corner_y = inventory_offset_y + block_grid_coord_y * block_spaceing;

            unsigned int *ui_ib = &ib_data_inventory[ i_block * num_index_per_block ];
            ui_ib[ 0 ] = i_block * num_points_per_block + 0;
            ui_ib[ 1 ] = i_block * num_points_per_block + 3;
            ui_ib[ 2 ] = i_block * num_points_per_block + 1;
            ui_ib[ 3 ] = i_block * num_points_per_block + 3;
            ui_ib[ 4 ] = i_block * num_points_per_block + 0;
            ui_ib[ 5 ] = i_block * num_points_per_block + 2;

            for ( int i_point = 0; i_point < num_points_per_block; i_point++ ) {
                int vb_index = i_block * num_points_per_block + i_point;
                UIOverlayVertex *ui_vertex = &vb_data_inventory[ vb_index ];
                ui_vertex->data.block.x = ( i_point / 2 );
                ui_vertex->data.block.y = ( i_point % 2 );

                ui_vertex->screen_x = block_corner_x + block_size * ui_vertex->data.block.x;
                ui_vertex->screen_y = block_corner_y + block_size * ui_vertex->data.block.y;
                ui_vertex->is_block = 1;
                BlockID blockID = ( BlockID )i_block;
                ui_vertex->data.block.id = blockID - 1;
                ui_vertex->data.block.a = 1.0f;
            }
        }

        index_buffer_init( &ui_overlay->draw_inventory.ib );
        index_buffer_set_data( &ui_overlay->draw_inventory.ib, ib_data_inventory, ib_size );
        vertex_buffer_init( &ui_overlay->draw_inventory.vb );
        vertex_buffer_set_data( &ui_overlay->draw_inventory.vb, vb_data_inventory, sizeof( UIOverlayVertex ) * vb_size );
        vertex_array_init( &ui_overlay->draw_inventory.va );
        vertex_array_add_buffer( &ui_overlay->draw_inventory.va, &ui_overlay->draw_inventory.vb, &ui_overlay->vbl, 0, 0 );
        free( vb_data_inventory );
        free( ib_data_inventory );
    }

    shader_init( &ui_overlay->shader, &ui_overlay_vertex, &ui_overlay_fragment );
}

void ui_overlay_draw( UIOverlay *ui_overlay, Renderer *renderer, Texture *blocksTexture, InputState *input, const glm::mat4 &mvp_ui ) {
    shader_set_uniform_mat4f( &ui_overlay->shader, "u_MVP", mvp_ui );
    shader_set_uniform1i( &ui_overlay->shader, "u_Texture", blocksTexture->slot );

    if ( input->inventory_open ) {
        // renderer_draw( renderer, &ui_overlay->draw_inventory.va, &ui_overlay->draw_inventory.ib, &ui_overlay->shader, 1 );
    } else {
        glBlendFunc( GL_ONE_MINUS_DST_COLOR, GL_ZERO );
        renderer_draw( renderer, &ui_overlay->draw_crosshair.va, &ui_overlay->draw_crosshair.ib, &ui_overlay->shader, 1 );
        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    }
}