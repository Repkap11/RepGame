// Chat tests — the shared chat core (sanitizers, name rules, ChatLog cap,
// chat_format, chat_wrap), the CHAT/PLAYER_NAME codec bound checks, and
// the player.dat `name` key round trip.
#include "test_util.hpp"

int test_chat( ) {
    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };

    // --- chat_sanitize_text -------------------------------------------------
    {
        std::string clean = "Hello, World!";
        chat_sanitize_text( clean );
        check_bool( "chat: printable text untouched", clean == "Hello, World!" );

        std::string control = " a\tb\nc\x01""d \xff""e ";
        chat_sanitize_text( control );
        check_bool( "chat: controls stripped + trimmed", control == "abcd e" );

        std::string long_text( CHAT_TEXT_MAX + 40, 'x' );
        chat_sanitize_text( long_text );
        check_bool( "chat: text truncated to CHAT_TEXT_MAX", long_text.size( ) == CHAT_TEXT_MAX );

        std::string blanks = "   ";
        chat_sanitize_text( blanks );
        check_bool( "chat: all-space text sanitizes to empty", blanks.empty( ) );
    }

    // --- chat_valid_name / chat_sanitize_name -------------------------------
    {
        check_bool( "chat: valid names accepted",
                    chat_valid_name( "Alice" ) && chat_valid_name( "a_b_9" ) && chat_valid_name( std::string( CHAT_NAME_MAX, 'x' ) ) );
        check_bool( "chat: invalid names rejected",
                    !chat_valid_name( "" ) && !chat_valid_name( "bad name!" ) && !chat_valid_name( "has-dash" ) &&
                        !chat_valid_name( std::string( CHAT_NAME_MAX + 1, 'x' ) ) );

        std::string messy = " Al!ce-\xff""Bob ";
        chat_sanitize_name( messy );
        check_bool( "chat: name sanitize drops invalid chars", messy == "AlceBob" );
        std::string long_name( CHAT_NAME_MAX + 10, 'n' );
        chat_sanitize_name( long_name );
        check_bool( "chat: name truncated to CHAT_NAME_MAX", long_name.size( ) == CHAT_NAME_MAX );
    }

    // --- chat_format ---------------------------------------------------------
    {
        check_bool( "chat fmt: player", chat_format( ChatMessage{ CHAT_KIND_PLAYER, "Alice", "", "hi" } ) == "<Alice> hi" );
        check_bool( "chat fmt: emote", chat_format( ChatMessage{ CHAT_KIND_EMOTE, "Alice", "", "waves" } ) == "* Alice waves" );
        check_bool( "chat fmt: whisper",
                    chat_format( ChatMessage{ CHAT_KIND_WHISPER, "Alice", "Bob", "psst" } ) == "Alice -> Bob: psst" );
        check_bool( "chat fmt: system", chat_format( ChatMessage{ CHAT_KIND_SYSTEM, "", "", "joined" } ) == "joined" );
        check_bool( "chat fmt: info+error plain",
                    chat_format( ChatMessage{ CHAT_KIND_INFO, "", "", "note" } ) == "note" &&
                        chat_format( ChatMessage{ CHAT_KIND_ERROR, "", "", "nope" } ) == "nope" );
    }

    // --- ChatLog cap ---------------------------------------------------------
    {
        ChatLog log;
        for ( int i = 0; i < CHAT_LOG_MAX + 20; i++ ) {
            log.push( CHAT_KIND_PLAYER, "A", "", "msg" + std::to_string( i ), i );
        }
        check_bool( "chat: log capped at CHAT_LOG_MAX", log.entries( ).size( ) == CHAT_LOG_MAX );
        check_bool( "chat: oldest lines evicted",
                    log.entries( ).front( ).text == "msg" + std::to_string( 20 ) &&
                        log.entries( ).back( ).text == "msg" + std::to_string( CHAT_LOG_MAX + 19 ) );
        log.clear( );
        check_bool( "chat: clear empties", log.entries( ).empty( ) );
        log.push( CHAT_KIND_PLAYER, "A", "", "a\xff""b\x01""c", 0 );
        check_bool( "chat: push scrubs non-printables to '?'",
                    log.entries( ).back( ).text == "a?b?c" );
    }

    // --- chat_wrap with a fixed-width measure (1 unit per char) --------------
    {
        const auto width = []( const std::string &s ) { return static_cast<float>( s.size( ) ); };

        std::vector<std::string> lines = chat_wrap( "the quick brown fox jumps", 10.0f, width );
        check_bool( "chat wrap: normal wrap",
                    lines.size( ) == 3 && lines[ 0 ] == "the quick" && lines[ 1 ] == "brown fox" && lines[ 2 ] == "jumps" );

        // A word wider than the line hard-breaks mid-word.
        lines = chat_wrap( "supercalifragilistic", 8.0f, width );
        check_bool( "chat wrap: overlong word hard-breaks",
                    lines.size( ) == 3 && lines[ 0 ] == "supercal" && lines[ 1 ] == "ifragili" && lines[ 2 ] == "stic" );

        // A hard-broken word resumes normal wrapping after it.
        lines = chat_wrap( "hi superlongword ok", 6.0f, width );
        check_bool( "chat wrap: wrap resumes after break",
                    lines.size( ) == 4 && lines[ 0 ] == "hi" && lines[ 1 ] == "superl" && lines[ 2 ] == "ongwor" && lines[ 3 ] == "d ok" );

        lines = chat_wrap( "", 10.0f, width );
        check_bool( "chat wrap: empty input -> one empty line", lines.size( ) == 1 && lines[ 0 ].empty( ) );

        lines = chat_wrap( "a", 10.0f, width );
        check_bool( "chat wrap: single short word", lines.size( ) == 1 && lines[ 0 ] == "a" );
    }

    // --- CHAT codec ------------------------------------------------------------
    {
        NetChatPayload p{ CHAT_KIND_WHISPER, "Alice", "Bob", "meet me" };
        PacketWriter w;
        net_serialize_chat( w, p );
        NetChatPayload q;
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        check_bool( "chat codec: round trips",
                    net_deserialize_chat( r, q ) && q.kind == CHAT_KIND_WHISPER && q.from == "Alice" &&
                        q.target == "Bob" && q.text == "meet me" );

        // Oversize name field rejected before allocation.
        PacketWriter bad;
        bad.write_u8( CHAT_KIND_PLAYER );
        bad.write_u8( CHAT_NAME_MAX + 1 );
        char junk[ 64 ] = { 0 };
        bad.write_bytes( junk, sizeof( junk ) );
        NetChatPayload evil;
        PacketReader br( bad.buf( ).data( ), bad.buf( ).size( ) );
        check_bool( "chat codec: oversized name rejected", !net_deserialize_chat( br, evil ) );

        // Oversize text field rejected (u16 length says 300 > CHAT_TEXT_MAX).
        PacketWriter bad2;
        bad2.write_u8( CHAT_KIND_PLAYER );
        bad2.write_u8( 3 );
        bad2.write_bytes( "abc", 3 );
        bad2.write_u8( 0 );
        bad2.write_u16( CHAT_TEXT_MAX + 1 );
        bad2.write_bytes( junk, sizeof( junk ) );
        PacketReader br2( bad2.buf( ).data( ), bad2.buf( ).size( ) );
        check_bool( "chat codec: oversized text rejected", !net_deserialize_chat( br2, evil ) );
    }

    // --- PLAYER_NAME codec ----------------------------------------------------
    {
        NetPlayerNamePayload p{ "Alice_2" };
        PacketWriter w;
        net_serialize_player_name( w, p );
        NetPlayerNamePayload q;
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        check_bool( "chat codec: name round trips", net_deserialize_player_name( r, q ) && q.name == "Alice_2" );

        PacketWriter bad;
        bad.write_u8( CHAT_NAME_MAX + 1 );
        char junk[ 64 ] = { 0 };
        bad.write_bytes( junk, sizeof( junk ) );
        PacketReader br( bad.buf( ).data( ), bad.buf( ).size( ) );
        check_bool( "chat codec: oversized name rejected", !net_deserialize_player_name( br, q ) );
    }

    // --- player.dat `name` key --------------------------------------------------
    {
        PlayerData in;
        in.world_x = 1.0;
        in.world_y = 2.0;
        in.world_z = 3.0;
        in.name = "Alice_2";
        const std::string text = player_data_serialize( in );
        PlayerData out;
        check_bool( "player name: text parses",
                    player_data_parse( reinterpret_cast<const unsigned char *>( text.data( ) ), text.size( ), out ) );
        check_bool( "player name: round trips", out.name == "Alice_2" );

        // Absent in old saves -> empty.
        const char *old_save = "format 1\npos 1 2 3\n";
        PlayerData old_out;
        check_bool( "player name: absent -> empty",
                    player_data_parse( reinterpret_cast<const unsigned char *>( old_save ), strlen( old_save ), old_out ) &&
                        old_out.name.empty( ) );
    }

    // --- chat_edit_apply_input (the input box, driven like a key queue) ------
    {
        RepGameState &gs = *new RepGameState( );
        ChatUi &chat = gs.ui.chat;
        Input in{ };
        auto type = [ &in ]( const char *s ) {
            for ( const char *p = s; *p && in.text_chars_len < ( int )sizeof( in.text_chars ); p++ ) {
                in.text_chars[ in.text_chars_len++ ] = *p;
            }
        };
        auto drain = [ & ]( std::string *submitted = nullptr ) {
            const bool close = chat_edit_apply_input( chat, in, submitted );
            in.text_chars_len = 0;
            in.text_backspaces = 0;
            in.text_enters = 0;
            in.text_ups = 0;
            in.text_downs = 0;
            in.text_escape = false;
            return close;
        };

        type( "hello" );
        check_bool( "chat edit: typed chars land in the box", !drain( ) && chat.line == "hello" );

        // The Input queue holds 128 chars/tick; seed the line long and let
        // the cap clamp it.
        chat.line = std::string( 200, 'y' );
        for ( int i = 0; i < 100 && in.text_chars_len < ( int )sizeof( in.text_chars ); i++ ) {
            in.text_chars[ in.text_chars_len++ ] = 'x';
        }
        drain( );
        check_bool( "chat edit: line caps at CHAT_TEXT_MAX", chat.line.size( ) == CHAT_TEXT_MAX );

        chat.line = "abcd";
        in.text_backspaces = 2;
        drain( );
        check_bool( "chat edit: backspace pops", chat.line == "ab" );

        // History: two submitted lines, Up recalls the newest first.
        chat.sent.push_back( "first" );
        chat.sent.push_back( "second" );
        chat.line = "draft";
        in.text_ups = 2;
        drain( );
        check_bool( "chat edit: Up recalls older entries", chat.line == "first" && chat.history_pos == 0 );
        in.text_ups = 1;
        drain( );
        check_bool( "chat edit: Up clamps at the oldest", chat.line == "first" );
        in.text_downs = 2;
        drain( );
        check_bool( "chat edit: Down past newest restores the draft", chat.line == "draft" && chat.history_pos < 0 );

        chat.line = "send me";
        in.text_enters = 1;
        std::string submitted;
        check_bool( "chat edit: Enter submits + closes", drain( &submitted ) && submitted == "send me" );
        check_bool( "chat edit: submitted line joins history", !chat.sent.empty( ) && chat.sent.back( ) == "send me" );
        chat.line = "";
        in.text_escape = true;
        check_bool( "chat edit: Escape closes without submit", drain( &submitted ) );
        delete &gs;
    }

    // --- chat_submit: SP pushes locally, '/...' runs commands ----------------
    {
        RepGameState &gs = *new RepGameState( );
        gs.player_name = "Tester";
        gs.camera.pos = glm::dvec3( 5.0, 70.0, 5.0 );
        gs.camera.eye_height = PLAYER_EYE_HEIGHT;

        chat_submit( gs, "hello world" );
        check_bool( "chat submit: SP pushes <name> text",
                    gs.chat.entries( ).size( ) == 1 && chat_format( gs.chat.entries( ).back( ) ) == "<Tester> hello world" );

        chat_submit( gs, "/help" );
        check_bool( "chat cmd: /help lists commands",
                    gs.chat.entries( ).size( ) >= 3 && gs.chat.entries( )[ 1 ].kind == CHAT_KIND_INFO &&
                        gs.chat.entries( )[ 1 ].text.find( "/give" ) != std::string::npos &&
                        gs.chat.entries( )[ 1 ].text.find( "/msg" ) != std::string::npos );

        const size_t base = gs.chat.entries( ).size( );
        chat_submit( gs, "/bogus" );
        check_bool( "chat cmd: unknown command errors",
                    gs.chat.entries( ).size( ) == base + 1 && gs.chat.entries( ).back( ).kind == CHAT_KIND_ERROR &&
                        gs.chat.entries( ).back( ).text.find( "Unknown command" ) != std::string::npos );

        chat_submit( gs, "/give" );
        check_bool( "chat cmd: usage error reports Usage:",
                    gs.chat.entries( ).back( ).kind == CHAT_KIND_ERROR &&
                        gs.chat.entries( ).back( ).text.find( "Usage:" ) != std::string::npos );

        // Relative coords land the feet at base + offset.
        gs.camera.pos = glm::dvec3( 10.0, 100.0 + PLAYER_EYE_HEIGHT, -20.0 );
        chat_submit( gs, "/tp ~ ~10 ~" );
        check_bool( "chat cmd: /tp ~ ~10 ~ keeps x/z, feet at +10",
                    gs.camera.pos.x == 10.0 && gs.camera.pos.z == -20.0 &&
                        fabs( gs.camera.pos.y - ( 110.0 + PLAYER_EYE_HEIGHT ) ) < 0.001 );
        chat_submit( gs, "/tp 1 64 -7" );
        check_bool( "chat cmd: /tp x y z lands the feet at y",
                    fabs( gs.camera.pos.x - 1.0 ) < 0.001 && fabs( gs.camera.pos.z - -7.0 ) < 0.001 &&
                        fabs( gs.camera.pos.y - ( 64.0 + PLAYER_EYE_HEIGHT ) ) < 0.001 &&
                        gs.camera.y_speed == 0.0 && gs.vitals.fall_dist == 0.0 );

        // Block coords floor fractional positions — feet at x=10.6 land
        // in block 10, not lround's 11.
        if ( block_definitions == nullptr ) {
            block_definitions_initilize_definitions( nullptr );
        }
        test_setup_world( gs.world );
        gs.camera.pos = glm::dvec3( 10.6, 8.0 + PLAYER_EYE_HEIGHT, 8.2 );
        gs.world.set_loaded_block( glm::ivec3( 10, 8, 8 ), BLOCK_STATE_AIR );
        chat_submit( gs, "/setblock ~ ~ ~ stone" );
        gs.tick_number++;
        repgame_drain_block_updates( gs );
        check_bool( "chat cmd: /setblock ~ ~ ~ floors the fractional feet",
                    gs.world.get_loaded_block( glm::ivec3( 10, 8, 8 ) ).id == STONE );

        chat_submit( gs, "/gamemode c" );
        check_bool( "chat cmd: /gamemode c -> creative", gs.game_mode == GameMode_Creative );

        gs.world_time = 5 * DAY_LENGTH_TICKS + DAY_LENGTH_TICKS / 4;
        chat_submit( gs, "/time set night" );
        check_bool( "chat cmd: /time set night keeps the day index",
                    gs.world_time == 5 * DAY_LENGTH_TICKS + 3 * DAY_LENGTH_TICKS / 4 );

        chat_submit( gs, "/name bad name!" );
        check_bool( "chat cmd: invalid /name errors + keeps the name",
                    gs.chat.entries( ).back( ).kind == CHAT_KIND_ERROR && gs.player_name == "Tester" );
        chat_submit( gs, "/name Alice_2" );
        check_bool( "chat cmd: /name sets the requested name", gs.player_name == "Alice_2" );

        test_setup_inventories( gs );
        chat_submit( gs, "/give stone 5" );
        check_bool( "chat cmd: /give stone 5 fills the hotbar", gs.hotbar.findSlotWithItem( STONE, false ) >= 0 );

        chat_submit( gs, "/clear" );
        check_bool( "chat cmd: /clear empties the log", gs.chat.entries( ).empty( ) );
        delete &gs;
    }
    return failures;
}
