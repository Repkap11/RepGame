#include <math.h>

#include "common/RepGame.hpp"
#include "common/ui_overlay.hpp"
#include "common/ui_layout.hpp"
#include "common/ui_overlay_buffers.hpp"
#include "common/inventory_renderer.hpp"
#include "common/renderer/shader.hpp"
#include "common/texture_ids_gen.hpp"

#define WIDTH ( 1.5f )
#define SCALE ( 10.0f )

#define UI_OVERLAY_VERTEX_COUNT_CROSSHAIR 8

#define CROSSHAIR_COLOR { 0, 0 }

constexpr UIOverlayVertex vb_data_crosshair_element[ UI_OVERLAY_VERTEX_COUNT_CROSSHAIR ] = {
    { -SCALE * WIDTH, -WIDTH, { 0, 0 }, 0, ISO_FACE_TOP, { 0, 0, 0 } }, // 0
    { -SCALE * WIDTH, WIDTH, { 0, 0 }, 0, ISO_FACE_TOP, { 0, 0, 0 } },  // 1
    { SCALE * WIDTH, -WIDTH, { 0, 0 }, 0, ISO_FACE_TOP, { 0, 0, 0 } },  // 2
    { SCALE * WIDTH, WIDTH, { 0, 0 }, 0, ISO_FACE_TOP, { 0, 0, 0 } },   // 3

    { -WIDTH, -SCALE *WIDTH, { 0, 0 }, 0, ISO_FACE_TOP, { 0, 0, 0 } }, // 4
    { -WIDTH, SCALE *WIDTH, { 0, 0 }, 0, ISO_FACE_TOP, { 0, 0, 0 } },  // 5
    { WIDTH, -SCALE *WIDTH, { 0, 0 }, 0, ISO_FACE_TOP, { 0, 0, 0 } },  // 6
    { WIDTH, SCALE *WIDTH, { 0, 0 }, 0, ISO_FACE_TOP, { 0, 0, 0 } },   // 7
};
constexpr UIOverlayInstance vb_data_crosshair_instance = { 0, 0, 0, 1, 1, 0, 0, { 0, 0, 0 }, { 1, 1, 1, 0.5f }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };

float holding_alpha = 1.0f;

UIOverlayInstance vb_data_holding_block_instance = { 0, 0, 0, 0, 0, 1, 0, { AIR, AIR, AIR }, { 1, 1, 1, holding_alpha }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };

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
    this->render_chain_panel_backdrop.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance, vb_isometric_quad_inventory, VB_ISOMETRIC_QUAD_SIZE, ib_isometric_quad, IB_ISOMETRIC_QUAD_SIZE );
    this->render_chain_vitals.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance, vb_isometric_quad_inventory, VB_ISOMETRIC_QUAD_SIZE, ib_isometric_quad, IB_ISOMETRIC_QUAD_SIZE );

    showErrors( );

    this->shader.init( &ui_overlay_vertex, &ui_overlay_fragment );
}

void UIOverlay::on_screen_size_change( int width, int height ) {
    this->screen_width = width;
    this->screen_height = height;
    // When the screen changes, we need to reprocess the held block graphics
    set_holding_item( this->heldItemID );
}

void UIOverlay::set_holding_item( ItemID holding_item ) {
    this->heldItemID = holding_item;
    if ( this->heldItemID == LAST_BLOCK_ID || !item_id_valid( this->heldItemID ) ) {
        this->render_chain_held_block.clear( );
        return;
    }

    // The held item draws through ItemEntities::draw_held from
    // draw_ui_pass (extruded sprite for flat items, perspective cube for
    // isometric block icons); the overlay chain only tracks the id.
    this->render_chain_held_block.clear( );
}

void UIOverlay::draw( CreativeInventory &creative_inventory, SurvivalInventory &survival_inventory, Hotbar &hotbar, GameMode game_mode, const Renderer &renderer, const Texture &blocksTexture, Input &input, FontRenderer &font, const glm::mat4 &mvp_ui, InventoryRenderer *chest_renderer, const ChestContents *chest_contents, bool crafting_table_open, bool furnace_open, float held_block_light ) {
    this->shader.set_uniform_mat4f( "u_MVP", mvp_ui );
    this->shader.set_uniform1i_texture( "u_Texture", blocksTexture );

    // The world light only applies to the in-hand block's own draw call —
    // hotbar/inventory icons stay at full brightness.
    this->shader.set_uniform1f( "u_HeldBlockLight", 1.0f );
    hotbar.draw( renderer, blocksTexture, this->shader );
    this->shader.set_uniform1f( "u_HeldBlockLight", held_block_light );
    if ( !this->vr_mode ) {
        render_chain_held_block.draw( renderer, this->shader );
    }
    this->shader.set_uniform1f( "u_HeldBlockLight", 1.0f );

    // Render quantity text on the hotbar (survival mode only).
    if ( game_mode == GameMode_Survival ) {
        hotbar.drawQuantities( renderer, font, mvp_ui );
    }

    const bool chest_open = chest_renderer != nullptr && chest_contents != nullptr;
    if ( input.inventory_open ) {
        if ( game_mode == GameMode_Creative && !chest_open && !crafting_table_open && !furnace_open ) {
            creative_inventory.draw( renderer, blocksTexture, this->shader, input.search_focused, font, mvp_ui );
        } else {
            survival_inventory.draw( renderer, blocksTexture, this->shader );
            survival_inventory.drawQuantities( renderer, font, mvp_ui );
        }
        if ( chest_open ) {
            // Chest panel on top, then its stack-size labels.
            this->draw_slot_panel( *chest_renderer, chest_contents->slots, CHEST_SLOTS, renderer, blocksTexture, font, mvp_ui );
        }
    } else if ( !this->vr_mode && !input.menu_open ) {
        glBlendFunc( GL_ONE_MINUS_DST_COLOR, GL_ZERO );
        this->render_chain_crosshair.draw( renderer, this->shader );
        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    }
    showErrors( );
}

void UIOverlay::draw_panel_backdrop( int left, int bottom, int width, int height, const Renderer &renderer, const glm::mat4 &mvp_ui ) {
    this->shader.set_uniform_mat4f( "u_MVP", mvp_ui );
    this->render_chain_panel_backdrop.clear( );
    auto pair = this->render_chain_panel_backdrop.create_instance( );
    UIOverlayInstance &ui_vertex = pair.second;
    ui_vertex.screen_x = static_cast<float>( left );
    ui_vertex.screen_y = static_cast<float>( bottom );
    ui_vertex.screen_z = ORDER_Z_PANEL_BACKDROP;
    ui_vertex.width = static_cast<float>( width );
    ui_vertex.height = static_cast<float>( height );
    ui_vertex.is_block = 0;
    ui_vertex.is_isometric = 0;
    // Same gray as the per-cell squares inside the panels (0.5) so the
    // group reads as one continuous window behind the slots.
    ui_vertex.tint[ 0 ] = 0.5f;
    ui_vertex.tint[ 1 ] = 0.5f;
    ui_vertex.tint[ 2 ] = 0.5f;
    ui_vertex.tint[ 3 ] = 1.0f;
    this->render_chain_panel_backdrop.draw( renderer, this->shader );
}

void UIOverlay::draw_slot_panel( InventoryRenderer &panel, const InventorySlot *slots, int n, const Renderer &renderer, const Texture &blocksTexture, FontRenderer &font, const glm::mat4 &mvp_ui ) {
    panel.draw( renderer, blocksTexture, this->shader );
    for ( int i = 0; i < n; i++ ) {
        const InventorySlot &slot = slots[ i ];
        if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
            continue;
        }
        float slot_x, slot_y;
        if ( !panel.getSlotPosition( i, slot_x, slot_y ) ) {
            continue;
        }
        char buf[ 16 ];
        snprintf( buf, sizeof( buf ), "%d", slot.quantity );
        const float text_size = 24.0f;
        const float text_width = font.getTextWidth( buf, text_size );
        const float tx = slot_x - text_width - 2.0f;
        const float ty = slot_y + 2.0f;
        float tint[ 4 ] = { 1.0f, 1.0f, 1.0f, 1.0f };
        font.text_append( buf, tx, ty, text_size, tint );
    }
}

void UIOverlay::draw_held_inventory_item( const InventorySlot &held, bool is_holding, int mouse_x, int mouse_y, int block_size, int cell_size, const Renderer &renderer, const Texture &blocksTexture, FontRenderer &font, const glm::mat4 &mvp_ui ) {
    if ( !is_holding || held.item_id == LAST_BLOCK_ID || held.quantity <= 0 ) {
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

    // Convert mouse screen coords (top-left origin, Y-down) to centered
    // UI coords (center origin, Y-up).
    float centered_x = static_cast<float>( mouse_x ) - this->screen_width / 2.0f;
    float centered_y = this->screen_height / 2.0f - static_cast<float>( mouse_y );

    // Treat the mouse as the center of a virtual cell, matching how
    // inventory icons and quantity text are laid out within a cell.
    float cell_start_x = centered_x - cell_size / 2.0f;
    float cell_start_y = centered_y - cell_size / 2.0f;

    this->render_chain_dragged_item.clear( );
    const int icon_boxes = item_icon_box_count( held.item_id );
    for ( int box = 0; box < icon_boxes; box++ ) {
        auto [ entity, ui_vertex ] = this->render_chain_dragged_item.create_instance( );

        // Set fields individually, matching InventoryRenderer::singleItemRender.
        // The block icon is centered within the cell, so it is centered on the
        // cursor. block_offset isn't used here since it is relative to the items
        // background edge, not the cell corner.
        ui_vertex.screen_x = cell_start_x + ( cell_size - block_size ) / 2.0f;
        ui_vertex.screen_y = cell_start_y + ( cell_size - block_size ) / 2.0f;
        // Later boxes draw nearer — the stair's step must beat the slab's
        // top-face quad where they overlap (see singleItemRender).
        ui_vertex.screen_z = ORDER_Z_INV_BLOCKS + 0.1f + box * 0.001f;
        ui_vertex.width = block_size;
        ui_vertex.height = block_size;
        ui_vertex.is_block = 1;

        set_item_icon( ui_vertex, held.item_id, box );
    }

    this->render_chain_dragged_item.draw( renderer, this->shader );

    // Render the quantity text at the bottom-right of the cell, matching
    // the in-inventory text placement: slot_x - text_width - 2, slot_y + 2.
    float slot_x = cell_start_x + cell_size;
    float slot_y = cell_start_y;
    char buf[ 16 ];
    snprintf( buf, sizeof( buf ), "%d", held.quantity );
    float text_size = 24.0f;
    float text_width = font.getTextWidth( buf, text_size );
    float tx = slot_x - text_width - 2.0f;
    float ty = slot_y + 2.0f;
    float tint[ 4 ] = { 1.0f, 1.0f, 1.0f, 1.0f };
    font.text_append( buf, tx, ty, text_size, tint );

    showErrors( );
}

// One flat atlas icon for the vitals row. is_block makes the shader sample
// the texture layer in id_isos; the 1.25 tint cancels the 0.8 front-face
// darkening so HUD icons draw at full texture brightness.
static void vitals_icon( UIOverlayInstance &v, float x, float y, float z, float size, float texture_id ) {
    v.screen_x = x;
    v.screen_y = y;
    v.screen_z = z;
    v.width = size;
    v.height = size;
    v.is_block = 1;
    v.is_isometric = 0;
    for ( int f = 0; f < ISOMETRIC_FACES; f++ ) {
        v.id_isos[ f ] = texture_id;
    }
    for ( int i = 0; i < 3; i++ ) {
        v.tint[ i ] = 1.25f;
        v.block_scale[ i ] = 16.0f;
        v.block_offset[ i ] = 0.0f;
        v.block_tex_offset[ i ] = 0.0f;
    }
    v.tint[ 3 ] = 1.0f;
}

void UIOverlay::draw_vitals( float health, float max_health, int air_ticks, int max_air_ticks, const UiRect &row, const Renderer &renderer, const Texture &blocksTexture, const glm::mat4 &mvp_ui ) {
    this->shader.set_uniform_mat4f( "u_MVP", mvp_ui );
    this->shader.set_uniform1i_texture( "u_Texture", blocksTexture );
    this->shader.set_uniform1f( "u_HeldBlockLight", 1.0f );
    this->render_chain_vitals.clear( );

    const float icon = row.height;
    const float dx = icon * 1.1f;
    const float base_y = row.bottom;
    const float left = row.left;
    const float right = row.right( );

    const int n_hearts = static_cast<int>( ceilf( max_health / 2.0f ) );
    for ( int i = 0; i < n_hearts; i++ ) {
        auto pair = this->render_chain_vitals.create_instance( );
        vitals_icon( pair.second, left + i * dx, base_y, ORDER_Z_VITALS_BG, icon, TEX_HEART_CONTAINER );
        const float hp = health - i * 2.0f;
        if ( hp > 0.0f ) {
            auto fill = this->render_chain_vitals.create_instance( );
            vitals_icon( fill.second, left + i * dx, base_y, ORDER_Z_VITALS, icon, hp > 1.0f ? TEX_HEART : TEX_HEART_HALF );
        }
    }
    // Bubbles pop off from the right as air runs out — only drawn while the
    // meter is below max, matching the underwater-only display.
    if ( air_ticks < max_air_ticks ) {
        const int n_bubbles = ( air_ticks * n_hearts + max_air_ticks - 1 ) / max_air_ticks;
        for ( int i = 0; i < n_bubbles; i++ ) {
            auto pair = this->render_chain_vitals.create_instance( );
            vitals_icon( pair.second, right - ( i + 1 ) * dx, base_y, ORDER_Z_VITALS, icon, TEX_BUBBLE );
        }
    }

    this->render_chain_vitals.draw( renderer, this->shader );
    showErrors( );
}

void UIOverlay::set_vr_mode( bool vr_mode ) {
    this->vr_mode = vr_mode;
}

void UIOverlay::cleanup( ) {
}
