#include <math.h>

#include "common/RepGame.hpp"
#include "common/ui_overlay.hpp"
#include "common/ui_overlay_buffers.hpp"
#include "common/renderer/shader.hpp"

#define WIDTH ( 1.5f )
#define SCALE ( 10.0f )

#define UI_OVERLAY_VERTEX_COUNT_CROSSHAIR 8

#define CROSSHAIR_COLOR { 0, 0 }

constexpr UIOverlayVertex vb_data_crosshair_element[ UI_OVERLAY_VERTEX_COUNT_CROSSHAIR ] = {
    { -SCALE * WIDTH, -WIDTH, { 0, 0 }, 0, ISO_FACE_TOP }, // 0
    { -SCALE * WIDTH, WIDTH, { 0, 0 }, 0, ISO_FACE_TOP },  // 1
    { SCALE * WIDTH, -WIDTH, { 0, 0 }, 0, ISO_FACE_TOP },  // 2
    { SCALE * WIDTH, WIDTH, { 0, 0 }, 0, ISO_FACE_TOP },   // 3

    { -WIDTH, -SCALE *WIDTH, { 0, 0 }, 0, ISO_FACE_TOP }, // 4
    { -WIDTH, SCALE *WIDTH, { 0, 0 }, 0, ISO_FACE_TOP },  // 5
    { WIDTH, -SCALE *WIDTH, { 0, 0 }, 0, ISO_FACE_TOP },  // 6
    { WIDTH, SCALE *WIDTH, { 0, 0 }, 0, ISO_FACE_TOP },   // 7
};
constexpr UIOverlayInstance vb_data_crosshair_instance = { 0, 0, 0, 1, 1, 0, 0, { 0, 0, 0 }, { 1, 1, 1, 0.5f } };

float holding_alpha = 1.0f;

UIOverlayInstance vb_data_holding_block_instance = { 0, 0, 0, 0, 0, 1, 0, { AIR, AIR, AIR }, { 1, 1, 1, holding_alpha } };

int holding_block_vertex_face_map[] = {
    FACE_TOP,   FACE_TOP,   FACE_TOP,   FACE_TOP,

    FACE_FRONT, FACE_FRONT, FACE_FRONT, FACE_FRONT,

    FACE_RIGHT, FACE_RIGHT, FACE_RIGHT, FACE_RIGHT,
};

#define UI_OVERLAY_INDEX_COUNT_CROSSHAIR ( 3 * 2 * 2 )
constexpr unsigned int ib_data_crosshair[] = {
    0, 3, 1, //
    3, 0, 2, //

    4, 7, 5, //
    7, 4, 6, //
};

MK_SHADER( ui_overlay_vertex );
MK_SHADER( ui_overlay_fragment );

void UIOverlay::init( const VertexBufferLayout &ui_overlay_vbl_vertex, const VertexBufferLayout &ui_overlay_vbl_instance ) {
    // Calc The Vertices
    showErrors( );

    this->render_chain_crosshair.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance, vb_data_crosshair_element, UI_OVERLAY_VERTEX_COUNT_CROSSHAIR, ib_data_crosshair, UI_OVERLAY_INDEX_COUNT_CROSSHAIR );
    auto pair_crosshair = this->render_chain_crosshair.create_instance( );
    pair_crosshair.second = vb_data_crosshair_instance;

    this->render_chain_held_block.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance, vb_isometric_quad_held, VB_ISOMETRIC_QUAD_SIZE, ib_isometric_quad, IB_ISOMETRIC_QUAD_SIZE );
    this->render_chain_dragged_item.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance, vb_isometric_quad_inventory, VB_ISOMETRIC_QUAD_SIZE, ib_isometric_quad, IB_ISOMETRIC_QUAD_SIZE );

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

    vb_data_holding_block_instance.width = this->screen_width / 4;
    vb_data_holding_block_instance.height = this->screen_width / 4;

    vb_data_holding_block_instance.screen_x = -1.2 * this->screen_width / 2;
    vb_data_holding_block_instance.screen_y = -1.3 * this->screen_height / 2;
    vb_data_holding_block_instance.screen_z = 0;

    set_block_icon( vb_data_holding_block_instance, holdingBlock );

    this->render_chain_held_block.clear( );
    auto pair = this->render_chain_held_block.create_instance( );
    pair.second = vb_data_holding_block_instance;
}

void UIOverlay::draw( CreativeInventory &creative_inventory, SurvivalInventory &survival_inventory, Hotbar &hotbar, GameMode game_mode, const Renderer &renderer, const Texture &blocksTexture, Input &input, FontRenderer &font, const glm::mat4 &mvp_ui ) {
    this->shader.set_uniform_mat4f( "u_MVP", mvp_ui );
    this->shader.set_uniform1i_texture( "u_Texture", blocksTexture );

    hotbar.draw( renderer, blocksTexture, this->shader );
    render_chain_held_block.draw( renderer, this->shader );

    // Render quantity text on the hotbar (survival mode only).
    if ( game_mode == GameMode_Survival ) {
        hotbar.drawQuantities( renderer, font, mvp_ui );
    }

    if ( input.inventory_open ) {
        if ( game_mode == GameMode_Creative ) {
            creative_inventory.draw( renderer, blocksTexture, this->shader );
        } else {
            survival_inventory.draw( renderer, blocksTexture, this->shader );
            survival_inventory.drawQuantities( renderer, font, mvp_ui );
        }
    } else {
        glBlendFunc( GL_ONE_MINUS_DST_COLOR, GL_ZERO );
        this->render_chain_crosshair.draw( renderer, this->shader );
        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    }
    showErrors( );
}

void UIOverlay::draw_held_inventory_item( const InventorySlot &held, bool is_holding, int mouse_x, int mouse_y, int block_size, int block_offset, int cell_size, int cell_offset, const Renderer &renderer, const Texture &blocksTexture, FontRenderer &font, const glm::mat4 &mvp_ui ) {
    if ( !is_holding || held.block_id == LAST_BLOCK_ID || held.quantity <= 0 ) {
        return;
    }
    // The font renderer (called from drawQuantities) binds its own texture on
    // GL_TEXTURE0 and changes GL state. Re-bind the blocks texture and restore
    // GL state before drawing the dragged item.
    blocksTexture.bind( );
    glEnable( GL_DEPTH_TEST );
    glClear( GL_DEPTH_BUFFER_BIT );

    this->shader.set_uniform_mat4f( "u_MVP", mvp_ui );
    this->shader.set_uniform1i_texture( "u_Texture", blocksTexture );

    Block *holdingBlock = block_definition_get_definition( held.block_id );

    // Convert mouse screen coords (top-left origin, Y-down) to centered
    // UI coords (center origin, Y-up).
    float centered_x = static_cast<float>( mouse_x ) - this->screen_width / 2.0f;
    float centered_y = this->screen_height / 2.0f - static_cast<float>( mouse_y );

    // Treat the mouse as the center of a virtual cell, matching how
    // inventory icons and quantity text are laid out within a cell.
    float cell_start_x = centered_x - cell_size / 2.0f;
    float cell_start_y = centered_y - cell_size / 2.0f;

    this->render_chain_dragged_item.clear( );
    auto [ entity, ui_vertex ] = this->render_chain_dragged_item.create_instance( );

    // Set fields individually, matching InventoryRenderer::singleItemRender.
    ui_vertex.screen_x = cell_start_x + block_offset;
    ui_vertex.screen_y = cell_start_y + block_offset;
    ui_vertex.screen_z = ORDER_Z_INV_BLOCKS + 0.1f;
    ui_vertex.width = block_size;
    ui_vertex.height = block_size;
    ui_vertex.is_block = 1;

    set_block_icon( ui_vertex, holdingBlock );

    this->render_chain_dragged_item.draw( renderer, this->shader );

    // Render the quantity text at the bottom-right of the cell, matching
    // the in-inventory text placement: slot_x - text_width - 2, slot_y + 2.
    float slot_x = cell_start_x + cell_offset + cell_size;
    float slot_y = cell_start_y + cell_offset;
    char buf[ 16 ];
    snprintf( buf, sizeof( buf ), "%d", held.quantity );
    float text_size = 24.0f;
    float text_width = font.getTextWidth( buf, text_size );
    float tx = slot_x - text_width - 2.0f;
    float ty = slot_y + 2.0f;
    float tint[ 4 ] = { 1.0f, 1.0f, 1.0f, 1.0f };
    font.drawString( renderer, buf, tx, ty, text_size, tint, mvp_ui );

    showErrors( );
}

void UIOverlay::cleanup( ) {
}
