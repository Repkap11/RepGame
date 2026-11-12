// Sign text tests — the shared SignText model (block_is_sign, sanitize),
// the editor's pure input function, chunk storage, the sign side file,
// and the SIGN_TEXT codec.
#include "test_util.hpp"

int test_signs( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };

    // --- block_is_sign / block_is_wall_sign / block_is_hanging_sign --------
    // The switch is keyed by id (the server has no def table); cross-check it
    // against the definition flags so a new sign wood fails here instead of
    // silently missing text support.
    {
        bool mismatch = false;
        int sign_count = 0;
        for ( int i = 0; i < LAST_BLOCK_ID && !mismatch; i++ ) {
            const BlockID id = static_cast<BlockID>( i );
            const Block *def = block_definition_get_definition( id );
            if ( def == nullptr ) {
                continue;
            }
            const bool want_sign = def->is_standing_sign || def->is_hanging_sign || def->standing_variant != LAST_BLOCK_ID;
            if ( block_is_sign( id ) != want_sign ) {
                pr_test( "    block_is_sign mismatch id=%d", i );
                mismatch = true;
            }
            if ( block_is_sign( id ) ) {
                sign_count++;
                const bool want_wall = !def->is_standing_sign && !def->is_hanging_sign;
                if ( block_is_wall_sign( id ) != want_wall ) {
                    pr_test( "    block_is_wall_sign mismatch id=%d", i );
                    mismatch = true;
                }
                if ( block_is_hanging_sign( id ) != def->is_hanging_sign ) {
                    pr_test( "    block_is_hanging_sign mismatch id=%d", i );
                    mismatch = true;
                }
            }
        }
        check_bool( "sign: block_is_sign matches the def table", !mismatch );
        check_bool( "sign: all wood variants covered", sign_count >= 39 );
        check_bool( "sign: base ids classified",
                    block_is_sign( STANDING_SIGN ) && !block_is_wall_sign( STANDING_SIGN ) && block_is_sign( WALL_SIGN ) &&
                        block_is_wall_sign( WALL_SIGN ) );
        check_bool( "sign: hanging ids classified",
                    block_is_sign( OAK_HANGING_SIGN ) && block_is_hanging_sign( OAK_HANGING_SIGN ) && !block_is_wall_sign( OAK_HANGING_SIGN ) );
        check_bool( "sign: non-signs rejected", !block_is_sign( STONE ) && !block_is_sign( AIR ) );
    }

    // --- sign_sanitize -------------------------------------------------------
    {
        std::string clean = "Hello, World!";
        sign_sanitize_line( clean );
        check_bool( "sign: printable ASCII untouched", clean == "Hello, World!" );

        std::string control = "a\tb\nc\x01""d";
        sign_sanitize_line( control );
        check_bool( "sign: control chars stripped", control == "abcd" );

        std::string utf8 = "caf\xc3\xa9"; // é is two bytes, both >= 128
        sign_sanitize_line( utf8 );
        check_bool( "sign: UTF-8 multibyte stripped", utf8 == "caf" );

        std::string long_line( SIGN_LINE_MAX + 10, 'x' );
        sign_sanitize_line( long_line );
        check_bool( "sign: line truncated to SIGN_LINE_MAX", long_line.size() == SIGN_LINE_MAX );

        SignText text;
        text.lines[ 0 ] = "ok";
        text.lines[ 1 ] = "bad\xff""byte";
        text.lines[ 2 ] = std::string( 30, 'y' );
        sign_sanitize( text );
        check_bool( "sign: sanitize covers all lines",
                    text.lines[ 0 ] == "ok" && text.lines[ 1 ] == "badbyte" && text.lines[ 2 ].size( ) == SIGN_LINE_MAX );
        check_bool( "sign: empty() detects no text", SignText{ }.empty( ) && !text.empty( ) );
    }

    // --- sign_edit_apply_input ----------------------------------------------
    {
        Input in{ };
        SignEditUi edit;
        // Chars land on the focused line; Enter advances.
        strcpy( in.text_chars, "HI" );
        in.text_chars_len = 2;
        check_bool( "sign edit: typing stays open", !sign_edit_apply_input( edit, in ) );
        in.text_chars_len = 0;
        check_bool( "sign edit: chars on line 0", edit.text.lines[ 0 ] == "HI" && edit.line == 0 );
        in.text_enters = 1;
        sign_edit_apply_input( edit, in );
        in.text_enters = 0;
        check_bool( "sign edit: Enter advances line", edit.line == 1 );
        // Up/Down navigate, clamped at the edges.
        in.text_ups = 5;
        sign_edit_apply_input( edit, in );
        in.text_ups = 0;
        check_bool( "sign edit: Up clamps at line 0", edit.line == 0 );
        in.text_downs = 2;
        sign_edit_apply_input( edit, in );
        in.text_downs = 0;
        check_bool( "sign edit: Down moves to line 2", edit.line == 2 );
        // Backspace pops a char, then climbs a line on an empty line.
        strcpy( in.text_chars, "ab" );
        in.text_chars_len = 2;
        sign_edit_apply_input( edit, in );
        in.text_chars_len = 0;
        in.text_backspaces = 3; // pops 'b', pops 'a', climbs to line 1
        sign_edit_apply_input( edit, in );
        in.text_backspaces = 0;
        check_bool( "sign edit: backspace pops then climbs", edit.text.lines[ 2 ].empty( ) && edit.line == 1 );
        // Overlong lines refuse extra chars.
        edit.text.lines[ 1 ] = std::string( SIGN_LINE_MAX, 'z' );
        strcpy( in.text_chars, "x" );
        in.text_chars_len = 1;
        sign_edit_apply_input( edit, in );
        in.text_chars_len = 0;
        check_bool( "sign edit: line cap enforced", edit.text.lines[ 1 ].size( ) == SIGN_LINE_MAX );
        // Non-printable input is ignored.
        in.text_chars[ 0 ] = '\x07';
        in.text_chars_len = 1;
        sign_edit_apply_input( edit, in );
        in.text_chars_len = 0;
        check_bool( "sign edit: control chars ignored", edit.text.lines[ 1 ].size( ) == SIGN_LINE_MAX );
        // Enter on the last line commits; Escape commits from anywhere.
        edit.line = SIGN_LINES - 1;
        in.text_enters = 1;
        check_bool( "sign edit: Enter on last line closes", sign_edit_apply_input( edit, in ) );
        in.text_enters = 0;
        edit.line = 0;
        in.text_escape = true;
        check_bool( "sign edit: Escape closes", sign_edit_apply_input( edit, in ) );
    }

    // --- Chunk/World storage --------------------------------------------------
    {
        World world;
        test_setup_world( world );
        const glm::ivec3 sign_pos( 4, 5, 4 );
        const glm::ivec3 wall_pos( 8, 5, 4 );
        world.set_loaded_block( sign_pos, { STANDING_SIGN, 0, 0, STANDING_SIGN, 0 } );
        world.set_loaded_block( wall_pos, { WALL_SIGN, 0, 0, WALL_SIGN, 0 } );

        SignText text;
        text.lines[ 0 ] = "HELLO";
        text.lines[ 3 ] = "WORLD";
        check_bool( "sign store: write lands", world.set_sign_text( sign_pos, text ) );
        const SignText *got = world.get_sign_text( sign_pos );
        check_bool( "sign store: read back", got != nullptr && got->lines[ 0 ] == "HELLO" && got->lines[ 3 ] == "WORLD" &&
                                             got->lines[ 1 ].empty( ) && got->lines[ 2 ].empty( ) );

        check_bool( "sign store: wall sign stores", world.set_sign_text( wall_pos, text ) );
        check_bool( "sign store: non-sign rejected", !world.set_sign_text( glm::ivec3( 0, 0, 0 ), text ) );
        check_bool( "sign store: unloaded rejected", !world.set_sign_text( glm::ivec3( 500, 5, 500 ), text ) );

        // Empty text erases the record rather than storing blanks.
        check_bool( "sign store: empty write erases", world.set_sign_text( wall_pos, SignText{ } ) );
        check_bool( "sign store: record gone", world.get_sign_text( wall_pos ) == nullptr );

        // Overwriting the sign cell drops its text.
        world.set_loaded_block( sign_pos, BLOCK_STATE_AIR );
        check_bool( "sign store: break clears text", world.get_sign_text( sign_pos ) == nullptr );

        // collect_signs_near finds stored signs and skips stale/far ones.
        world.set_loaded_block( sign_pos, { STANDING_SIGN, 0, 0, STANDING_SIGN, 0 } );
        world.set_sign_text( sign_pos, text );
        world.set_loaded_block( glm::ivec3( 1, 5, 1 ), { STANDING_SIGN, 0, 0, STANDING_SIGN, 0 } );
        std::vector<WorldSign> near;
        world.collect_signs_near( glm::ivec3( 4, 5, 4 ), SIGN_TEXT_DRAW_RADIUS, near );
        check_bool( "sign store: collect finds text sign", near.size( ) == 1 && near[ 0 ].pos == sign_pos );
    }

    // --- MapStorage sign side file --------------------------------------------
    {
        // $HOME under /tmp so the sign files land in a throwaway world.
        ScopedTempHome temp_home( "/tmp/repgame_test_sign_home" );
        MapStorage storage;
        storage.init( "TestSigns" );
        const glm::ivec3 chunk_off( 0, 0, 0 );
        SignTextMap signs;
        SignText text;
        text.lines[ 0 ] = "HELLO";
        text.lines[ 2 ] = "MIDDLE";
        signs[ glm::ivec3( 4, 5, 4 ) ] = text;
        SignText other;
        other.lines[ 1 ] = "OTHER";
        signs[ glm::ivec3( 9, 1, 15 ) ] = other;
        storage.persist_signs( chunk_off, signs );

        SignTextMap loaded;
        check_bool( "sign file: records load", storage.load_signs( chunk_off, loaded ) == 1 && loaded.size( ) == 2 );
        check_bool( "sign file: text round trips",
                    loaded.count( glm::ivec3( 4, 5, 4 ) ) == 1 && loaded[ glm::ivec3( 4, 5, 4 ) ].lines[ 0 ] == "HELLO" &&
                        loaded[ glm::ivec3( 4, 5, 4 ) ].lines[ 2 ] == "MIDDLE" &&
                        loaded[ glm::ivec3( 9, 1, 15 ) ].lines[ 1 ] == "OTHER" );

        // Persisting an empty map removes the file.
        storage.persist_signs( chunk_off, SignTextMap{ } );
        SignTextMap empty_load;
        check_bool( "sign file: empty persist deletes", storage.load_signs( chunk_off, empty_load ) == 0 );
    }

    // --- SIGN_TEXT codec -------------------------------------------------------
    {
        NetSignTextPayload p;
        p.x = 4;
        p.y = 5;
        p.z = -9;
        p.text.lines[ 0 ] = "HELLO";
        p.text.lines[ 3 ] = "WORLD";
        PacketWriter w;
        net_serialize_sign_text( w, p );
        NetSignTextPayload q;
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        check_bool( "sign codec: round trips",
                    net_deserialize_sign_text( r, q ) && q.x == 4 && q.y == 5 && q.z == -9 && q.text.lines[ 0 ] == "HELLO" &&
                        q.text.lines[ 3 ] == "WORLD" && q.text.lines[ 1 ].empty( ) );

        // An over-long line field is rejected before it can allocate/overflow.
        PacketWriter bad;
        bad.write_i32( 0 );
        bad.write_i32( 0 );
        bad.write_i32( 0 );
        bad.write_u8( SIGN_LINE_MAX + 1 );
        char junk[ 64 ] = { 0 };
        bad.write_bytes( junk, sizeof( junk ) );
        NetSignTextPayload evil;
        PacketReader br( bad.buf( ).data( ), bad.buf( ).size( ) );
        check_bool( "sign codec: oversized line rejected", !net_deserialize_sign_text( br, evil ) );
    }
    return failures;
}
