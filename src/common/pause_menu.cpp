#include <math.h>
#include <stdio.h>

#include "common/RepGame.hpp"
#include "common/light.hpp"
#include "common/pause_menu.hpp"
#include "common/ui_layout.hpp"
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
bool PauseMenu::is_stepper_row( const int button ) {
    return button == PauseMenu::BUTTON_RENDER_ROW || button == PauseMenu::BUTTON_BRIGHT_ROW || button == PauseMenu::BUTTON_TIME_ROW;
}

void PauseMenu::stepper_ids( const int row_button, int &minus, int &reset, int &plus ) {
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

// Lines of one or two side-by-side buttons; the steppers are always
// alone on their line. Compile-time gaps leave a single-button line,
// which is just a full-width button.
int PauseMenu::build_lines( const bool dead, Line *lines, const int max_lines ) {
    int nl = 0;
    if ( dead ) {
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
    return nl;
}

// Worst-case label per row kind; the panel width is derived from these,
// so labels can't overflow their buttons.
const char *PauseMenu::worst_label( const int button, const bool dead, char *buf, const size_t buf_size ) {
    switch ( button ) {
        case BUTTON_RESUME:
            return dead ? "Respawn" : "Resume";
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
}

// Splits a layout row into its cells with the same whole-pixel edges the
// old manual layout produced (lroundf'd x edges keep the gaps even).
int PauseMenu::split_row( const Line &line, const UiRect &row, const float gap, int ids[ 3 ], UiRect rects[ 3 ] ) {
    if ( line.count == 1 && is_stepper_row( line.kinds[ 0 ] ) ) {
        int minus, reset, plus;
        stepper_ids( line.kinds[ 0 ], minus, reset, plus );
        const float sq = row.height;
        const float mid_x = row.left + sq + gap;
        ids[ 0 ] = minus;
        rects[ 0 ] = UiRect{ row.left, row.bottom, sq, row.height };
        ids[ 1 ] = reset;
        rects[ 1 ] = UiRect{ mid_x, row.bottom, row.right( ) - sq - gap - mid_x, row.height };
        ids[ 2 ] = plus;
        rects[ 2 ] = UiRect{ row.right( ) - sq, row.bottom, sq, row.height };
        return 3;
    }
    const float cell_w = ( row.width - ( line.count - 1 ) * gap ) / line.count;
    for ( int j = 0; j < line.count; j++ ) {
        const float x = j == 0 ? row.left : lroundf( row.left + j * ( cell_w + gap ) );
        const float right = j == line.count - 1 ? row.right( ) : lroundf( x + cell_w );
        ids[ j ] = line.kinds[ j ];
        rects[ j ] = UiRect{ x, row.bottom, right - x, row.height };
    }
    return line.count;
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

void PauseMenu::set_dead( RepGameState &state, const bool dead ) {
    if ( this->dead_mode == dead ) {
        return;
    }
    this->dead_mode = dead;
    // The row set and panel tint change — relayout immediately like a
    // resize. compute() is a no-op until the layout is initialized.
    state.ui.layout.compute( ui_layout_input( state ) );
}

int PauseMenu::hitTest( const RepGameState &state, const int abs_x, const int abs_y ) const {
    const UiLayoutRects &rects = state.ui.layout.rects( );
    // Mouse/tap coords are top-left origin, y down; the UI is center origin,
    // y up (see UIOverlay::draw_held_inventory_item).
    const float x = ( float )abs_x - state.screen.width * 0.5f;
    const float y = state.screen.height * 0.5f - ( float )abs_y;
    Line lines[ MAX_ROWS ];
    const int nl = build_lines( this->dead_mode, lines, MAX_ROWS );
    const int nrows = nl < rects.pause_row_count ? nl : rects.pause_row_count;
    for ( int i = 0; i < nrows; i++ ) {
        int ids[ 3 ];
        UiRect cells[ 3 ];
        const int n = split_row( lines[ i ], rects.pause_rows[ i ], rects.pause_gap, ids, cells );
        for ( int j = 0; j < n; j++ ) {
            if ( cells[ j ].contains( x, y ) ) {
                return ids[ j ];
            }
        }
    }
    return BUTTON_NONE;
}

// The live label for a button id — the stepper reset buttons carry the
// row label, the -/+ cells their glyphs.
static void button_label( const int button, const bool dead, const RepGameState &state, char *buf, const size_t buf_size ) {
    switch ( button ) {
        case PauseMenu::BUTTON_RESUME:
            snprintf( buf, buf_size, "%s", dead ? "Respawn" : "Resume" );
            break;
        case PauseMenu::BUTTON_GRAPHICS:
            snprintf( buf, buf_size, "Graphics: %s", quality_names[ state.input.worldDrawQuality ] );
            break;
        case PauseMenu::BUTTON_SHADOW:
            snprintf( buf, buf_size, "Shadows: %s%s", light_shadow_smooth( ) ? "Smooth" : "Fast",
                      light_shadow_smooth_forced( ) ? " (env)" : "" );
            break;
        case PauseMenu::BUTTON_RENDER_RESET:
            snprintf( buf, buf_size, "Render Distance: %d", state.input.render_distance );
            break;
        case PauseMenu::BUTTON_BRIGHT_RESET:
            snprintf( buf, buf_size, "Cave Brightness: %d%%", ( int )lroundf( imgui_overlay_get_imgui_debug_vars( ).min_ambient * 100.0f ) );
            break;
        case PauseMenu::BUTTON_TIME_RESET: {
            // Same clock as the ImGui overlay: ticks within the day
            // mapped onto 24*60 minutes.
            const long time_of_day = state.world_time % DAY_LENGTH_TICKS;
            const int clock_minutes = static_cast<int>( time_of_day * 24 * 60 / DAY_LENGTH_TICKS );
            snprintf( buf, buf_size, "Time of Day: %02d:%02d", clock_minutes / 60, clock_minutes % 60 );
            break;
        }
        case PauseMenu::BUTTON_DEBUG_VIEW:
            snprintf( buf, buf_size, "Block Debug View: %s", state.input.debug_mode ? "On" : "Off" );
            break;
        case PauseMenu::BUTTON_GAME_MODE:
            snprintf( buf, buf_size, "Game Mode: %s", state.game_mode == GameMode_Creative ? "Creative" : "Survival" );
            break;
        case PauseMenu::BUTTON_PEACEFUL:
            snprintf( buf, buf_size, "Mobs: %s", state.peaceful ? "Peaceful" : "Hostile" );
            break;
        case PauseMenu::BUTTON_FLYING:
            snprintf( buf, buf_size, "Flying: %s", state.input.player_flying ? "On" : "Off" );
            break;
        case PauseMenu::BUTTON_IMGUI:
            snprintf( buf, buf_size, "Debug Overlay: %s", state.ui.imgui.shown ? "On" : "Off" );
            break;
        case PauseMenu::BUTTON_EXIT:
            snprintf( buf, buf_size, "Exit Game" );
            break;
        case PauseMenu::BUTTON_RENDER_MINUS:
        case PauseMenu::BUTTON_BRIGHT_MINUS:
        case PauseMenu::BUTTON_TIME_MINUS:
            snprintf( buf, buf_size, "-" );
            break;
        case PauseMenu::BUTTON_RENDER_PLUS:
        case PauseMenu::BUTTON_BRIGHT_PLUS:
        case PauseMenu::BUTTON_TIME_PLUS:
            snprintf( buf, buf_size, "+" );
            break;
        default:
            buf[ 0 ] = '\0';
            break;
    }
}

void PauseMenu::draw( const Renderer &renderer, Shader &shader, const Texture &blocksTexture, FontRenderer &font, const glm::mat4 &mvp, const RepGameState &state ) {
    const UiLayoutRects &rects = state.ui.layout.rects( );
    if ( rects.pause_row_count == 0 ) {
        return;
    }
    const float text_size = 13.0f * rects.pause_k;
    const float title_size = 13.0f * ( rects.pause_k + 1 );
    shader.set_uniform_mat4f( "u_MVP", mvp );
    shader.set_uniform1i_texture( "u_Texture", blocksTexture );
    shader.set_uniform1f( "u_HeldBlockLight", 1.0f );
    // The menu must cover everything else in the UI pass (hotbar, held item).
    glEnable( GL_DEPTH_TEST );
    glClear( GL_DEPTH_BUFFER_BIT );

    this->render_chain_quads.clear( );
    const UiRect &panel = rects.pause_panel;
    if ( this->dead_mode ) {
        // Slightly blood-tinged panel for the death screen.
        add_quad( this->render_chain_quads, panel.left, panel.bottom, panel.width, panel.height, ORDER_Z_MENU_PANEL, 0.22f, 0.08f, 0.08f, 0.9f );
    } else {
        add_quad( this->render_chain_quads, panel.left, panel.bottom, panel.width, panel.height, ORDER_Z_MENU_PANEL, 0.1f, 0.1f, 0.1f, 0.85f );
    }

    Line lines[ MAX_ROWS ];
    const int nl = build_lines( this->dead_mode, lines, MAX_ROWS );
    const int nrows = nl < rects.pause_row_count ? nl : rects.pause_row_count;
    for ( int i = 0; i < nrows; i++ ) {
        int ids[ 3 ];
        UiRect cells[ 3 ];
        const int n = split_row( lines[ i ], rects.pause_rows[ i ], rects.pause_gap, ids, cells );
        for ( int j = 0; j < n; j++ ) {
            add_quad( this->render_chain_quads, cells[ j ].left, cells[ j ].bottom, cells[ j ].width, cells[ j ].height, ORDER_Z_MENU_BUTTON, 0.35f, 0.35f, 0.35f, 1.0f );
        }
    }
    this->render_chain_quads.draw( renderer, shader );

    // Hover highlight over whichever button the (unlocked) pointer is on.
    this->render_chain_hover.clear( );
    const int hovered = this->hitTest( state, state.input.mouse.absPosition.x, state.input.mouse.absPosition.y );
    if ( hovered != BUTTON_NONE ) {
        for ( int i = 0; i < nrows; i++ ) {
            int ids[ 3 ];
            UiRect cells[ 3 ];
            const int n = split_row( lines[ i ], rects.pause_rows[ i ], rects.pause_gap, ids, cells );
            for ( int j = 0; j < n; j++ ) {
                if ( ids[ j ] == hovered ) {
                    add_quad( this->render_chain_hover, cells[ j ].left, cells[ j ].bottom, cells[ j ].width, cells[ j ].height, ORDER_Z_MENU_HOVER, 1.0f, 1.0f, 1.0f, 0.15f );
                    i = nrows;
                    break;
                }
            }
        }
    }
    this->render_chain_hover.draw( renderer, shader );

    const float white[ 4 ] = { 1.0f, 1.0f, 1.0f, 1.0f };
    char buf[ 64 ];

    // Title baseline sits on the band's bottom edge: 'd' tops touch the
    // band top so the padding reads even on both sides.
    const char *title = this->dead_mode ? "You died!" : "Paused";
    font.text_append( title, panel.center_x( ) - font.getTextWidth( title, title_size ) / 2.0f, rects.pause_title.bottom, title_size, white );

    // Center by cap height so the capital letters sit symmetric in the
    // button (FontRenderer snaps the baseline to the pixel grid itself).
    for ( int i = 0; i < nrows; i++ ) {
        int ids[ 3 ];
        UiRect cells[ 3 ];
        const int n = split_row( lines[ i ], rects.pause_rows[ i ], rects.pause_gap, ids, cells );
        for ( int j = 0; j < n; j++ ) {
            button_label( ids[ j ], this->dead_mode, state, buf, sizeof( buf ) );
            if ( buf[ 0 ] == '\0' ) {
                continue;
            }
            const float baseline = cells[ j ].center_y( ) - font.getCapHeight( text_size ) / 2.0f;
            font.text_append( buf, cells[ j ].center_x( ) - font.getTextWidth( buf, text_size ) / 2.0f, baseline, text_size, white );
        }
    }
    showErrors( );
}

void PauseMenu::cleanup( ) {
    this->render_chain_quads.clear( );
    this->render_chain_hover.clear( );
}
