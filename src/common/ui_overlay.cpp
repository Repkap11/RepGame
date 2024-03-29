#include <math.h>

#include "common/RepGame.hpp"
#include "common/ui_overlay.hpp"
#include "common/abstract/shader.hpp"

#define WIDTH ( 1.5f )
#define SCALE ( 10.0f )

#define UI_OVERLAY_VERTEX_COUNT_CROSSHAIR 8

#define CROSSHAIR_COLOR                                                                                                                                                                                                                        \
    { 0, 0, 0 }, {                                                                                                                                                                                                                             \
        1, 1, 1, 0.5f                                                                                                                                                                                                                          \
    }

UIOverlayVertex vb_data_crosshair[] = {
    { -SCALE * WIDTH, -WIDTH, 0, CROSSHAIR_COLOR, 0 }, // 0
    { -SCALE * WIDTH, WIDTH, 0, CROSSHAIR_COLOR, 0 },  // 1
    { SCALE * WIDTH, -WIDTH, 0, CROSSHAIR_COLOR, 0 },  // 2
    { SCALE * WIDTH, WIDTH, 0, CROSSHAIR_COLOR, 0 },   // 3

    { -WIDTH, -SCALE *WIDTH, 0, CROSSHAIR_COLOR, 0 }, // 4
    { -WIDTH, SCALE *WIDTH, 0, CROSSHAIR_COLOR, 0 },  // 5
    { WIDTH, -SCALE *WIDTH, 0, CROSSHAIR_COLOR, 0 },  // 6
    { WIDTH, SCALE *WIDTH, 0, CROSSHAIR_COLOR, 0 },   // 7

};

float holding_alpha = 1.0f;

float id = ( float )TNT;
#define UI_OVERLAY_VERTEX_COUNT_HOLDING_BLOCK ( 4 * 3 )
UIOverlayVertex vb_data_holding_block[] = {
    { 0, 0, 1, { 1, 0, id }, { 1, 1, 1, holding_alpha }, FACE_TOP }, // 0
    { 0, 0, 1, { 1, 1, id }, { 1, 1, 1, holding_alpha }, FACE_TOP }, // 1
    { 0, 0, 1, { 0, 0, id }, { 1, 1, 1, holding_alpha }, FACE_TOP }, // 2
    { 0, 0, 1, { 0, 1, id }, { 1, 1, 1, holding_alpha }, FACE_TOP }, // 3

    { 0, 0, 1, { 1, 0, id }, { 1, 1, 1, holding_alpha }, FACE_FRONT }, // 4
    { 0, 0, 1, { 1, 1, id }, { 1, 1, 1, holding_alpha }, FACE_FRONT }, // 5
    { 0, 0, 1, { 0, 0, id }, { 1, 1, 1, holding_alpha }, FACE_FRONT }, // 6
    { 0, 0, 1, { 0, 1, id }, { 1, 1, 1, holding_alpha }, FACE_FRONT }, // 7

    { 0, 0, 1, { 1, 0, id }, { 1, 1, 1, holding_alpha }, FACE_RIGHT }, // 8
    { 0, 0, 1, { 1, 1, id }, { 1, 1, 1, holding_alpha }, FACE_RIGHT }, // 9
    { 0, 0, 1, { 0, 0, id }, { 1, 1, 1, holding_alpha }, FACE_RIGHT }, // 10
    { 0, 0, 1, { 0, 1, id }, { 1, 1, 1, holding_alpha }, FACE_RIGHT }, // 11

};

int holding_block_vertex_face_map[] = {
    FACE_TOP,   FACE_TOP,   FACE_TOP,   FACE_TOP,

    FACE_FRONT, FACE_FRONT, FACE_FRONT, FACE_FRONT,

    FACE_RIGHT, FACE_RIGHT, FACE_RIGHT, FACE_RIGHT,
};

float isometric_coords[][ 2 ]{
    { 0, 0 },      { -1, 0.5f }, { 1, 0.5f },  { 0, 1 }, //
    { -1, -0.5f }, { -1, 0.5f }, { 0, -1 },    { 0, 0 }, //
    { 0, -1 },     { 0, 0 },     { 1, -0.5f }, { 1, 0.5f },
};
float square_coords[][ 2 ]{
    { 0, 0 },   { 0, 0 },  { 0, 0 },  { 0, 0 }, //
    { -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 }, //
    { 0, 0 },   { 0, 0 },  { 0, 0 },  { 0, 0 },
};

#define UI_OVERLAY_INDEX_COUNT_CROSSHAIR ( 3 * 2 * 2 )
unsigned int ib_data_crosshair[] = {
    0, 3, 1, //
    3, 0, 2, //

    4, 7, 5, //
    7, 4, 6, //
};

#define UI_OVERLAY_INDEX_COUNT_HOLDING_BLOCK_ISOMETRIC ( 3 * 2 * 3 )
unsigned int ib_holding_block_isometric[] = {
    0,  3,  1, //
    3,  0,  2, //

    4,  7,  5, //
    7,  4,  6, //

    8,  11, 9,  //
    11, 8,  10, //
};

#define UI_OVERLAY_INDEX_COUNT_HOLDING_BLOCK_SQUARE ( 3 * 2 * 1 )
unsigned int ib_holding_block_square[] = {
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
    vertex_buffer_layout_push_float( &ui_overlay->vbl, 2 );        // UIOverlayVertex screen_position
    vertex_buffer_layout_push_unsigned_int( &ui_overlay->vbl, 1 ); // UIOverlayVertex is_block
    vertex_buffer_layout_push_float( &ui_overlay->vbl, 3 );        // UIOverlayVertex texture
    vertex_buffer_layout_push_float( &ui_overlay->vbl, 4 );        // UIOverlayVertex tint
    vertex_buffer_layout_push_unsigned_int( &ui_overlay->vbl, 1 ); // UIOverlayVertex face_type

    {
        index_buffer_init( &ui_overlay->draw_crosshair.ib );
        index_buffer_set_data( &ui_overlay->draw_crosshair.ib, ib_data_crosshair, UI_OVERLAY_INDEX_COUNT_CROSSHAIR );
        vertex_buffer_init( &ui_overlay->draw_crosshair.vb );
        vertex_buffer_set_data( &ui_overlay->draw_crosshair.vb, vb_data_crosshair, sizeof( UIOverlayVertex ) * UI_OVERLAY_VERTEX_COUNT_CROSSHAIR );
        vertex_array_init( &ui_overlay->draw_crosshair.va );
        vertex_array_add_buffer( &ui_overlay->draw_crosshair.va, &ui_overlay->draw_crosshair.vb, &ui_overlay->vbl, 0, 0 );
    }
    {
        index_buffer_init( &ui_overlay->draw_holding_block.ib_isometric );
        index_buffer_set_data( &ui_overlay->draw_holding_block.ib_isometric, ib_holding_block_isometric, UI_OVERLAY_INDEX_COUNT_HOLDING_BLOCK_ISOMETRIC );

        index_buffer_init( &ui_overlay->draw_holding_block.ib_square );
        index_buffer_set_data( &ui_overlay->draw_holding_block.ib_square, ib_holding_block_square, UI_OVERLAY_INDEX_COUNT_HOLDING_BLOCK_SQUARE );

        vertex_buffer_init( &ui_overlay->draw_holding_block.vb );
        vertex_buffer_set_data( &ui_overlay->draw_holding_block.vb, vb_data_holding_block, sizeof( UIOverlayVertex ) * UI_OVERLAY_VERTEX_COUNT_HOLDING_BLOCK );
        vertex_array_init( &ui_overlay->draw_holding_block.va );
        vertex_array_add_buffer( &ui_overlay->draw_holding_block.va, &ui_overlay->draw_holding_block.vb, &ui_overlay->vbl, 0, 0 );
    }
    showErrors( );

    shader_init( &ui_overlay->shader, &ui_overlay_vertex, &ui_overlay_fragment );

    inventory_init( &ui_overlay->inventory, &ui_overlay->vbl );
}

void ui_overlay_on_screen_size_change( UIOverlay *ui_overlay, int width, int height ) {
    ui_overlay->screen_width = width;
    ui_overlay->screen_height = height;
    // When the screen changes, we need to reprocess the held block graphics
    ui_overlay_set_holding_block( ui_overlay, ui_overlay->draw_holding_block.heldBlockID );
    inventory_render(&ui_overlay->inventory, width, height);
}

void ui_overlay_set_holding_block( UIOverlay *ui_overlay, BlockID holding_block ) {
    ui_overlay->draw_holding_block.heldBlockID = holding_block;
    Block *holdingBlock = block_definition_get_definition( ui_overlay->draw_holding_block.heldBlockID );
    float( *which_shape )[ 2 ];
    if ( holdingBlock->icon_is_isometric ) {
        which_shape = isometric_coords;
    } else {
        which_shape = square_coords;
    }
    for ( int i = 0; i < UI_OVERLAY_VERTEX_COUNT_HOLDING_BLOCK; i++ ) {
        UIOverlayVertex *vertex = &vb_data_holding_block[ i ];

        vertex->screen_x = -1.0 * ui_overlay->screen_width / 2 + 50 * ( which_shape[ i ][ 0 ] + 2 );
        vertex->screen_y = -1.0 * ui_overlay->screen_height / 2 + 50 * ( which_shape[ i ][ 1 ] + 2 );

        BlockID texture;
        if ( !holdingBlock->icon_is_isometric ) {
            texture = holdingBlock->inventory_non_isometric_id;
        } else {
            texture = holdingBlock->textures[ holding_block_vertex_face_map[ i ] ];
        }
        vertex->texture.id = texture - 1;
    }
    vertex_buffer_set_data( &ui_overlay->draw_holding_block.vb, vb_data_holding_block, sizeof( UIOverlayVertex ) * UI_OVERLAY_VERTEX_COUNT_HOLDING_BLOCK );
}

void ui_overlay_draw( UIOverlay *ui_overlay, Renderer *renderer, Texture *blocksTexture, InputState *input, const glm::mat4 &mvp_ui ) {

    shader_set_uniform_mat4f( &ui_overlay->shader, "u_MVP", mvp_ui );
    shader_set_uniform1i( &ui_overlay->shader, "u_Texture", blocksTexture->slot );

    if ( input->inventory_open ) {
        inventory_draw( &ui_overlay->inventory, renderer, blocksTexture, input, mvp_ui, &ui_overlay->shader );
    } else {
        IndexBuffer *which_index_buffer;
        Block *heldBlock = block_definition_get_definition( ui_overlay->draw_holding_block.heldBlockID );
        if ( heldBlock->icon_is_isometric ) {
            which_index_buffer = &ui_overlay->draw_holding_block.ib_isometric;
        } else {
            which_index_buffer = &ui_overlay->draw_holding_block.ib_square;
        }
        renderer_draw( renderer, &ui_overlay->draw_holding_block.va, which_index_buffer, &ui_overlay->shader, 1 );
        glBlendFunc( GL_ONE_MINUS_DST_COLOR, GL_ZERO );
        renderer_draw( renderer, &ui_overlay->draw_crosshair.va, &ui_overlay->draw_crosshair.ib, &ui_overlay->shader, 1 );
        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    }
    showErrors( );
}

void ui_overlay_cleanup( UIOverlay *ui_overlay ) {
    inventory_cleanup( &ui_overlay->inventory );
}
