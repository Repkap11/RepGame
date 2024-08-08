#include <math.h>

#include "common/RepGame.hpp"
#include "common/ui_overlay.hpp"
#include "common/abstract/shader.hpp"

#define WIDTH ( 1.5f )
#define SCALE ( 10.0f )

#define UI_OVERLAY_VERTEX_COUNT_CROSSHAIR 8

#define CROSSHAIR_COLOR { 0, 0 }

UIOverlayVertex vb_data_crosshair_element[ UI_OVERLAY_VERTEX_COUNT_CROSSHAIR ] = {
    { -SCALE * WIDTH, -WIDTH, { 0, 0 }, 0, ISO_FACE_TOP }, // 0
    { -SCALE * WIDTH, WIDTH, { 0, 0 }, 0, ISO_FACE_TOP },  // 1
    { SCALE * WIDTH, -WIDTH, { 0, 0 }, 0, ISO_FACE_TOP },  // 2
    { SCALE * WIDTH, WIDTH, { 0, 0 }, 0, ISO_FACE_TOP },   // 3

    { -WIDTH, -SCALE *WIDTH, { 0, 0 }, 0, ISO_FACE_TOP }, // 4
    { -WIDTH, SCALE *WIDTH, { 0, 0 }, 0, ISO_FACE_TOP },  // 5
    { WIDTH, -SCALE *WIDTH, { 0, 0 }, 0, ISO_FACE_TOP },  // 6
    { WIDTH, SCALE *WIDTH, { 0, 0 }, 0, ISO_FACE_TOP },   // 7
};
UIOverlayInstance vb_data_crosshair_instance = { 0, 0, 0, 1, 1, 0, 0, { 0, 0, 0 }, { 1, 1, 1, 0.5f } };

float holding_alpha = 1.0f;

UIOverlayInstance vb_data_holding_block_instance = { 0, 0, 0, 0, 0, 1, 0, { AIR, AIR, AIR }, { 1, 1, 1, holding_alpha } };

int holding_block_vertex_face_map[] = {
    FACE_TOP,   FACE_TOP,   FACE_TOP,   FACE_TOP,

    FACE_FRONT, FACE_FRONT, FACE_FRONT, FACE_FRONT,

    FACE_RIGHT, FACE_RIGHT, FACE_RIGHT, FACE_RIGHT,
};

#define ISOMETRIC_FACES 3

#define UI_OVERLAY_INDEX_COUNT_CROSSHAIR ( 3 * 2 * 2 )
unsigned int ib_data_crosshair[] = {
    0, 3, 1, //
    3, 0, 2, //

    4, 7, 5, //
    7, 4, 6, //
};

// #define UI_OVERLAY_INDEX_COUNT_HOLDING_BLOCK_ISOMETRIC ( 3 * 2 * 3 )
// unsigned int ib_holding_block_isometric[] = {
//     0,  3,  1, //
//     3,  0,  2, //

//     4,  7,  5, //
//     7,  4,  6, //

//     8,  11, 9,  //
//     11, 8,  10, //
// };

// #define UI_OVERLAY_INDEX_COUNT_HOLDING_BLOCK_SQUARE ( 3 * 2 * 1 )
// unsigned int ib_holding_block_square[] = {
//     4, 7, 5, //
//     7, 4, 6, //
// };

static int inventory_isometric_face[] = { FACE_TOP, FACE_FRONT, FACE_RIGHT };

#define VB_ISOMETRIC_QUAD_SIZE 16
static const UIOverlayVertex vb_isometric_quad[ VB_ISOMETRIC_QUAD_SIZE ] = {
    // Isometric
    { 0.5f, 0.625, { 1, 0 }, 1, ISO_FACE_TOP },   // 0
    { 0.0f, 0.8125f, { 1, 1 }, 1, ISO_FACE_TOP }, // 1
    { 1.0f, 0.8125f, { 0, 0 }, 1, ISO_FACE_TOP }, // 2
    { 0.5f, 1.0f, { 0, 1 }, 1, ISO_FACE_TOP },    // 3

    { 0.0f, 0.1875f, { 1, 0 }, 1, ISO_FACE_FRONT }, // 4
    { 0.0f, 0.8125f, { 1, 1 }, 1, ISO_FACE_FRONT }, // a
    { 0.5f, 0.0f, { 0, 0 }, 1, ISO_FACE_FRONT },    // 5
    { 0.5f, 0.625, { 0, 1 }, 1, ISO_FACE_FRONT },   // b

    { 0.5f, 0.0f, { 1, 0 }, 1, ISO_FACE_RIGHT },    // c
    { 0.5f, 0.625, { 1, 1 }, 1, ISO_FACE_RIGHT },   // d
    { 1.0f, 0.1875f, { 0, 0 }, 1, ISO_FACE_RIGHT }, // 6
    { 1.0f, 0.8125f, { 0, 1 }, 1, ISO_FACE_RIGHT }, // e

    // Quad
    { 0.5f, 0.0f + 0.24, { 0, 0 }, 0, ISO_FACE_FRONT },    //
    { 0.5f, 0.625f + 0.24, { 0, 1 }, 0, ISO_FACE_FRONT },  //
    { 1.0f, 0.1875f + 0.24, { 1, 0 }, 0, ISO_FACE_FRONT }, //
    { 1.0f, 0.8125f + 0.24, { 1, 1 }, 0, ISO_FACE_FRONT }  //
};

#define IB_ISOMETRIC_QUAD_SIZE 24
static const unsigned int ib_isometric_quad[ IB_ISOMETRIC_QUAD_SIZE ] = {
    // Top
    // 0, 3, 1, 3, 0, 2, 4, 7, 5, 7, 4, 6, 8, 11, 9, 11, 8, 10,
    0,  3,  1, //
    3,  0,  2, //

    4,  7,  5, //
    7,  4,  6, //

    8,  11, 9,  //
    11, 8,  10, //

    13, 12, 14, //
    13, 14, 15  //
};

MK_SHADER( ui_overlay_vertex );
MK_SHADER( ui_overlay_fragment );

void UIOverlay::init( const VertexBufferLayout &ui_overlay_vbl_vertex, const VertexBufferLayout &ui_overlay_vbl_instance ) {
    // Calc The Vertices
    showErrors( );

    this->render_chain_crosshair.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance, vb_data_crosshair_element, UI_OVERLAY_VERTEX_COUNT_CROSSHAIR, ib_data_crosshair, UI_OVERLAY_INDEX_COUNT_CROSSHAIR );
    auto pair_crosshair = this->render_chain_crosshair.create_instance( );
    pair_crosshair.second = vb_data_crosshair_instance;

    this->render_chain_held_block.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance, vb_isometric_quad, VB_ISOMETRIC_QUAD_SIZE, ib_isometric_quad, IB_ISOMETRIC_QUAD_SIZE );

    showErrors( );

    this->shader.init( &ui_overlay_vertex, &ui_overlay_fragment );
}

void UIOverlay::on_screen_size_change( int width, int height ) {
    this->screen_width = width;
    this->screen_height = height;
    // When the screen changes, we need to reprocess the held block graphics
    set_holding_block( this->heldBlockID );
}

void UIOverlay::set_holding_block( BlockID holding_block ) {
    this->heldBlockID = holding_block;
    if ( this->heldBlockID == LAST_BLOCK_ID ) {
        this->render_chain_held_block.clear( );
        return;
    }
    Block *holdingBlock = block_definition_get_definition( this->heldBlockID );

    vb_data_holding_block_instance.is_isometric = holdingBlock->icon_is_isometric;
    vb_data_holding_block_instance.width = this->screen_width / 4;
    vb_data_holding_block_instance.height = this->screen_width / 4;

    vb_data_holding_block_instance.screen_x = -1.2 * this->screen_width / 2;
    vb_data_holding_block_instance.screen_y = -1.3 * this->screen_height / 2;
    vb_data_holding_block_instance.screen_z = 0;

    for ( int face = 0; face < ISOMETRIC_FACES; ++face ) {
        if ( !holdingBlock->icon_is_isometric ) {
            // Like Reeds
            vb_data_holding_block_instance.id_isos[ face ] = holdingBlock->inventory_non_isometric_id - 1;
        } else {
            // Like grass
            vb_data_holding_block_instance.id_isos[ face ] = ( holdingBlock->textures[ inventory_isometric_face[ face ] ] - 1 );
        }
    }

    this->render_chain_held_block.clear( );
    auto pair = this->render_chain_held_block.create_instance( );
    pair.second = vb_data_holding_block_instance;
}

void UIOverlay::draw( Inventory &inventory, Inventory &hotbar, const Renderer &renderer, const Texture &blocksTexture, Input &input, const glm::mat4 &mvp_ui ) {

    // if ( true ) {
    //     const ImGuiDebugVars &debug_vars = imgUIOverlay::get_imgui_debug_vars( );
    //     int which_vb;

    //     which_vb = 1;
    //     vb_isometric_quad[ which_vb ].screen_x = X_SCALE * debug_vars.corner1.x;
    //     vb_isometric_quad[ which_vb ].screen_y = Y_SCALE * debug_vars.corner1.y;

    //     which_vb = 3;
    //     vb_isometric_quad[ which_vb ].screen_x = X_SCALE * debug_vars.corner3.x;
    //     vb_isometric_quad[ which_vb ].screen_y = Y_SCALE * debug_vars.corner3.y;
    //     this->render_chain_held_block.update_element( vb_isometric_quad, IB_ISOMETRIC_QUAD_SIZE );
    // }

    this->shader.set_uniform_mat4f( "u_MVP", mvp_ui );
    this->shader.set_uniform1i_texture( "u_Texture", blocksTexture );

    hotbar.draw( renderer, blocksTexture, this->shader );
    render_chain_held_block.draw( renderer, this->shader );

    if ( input.inventory_open ) {
        inventory.draw( renderer, blocksTexture, this->shader );
    } else {
        glBlendFunc( GL_ONE_MINUS_DST_COLOR, GL_ZERO );
        this->render_chain_crosshair.draw( renderer, this->shader );
        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    }
    showErrors( );
}

void UIOverlay::cleanup( ) {
}
