#include <math.h>
#include <stdio.h>

#include "common/RepGame.hpp"
#include "common/light.hpp"
#include "common/pause_menu.hpp"
#include "common/ui_overlay_buffers.hpp"

// Z order within the UI depth pass (larger screen_z wins; draw_ui_pass
// clears depth before the menu so these only order against each other).
#define ORDER_Z_MENU_PANEL 0.6f
#define ORDER_Z_MENU_BUTTON 0.7f
#define ORDER_Z_MENU_HOVER 0.8f

static const char *const quality_names[] = { "LOW", "MEDIUM", "HIGH", "X_HIGH" };

// The -/+ stepper rows (render distance, cave brightness, time of day)
// share the [-] [label] [+] layout; stepper_ids maps a row kind to its
// button ids.
static bool is_stepper_row( const int button ) {
    return button == PauseMenu::BUTTON_RENDER_ROW || button == PauseMenu::BUTTON_BRIGHT_ROW || button == PauseMenu::BUTTON_TIME_ROW;
}

static void stepper_ids( const int row_button, int &minus, int &reset, int &plus ) {
    if ( row_button == PauseMenu::BUTTON_RENDER_ROW ) {
        minus = PauseMenu::BUTTON_RENDER_MINUS;
        reset = PauseMenu::BUTTON_RENDER_RESET;
        plus = PauseMenu::BUTTON_RENDER_PLUS;
    } else if ( row_button == PauseMenu::BUTTON_BRIGHT_ROW ) {
        minus = PauseMenu::BUTTON_BRIGHT_MINUS;
        reset = PauseMenu::BUTTON_BRIGHT_RESET;
        plus = PauseMenu::BUTTON_BRIGHT_PLUS;
    } else {
        minus = PauseMenu::BUTTON_TIME_MINUS;
        reset = PauseMenu::BUTTON_TIME_RESET;
        plus = PauseMenu::BUTTON_TIME_PLUS;
    }
}

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

void PauseMenu::set_dead( const bool dead ) {
    if ( this->dead_mode == dead ) {
        return;
    }
    this->dead_mode = dead;
    if ( this->layout_font != nullptr && this->screen_width > 0 ) {
        this->onScreenSizeChange( this->screen_width, this->screen_height, *this->layout_font );
    }
}

void PauseMenu::onScreenSizeChange( const int width, const int height, const FontRenderer &font ) {
    this->screen_width = width;
    this->screen_height = height;
    this->layout_font = &font;
    this->num_regions = 0;
    this->num_rows = 0;

    // Lines of one or two side-by-side buttons; the steppers are always
    // alone on their line. Compile-time gaps leave a single-button line,
    // which is just a full-width button.
    struct Line {
        int kinds[ 3 ];
        int count;
    };
    Line lines[ MAX_ROWS ];
    int nl = 0;
    if ( this->dead_mode ) {
        // Death screen: the RESUME button relabels to "Respawn" in draw();
        // everything else hides — settings mid-death would be weird.
        lines[ nl++ ] = Line{ { BUTTON_RESUME }, 1 };
        lines[ nl++ ] = Line{ { BUTTON_EXIT }, 1 };
    } else {
        lines[ nl++ ] = Line{ { BUTTON_RESUME }, 1 };
#if SUPPORTS_FRAME_BUFFER
        lines[ nl++ ] = Line{ { BUTTON_GRAPHICS, BUTTON_SHADOW }, 2 };
#else
        lines[ nl++ ] = Line{ { BUTTON_SHADOW }, 1 };
#endif
        lines[ nl++ ] = Line{ { BUTTON_RENDER_ROW }, 1 };
        lines[ nl++ ] = Line{ { BUTTON_BRIGHT_ROW }, 1 };
        lines[ nl++ ] = Line{ { BUTTON_TIME_ROW }, 1 };
        lines[ nl++ ] = Line{ { BUTTON_GAME_MODE, BUTTON_PEACEFUL }, 2 };
        lines[ nl++ ] = Line{ { BUTTON_FLYING, BUTTON_DEBUG_VIEW }, 2 };
#if SUPPORTS_IMGUI_OVERLAY
        lines[ nl++ ] = Line{ { BUTTON_IMGUI }, 1 };
#endif
        lines[ nl++ ] = Line{ { BUTTON_EXIT }, 1 };
    }

    // Like InventoryRenderer::onSizeChange, the UI is sized as a fraction of
    // a 16:9-normalized height so buttons stay finger-sized on wide phones.
    const float ui_height_basis = fmaxf( ( float )height, width * ( 9.0f / 16.0f ) );

    // Worst-case label per row kind; the panel width is derived from these,
    // so labels can't overflow their buttons.
    auto worst_label = [this]( int button, char *buf, size_t buf_size ) -> const char * {
        switch ( button ) {
            case BUTTON_RESUME:
                return this->dead_mode ? "Respawn" : "Resume";
            case BUTTON_GRAPHICS:
                return "Graphics: X_HIGH";
            case BUTTON_SHADOW:
                return "Shadows: Smooth (env)";
            case BUTTON_RENDER_ROW:
                snprintf( buf, buf_size, "Render Distance: %d", RENDER_RADIUS_MAX );
                return buf;
            case BUTTON_BRIGHT_ROW:
                return "Cave Brightness: 100%";
            case BUTTON_TIME_ROW:
                return "Time of Day: 00:00";
            case BUTTON_DEBUG_VIEW:
                return "Block Debug View: Off";
            case BUTTON_GAME_MODE:
                return "Game Mode: Survival";
            case BUTTON_PEACEFUL:
                return "Mobs: Peaceful";
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
        for ( int i = 0; i < nl; i++ ) {
            const Line &line = lines[ i ];
            float line_w;
            if ( line.count == 1 && is_stepper_row( line.kinds[ 0 ] ) ) {
                // - and + squares at the ends, reset button between them.
                const char *label = worst_label( line.kinds[ 0 ], worst_buf, sizeof( worst_buf ) );
                line_w = 2.0f * button_height + 2.0f * gap + 2.0f * hpad + font.getTextWidth( label, t );
            } else {
                // c equal cells sharing the line's gap; a single-button
                // line degenerates to its own worst-label width.
                float max_cell_w = 0.0f;
                for ( int j = 0; j < line.count; j++ ) {
                    const char *label = worst_label( line.kinds[ j ], worst_buf, sizeof( worst_buf ) );
                    const float cell_w = font.getTextWidth( label, t ) + 2.0f * hpad;
                    if ( cell_w > max_cell_w ) {
                        max_cell_w = cell_w;
                    }
                }
                line_w = line.count * max_cell_w + ( line.count - 1 ) * gap;
            }
            if ( line_w > button_width ) {
                button_width = line_w;
            }
        }
        panel_width = button_width + 2.0f * pad;
        panel_height = pad + title_height + pad + nl * button_height + ( nl - 1 ) * gap + pad;
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
    if ( this->dead_mode ) {
        // Slightly blood-tinged panel for the death screen.
        add_quad( this->render_chain_quads, panel_left, panel_bottom, panel_width, panel_height, ORDER_Z_MENU_PANEL, 0.22f, 0.08f, 0.08f, 0.9f );
    } else {
        add_quad( this->render_chain_quads, panel_left, panel_bottom, panel_width, panel_height, ORDER_Z_MENU_PANEL, 0.1f, 0.1f, 0.1f, 0.85f );
    }

    const float buttons_left = panel_left + pad;
    const float buttons_right = panel_right - pad;
    // Same padding between the title band and the first button.
    const float buttons_top = panel_top - pad - title_height - pad;
    for ( int i = 0; i < nl; i++ ) {
        const Line &line = lines[ i ];
        const float btn_top = buttons_top - i * ( button_height + gap );
        const float btn_y = btn_top - button_height;
        const float btn_cy = btn_y + button_height / 2.0f;
        if ( line.count == 1 && is_stepper_row( line.kinds[ 0 ] ) ) {
            // [-] [label] [+]: the middle button resets the value to the
            // default (like the '0' key for render distance).
            int minus, reset, plus;
            stepper_ids( line.kinds[ 0 ], minus, reset, plus );
            const float minus_x = buttons_left;
            const float plus_x = buttons_right - button_height;
            const float mid_x = minus_x + button_height + gap;
            const float mid_w = plus_x - gap - mid_x;
            add_quad( this->render_chain_quads, minus_x, btn_y, button_height, button_height, ORDER_Z_MENU_BUTTON, 0.35f, 0.35f, 0.35f, 1.0f );
            add_quad( this->render_chain_quads, mid_x, btn_y, mid_w, button_height, ORDER_Z_MENU_BUTTON, 0.35f, 0.35f, 0.35f, 1.0f );
            add_quad( this->render_chain_quads, plus_x, btn_y, button_height, button_height, ORDER_Z_MENU_BUTTON, 0.35f, 0.35f, 0.35f, 1.0f );
            Row &row = this->rows[ this->num_rows++ ];
            row.button = line.kinds[ 0 ];
            row.center_x = mid_x + mid_w / 2.0f;
            row.center_y = btn_cy;
            row.left_cx = minus_x + button_height / 2.0f;
            row.right_cx = plus_x + button_height / 2.0f;
            this->regions[ this->num_regions++ ] = Region{ minus, minus_x, btn_y, button_height, button_height };
            this->regions[ this->num_regions++ ] = Region{ reset, mid_x, btn_y, mid_w, button_height };
            this->regions[ this->num_regions++ ] = Region{ plus, plus_x, btn_y, button_height, button_height };
        } else {
            // Equal cells; x edges snap to whole pixels (like the panel
            // edges) so the gaps between cells stay even.
            const float cell_w = ( button_width - ( line.count - 1 ) * gap ) / line.count;
            for ( int j = 0; j < line.count; j++ ) {
                const float cell_x = j == 0 ? buttons_left : lroundf( buttons_left + j * ( cell_w + gap ) );
                const float cell_right = j == line.count - 1 ? buttons_right : lroundf( cell_x + cell_w );
                const float cell_w_px = cell_right - cell_x;
                add_quad( this->render_chain_quads, cell_x, btn_y, cell_w_px, button_height, ORDER_Z_MENU_BUTTON, 0.35f, 0.35f, 0.35f, 1.0f );
                Row &row = this->rows[ this->num_rows++ ];
                row.button = line.kinds[ j ];
                row.center_x = cell_x + cell_w_px / 2.0f;
                row.center_y = btn_cy;
                row.left_cx = 0.0f;
                row.right_cx = 0.0f;
                this->regions[ this->num_regions++ ] = Region{ line.kinds[ j ], cell_x, btn_y, cell_w_px, button_height };
            }
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
    const char *title = this->dead_mode ? "You died!" : "Paused";
    font.drawString( renderer, title, -font.getTextWidth( title, this->title_size ) / 2.0f,
                     this->title_center_y - font.getCapHeight( this->title_size ) / 2.0f, this->title_size, white, mvp );

    for ( int i = 0; i < this->num_rows; i++ ) {
        const Row &row = this->rows[ i ];
        const float baseline = row.center_y - font.getCapHeight( this->text_size ) / 2.0f;
        switch ( row.button ) {
            case BUTTON_RESUME:
                snprintf( buf, sizeof( buf ), "%s", this->dead_mode ? "Respawn" : "Resume" );
                break;
            case BUTTON_GRAPHICS:
                snprintf( buf, sizeof( buf ), "Graphics: %s", quality_names[ state.input.worldDrawQuality ] );
                break;
            case BUTTON_SHADOW:
                snprintf( buf, sizeof( buf ), "Shadows: %s%s", light_shadow_smooth( ) ? "Smooth" : "Fast",
                          light_shadow_smooth_forced( ) ? " (env)" : "" );
                break;
            case BUTTON_RENDER_ROW:
                snprintf( buf, sizeof( buf ), "Render Distance: %d", state.input.render_distance );
                break;
            case BUTTON_BRIGHT_ROW:
                snprintf( buf, sizeof( buf ), "Cave Brightness: %d%%", ( int )lroundf( imgui_overlay_get_imgui_debug_vars( ).min_ambient * 100.0f ) );
                break;
            case BUTTON_TIME_ROW: {
                // Same clock as the ImGui overlay: ticks within the day
                // mapped onto 24*60 minutes.
                const long time_of_day = state.world_time % DAY_LENGTH_TICKS;
                const int clock_minutes = static_cast<int>( time_of_day * 24 * 60 / DAY_LENGTH_TICKS );
                snprintf( buf, sizeof( buf ), "Time of Day: %02d:%02d", clock_minutes / 60, clock_minutes % 60 );
                break;
            }
            case BUTTON_DEBUG_VIEW:
                snprintf( buf, sizeof( buf ), "Block Debug View: %s", state.input.debug_mode ? "On" : "Off" );
                break;
            case BUTTON_GAME_MODE:
                snprintf( buf, sizeof( buf ), "Game Mode: %s", state.game_mode == GameMode_Creative ? "Creative" : "Survival" );
                break;
            case BUTTON_PEACEFUL:
                snprintf( buf, sizeof( buf ), "Mobs: %s", state.peaceful ? "Peaceful" : "Hostile" );
                break;
            case BUTTON_FLYING:
                snprintf( buf, sizeof( buf ), "Flying: %s", state.input.player_flying ? "On" : "Off" );
                break;
            case BUTTON_IMGUI:
                snprintf( buf, sizeof( buf ), "Debug Overlay: %s", state.ui.imgui.shown ? "On" : "Off" );
                break;
            case BUTTON_EXIT:
                snprintf( buf, sizeof( buf ), "Exit Game" );
                break;
            default:
                buf[ 0 ] = '\0';
                break;
        }
        if ( is_stepper_row( row.button ) ) {
            font.drawString( renderer, "-", row.left_cx - font.getTextWidth( "-", this->text_size ) / 2.0f, baseline, this->text_size, white, mvp );
            font.drawString( renderer, "+", row.right_cx - font.getTextWidth( "+", this->text_size ) / 2.0f, baseline, this->text_size, white, mvp );
        }
        if ( buf[ 0 ] != '\0' ) {
            font.drawString( renderer, buf, row.center_x - font.getTextWidth( buf, this->text_size ) / 2.0f, baseline, this->text_size, white, mvp );
        }
    }
    showErrors( );
}

void PauseMenu::cleanup( ) {
    this->render_chain_quads.clear( );
    this->render_chain_hover.clear( );
}
