// Save-format migration tests — split out of rep_tests.cpp.
#include "test_util.hpp"
// Pre-2047 save migration: 1249 was the empty-slot marker, and ids that are
// now undefined or past LAST_BLOCK_ID must load as empty slots / AIR.
int test_save_migration( ) {
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

    check_bool( "slot: sentinel 1249 loads empty", save_slot_id_is_empty( LEGACY_EMPTY_BLOCK_ID ) );
    uint32_t undefined_id = LAST_BLOCK_ID;
    for ( uint32_t i = 0; i < LAST_BLOCK_ID; i++ ) {
        if ( i != LEGACY_EMPTY_BLOCK_ID && !block_id_defined( i ) ) {
            undefined_id = i;
            break;
        }
    }
    check_bool( "slot: an undefined id exists below LAST_BLOCK_ID", undefined_id < LAST_BLOCK_ID );
    check_bool( "slot: undefined id loads empty", undefined_id < LAST_BLOCK_ID && save_slot_id_is_empty( undefined_id ) );
    check_bool( "slot: out-of-range id loads empty", save_slot_id_is_empty( LAST_BLOCK_ID + 42 ) );
    check_bool( "slot: real block survives", !save_slot_id_is_empty( GRASS ) );
    check_bool( "slot: pure item survives", !save_slot_id_is_empty( STICK ) );
    check_bool( "slot: undefined item id loads empty", save_slot_id_is_empty( ITEM_ID_END ) );
    check_bool( "chunk: sentinel 1249 loads AIR", save_chunk_block_id( LEGACY_EMPTY_BLOCK_ID ) == AIR );
    check_bool( "chunk: out-of-range id loads AIR", save_chunk_block_id( LAST_BLOCK_ID + 42 ) == AIR );
    check_bool( "chunk: real block survives", save_chunk_block_id( GRASS ) == GRASS );
    check_bool( "sentinel 1249 is not a defined block", !block_id_defined( LEGACY_EMPTY_BLOCK_ID ) );
    return failures;
}

// player.dat: the versioned text format round-trips every field exactly,
// tolerates unknown keys/malformed lines, and each frozen legacy binary
// layout still loads. The layout copies below are independent of the ones
// in player_data.cpp on purpose — if either drifts, the size dispatch
// stops matching and this test fails.
int test_player_data( ) {
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

    // --- text round trip ----------------------------------------------------
    PlayerData in;
    in.world_x = -591.5123456789123;
    in.world_y = 35.125;
    in.world_z = -407.5;
    in.angle_H = 135.5f;
    in.angle_V = -30.25f;
    in.flying = true;
    in.worldDrawQuality = 2;
    in.game_mode = GameMode_Survival;
    in.world_time = 123456789012345LL;
    in.render_distance = 8;
    in.min_ambient = 0.4f;
    in.selected_hotbar_slot = 5;
    in.hotbar_inventory[ 3 ].item_id = STICK;
    in.hotbar_inventory[ 3 ].quantity = 17;
    in.survival_inventory[ 20 ].item_id = APPLE;
    in.survival_inventory[ 20 ].quantity = 3;
    const std::string text = player_data_serialize( in );
    PlayerData out;
    check_bool( "player: text parses", player_data_parse( reinterpret_cast<const unsigned char *>( text.data( ) ), text.size( ), out ) );
    check_bool( "player: pos exact", out.world_x == in.world_x && out.world_y == in.world_y && out.world_z == in.world_z );
    check_bool( "player: angles exact", out.angle_H == in.angle_H && out.angle_V == in.angle_V );
    check_bool( "player: flags+quality+mode",
                out.flying == in.flying && out.no_clip == in.no_clip && out.worldDrawQuality == in.worldDrawQuality && out.game_mode == in.game_mode );
    check_bool( "player: time+rd+ambient", out.world_time == in.world_time && out.render_distance == in.render_distance && out.min_ambient == in.min_ambient );
    check_bool( "player: slots round trip",
                out.hotbar_inventory[ 3 ].item_id == STICK && out.hotbar_inventory[ 3 ].quantity == 17 &&
                    out.survival_inventory[ 20 ].item_id == APPLE && out.survival_inventory[ 20 ].quantity == 3 );
    check_bool( "player: empty slots stay empty", out.hotbar_inventory[ 0 ].item_id == LAST_BLOCK_ID && out.selected_hotbar_slot == 5 );

    // --- tolerance: comments, CRLF, unknown keys, malformed + oob lines -----
    const char *loose = "# RepGame player data\n"
                        "\n"
                        "format 1\r\n"
                        "pos 1.5 2.5 3.5\r\n"
                        "angles 10 20\n"
                        "future_key 99\n"
                        "hotbar 999 42 5\n"
                        "hotbar 0 x y\n";
    PlayerData loose_out;
    check_bool( "player: loose file parses",
                player_data_parse( reinterpret_cast<const unsigned char *>( loose ), strlen( loose ), loose_out ) );
    check_bool( "player: loose pos+angles", loose_out.world_x == 1.5 && loose_out.world_y == 2.5 && loose_out.world_z == 3.5 &&
                                              loose_out.angle_H == 10.0f && loose_out.angle_V == 20.0f );
    check_bool( "player: oob+malformed slots ignored", loose_out.hotbar_inventory[ 0 ].item_id == LAST_BLOCK_ID );

    // --- rejection: no pos, no format, garbage ------------------------------
    const char *no_pos = "format 1\nflying 1\n";
    PlayerData junk;
    check_bool( "player: format without pos rejected",
                !player_data_parse( reinterpret_cast<const unsigned char *>( no_pos ), strlen( no_pos ), junk ) );
    const char *no_format = "pos 1 2 3\n";
    check_bool( "player: text without format rejected",
                !player_data_parse( reinterpret_cast<const unsigned char *>( no_format ), strlen( no_format ), junk ) );
    const char *garbage = "garbage data here";
    check_bool( "player: garbage rejected",
                !player_data_parse( reinterpret_cast<const unsigned char *>( garbage ), strlen( garbage ), junk ) );

    // --- legacy binary layouts (independent copies of the frozen structs) ---
    struct __attribute__( ( packed ) ) LegacyBin {
        int reserved;
        double world_x, world_y, world_z;
        float angle_H, angle_V;
        bool flying, no_clip;
        int worldDrawQuality;
        InventorySlot hotbar[ HOTBAR_WIDTH * HOTBAR_HEIGHT ];
        int selected_hotbar_slot;
        GameMode game_mode;
        InventorySlot survival[ SURVIVAL_INVENTORY_WIDTH * SURVIVAL_INVENTORY_HEIGHT ];
        long world_time;
        int render_distance;
        float min_ambient;
    };
    LegacyBin v5 = { };
    v5.world_x = 10.5;
    v5.world_y = 40.25;
    v5.world_z = -20.5;
    v5.angle_H = 90.0f;
    v5.flying = true;
    v5.worldDrawQuality = 1;
    v5.hotbar[ 2 ].item_id = GRASS;
    v5.hotbar[ 2 ].quantity = 7;
    v5.selected_hotbar_slot = 2;
    v5.game_mode = GameMode_Survival;
    v5.survival[ 5 ].item_id = STICK;
    v5.survival[ 5 ].quantity = 2;
    v5.world_time = 987654;
    v5.render_distance = 12;
    v5.min_ambient = 0.25f;
    PlayerData b5;
    check_bool( "player: binary latest parses",
                player_data_parse( reinterpret_cast<const unsigned char *>( &v5 ), sizeof( v5 ), b5 ) );
    check_bool( "player: binary latest fields",
                b5.world_x == 10.5 && b5.world_y == 40.25 && b5.world_z == -20.5 && b5.flying && b5.world_time == 987654 &&
                    b5.render_distance == 12 && b5.min_ambient == 0.25f && b5.hotbar_inventory[ 2 ].quantity == 7 &&
                    b5.survival_inventory[ 5 ].item_id == STICK && b5.selected_hotbar_slot == 2 );

    struct __attribute__( ( packed ) ) LegacyBinNoAmbient {
        int reserved;
        double world_x, world_y, world_z;
        float angle_H, angle_V;
        bool flying, no_clip;
        int worldDrawQuality;
        InventorySlot hotbar[ HOTBAR_WIDTH * HOTBAR_HEIGHT ];
        int selected_hotbar_slot;
        GameMode game_mode;
        InventorySlot survival[ SURVIVAL_INVENTORY_WIDTH * SURVIVAL_INVENTORY_HEIGHT ];
        long world_time;
        int render_distance;
    };
    LegacyBinNoAmbient no_ambient = { };
    no_ambient.world_x = 1.0;
    no_ambient.world_time = 42;
    no_ambient.render_distance = 9;
    PlayerData bna;
    check_bool( "player: no_ambient parses",
                player_data_parse( reinterpret_cast<const unsigned char *>( &no_ambient ), sizeof( no_ambient ), bna ) );
    check_bool( "player: no_ambient defaults", bna.world_x == 1.0 && bna.world_time == 42 && bna.render_distance == 9 && bna.min_ambient < 0.0f );

    struct __attribute__( ( packed ) ) LegacyBinNoDistance {
        int reserved;
        double world_x, world_y, world_z;
        float angle_H, angle_V;
        bool flying, no_clip;
        int worldDrawQuality;
        InventorySlot hotbar[ HOTBAR_WIDTH * HOTBAR_HEIGHT ];
        int selected_hotbar_slot;
        GameMode game_mode;
        InventorySlot survival[ SURVIVAL_INVENTORY_WIDTH * SURVIVAL_INVENTORY_HEIGHT ];
        long world_time;
    };
    LegacyBinNoDistance no_dist = { };
    no_dist.world_x = 2.0;
    no_dist.world_time = 77;
    PlayerData bnd;
    check_bool( "player: no_distance parses",
                player_data_parse( reinterpret_cast<const unsigned char *>( &no_dist ), sizeof( no_dist ), bnd ) );
    check_bool( "player: no_distance defaults", bnd.world_x == 2.0 && bnd.world_time == 77 && bnd.render_distance == 0 && bnd.min_ambient < 0.0f );

    struct __attribute__( ( packed ) ) LegacyBinNoTime {
        int reserved;
        double world_x, world_y, world_z;
        float angle_H, angle_V;
        bool flying, no_clip;
        int worldDrawQuality;
        InventorySlot hotbar[ HOTBAR_WIDTH * HOTBAR_HEIGHT ];
        int selected_hotbar_slot;
        GameMode game_mode;
        InventorySlot survival[ SURVIVAL_INVENTORY_WIDTH * SURVIVAL_INVENTORY_HEIGHT ];
    };
    LegacyBinNoTime no_time = { };
    no_time.world_x = 3.0;
    PlayerData bnt;
    check_bool( "player: no_time parses",
                player_data_parse( reinterpret_cast<const unsigned char *>( &no_time ), sizeof( no_time ), bnt ) );
    check_bool( "player: no_time defaults", bnt.world_x == 3.0 && bnt.world_time == 0 );

    struct __attribute__( ( packed ) ) LegacyBinFloatPos {
        int reserved;
        float world_x, world_y, world_z;
        float angle_H, angle_V;
        bool flying, no_clip;
        int worldDrawQuality;
        InventorySlot hotbar[ HOTBAR_WIDTH * HOTBAR_HEIGHT ];
        int selected_hotbar_slot;
        GameMode game_mode;
        InventorySlot survival[ SURVIVAL_INVENTORY_WIDTH * SURVIVAL_INVENTORY_HEIGHT ];
    };
    LegacyBinFloatPos legacy = { };
    legacy.world_x = 4.5f;
    legacy.worldDrawQuality = 3;
    PlayerData bl;
    check_bool( "player: float-pos legacy parses",
                player_data_parse( reinterpret_cast<const unsigned char *>( &legacy ), sizeof( legacy ), bl ) );
    check_bool( "player: float-pos widened", bl.world_x == 4.5 && bl.worldDrawQuality == 3 && bl.world_time == 0 );

    // --- MapStorage file round trip -----------------------------------------
    {
        // Point the map root at a throwaway dir under /tmp (getRepGamePath
        // reads $HOME): no writes land in a real world.
        ScopedTempHome temp_home( "/tmp/repgame_test_player_home" );
        MapStorage storage;
        storage.init( "TestPlayerData" );
        storage.write_player_data( in );
        // The file on disk is text.
        char path[ 512 ];
        char *root = getRepGamePath( );
        snprintf( path, sizeof( path ), "%s%s%s", root, REPGAME_PATH_DIVIDOR "TestPlayerData", "/player.dat" );
        free( root );
        FILE *f = fopen( path, "rb" );
        check_bool( "player: file written", f != nullptr );
        if ( f != nullptr ) {
            char head[ 64 ] = { 0 };
            const size_t got = fread( head, 1, sizeof( head ) - 1, f );
            fclose( f );
            check_bool( "player: file is text", got > 0 && strstr( head, "format 1" ) != nullptr );
        }
        PlayerData rt;
        check_bool( "player: file round trips", storage.read_player_data( rt ) == 1 && rt.world_x == in.world_x && rt.world_time == in.world_time && rt.hotbar_inventory[ 3 ].quantity == 17 );

        // A hand-written file with an out-of-range item id must load the
        // slot as empty (sanitize step, same as binary saves).
        FILE *w = fopen( path, "wb" );
        fputs( "format 1\npos 1 2 3\nhotbar 0 99999 4\n", w );
        fclose( w );
        PlayerData bad;
        check_bool( "player: crafted file reads", storage.read_player_data( bad ) == 1 );
        check_bool( "player: invalid item id sanitized", bad.hotbar_inventory[ 0 ].item_id == LAST_BLOCK_ID && bad.hotbar_inventory[ 0 ].quantity == 0 );
    }
    return failures;
}

