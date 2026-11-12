// The sign editor: a modal four-line text box opened by right-clicking a
// standing, wall or hanging sign. The queued Input text counters are
// drained here each tick; committing writes through World::set_sign_text
// (and, in multiplayer, sends a SIGN_TEXT message — the server's echo is
// the authoritative copy).

#include "common/RepGame.hpp"
#include "common/constants.hpp"
#include "common/world.hpp"

// Apply one tick of queued text input to the editor. Pure (test-driven):
// reads but does not consume Input's text_* counters. Returns true when
// the edit should commit and close — Enter on the last line or Escape.
bool sign_edit_apply_input( SignEditUi &edit, const Input &in ) {
    if ( edit.line < 0 ) {
        edit.line = 0;
    } else if ( edit.line > SIGN_LINES - 1 ) {
        edit.line = SIGN_LINES - 1;
    }
    std::string &line = edit.text.lines[ edit.line ];
    for ( int i = 0; i < in.text_chars_len; i++ ) {
        const unsigned char c = static_cast<unsigned char>( in.text_chars[ i ] );
        if ( c >= 32 && c <= 126 && line.size( ) < SIGN_LINE_MAX ) {
            line += static_cast<char>( c );
        }
    }
    for ( int i = 0; i < in.text_backspaces; i++ ) {
        std::string &cur = edit.text.lines[ edit.line ];
        if ( !cur.empty( ) ) {
            cur.pop_back( );
        } else if ( edit.line > 0 ) {
            // Backspace on an empty line moves up a line (deleting the
            // line break), matching the sign editor in Minecraft.
            edit.line--;
        }
    }
    edit.line -= in.text_ups;
    if ( edit.line < 0 ) {
        edit.line = 0;
    }
    edit.line += in.text_downs;
    if ( edit.line > SIGN_LINES - 1 ) {
        edit.line = SIGN_LINES - 1;
    }
    for ( int i = 0; i < in.text_enters; i++ ) {
        if ( edit.line < SIGN_LINES - 1 ) {
            edit.line++;
        } else {
            return true; // Enter on the last line confirms.
        }
    }
    return in.text_escape;
}

void repgame_open_sign_editor( RepGameState &state, const glm::ivec3 &pos ) {
#if defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS ) || defined( REPGAME_WASM )
    if ( state.ui.sign_edit.open ) {
        repgame_close_sign_editor( state, true );
    }
    // The editor is modal: close any screen already up so panels don't
    // stack under the sign UI.
    state.input.inventory_open = false;
    state.input.menu_open = false;
    state.ui.chest.open = false;
    state.ui.crafting.table_open = false;
    state.ui.furnace.open = false;
    SignEditUi &edit = state.ui.sign_edit;
    edit.pos = pos;
    edit.line = 0;
    edit.opened_tick = state.tick_number;
    const SignText *existing = state.world.get_sign_text( pos );
    edit.text = existing != nullptr ? *existing : SignText{ };
    edit.open = true;
    // Releases the pointer (should_lock_pointer) and starts SDL text
    // capture on the next platform pointer-state pass.
    state.input.text_input_begin( );
#else
    ( void )state;
    ( void )pos;
#endif
}

void repgame_close_sign_editor( RepGameState &state, bool commit ) {
    SignEditUi &edit = state.ui.sign_edit;
    if ( !edit.open ) {
        return;
    }
    edit.open = false;
#if defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS ) || defined( REPGAME_WASM )
    state.input.text_input_end( );
#endif
    if ( commit ) {
        repgame_commit_sign_edit( state );
    }
}

void repgame_commit_sign_edit( RepGameState &state ) {
    const SignEditUi &edit = state.ui.sign_edit;
    state.world.set_sign_text( edit.pos, edit.text );
    if ( state.multiplayer.is_active( ) ) {
        state.multiplayer.send_sign_text( edit.pos, edit.text );
    }
}

void repgame_tick_sign_editor( RepGameState &state ) {
    Input &in = state.input;
    SignEditUi &edit = state.ui.sign_edit;
    if ( !edit.open ) {
        return;
    }
    const bool wants_close = sign_edit_apply_input( edit, in );
    in.text_chars_len = 0;
    in.text_backspaces = 0;
    in.text_enters = 0;
    in.text_ups = 0;
    in.text_downs = 0;
    in.text_escape = false;
    if ( wants_close ) {
        repgame_close_sign_editor( state, true );
        return;
    }
    // Close without committing when the sign can't be edited any more:
    // chunk unloaded (LAST_BLOCK_ID), the cell no longer holds a sign
    // (past the open-time grace ticks), or the player left reach — the
    // same +2 slack the container screens use.
    const BlockState state_at = state.world.get_loaded_block( edit.pos );
    if ( state_at.id == LAST_BLOCK_ID ) {
        repgame_close_sign_editor( state, false );
        return;
    }
    const bool placement_grace = state.tick_number - edit.opened_tick < 5;
    if ( !placement_grace && !block_is_sign( state_at.id ) ) {
        repgame_close_sign_editor( state, false );
        return;
    }
    const glm::dvec3 center = glm::dvec3( edit.pos ) + glm::dvec3( 0.5 );
    if ( glm::length( state.camera.pos - center ) > REACH_DISTANCE + 2.0 ) {
        repgame_close_sign_editor( state, false );
    }
}

// Menu-layer z constants, matching pause_menu.cpp's layering (larger
// screen_z wins; the UI pass clears depth before menu draws).
#define ORDER_Z_SIGN_PANEL 0.6f
#define ORDER_Z_SIGN_FIELD 0.7f

void repgame_draw_sign_editor( RepGameState &state, const Renderer &renderer, Shader &shader, FontRenderer &font, const glm::mat4 &mvp_ui ) {
    const SignEditUi &edit = state.ui.sign_edit;
    const UiLayoutRects &rects = state.ui.layout.rects( );
    const int k = rects.sign_k > 0 ? rects.sign_k : 1;
    const float text_size = UI_TEXT_BASE_SIZE * k;

    shader.set_uniform_mat4f( "u_MVP", mvp_ui );
    // Cover everything else in the UI pass, like the pause menu.
    glEnable( GL_DEPTH_TEST );
    glClear( GL_DEPTH_BUFFER_BIT );

    const float panel_tint[ 4 ] = { 0.1f, 0.1f, 0.1f, 0.85f };
    state.ui.overlay.draw_ui_quad( rects.sign_panel, panel_tint, ORDER_Z_SIGN_PANEL, renderer, mvp_ui );
    for ( int i = 0; i < SIGN_LINES; i++ ) {
        // The focused line reads brighter; the rest sit back as fields.
        const bool active = i == edit.line;
        const float field_tint[ 4 ] = { active ? 0.42f : 0.26f, active ? 0.42f : 0.26f, active ? 0.42f : 0.26f, 1.0f };
        state.ui.overlay.draw_ui_quad( rects.sign_lines[ i ], field_tint, ORDER_Z_SIGN_FIELD, renderer, mvp_ui );
    }

    const float white[ 4 ] = { 1.0f, 1.0f, 1.0f, 1.0f };
    const float dim[ 4 ] = { 1.0f, 1.0f, 1.0f, 0.55f };
    const char *title = "Edit sign";
    font.text_append( title, rects.sign_panel.center_x( ) - font.getTextWidth( title, text_size ) / 2.0f, rects.sign_title.bottom, text_size, white );

    for ( int i = 0; i < SIGN_LINES; i++ ) {
        const UiRect &row = rects.sign_lines[ i ];
        std::string shown = edit.text.lines[ i ];
        if ( i == edit.line ) {
            shown += '_'; // the cursor
        }
        if ( shown.empty( ) ) {
            continue;
        }
        const float baseline = row.center_y( ) - font.getCapHeight( text_size ) / 2.0f;
        font.text_append( shown.c_str( ), row.center_x( ) - font.getTextWidth( shown.c_str( ), text_size ) / 2.0f, baseline, text_size, white );
    }

    const char *hint = "Enter/Esc to save";
    const float hint_size = UI_TEXT_BASE_SIZE;
    font.text_append( hint, rects.sign_panel.center_x( ) - font.getTextWidth( hint, hint_size ) / 2.0f, rects.sign_hint.bottom, hint_size, dim );
}
