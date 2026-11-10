#include "common/harness/script.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk_loader.hpp"
#include "common/game/game_state.hpp"
#include "common/harness/debug_commands.hpp"
#include "common/harness/results.hpp"
#include "common/harness/run_policy.hpp"
#include "common/item.hpp"
#include "common/Logging.hpp"
#include "common/mobs.hpp"

// --- Script state -------------------------------------------------------------
// Lines keep their 1-based source number so asserts/errors cite the file.
static std::vector<std::string> g_lines;
static std::vector<int> g_lineno;
static size_t g_pc = 0;
static bool g_loaded = false;
static bool g_done = false;

// Blocking-command state for the current line.
static long g_wait_until = -1;
static long g_idle_started = -1;
static int g_idle_streak = 0;
static long g_idle_streak_req = 20;
static long g_idle_timeout = 6000;
static bool g_shot_requested = false;

std::vector<std::string> script::tokenize( const std::string &line ) {
    std::vector<std::string> out;
    size_t i = 0;
    while ( i < line.size( ) ) {
        while ( i < line.size() && isspace( line[ i ] ) ) {
            i++;
        }
        size_t start = i;
        while ( i < line.size() && !isspace( line[ i ] ) ) {
            i++;
        }
        if ( i > start ) {
            out.push_back( line.substr( start, i - start ) );
        }
    }
    return out;
}

bool script::eval_cmp( const char *op, const double actual, const double expected ) {
    if ( strcmp( op, "==" ) == 0 ) {
        return actual == expected;
    }
    if ( strcmp( op, "!=" ) == 0 ) {
        return actual != expected;
    }
    if ( strcmp( op, "<" ) == 0 ) {
        return actual < expected;
    }
    if ( strcmp( op, "<=" ) == 0 ) {
        return actual <= expected;
    }
    if ( strcmp( op, ">" ) == 0 ) {
        return actual > expected;
    }
    if ( strcmp( op, ">=" ) == 0 ) {
        return actual >= expected;
    }
    return false;
}

static void load_script( const RepGameState &state, const char *path ) {
    g_loaded = true;
    FILE *f = fopen( path, "r" );
    if ( f == nullptr ) {
        pr_debug( "REPGAME_SCRIPT: cannot open %s", path );
        results::assert_( state, 0, "script", false, "readable file", path );
        g_done = true;
        return;
    }
    char buf[ 1024 ];
    int lineno = 0;
    while ( fgets( buf, sizeof( buf ), f ) != nullptr ) {
        lineno++;
        buf[ strcspn( buf, "\r\n" ) ] = '\0';
        const char *s = buf;
        while ( isspace( *s ) ) {
            s++;
        }
        // # comments and #! header lines (the Python side reads those).
        if ( *s == '\0' || *s == '#' ) {
            continue;
        }
        g_lines.push_back( s );
        g_lineno.push_back( lineno );
    }
    fclose( f );
}

// Event + failed assert for unknown commands / bad args so scripts fail loud.
static void cmd_error( const RepGameState &state, const int line, const char *text ) {
    results::event( state.tick_number, "cmd_error", "\"line\":%d,\"text\":\"%s\"", line, text );
    results::assert_( state, line, text, false, "valid command", text );
}

// Current line number for asserts/events.
static int cur_line( ) {
    return g_pc < g_lineno.size( ) ? g_lineno[ g_pc ] : -1;
}

// true = the current line is still blocked and resumes next tick.
static bool blocked_wait( const RepGameState &state, const long ticks ) {
    if ( g_wait_until < 0 ) {
        g_wait_until = state.tick_number + ticks;
    }
    return state.tick_number < g_wait_until;
}

static bool blocked_wait_idle( const RepGameState &state, const ChunkLoader &loader, const long streak_req, const long timeout ) {
    if ( g_idle_started < 0 ) {
        g_idle_started = state.tick_number;
        g_idle_streak = 0;
        g_idle_streak_req = streak_req > 0 ? streak_req : 20;
        g_idle_timeout = timeout > 0 ? timeout : 6000;
    }
    const bool idle = dbgcmd::loader_idle( loader );
    g_idle_streak = idle ? g_idle_streak + 1 : 0;
    if ( g_idle_streak >= g_idle_streak_req ) {
        return false;
    }
    if ( state.tick_number - g_idle_started >= g_idle_timeout ) {
        results::event( state.tick_number, "wait_timeout", "\"line\":%d", cur_line( ) );
        results::assert_( state, cur_line( ), "wait_idle", false, "idle within timeout", "still busy" );
        return false;
    }
    return true;
}

static void clear_block_state( ) {
    g_shot_requested = false;
    g_wait_until = -1;
    g_idle_started = -1;
    g_idle_streak = 0;
}

static void script_done( RepGameState &state ) {
    if ( g_done ) {
        return;
    }
    g_done = true;
    results::note_exit_reason( "script_done" );
    run_policy( ).script.pin_camera = false;
    if ( !run_policy( ).script.keep ) {
        state.input.exitGame = true;
    }
}

// Returns true when the line blocked (retry next tick), false when consumed.
static bool exec_line( RepGameState &state, ChunkLoader &loader, const std::vector<std::string> &argv ) {
    const char *cmd = argv[ 0 ].c_str( );
    const size_t argc = argv.size( );
    const int line = cur_line( );
    std::string err;

    auto ivec_arg = [&]( int i ) { return atoi( argv[ i ].c_str( ) ); };
    auto farg = [&]( int i ) { return atof( argv[ i ].c_str( ) ); };

    if ( strcmp( cmd, "wait" ) == 0 && argc == 2 ) {
        return blocked_wait( state, atol( argv[ 1 ].c_str( ) ) );
    }
    if ( strcmp( cmd, "wait_idle" ) == 0 && argc <= 3 ) {
        const long streak = argc >= 2 ? atol( argv[ 1 ].c_str( ) ) : 20;
        const long timeout = argc >= 3 ? atol( argv[ 2 ].c_str( ) ) : 6000;
        return blocked_wait_idle( state, loader, streak, timeout );
    }
    if ( strcmp( cmd, "camera" ) == 0 && ( argc == 6 || ( argc == 2 && strcmp( argv[ 1 ].c_str( ), "release" ) == 0 ) ) ) {
        if ( argc == 2 ) {
            run_policy( ).script.pin_camera = false;
        } else {
            ScriptState &sc = run_policy( ).script;
            sc.pin_camera = true;
            sc.cam_x = farg( 1 );
            sc.cam_y = farg( 2 );
            sc.cam_z = farg( 3 );
            sc.cam_yaw = static_cast<float>( farg( 4 ) );
            sc.cam_pitch = static_cast<float>( farg( 5 ) );
        }
        return false;
    }
    if ( strcmp( cmd, "release" ) == 0 && argc == 1 ) {
        run_policy( ).script.pin_camera = false;
        return false;
    }
    if ( strcmp( cmd, "screenshot" ) == 0 && argc == 2 ) {
        if ( !g_shot_requested ) {
            g_shot_requested = true;
            run_policy( ).script.shot_name = argv[ 1 ];
            run_policy( ).script.shot_pending = true;
        }
        return run_policy( ).script.shot_pending; // cleared in draw_frame
    }
    if ( strcmp( cmd, "quality" ) == 0 && argc == 2 ) {
        const char *t = argv[ 1 ].c_str( );
        int q = -1;
        if ( strcasecmp( t, "low" ) == 0 || strcmp( t, "0" ) == 0 ) {
            q = LOW;
        } else if ( strcasecmp( t, "medium" ) == 0 || strcmp( t, "1" ) == 0 ) {
            q = MEDIUM;
        } else if ( strcasecmp( t, "high" ) == 0 || strcmp( t, "2" ) == 0 ) {
            q = HIGH;
        } else if ( strcasecmp( t, "x_high" ) == 0 || strcasecmp( t, "xhigh" ) == 0 || strcmp( t, "3" ) == 0 ) {
            q = X_HIGH;
        }
        if ( q < 0 ) {
            cmd_error( state, line, "quality: want low|medium|high|x_high" );
        } else {
            state.input.worldDrawQuality = static_cast<WorldDrawQuality>( q );
        }
        return false;
    }
    if ( strcmp( cmd, "flags" ) == 0 && argc == 2 ) {
        state.world.set_chunk_debug_flags( atoi( argv[ 1 ].c_str( ) ) );
        return false;
    }
    if ( strcmp( cmd, "rd" ) == 0 && argc == 2 ) {
        state.input.render_distance = atoi( argv[ 1 ].c_str( ) );
        return false;
    }
    if ( strcmp( cmd, "time" ) == 0 && argc == 2 ) {
        dbgcmd::set_time( state, atol( argv[ 1 ].c_str( ) ) );
        return false;
    }
    if ( strcmp( cmd, "mode" ) == 0 && argc == 2 ) {
        if ( !dbgcmd::mode( state, argv[ 1 ].c_str( ), nullptr, &err ) ) {
            cmd_error( state, line, err.c_str( ) );
        }
        return false;
    }
    if ( strcmp( cmd, "peaceful" ) == 0 && argc == 2 ) {
        if ( !dbgcmd::peaceful( state, argv[ 1 ].c_str( ), &err ) ) {
            cmd_error( state, line, err.c_str( ) );
        }
        return false;
    }
    if ( strcmp( cmd, "fly" ) == 0 && argc == 2 ) {
        if ( !dbgcmd::fly( state, argv[ 1 ].c_str( ), &err ) ) {
            cmd_error( state, line, err.c_str( ) );
        }
        return false;
    }
    if ( strcmp( cmd, "setblock" ) == 0 && ( argc == 5 || argc == 6 ) ) {
        const unsigned char rotation = argc == 6 ? static_cast<unsigned char>( ivec_arg( 5 ) ) : 0;
        if ( !dbgcmd::setblock( state, glm::ivec3( ivec_arg( 1 ), ivec_arg( 2 ), ivec_arg( 3 ) ), argv[ 4 ].c_str( ), &err, rotation ) ) {
            cmd_error( state, line, err.c_str( ) );
        }
        return false;
    }
    if ( strcmp( cmd, "fill" ) == 0 && argc == 8 ) {
        if ( !dbgcmd::fill( state, glm::ivec3( ivec_arg( 1 ), ivec_arg( 2 ), ivec_arg( 3 ) ),
                            glm::ivec3( ivec_arg( 4 ), ivec_arg( 5 ), ivec_arg( 6 ) ), argv[ 7 ].c_str( ), &err ) ) {
            cmd_error( state, line, err.c_str( ) );
        }
        return false;
    }
    if ( strcmp( cmd, "goto" ) == 0 && argc == 4 ) {
        dbgcmd::goto_( state, glm::dvec3( farg( 1 ), farg( 2 ), farg( 3 ) ) );
        return false;
    }
    if ( strcmp( cmd, "drop" ) == 0 && ( argc == 6 || argc == 9 ) ) {
        const glm::dvec3 pos( farg( 3 ), farg( 4 ), farg( 5 ) );
        const glm::dvec3 vel = argc == 9 ? glm::dvec3( farg( 6 ), farg( 7 ), farg( 8 ) ) : glm::dvec3( 0.0 );
        if ( !dbgcmd::drop( state, argv[ 1 ].c_str( ), ivec_arg( 2 ), pos, vel, &err ) ) {
            cmd_error( state, line, err.c_str( ) );
        }
        return false;
    }
    if ( strcmp( cmd, "give" ) == 0 && ( argc == 2 || argc == 3 ) ) {
        if ( !dbgcmd::give( state, argv[ 1 ].c_str( ), argc == 3 ? ivec_arg( 2 ) : 1, &err ) ) {
            cmd_error( state, line, err.c_str( ) );
        }
        return false;
    }
    if ( strcmp( cmd, "slot" ) == 0 && argc == 2 ) {
        if ( !dbgcmd::slot( state, ivec_arg( 1 ), &err ) ) {
            cmd_error( state, line, err.c_str( ) );
        }
        return false;
    }
    if ( strcmp( cmd, "mob" ) == 0 && argc == 5 ) {
        if ( !dbgcmd::mob( state, argv[ 1 ].c_str( ), glm::dvec3( farg( 2 ), farg( 3 ), farg( 4 ) ), &err ) ) {
            cmd_error( state, line, err.c_str( ) );
        }
        return false;
    }
    if ( strcmp( cmd, "feed" ) == 0 && ( argc == 4 || argc == 5 ) ) {
        if ( !dbgcmd::feed( state, glm::dvec3( farg( 1 ), farg( 2 ), farg( 3 ) ), argc == 5 ? argv[ 4 ].c_str( ) : nullptr, &err ) ) {
            cmd_error( state, line, err.c_str( ) );
        }
        return false;
    }
    if ( strcmp( cmd, "shear" ) == 0 && argc == 4 ) {
        if ( !dbgcmd::shear( state, glm::dvec3( farg( 1 ), farg( 2 ), farg( 3 ) ), &err ) ) {
            cmd_error( state, line, err.c_str( ) );
        }
        return false;
    }
    if ( strcmp( cmd, "fall" ) == 0 && argc == 5 ) {
        if ( !dbgcmd::fall( state, argv[ 1 ].c_str( ), glm::ivec3( ivec_arg( 2 ), ivec_arg( 3 ), ivec_arg( 4 ) ), &err ) ) {
            cmd_error( state, line, err.c_str( ) );
        }
        return false;
    }
    if ( strcmp( cmd, "tnt" ) == 0 && argc == 4 ) {
        dbgcmd::tnt( state, glm::vec3( farg( 1 ), farg( 2 ), farg( 3 ) ), &err );
        return false;
    }
    if ( strcmp( cmd, "shoot" ) == 0 && argc == 7 ) {
        if ( !dbgcmd::shoot( state, glm::dvec3( farg( 1 ), farg( 2 ), farg( 3 ) ), glm::dvec3( farg( 4 ), farg( 5 ), farg( 6 ) ), &err ) ) {
            cmd_error( state, line, err.c_str( ) );
        }
        return false;
    }
    if ( ( strcmp( cmd, "probe" ) == 0 || strcmp( cmd, "probe_light" ) == 0 ) && argc == 4 ) {
        results::probe( state, loader, glm::ivec3( ivec_arg( 1 ), ivec_arg( 2 ), ivec_arg( 3 ) ) );
        return false;
    }
    if ( strcmp( cmd, "assert_block" ) == 0 && argc == 5 ) {
        const glm::ivec3 pos( ivec_arg( 1 ), ivec_arg( 2 ), ivec_arg( 3 ) );
        const BlockState bs = state.world.get_loaded_block( pos );
        char actual[ 64 ];
        bool ok;
        if ( bs.id == LAST_BLOCK_ID ) {
            strcpy( actual, "unloaded" );
            ok = false;
        } else {
            const char *n = block_definition_get_name( bs.id );
            snprintf( actual, sizeof( actual ), "%s", n != nullptr ? n : "?" );
            const BlockID want = dbgcmd::block_from_name( argv[ 4 ].c_str( ) );
            ok = want != LAST_BLOCK_ID && bs.id == want;
            if ( want == LAST_BLOCK_ID ) {
                ok = n != nullptr && strcasecmp( n, argv[ 4 ].c_str( ) ) == 0;
            }
        }
        results::assert_( state, line, argv[ 0 ].c_str( ), ok, argv[ 4 ].c_str( ), actual );
        return false;
    }
    if ( strcmp( cmd, "assert_light" ) == 0 && argc == 7 ) {
        const glm::ivec3 pos( ivec_arg( 1 ), ivec_arg( 2 ), ivec_arg( 3 ) );
        const char *chan = argv[ 4 ].c_str( );
        const int channel = strcasecmp( chan, "sky" ) == 0 ? LIGHT_CHANNEL_SKY
                          : strcasecmp( chan, "block" ) == 0 ? LIGHT_CHANNEL_BLOCK
                                                             : -1;
        if ( channel < 0 ) {
            cmd_error( state, line, "assert_light: want sky|block" );
            return false;
        }
        const double expected = atof( argv[ 6 ].c_str( ) );
        const int level = loader.light_probe( pos, channel );
        char actual[ 32 ];
        const BlockState bs = state.world.get_loaded_block( pos );
        if ( bs.id == LAST_BLOCK_ID ) {
            strcpy( actual, "unloaded" );
        } else {
            snprintf( actual, sizeof( actual ), "%d", level );
        }
        char want[ 48 ];
        snprintf( want, sizeof( want ), "%s %s", argv[ 5 ].c_str( ), argv[ 6 ].c_str( ) );
        const bool ok = bs.id != LAST_BLOCK_ID && script::eval_cmp( argv[ 5 ].c_str( ), level, expected );
        results::assert_( state, line, argv[ 0 ].c_str( ), ok, want, actual );
        return false;
    }
    // cset kind x y z slot item count — optimistic container write (works
    // offline: containers.set_slot no-ops without a connection).
    if ( strcmp( cmd, "cset" ) == 0 && argc == 8 ) {
        if ( !dbgcmd::cset( state, dbgcmd::container_from_name( argv[ 1 ].c_str( ) ),
                            glm::ivec3( ivec_arg( 2 ), ivec_arg( 3 ), ivec_arg( 4 ) ), ivec_arg( 5 ), argv[ 6 ].c_str( ),
                            ivec_arg( 7 ), &err ) ) {
            cmd_error( state, line, err.c_str( ) );
        }
        return false;
    }
    // assert_cslot kind x y z slot item [count] — the world-owned contents
    // at the slot matches (count defaults to 1).
    if ( strcmp( cmd, "assert_cslot" ) == 0 && ( argc == 7 || argc == 8 ) ) {
        const NetContainerKindDef *def = dbgcmd::container_from_name( argv[ 1 ].c_str( ) );
        const glm::ivec3 pos( ivec_arg( 2 ), ivec_arg( 3 ), ivec_arg( 4 ) );
        ContainerContents cc;
        bool is_container = false;
        char actual[ 96 ];
        bool ok = false;
        if ( def == nullptr || !dbgcmd::cprobe( state, def, pos, &cc, &is_container, &err ) ) {
            snprintf( actual, sizeof( actual ), "%s", def == nullptr ? "unknown kind" : err.c_str( ) );
        } else if ( !is_container ) {
            snprintf( actual, sizeof( actual ), "not a %s", def->name );
        } else {
            const int slot = ivec_arg( 5 );
            const ItemID want_item = slot >= 0 && slot < def->slots ? dbgcmd::item_from_name( argv[ 6 ].c_str( ) ) : static_cast<ItemID>( LAST_BLOCK_ID );
            const int want_count = argc == 8 ? ivec_arg( 7 ) : 1;
            if ( slot < 0 || slot >= def->slots || want_item == LAST_BLOCK_ID ) {
                snprintf( actual, sizeof( actual ), "bad slot or item" );
            } else {
                const InventorySlot &got = cc.slots[ slot ];
                const char *n = item_name( got.item_id );
                snprintf( actual, sizeof( actual ), "%s x%d", n != nullptr ? n : "?", got.quantity );
                ok = got.item_id == want_item && got.quantity == want_count;
            }
        }
        char want[ 96 ];
        snprintf( want, sizeof( want ), "%s x%d@%d", argv[ 6 ].c_str( ), argc == 8 ? ivec_arg( 7 ) : 1, ivec_arg( 5 ) );
        results::assert_( state, line, argv[ 0 ].c_str( ), ok, want, actual );
        return false;
    }
    if ( strcmp( cmd, "assert_mob_count" ) == 0 && ( argc == 3 || argc == 4 ) ) {
        const double expected = atof( argv[ 2 ].c_str( ) );
        const char *type = argc == 4 ? argv[ 3 ].c_str( ) : nullptr;
        const MobType want = type != nullptr ? dbgcmd::mob_from_name( type ) : MOB_TYPE_COUNT;
        if ( type != nullptr && want == MOB_TYPE_COUNT ) {
            cmd_error( state, line, "assert_mob_count: unknown mob type" );
            return false;
        }
        const auto &mobs = state.world.mobs.debug_mobs( );
        const int count = static_cast<int>( std::count_if( mobs.begin( ), mobs.end( ),
                                                           [want]( const auto &m ) { return want == MOB_TYPE_COUNT || m.type == want; } ) );
        char want_str[ 48 ], actual[ 32 ];
        snprintf( want_str, sizeof( want_str ), "%s %s", argv[ 1 ].c_str( ), argv[ 2 ].c_str( ) );
        snprintf( actual, sizeof( actual ), "%d", count );
        results::assert_( state, line, argv[ 0 ].c_str( ), script::eval_cmp( argv[ 1 ].c_str( ), count, expected ), want_str, actual );
        return false;
    }
    if ( strcmp( cmd, "echo" ) == 0 ) {
        std::string text;
        for ( size_t i = 1; i < argc; i++ ) {
            text += i == 1 ? "" : " ";
            text += argv[ i ];
        }
        results::event( state.tick_number, "echo", "\"text\":\"%s\"", text.c_str( ) );
        return false;
    }
    if ( strcmp( cmd, "dump" ) == 0 ) {
        LoaderDebugStats s;
        loader.debug_loader_stats( &s );
        char dbg[ 1024 ];
        loader.light_dbg_stats( dbg, sizeof( dbg ) );
        results::metric( state, s, dbg );
        return false;
    }
    if ( strcmp( cmd, "quit" ) == 0 ) {
        script_done( state );
        return false;
    }
    char bad[ 160 ];
    snprintf( bad, sizeof( bad ), "%s", cmd );
    cmd_error( state, line, bad );
    return false;
}

void script::tick( RepGameState &state, ChunkLoader &loader ) {
    const ScriptState &sc = run_policy( ).script;
    if ( !sc.enabled || g_done ) {
        return;
    }
    if ( !g_loaded ) {
        load_script( state, sc.path );
    }
    while ( g_pc < g_lines.size( ) ) {
        const std::vector<std::string> argv = script::tokenize( g_lines[ g_pc ] );
        if ( argv.empty( ) || exec_line( state, loader, argv ) ) {
            if ( argv.empty( ) ) {
                g_pc++;
                continue;
            }
            return; // blocked — retry this line next tick
        }
        clear_block_state( );
        g_pc++;
    }
    script_done( state );
}
