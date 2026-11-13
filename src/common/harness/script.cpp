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
#include "common/game/game_internal.hpp"
#include "common/harness/debug_commands.hpp"
#include "common/harness/results.hpp"
#include "common/harness/run_policy.hpp"
#include "common/item.hpp"
#include "common/sign.hpp"
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
    if ( strcmp( cmd, "damage" ) == 0 && argc == 2 ) {
        const bool applied = RepGame::player_damage( state, static_cast<float>( farg( 1 ) ) );
        results::event( state.tick_number, "damage_result", "\"applied\":%d,\"health\":%.3f,\"dead\":%d",
                        applied ? 1 : 0, state.vitals.health, state.vitals.dead ? 1 : 0 );
        return false;
    }
    if ( strcmp( cmd, "eat" ) == 0 && argc == 1 ) {
        const bool applied = repgame_eat_food( state );
        results::event( state.tick_number, "eat_result", "\"applied\":%d,\"health\":%.3f", applied ? 1 : 0, state.vitals.health );
        return false;
    }
    if ( strcmp( cmd, "respawn" ) == 0 && argc == 1 ) {
        repgame_respawn( state );
        results::event( state.tick_number, "respawn_result", "\"health\":%.3f", state.vitals.health );
        return false;
    }
    if ( strcmp( cmd, "sleep" ) == 0 && argc == 4 ) {
        const glm::ivec3 pos( ivec_arg( 1 ), ivec_arg( 2 ), ivec_arg( 3 ) );
        const bool slept = repgame_try_sleep( state, pos );
        results::event( state.tick_number, "sleep_result", "\"slept\":%d,\"time\":%lld", slept ? 1 : 0, static_cast<long long>( state.world_time ) );
        return false;
    }
    if ( strcmp( cmd, "inventory" ) == 0 && argc == 2 ) {
        state.input.inventory_open = atoi( argv[ 1 ].c_str( ) ) != 0;
        return false;
    }
    if ( strcmp( cmd, "debug" ) == 0 && argc == 2 ) {
        state.input.debug_mode = atoi( argv[ 1 ].c_str( ) ) != 0;
        return false;
    }
    if ( strcmp( cmd, "imgui" ) == 0 && argc == 2 ) {
        state.ui.imgui.shown = atoi( argv[ 1 ].c_str( ) ) != 0;
        return false;
    }
    if ( strcmp( cmd, "menu" ) == 0 && argc == 2 ) {
        state.input.menu_open = atoi( argv[ 1 ].c_str( ) ) != 0;
        return false;
    }
    if ( strcmp( cmd, "search" ) == 0 && argc == 2 ) {
        state.input.search_focused = atoi( argv[ 1 ].c_str( ) ) != 0;
        return false;
    }
    // container chest|furnace|table x y z — opens that container UI as a
    // right-click on the block would (single-player offline path);
    // container none closes any open container screen.
    if ( strcmp( cmd, "container" ) == 0 && argc == 2 && strcmp( argv[ 1 ].c_str( ), "none" ) == 0 ) {
        RepGame::close_chest( state );
        RepGame::close_furnace( state );
        if ( state.ui.crafting.table_open ) {
            state.ui.crafting.table_open = false;
            repgame_apply_ui_layout( state );
        }
        state.input.inventory_open = false;
        return false;
    }
    if ( strcmp( cmd, "container" ) == 0 && argc == 5 ) {
        const glm::ivec3 pos( ivec_arg( 2 ), ivec_arg( 3 ), ivec_arg( 4 ) );
        const char *kind = argv[ 1 ].c_str( );
        if ( strcmp( kind, "chest" ) == 0 ) {
            RepGame::open_chest( state, pos );
        } else if ( strcmp( kind, "furnace" ) == 0 ) {
            RepGame::open_furnace( state, pos );
        } else if ( strcmp( kind, "table" ) == 0 ) {
            state.ui.crafting.table_open = true;
            state.ui.crafting.table_pos = pos;
            state.input.inventory_open = true;
            repgame_apply_ui_layout( state );
        } else {
            cmd_error( state, line, "container: want chest|furnace|table|none" );
        }
        return false;
    }
    // uistate — dump the layout rects + each container panel's applied
    // items-slab position (centered y-up coords), for layout debugging.
    if ( strcmp( cmd, "uistate" ) == 0 && argc == 1 ) {
        const UiLayoutRects &r = state.ui.layout.rects( );
        const auto slab = []( const InventoryRenderer &p ) {
            char b[ 96 ];
            snprintf( b, sizeof( b ), "[%d,%d..%d,%d]", p.getItemsLeft( ), p.getItemsBottom( ), p.getItemsRight( ), p.getItemsTop( ) );
            return std::string( b );
        };
        const auto urect = []( const UiRect &u ) {
            char b[ 96 ];
            snprintf( b, sizeof( b ), "[%.1f,%.1f %.1fx%.1f]", u.left, u.bottom, u.width, u.height );
            return std::string( b );
        };
        results::event( state.tick_number, "uistate",
                        "\"overflow\":%u,"
                        "\"open\":{\"chest\":%d,\"furnace\":%d,\"table\":%d},"
                        "\"chest\":{\"rect\":\"%s\",\"slab\":\"%s\"},"
                        "\"furnace\":{\"in\":\"%s\",\"fuel\":\"%s\",\"out\":\"%s\",\"in_slab\":\"%s\",\"fuel_slab\":\"%s\",\"out_slab\":\"%s\"},"
                        "\"craft3\":{\"grid\":\"%s\",\"grid_slab\":\"%s\"},"
                        "\"surv\":{\"rect\":\"%s\",\"slab\":\"%s\"}",
                        r.overflow,
                        state.ui.chest.open ? 1 : 0, state.ui.furnace.open ? 1 : 0, state.ui.crafting.table_open ? 1 : 0,
                        urect( r.chest_items ).c_str( ), slab( state.ui.chest.inventory_renderer ).c_str( ),
                        urect( r.furnace_input ).c_str( ), urect( r.furnace_fuel ).c_str( ), urect( r.furnace_output ).c_str( ),
                        slab( state.ui.furnace.input ).c_str( ), slab( state.ui.furnace.fuel ).c_str( ), slab( state.ui.furnace.output ).c_str( ),
                        urect( r.crafting_grid3 ).c_str( ), slab( state.ui.crafting.grid3 ).c_str( ),
                        urect( r.survival_items ).c_str( ), slab( state.survival_inventory.inventory_renderer ).c_str( ) );
        return false;
    }
    if ( strcmp( cmd, "assert_health" ) == 0 && argc == 3 ) {
        const double expected = atof( argv[ 2 ].c_str() );
        char actual[ 32 ];
        snprintf( actual, sizeof( actual ), "%.3f", state.vitals.health );
        results::assert_( state, line, argv[ 0 ].c_str( ), script::eval_cmp( argv[ 1 ].c_str( ), state.vitals.health, expected ), argv[ 2 ].c_str( ), actual );
        return false;
    }
    if ( strcmp( cmd, "assert_dead" ) == 0 && argc == 3 ) {
        const double expected = atof( argv[ 2 ].c_str() );
        results::assert_( state, line, argv[ 0 ].c_str( ), script::eval_cmp( argv[ 1 ].c_str( ), state.vitals.dead ? 1.0 : 0.0, expected ), argv[ 2 ].c_str( ), state.vitals.dead ? "1" : "0" );
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
    // setsign x y z line1|line2|... — write a sign's stored text directly,
    // bypassing the editor. Args after z join with spaces so a line can
    // contain them; '|' splits lines (max SIGN_LINES).
    if ( strcmp( cmd, "setsign" ) == 0 && argc >= 4 ) {
        const glm::ivec3 pos( ivec_arg( 1 ), ivec_arg( 2 ), ivec_arg( 3 ) );
        std::string joined;
        for ( size_t i = 4; i < argc; i++ ) {
            joined += i == 4 ? "" : " ";
            joined += argv[ i ];
        }
        SignText text;
        size_t start = 0;
        for ( int li = 0; li < SIGN_LINES && start <= joined.size( ); li++ ) {
            const size_t bar = joined.find( '|', start );
            text.lines[ li ] = joined.substr( start, bar == std::string::npos ? std::string::npos : bar - start );
            if ( bar == std::string::npos ) {
                break;
            }
            start = bar + 1;
        }
        if ( !state.world.set_sign_text( pos, text ) ) {
            cmd_error( state, line, "setsign: not a sign or chunk unloaded" );
        }
        return false;
    }
    // assert_sign x y z [line1|line2|...] — the stored text matches; a
    // missing text arg expects no record at all.
    if ( strcmp( cmd, "assert_sign" ) == 0 && argc >= 4 ) {
        const glm::ivec3 pos( ivec_arg( 1 ), ivec_arg( 2 ), ivec_arg( 3 ) );
        std::string joined;
        for ( size_t i = 4; i < argc; i++ ) {
            joined += i == 4 ? "" : " ";
            joined += argv[ i ];
        }
        SignText want;
        size_t start = 0;
        for ( int li = 0; li < SIGN_LINES && start <= joined.size( ); li++ ) {
            const size_t bar = joined.find( '|', start );
            want.lines[ li ] = joined.substr( start, bar == std::string::npos ? std::string::npos : bar - start );
            if ( bar == std::string::npos ) {
                break;
            }
            start = bar + 1;
        }
        const SignText *got = state.world.get_sign_text( pos );
        const bool ok = want.empty( ) ? ( got == nullptr || got->empty( ) )
                                     : ( got != nullptr && got->lines == want.lines );
        char actual[ 128 ];
        if ( got == nullptr ) {
            snprintf( actual, sizeof( actual ), "no record" );
        } else {
            std::string a;
            for ( int li = 0; li < SIGN_LINES; li++ ) {
                a += li ? "|" : "";
                a += got->lines[ li ];
            }
            snprintf( actual, sizeof( actual ), "%s", a.c_str( ) );
        }
        results::assert_( state, line, argv[ 0 ].c_str( ), ok, joined.c_str( ), actual );
        return false;
    }
    // signopen x y z / signclose — the editor's real open and commit paths.
    if ( strcmp( cmd, "signopen" ) == 0 && argc == 4 ) {
        repgame_open_sign_editor( state, glm::ivec3( ivec_arg( 1 ), ivec_arg( 2 ), ivec_arg( 3 ) ) );
        return false;
    }
    if ( strcmp( cmd, "signclose" ) == 0 && argc == 1 ) {
        repgame_close_sign_editor( state, true );
        return false;
    }
    // type text... — queue printable chars on the editor's active line
    // like SDL text input events (joins args with spaces).
    if ( strcmp( cmd, "type" ) == 0 && argc >= 2 ) {
        for ( size_t i = 1; i < argc; i++ ) {
            if ( i > 1 && state.input.text_chars_len < ( int )sizeof( state.input.text_chars ) ) {
                state.input.text_chars[ state.input.text_chars_len++ ] = ' ';
            }
            for ( const char *p = argv[ i ].c_str( ); *p && state.input.text_chars_len < ( int )sizeof( state.input.text_chars ); p++ ) {
                state.input.text_chars[ state.input.text_chars_len++ ] = *p;
            }
        }
        return false;
    }
    // signkey enter|backspace|up|down|escape — one editor key press.
    if ( strcmp( cmd, "signkey" ) == 0 && argc == 2 ) {
        const char *k = argv[ 1 ].c_str( );
        if ( strcmp( k, "enter" ) == 0 ) {
            state.input.text_enters++;
        } else if ( strcmp( k, "backspace" ) == 0 ) {
            state.input.text_backspaces++;
        } else if ( strcmp( k, "up" ) == 0 ) {
            state.input.text_ups++;
        } else if ( strcmp( k, "down" ) == 0 ) {
            state.input.text_downs++;
        } else if ( strcmp( k, "escape" ) == 0 ) {
            state.input.text_escape = true;
        } else {
            cmd_error( state, line, "signkey: want enter|backspace|up|down|escape" );
        }
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
