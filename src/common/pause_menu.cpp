#include <math.h>
#include <stdio.h>

#include "common/RepGame.hpp"
#include "common/pause_menu.hpp"
#include "common/ui_overlay_buffers.hpp"

// Z order within the UI depth pass (larger screen_z wins; draw_ui_pass
// clears depth before the menu so these only order against each other).
#define ORDER_Z_MENU_BACKDROP 0.5f
#define ORDER_Z_MENU_PANEL 0.6f
#define ORDER_Z_MENU_BUTTON 0.7f
#define ORDER_Z_MENU_HOVER 0.8f

static const char *const quality_names[] = { "LOW", "MEDIUM", "HIGH", "X_HIGH" };

void PauseMenu::init( const VertexBufferLayout &vbl_vertex, const VertexBufferLayout &vbl_instance ) {
    this->render_chain_quads.init( vbl_vertex, vbl_instance, vb_isometric_quad_inventory, VB_ISOMETRIC_QUAD_SIZE, ib_isometric_quad, IB_ISOMETRIC_QUAD_SIZE );
    this->render_chain_hover.init( vbl_vertex, vbl_instance, vb_isometric_quad_inventory, VB_ISOMETRIC_QUAD_SIZE, ib_isometric_quad, IB_ISOMETRIC_QUAD_SIZE );
}

static UIOverlayInstance &add_quad( RenderChain<UIOverlayVertex, UIOverlayInstance> &chain, //
                                    float x, float y, float w, float h, float z,          //
                                    float r, float g, float b, float a ) {
    auto pair = chain.create_instance( );
    UIOverlayInstance &quad = pair.second;
    quad.screen_x = x;
    quad.screen_y = y;
    quad.screen_z = z;
    quad.width = w;
    quad.height = h;
    quad.is_block = 0;
    quad.is_isometric = 0;
    quad.tint[ 0 ] = r;
    quad.tint[ 1 ] = g;
    quad.tint[ 2 ] = b;
    quad.tint[ 3 ] = a;
    return quad;
}

void PauseMenu::onScreenSizeChange( const int width, const int height, const FontRenderer &font ) {
    this->screen_width = width;
    this->screen_height = height;
    this->num_regions = 0;
    this->num_rows = 0;

    int row_kinds[ MAX_ROWS ];
    int n = 0;
    row_kinds[ n++ ] = BUTTON_RESUME;
#if SUPPORTS_FRAME_BUFFER
    row_kinds[ n++ ] = BUTTON_GRAPHICS;
#endif
    row_kinds[ n++ ] = BUTTON_RENDER_ROW;
    row_kinds[ n++ ] = BUTTON_DEBUG_VIEW;
    row_kinds[ n++ ] = BUTTON_GAME_MODE;
    row_kinds[ n++ ] = BUTTON_FLYING;
#if SUPPORTS_IMGUI_OVERLAY
    row_kinds[ n++ ] = BUTTON_IMGUI;
#endif
    row_kinds[ n++ ] = BUTTON_EXIT;

    // Like InventoryRenderer::onSizeChange, the UI is sized as a fraction of
    // a 16:9-normalized height so buttons stay finger-sized on wide phones.
    const float ui_height_basis = fmaxf( ( float )height, width * ( 9.0f / 16.0f ) );

    // Worst-case label per row kind; the panel width is derived from these,
    // so labels can't overflow their buttons.
    auto worst_label = []( int button, char *buf, size_t buf_size ) -> const char * {
        switch ( button ) {
            case BUTTON_RESUME:
                return "Resume";
            case BUTTON_GRAPHICS:
                return "Graphics: X_HIGH";
            case BUTTON_RENDER_ROW:
                snprintf( buf, buf_size, "Render Distance: %d", RENDER_RADIUS_MAX );
                return buf;
            case BUTTON_DEBUG_VIEW:
                return "Block Debug View: Off";
            case BUTTON_GAME_MODE:
                return "Game Mode: Survival";
            case BUTTON_FLYING:
                return "Flying: Off";
            case BUTTON_IMGUI:
                return "Debug Overlay: Off";
            case BUTTON_EXIT:
                return "Exit Game";
            default:
                return "";
        }
    };

    // Content-driven layout: pick the integer font scale k, then derive
    // every dimension from the text height t = 13*k so the whole menu lands
    // on the pixel grid.
    int k = 0;
    float t = 13.0f, button_height = 0, gap = 0, pad = 0, title_height = 0, button_width = 0, panel_width = 0, panel_height = 0;
    char worst_buf[ 64 ];
    for ( k = ( int )lroundf( ui_height_basis / 400.0f ); k >= 1; k-- ) {
        t = 13.0f * k;
        button_height = t + 2.0f * lroundf( t * 0.35f );
        gap = lroundf( t * 0.4f );
        pad = t;                            // outer padding on all four sides
        // Title text is one step bigger; the band is its cap height so the
        // visible padding above and below the caps equals the other sides.
        this->title_size = 13.0f * ( k + 1 );
        title_height = font.getCapHeight( this->title_size );
        const float hpad = t;               // text padding inside a button
        button_width = 0.0f;
        for ( int i = 0; i < n; i++ ) {
            const char *label = worst_label( row_kinds[ i ], worst_buf, sizeof( worst_buf ) );
            float row_w;
            if ( row_kinds[ i ] == BUTTON_RENDER_ROW ) {
                // - and + squares at the ends, reset button between them.
                row_w = 2.0f * button_height + 2.0f * gap + 2.0f * hpad + font.getTextWidth( label, t );
            } else {
                row_w = font.getTextWidth( label, t ) + 2.0f * hpad;
            }
            if ( row_w > button_width ) {
                button_width = row_w;
            }
        }
        panel_width = button_width + 2.0f * pad;
        panel_height = pad + title_height + pad + n * button_height + ( n - 1 ) * gap + pad;
        if ( panel_height <= height * 0.9f && panel_width <= width * 0.95f ) {
            break;
        }
    }
    this->text_size = t;

    // Snap the panel edges to whole screen pixels (ortho_center coords) —
    // the same trick FontRenderer::drawString uses — so the padding comes
    // out exactly even and every derived edge stays on the pixel grid.
    const float panel_left = floorf( -panel_width / 2.0f + width * 0.5f ) - width * 0.5f;
    const float panel_bottom = floorf( -panel_height / 2.0f + height * 0.5f ) - height * 0.5f;
    const float panel_top = panel_bottom + panel_height;
    const float panel_right = panel_left + panel_width;
    // Baseline on the band's bottom edge: 'd' tops touch the band top and
    // cap bottoms touch the band bottom, so both sides read as pad.
    this->title_center_y = panel_top - pad - title_height + font.getCapHeight( this->title_size ) / 2.0f;

    this->render_chain_quads.clear( );
    // Dim the whole screen, then the panel.
    add_quad( this->render_chain_quads, -width / 2.0f, -height / 2.0f, ( float )width, ( float )height, ORDER_Z_MENU_BACKDROP, 0.0f, 0.0f, 0.0f, 0.6f );
    add_quad( this->render_chain_quads, panel_left, panel_bottom, panel_width, panel_height, ORDER_Z_MENU_PANEL, 0.1f, 0.1f, 0.1f, 0.85f );

    const float buttons_left = panel_left + pad;
    const float buttons_right = panel_right - pad;
    // Same padding between the title band and the first button.
    const float buttons_top = panel_top - pad - title_height - pad;
    for ( int i = 0; i < n; i++ ) {
        const float btn_top = buttons_top - i * ( button_height + gap );
        const float btn_y = btn_top - button_height;
        const float btn_cy = btn_y + button_height / 2.0f;
        Row &row = this->rows[ this->num_rows++ ];
        row.button = row_kinds[ i ];
        row.center_y = btn_cy;
        row.left_cx = 0.0f;
        row.right_cx = 0.0f;
        if ( row_kinds[ i ] == BUTTON_RENDER_ROW ) {
            // [-] [Render Distance: N] [+]: the middle button resets the
            // distance to the default, like the '0' key.
            const float minus_x = buttons_left;
            const float plus_x = buttons_right - button_height;
            const float mid_x = minus_x + button_height + gap;
            const float mid_w = plus_x - gap - mid_x;
            add_quad( this->render_chain_quads, minus_x, btn_y, button_height, button_height, ORDER_Z_MENU_BUTTON, 0.35f, 0.35f, 0.35f, 1.0f );
            add_quad( this->render_chain_quads, mid_x, btn_y, mid_w, button_height, ORDER_Z_MENU_BUTTON, 0.35f, 0.35f, 0.35f, 1.0f );
            add_quad( this->render_chain_quads, plus_x, btn_y, button_height, button_height, ORDER_Z_MENU_BUTTON, 0.35f, 0.35f, 0.35f, 1.0f );
            row.left_cx = minus_x + button_height / 2.0f;
            row.right_cx = plus_x + button_height / 2.0f;
            this->regions[ this->num_regions++ ] = Region{ BUTTON_RENDER_MINUS, minus_x, btn_y, button_height, button_height };
            this->regions[ this->num_regions++ ] = Region{ BUTTON_RENDER_RESET, mid_x, btn_y, mid_w, button_height };
            this->regions[ this->num_regions++ ] = Region{ BUTTON_RENDER_PLUS, plus_x, btn_y, button_height, button_height };
        } else {
            add_quad( this->render_chain_quads, buttons_left, btn_y, button_width, button_height, ORDER_Z_MENU_BUTTON, 0.35f, 0.35f, 0.35f, 1.0f );
            this->regions[ this->num_regions++ ] = Region{ row_kinds[ i ], buttons_left, btn_y, button_width, button_height };
        }
    }
}

int PauseMenu::hitTest( const int abs_x, const int abs_y ) const {
    // Mouse/tap coords are top-left origin, y down; the UI is center origin,
    // y up (see UIOverlay::draw_held_inventory_item).
    const float x = ( float )abs_x - this->screen_width / 2.0f;
    const float y = this->screen_height / 2.0f - ( float )abs_y;
    for ( int i = 0; i < this->num_regions; i++ ) {
        const Region &r = this->regions[ i ];
        if ( x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h ) {
            return r.button;
        }
    }
    return BUTTON_NONE;
}

void PauseMenu::draw( const Renderer &renderer, Shader &shader, const Texture &blocksTexture, FontRenderer &font, const glm::mat4 &mvp, const RepGameState &state ) {
    shader.set_uniform_mat4f( "u_MVP", mvp );
    shader.set_uniform1i_texture( "u_Texture", blocksTexture );
    shader.set_uniform1f( "u_HeldBlockLight", 1.0f );
    // The menu must cover everything else in the UI pass (hotbar, held item).
    glEnable( GL_DEPTH_TEST );
    glClear( GL_DEPTH_BUFFER_BIT );
    this->render_chain_quads.draw( renderer, shader );

    // Hover highlight over whichever button the (unlocked) pointer is on.
    this->render_chain_hover.clear( );
    const int hovered = this->hitTest( state.input.mouse.absPosition.x, state.input.mouse.absPosition.y );
    if ( hovered != BUTTON_NONE ) {
        for ( int i = 0; i < this->num_regions; i++ ) {
            const Region &r = this->regions[ i ];
            if ( r.button == hovered ) {
                add_quad( this->render_chain_hover, r.x, r.y, r.w, r.h, ORDER_Z_MENU_HOVER, 1.0f, 1.0f, 1.0f, 0.15f );
                break;
            }
        }
    }
    this->render_chain_hover.draw( renderer, shader );

    const float white[ 4 ] = { 1.0f, 1.0f, 1.0f, 1.0f };
    char buf[ 64 ];

    // Center by cap height so the capital letters sit symmetric in the button
    // (FontRenderer snaps the baseline to the pixel grid itself).
    font.drawString( renderer, "Paused", -font.getTextWidth( "Paused", this->title_size ) / 2.0f,
                     this->title_center_y - font.getCapHeight( this->title_size ) / 2.0f, this->title_size, white, mvp );

    for ( int i = 0; i < this->num_rows; i++ ) {
        const Row &row = this->rows[ i ];
        const float baseline = row.center_y - font.getCapHeight( this->text_size ) / 2.0f;
        switch ( row.button ) {
            case BUTTON_RESUME:
                snprintf( buf, sizeof( buf ), "Resume" );
                break;
            case BUTTON_GRAPHICS:
                snprintf( buf, sizeof( buf ), "Graphics: %s", quality_names[ state.input.worldDrawQuality ] );
                break;
            case BUTTON_RENDER_ROW: {
                snprintf( buf, sizeof( buf ), "Render Distance: %d", state.input.render_distance );
                font.drawString( renderer, "-", row.left_cx - font.getTextWidth( "-", this->text_size ) / 2.0f, baseline, this->text_size, white, mvp );
                font.drawString( renderer, "+", row.right_cx - font.getTextWidth( "+", this->text_size ) / 2.0f, baseline, this->text_size, white, mvp );
                break;
            }
            case BUTTON_DEBUG_VIEW:
                snprintf( buf, sizeof( buf ), "Block Debug View: %s", state.input.debug_mode ? "On" : "Off" );
                break;
            case BUTTON_GAME_MODE:
                snprintf( buf, sizeof( buf ), "Game Mode: %s", state.game_mode == GameMode_Creative ? "Creative" : "Survival" );
                break;
            case BUTTON_FLYING:
                snprintf( buf, sizeof( buf ), "Flying: %s", state.input.player_flying ? "On" : "Off" );
                break;
            case BUTTON_IMGUI:
                snprintf( buf, sizeof( buf ), "Debug Overlay: %s", state.imgui_overlay.shown ? "On" : "Off" );
                break;
            case BUTTON_EXIT:
                snprintf( buf, sizeof( buf ), "Exit Game" );
                break;
            default:
                buf[ 0 ] = '\0';
                break;
        }
        if ( buf[ 0 ] != '\0' ) {
            font.drawString( renderer, buf, -font.getTextWidth( buf, this->text_size ) / 2.0f, baseline, this->text_size, white, mvp );
        }
    }
    showErrors( );
}

void PauseMenu::cleanup( ) {
    this->render_chain_quads.clear( );
    this->render_chain_hover.clear( );
}
