// The chat console: a non-modal input bar + scrollback left-anchored
// above the hotbar. 't' opens it, '/' opens it prefilled as a command,
// Escape closes it, Enter submits. It shares the Input text counters
// with the sign editor (the two are mutually exclusive) and drains them
// here each tick. Slash lines run local commands; anything else goes out
// as PLAYER chat in multiplayer, or straight into the local log in
// single player.

#include "common/RepGame.hpp"
#include "common/chat.hpp"
#include "common/constants.hpp"
#include "common/ui_layout.hpp"
#include "common/font_renderer.hpp"

// Apply one tick of queued text input to the chat box. Pure (test-driven):
// reads but does not consume Input's text_* counters. `submitted` receives
// the line when Enter ends the edit (empty included); the return value
// says the box should close (Enter or Escape).
bool chat_edit_apply_input( ChatUi &chat, const Input &in, std::string *submitted ) {
    for ( int i = 0; i < in.text_chars_len; i++ ) {
        const unsigned char c = static_cast<unsigned char>( in.text_chars[ i ] );
        if ( c >= 32 && c <= 126 && chat.line.size( ) < CHAT_TEXT_MAX ) {
            chat.line += static_cast<char>( c );
        }
    }
    for ( int i = 0; i < in.text_backspaces; i++ ) {
        if ( !chat.line.empty( ) ) {
            chat.line.pop_back( );
        }
    }
    for ( int i = 0; i < in.text_ups; i++ ) {
        // The first Up stashes the in-progress draft and steps into the
        // history; further Ups walk toward the oldest entry.
        if ( chat.history_pos < 0 ) {
            chat.draft = chat.line;
            chat.history_pos = static_cast<int>( chat.sent.size( ) ) - 1;
        } else if ( chat.history_pos > 0 ) {
            chat.history_pos--;
        }
        if ( chat.history_pos >= 0 && chat.history_pos < static_cast<int>( chat.sent.size( ) ) ) {
            chat.line = chat.sent[ chat.history_pos ];
        }
    }
    for ( int i = 0; i < in.text_downs; i++ ) {
        if ( chat.history_pos < 0 ) {
            continue;
        }
        if ( chat.history_pos < static_cast<int>( chat.sent.size( ) ) - 1 ) {
            chat.history_pos++;
            chat.line = chat.sent[ chat.history_pos ];
        } else {
            // Past the newest entry: back to the stashed draft.
            chat.history_pos = -1;
            chat.line = chat.draft;
        }
    }
    if ( in.text_enters > 0 ) {
        if ( submitted != nullptr ) {
            *submitted = chat.line;
        }
        // Enter records a non-empty line in the Up/Down history, skipping
        // consecutive duplicates.
        if ( !chat.line.empty( ) && ( chat.sent.empty( ) || chat.sent.back( ) != chat.line ) ) {
            chat.sent.push_back( chat.line );
            while ( chat.sent.size( ) > CHAT_HISTORY_MAX ) {
                chat.sent.pop_front( );
            }
        }
        return true;
    }
    return in.text_escape;
}

void repgame_open_chat( RepGameState &state, const std::string &prefix ) {
#if defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS ) || defined( REPGAME_WASM ) || defined( REPGAME_ANDROID )
    ChatUi &chat = state.ui.chat;
    // Refused while another modal screen or text capture owns the keys.
    if ( chat.open || state.ui.sign_edit.open || state.input.menu_open || state.input.inventory_open ) {
        return;
    }
    chat.line = prefix;
    chat.history_pos = -1;
    chat.opened_tick = state.tick_number;
    chat.open = true;
    // Releases the pointer (should_lock_pointer) and starts SDL text
    // capture on the next platform pointer-state pass. text_input_begin
    // clears the queued counters, so the opening 't'/'/' never leaks into
    // the box.
    state.input.text_input_begin( );
#else
    ( void )state;
    ( void )prefix;
#endif
}

void repgame_close_chat( RepGameState &state ) {
    ChatUi &chat = state.ui.chat;
    if ( !chat.open ) {
        return;
    }
    chat.open = false;
    chat.line.clear( );
    chat.history_pos = -1;
#if defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS ) || defined( REPGAME_WASM ) || defined( REPGAME_ANDROID )
    state.input.text_input_end( );
#endif
}

void repgame_tick_chat( RepGameState &state ) {
    Input &in = state.input;
    // The key handler arms chat_open_request on a down edge; the open is
    // refused (dropped, not deferred) while another screen is up.
    if ( in.chat_open_request != 0 ) {
        const std::string prefix = in.chat_open_request == 2 ? "/" : "";
        in.chat_open_request = 0;
        repgame_open_chat( state, prefix );
    }
    if ( !state.ui.chat.open ) {
        return;
    }
    std::string submitted;
    const bool wants_close = chat_edit_apply_input( state.ui.chat, in, &submitted );
    in.text_chars_len = 0;
    in.text_backspaces = 0;
    in.text_enters = 0;
    in.text_ups = 0;
    in.text_downs = 0;
    in.text_escape = false;
    if ( wants_close ) {
        repgame_close_chat( state );
        chat_submit( state, submitted );
    }
}

void chat_submit( RepGameState &state, const std::string &line_in ) {
    std::string line = line_in;
    chat_sanitize_text( line );
    if ( line.empty( ) ) {
        return;
    }
    if ( line[ 0 ] == '/' ) {
        chat_run_command( state, line.substr( 1 ) );
        return;
    }
    if ( state.multiplayer.is_active( ) ) {
        state.multiplayer.send_chat( CHAT_KIND_PLAYER, "", line );
    } else {
        // Single player: the line lands in the local log directly.
        state.chat.push( CHAT_KIND_PLAYER, state.player_name, "", line, state.tick_number );
    }
}

// HUD-layer z — same scale as the overlay's quads.
#define ORDER_Z_CHAT 0.1f

static void chat_kind_color( const ChatKind kind, float tint[ 4 ] ) {
    switch ( kind ) {
        case CHAT_KIND_PLAYER:
            tint[ 0 ] = 1.0f; tint[ 1 ] = 1.0f; tint[ 2 ] = 1.0f;
            break;
        case CHAT_KIND_SYSTEM:
            tint[ 0 ] = 0.95f; tint[ 1 ] = 0.95f; tint[ 2 ] = 0.4f;
            break;
        case CHAT_KIND_WHISPER:
            tint[ 0 ] = 0.65f; tint[ 1 ] = 0.65f; tint[ 2 ] = 0.65f;
            break;
        case CHAT_KIND_EMOTE:
            tint[ 0 ] = 0.8f; tint[ 1 ] = 0.8f; tint[ 2 ] = 0.95f;
            break;
        case CHAT_KIND_INFO:
            tint[ 0 ] = 0.7f; tint[ 1 ] = 0.95f; tint[ 2 ] = 0.7f;
            break;
        case CHAT_KIND_ERROR:
        default:
            tint[ 0 ] = 1.0f; tint[ 1 ] = 0.4f; tint[ 2 ] = 0.4f;
            break;
    }
    tint[ 3 ] = 1.0f;
}

void repgame_draw_chat( RepGameState &state, const Renderer &renderer, FontRenderer &font, const glm::mat4 &mvp_ui ) {
    const UiLayoutRects &rects = state.ui.layout.rects( );
    const ChatUi &chat_ui = state.ui.chat;
    if ( rects.chat_log.width <= 0.0f ) {
        return;
    }
    const int k = rects.chat_k > 0 ? rects.chat_k : 1;
    const float text_size = UI_TEXT_BASE_SIZE * k;
    const int row_count = rects.chat_lines > 0 ? rects.chat_lines : 1;
    const float row_h = rects.chat_log.height / row_count;
    const float cap_h = font.getCapHeight( text_size );
    const float text_x = rects.chat_log.left + 4.0f;
    const float max_w = rects.chat_log.width - 8.0f;
    const auto measure = [ & ]( const std::string &s ) { return font.getTextWidth( s.c_str( ), text_size ); };

    // Backdrops exist only while the box is open; the closed view is bare
    // text fading out over the world.
    if ( chat_ui.open ) {
        const float bg[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.5f };
        state.ui.overlay.draw_ui_quad( rects.chat_log, bg, ORDER_Z_CHAT, renderer, mvp_ui );
        state.ui.overlay.draw_ui_quad( rects.chat_input, bg, ORDER_Z_CHAT, renderer, mvp_ui );
    }

    // Build display lines newest-first: the log's tail wraps bottom-up.
    struct ChatRow {
        std::string text;
        float tint[ 4 ];
    };
    std::vector<ChatRow> rows;
    const int max_rows = chat_ui.open ? row_count : 10;
    for ( auto it = state.chat.entries( ).rbegin( ); it != state.chat.entries( ).rend( ) && static_cast<int>( rows.size( ) ) < max_rows; ++it ) {
        float alpha = 1.0f;
        if ( !chat_ui.open ) {
            // Closed: only lines younger than 10s, fading over the last
            // second.
            const float age = static_cast<float>( state.tick_number - it->tick ) / static_cast<float>( UPS_RATE );
            if ( age >= 10.0f ) {
                break;
            }
            if ( age > 9.0f ) {
                alpha = 10.0f - age;
            }
        }
        std::vector<std::string> wrapped = chat_wrap( chat_format( *it ), max_w, measure );
        for ( auto wit = wrapped.rbegin( ); wit != wrapped.rend( ) && static_cast<int>( rows.size( ) ) < max_rows; ++wit ) {
            ChatRow row;
            row.text = *wit;
            chat_kind_color( it->kind, row.tint );
            row.tint[ 3 ] = alpha;
            rows.push_back( row );
        }
    }
    // rows[0] is the newest line — it draws on the bottom row. Rows are
    // always outlined: 1px strokes in low-luminance tints (error red,
    // whisper gray) are illegible on the translucent backdrop without a
    // dark edge, and closed state has no backdrop at all. The outline
    // shares the tint alpha, fading with the line.
    for ( size_t i = 0; i < rows.size( ); i++ ) {
        const float baseline = rects.chat_log.bottom + ( static_cast<float>( i ) + 0.5f ) * row_h - cap_h * 0.5f;
        font.text_append_outlined( rows[ i ].text.c_str( ), text_x, baseline, text_size, rows[ i ].tint );
    }

    // The input bar: the line + a cursor, tail-scrolled to the box.
    if ( chat_ui.open ) {
        std::string shown = chat_ui.line + '_';
        while ( !shown.empty( ) && measure( shown ) > max_w ) {
            shown.erase( shown.begin( ) );
        }
        const float white[ 4 ] = { 1.0f, 1.0f, 1.0f, 1.0f };
        const float baseline = rects.chat_input.center_y( ) - cap_h * 0.5f;
        font.text_append( shown.c_str( ), rects.chat_input.left + 4.0f, baseline, text_size, white );
    }
}
