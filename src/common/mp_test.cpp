#include "common/mp_test.hpp"
#include "common/harness/debug_commands.hpp"
#include "common/utils/env_config.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <map>

#include "common/RepGame.hpp"
#include "common/chunk.hpp"
#include "common/item.hpp"
#include "common/mobs.hpp"
#include "common/multiplayer.hpp"
#include "common/net_entities.hpp"

// ---------------------------------------------------------------------------
// File/env plumbing — all state lives in these globals; nothing runs unless
// REPGAME_MP_DUMP is set (init() opens the files lazily so an early call is
// harmless before the paths exist).
// ---------------------------------------------------------------------------

static FILE *g_dump = nullptr;
static FILE *g_ctl = nullptr;
static long g_ctl_off = 0;
static int g_dump_every = 5;
static bool g_ready_sent = false;
static bool g_inited = false;
// `mode survival` pins survival + walking player state every tick so the
// REPGAME_FLY camera pin (which re-asserts no_clip/flying each tick) can't
// undo it — the flags are read in draw_frame after tick().
static bool g_force_survival = false;

bool mp_test::active( ) {
    return g_inited && g_dump != nullptr;
}

static void jput( const char *fmt, ... ) {
    va_list ap;
    va_start( ap, fmt );
    vfprintf( g_dump, fmt, ap );
    va_end( ap );
}

static void jflush( ) {
    fputc( '\n', g_dump );
    fflush( g_dump );
}

// 64-bit net ids serialize as decimal strings (JSON numbers lose precision).
static void jnet_id( const NetEntityId id ) {
    jput( "\"%llu\"", static_cast<unsigned long long>( id ) );
}

void mp_test::event( RepGameState *state, const char *name, const char *fmt, ... ) {
    if ( !g_inited ) {
        // entities.tick runs before mp_test::tick — events emitted that early
        // (e.g. the first VIEW) must not be lost to lazy init.
        init( );
    }
    if ( g_dump == nullptr ) {
        return;
    }
    jput( "{\"type\":\"event\",\"tick\":%ld,\"name\":\"%s\"", state != nullptr ? state->tick_number : 0L, name );
    if ( fmt != nullptr && fmt[ 0 ] != '\0' ) {
        jput( ",\"detail\":{" );
        va_list ap;
        va_start( ap, fmt );
        vfprintf( g_dump, fmt, ap );
        va_end( ap );
        jput( "}" );
    }
    jput( "}" );
    jflush( );
}

// The name lookups and world-action commands live in
// common/harness/debug_commands.cpp so the REPGAME_SCRIPT runner can reuse
// them — mp_test keeps its own parsing and REPGAME_MP_DUMP event stream.

// --- State dump ------------------------------------------------------------

void mp_test::dump_state( RepGameState &state, const char *tag ) {
    if ( g_dump == nullptr ) {
        return;
    }
    NetEntities &net = state.multiplayer.entities;
    jput( "{\"type\":\"state\",\"tick\":%ld,\"session\":\"%u\",\"connected\":%d,\"world_time\":%ld",
          state.tick_number, net.session( ), net.connected( ) ? 1 : 0, state.world_time );
    if ( tag != nullptr ) {
        jput( ",\"tag\":\"%s\"", tag );
    }
    jput( ",\"pos\":[%.4f,%.4f,%.4f]", state.camera.pos.x, state.camera.pos.y, state.camera.pos.z );

    // Inventory totals (hotbar + survival) keyed by item name.
    std::map<ItemID, int> inv;
    InventorySlot bar[ HOTBAR_WIDTH * HOTBAR_HEIGHT ];
    state.hotbar.saveInventory( bar );
    for ( const InventorySlot &s : bar ) {
        if ( s.item_id != LAST_BLOCK_ID && s.quantity > 0 ) {
            inv[ s.item_id ] += s.quantity;
        }
    }
    const InventorySlot *slots = state.survival_inventory.getSlots( );
    for ( int i = 0; i < SURVIVAL_INVENTORY_WIDTH * SURVIVAL_INVENTORY_HEIGHT; i++ ) {
        if ( slots[ i ].item_id != LAST_BLOCK_ID && slots[ i ].quantity > 0 ) {
            inv[ slots[ i ].item_id ] += slots[ i ].quantity;
        }
    }
    jput( ",\"inventory\":{" );
    bool first = true;
    for ( const auto &pair : inv ) {
        const char *n = item_name( pair.first );
        jput( "%s\"%s\":%d", first ? "" : ",", n != nullptr ? n : "?", pair.second );
        first = false;
    }
    jput( "}" );

    jput( ",\"items\":[" );
    first = true;
    for ( const ItemEntities::ItemEntity &it : state.world.items.debug_items( ) ) {
        float yaw, bob;
        ItemEntities::render_pose( it, 0.0f, &yaw, &bob );
        jput( "%s{\"net_id\":", first ? "" : "," );
        jnet_id( it.net.id );
        jput( ",\"remote\":%d,\"pickup_pending\":%d,\"item\":%d,\"item_name\":\"%s\",\"count\":%d,"
              "\"pos\":[%.4f,%.4f,%.4f],\"vel\":[%.4f,%.4f,%.4f],\"age\":%d,\"grounded\":%d,\"in_water\":%d,"
              "\"spin_seed\":%.5f,\"yaw\":%.4f,\"bob\":%.4f}",
              it.net.mirror ? 1 : 0, it.pickup_pending ? 1 : 0, static_cast<int>( it.item ),
              item_name( it.item ) != nullptr ? item_name( it.item ) : "?", it.count,
              it.pos.x, it.pos.y, it.pos.z, it.vel.x, it.vel.y, it.vel.z,
              it.age_ticks, it.grounded, it.in_water, it.spin_seed, yaw, bob );
        first = false;
    }
    jput( "]" );

    jput( ",\"mobs\":[" );
    first = true;
    for ( const auto &m : state.world.mobs.debug_mobs( ) ) {
        jput( "%s{\"net_id\":", first ? "" : "," );
        jnet_id( m.net.id );
        jput( ",\"remote\":%d,\"uid\":%u,\"type\":%d,\"type_name\":\"%s\",\"variant\":%d,"
              "\"pos\":[%.4f,%.4f,%.4f],\"vel\":[%.4f,%.4f,%.4f],\"yaw\":%.3f,\"health\":%.3f,"
              "\"ai_state\":%d,\"hurt_ticks\":%d,\"death_ticks\":%d,\"fuse_ticks\":%d,"
              "\"walk_dist\":%.4f,\"leg_amp\":%.4f,\"head_yaw\":%.3f,\"head_pitch\":%.3f,"
              "\"sneak\":%.3f,\"despawnable\":%d,\"targeting\":%d,\"net_flags\":%u}",
              m.net.mirror ? 1 : 0, m.uid, static_cast<int>( m.type ), mob_types[ m.type ].name, m.variant,
              m.pos.x, m.pos.y, m.pos.z, m.vel.x, m.vel.y, m.vel.z, m.yaw, m.health,
              m.ai_state, m.hurt_ticks, m.death_ticks, m.fuse_ticks,
              m.walk_dist, m.leg_amp, m.head_yaw, m.head_pitch,
              m.sneak, m.despawnable, m.targeting, m.net_flags );
        first = false;
    }
    jput( "]" );

    jput( ",\"bodies\":[" );
    first = true;
    for ( const BlockBodies::Body &b : state.world.bodies.debug_bodies( ) ) {
        const long fuse_left = b.kind == BlockBodies::BODY_PRIMED_TNT
                                   ? b.fuse_ticks - ( state.tick_number - b.fuse_start_tick )
                                   : 0;
        jput( "%s{\"net_id\":", first ? "" : "," );
        jnet_id( b.net.id );
        jput( ",\"remote\":%d,\"kind\":%d,\"block\":%d,"
              "\"pos\":[%.4f,%.4f,%.4f],\"vel\":[%.4f,%.4f,%.4f],\"grounded\":%d,\"fuse_left\":%ld}",
              b.net.mirror ? 1 : 0, static_cast<int>( b.kind ), static_cast<int>( b.state.id ),
              b.min.x, b.min.y, b.min.z, b.vel.x, b.vel.y, b.vel.z, b.grounded, fuse_left );
        first = false;
    }
    jput( "]" );

    int owners, viewers;
    net.role_counts( &owners, &viewers );
    jput( ",\"roles\":{\"owner\":%d,\"viewer\":%d}", owners, viewers );
    jput( "}" );
    jflush( );
}

// --- Command execution -----------------------------------------------------

static void cmd_error( RepGameState &state, const char *line ) {
    mp_test::event( &state, "cmd_error", "\"line\":\"%.120s\"", line );
}

static void run_cmd( RepGameState &state, char *line ) {
    // Tokenize on whitespace.
    char *argv[ 16 ];
    int argc = 0;
    for ( char *tok = strtok( line, " \t" ); tok != nullptr && argc < 16; tok = strtok( nullptr, " \t" ) ) {
        argv[ argc++ ] = tok;
    }
    if ( argc == 0 ) {
        return;
    }
    const char *cmd = argv[ 0 ];
    std::string err;

    if ( strcmp( cmd, "goto" ) == 0 && argc == 4 ) {
        dbgcmd::goto_( state, glm::dvec3( atof( argv[ 1 ] ), atof( argv[ 2 ] ), atof( argv[ 3 ] ) ) );
        return;
    }
    if ( strcmp( cmd, "drop" ) == 0 && ( argc == 6 || argc == 9 ) ) {
        const glm::dvec3 pos( atof( argv[ 3 ] ), atof( argv[ 4 ] ), atof( argv[ 5 ] ) );
        const glm::dvec3 vel = argc == 9 ? glm::dvec3( atof( argv[ 6 ] ), atof( argv[ 7 ] ), atof( argv[ 8 ] ) )
                                        : glm::dvec3( 0.0 );
        if ( !dbgcmd::drop( state, argv[ 1 ], atoi( argv[ 2 ] ), pos, vel, &err ) ) {
            cmd_error( state, line );
        }
        return;
    }
    if ( strcmp( cmd, "mob" ) == 0 && argc == 5 ) {
        if ( !dbgcmd::mob( state, argv[ 1 ], glm::dvec3( atof( argv[ 2 ] ), atof( argv[ 3 ] ), atof( argv[ 4 ] ) ), &err ) ) {
            cmd_error( state, line );
        }
        return;
    }
    if ( strcmp( cmd, "hit" ) == 0 && argc == 2 ) {
        if ( !dbgcmd::hit( state, strtoull( argv[ 1 ], nullptr, 10 ), &err ) ) {
            mp_test::event( &state, "cmd_error", "\"line\":\"hit: no mob with that net_id\"" );
        }
        return;
    }
    if ( strcmp( cmd, "feed" ) == 0 && ( argc == 4 || argc == 5 ) ) {
        if ( !dbgcmd::feed( state, glm::dvec3( atof( argv[ 1 ] ), atof( argv[ 2 ] ), atof( argv[ 3 ] ) ),
                          argc == 5 ? argv[ 4 ] : nullptr, &err ) ) {
            cmd_error( state, line );
        }
        return;
    }
    if ( strcmp( cmd, "fall" ) == 0 && argc == 5 ) {
        if ( !dbgcmd::fall( state, argv[ 1 ], glm::ivec3( atoi( argv[ 2 ] ), atoi( argv[ 3 ] ), atoi( argv[ 4 ] ) ),
                          &err ) ) {
            cmd_error( state, line );
        }
        return;
    }
    if ( strcmp( cmd, "tnt" ) == 0 && argc == 4 ) {
        dbgcmd::tnt( state, glm::vec3( atof( argv[ 1 ] ), atof( argv[ 2 ] ), atof( argv[ 3 ] ) ), &err );
        return;
    }
    if ( strcmp( cmd, "setblock" ) == 0 && argc == 5 ) {
        if ( !dbgcmd::setblock( state, glm::ivec3( atoi( argv[ 1 ] ), atoi( argv[ 2 ] ), atoi( argv[ 3 ] ) ), argv[ 4 ],
                                &err ) ) {
            cmd_error( state, line );
        }
        return;
    }
    if ( strcmp( cmd, "probe" ) == 0 && argc == 4 ) {
        const glm::ivec3 pos( atoi( argv[ 1 ] ), atoi( argv[ 2 ] ), atoi( argv[ 3 ] ) );
        const BlockState bs = state.world.get_loaded_block( pos );
        const char *n = bs.id == LAST_BLOCK_ID ? "unloaded" : block_definition_get_name( bs.id );
        mp_test::event( &state, "probe", "\"pos\":[%d,%d,%d],\"block\":%d,\"name\":\"%s\",\"rotation\":%d",
                        pos.x, pos.y, pos.z, static_cast<int>( bs.id ), n != nullptr ? n : "?", bs.rotation );
        return;
    }
    if ( strcmp( cmd, "role" ) == 0 && argc == 4 ) {
        const glm::ivec3 chunk( atoi( argv[ 1 ] ), atoi( argv[ 2 ] ), atoi( argv[ 3 ] ) );
        mp_test::event( &state, "role", "\"chunk\":[%d,%d,%d],\"role\":%d",
                        chunk.x, chunk.y, chunk.z, state.multiplayer.entities.role_of( chunk ) );
        return;
    }
    // copen/cclose/cset/cprobe: container-sync drivers + world-state probe.
    if ( strcmp( cmd, "copen" ) == 0 && argc == 5 ) {
        if ( !dbgcmd::copen( state, dbgcmd::container_from_name( argv[ 1 ] ),
                             glm::ivec3( atoi( argv[ 2 ] ), atoi( argv[ 3 ] ), atoi( argv[ 4 ] ) ), &err ) ) {
            cmd_error( state, line );
        }
        return;
    }
    if ( strcmp( cmd, "cclose" ) == 0 && argc == 5 ) {
        if ( !dbgcmd::cclose( state, dbgcmd::container_from_name( argv[ 1 ] ),
                              glm::ivec3( atoi( argv[ 2 ] ), atoi( argv[ 3 ] ), atoi( argv[ 4 ] ) ), &err ) ) {
            cmd_error( state, line );
        }
        return;
    }
    if ( strcmp( cmd, "cset" ) == 0 && argc == 8 ) {
        if ( !dbgcmd::cset( state, dbgcmd::container_from_name( argv[ 1 ] ),
                            glm::ivec3( atoi( argv[ 2 ] ), atoi( argv[ 3 ] ), atoi( argv[ 4 ] ) ), atoi( argv[ 5 ] ), argv[ 6 ],
                            atoi( argv[ 7 ] ), &err ) ) {
            cmd_error( state, line );
        }
        return;
    }
    if ( strcmp( cmd, "cprobe" ) == 0 && argc == 5 ) {
        const glm::ivec3 pos( atoi( argv[ 2 ] ), atoi( argv[ 3 ] ), atoi( argv[ 4 ] ) );
        const NetContainerKindDef *def = dbgcmd::container_from_name( argv[ 1 ] );
        ContainerContents cc;
        bool is_container = false;
        if ( !dbgcmd::cprobe( state, def, pos, &cc, &is_container, &err ) ) {
            cmd_error( state, line );
            return;
        }
        // {"kind":..,"pos":[..],"is":0/1,"rev":n,"slots":[{"name","count"}],"aux":[..]}
        std::string slots;
        for ( size_t i = 0; i < cc.slots.size( ); i++ ) {
            if ( cc.slots[ i ].item_id == LAST_BLOCK_ID || cc.slots[ i ].quantity <= 0 ) {
                continue;
            }
            const char *n = item_name( cc.slots[ i ].item_id );
            char one[ 128 ];
            snprintf( one, sizeof( one ), "%s{\"slot\":%zu,\"name\":\"%s\",\"count\":%d}", slots.empty( ) ? "" : ",", i,
                      n != nullptr ? n : "?", cc.slots[ i ].quantity );
            slots += one;
        }
        std::string aux;
        for ( size_t i = 0; i < cc.aux.size( ); i++ ) {
            char one[ 24 ];
            snprintf( one, sizeof( one ), "%s%d", aux.empty( ) ? "" : ",", cc.aux[ i ] );
            aux += one;
        }
        mp_test::event( &state, "cprobe",
                        "\"kind\":\"%s\",\"pos\":[%d,%d,%d],\"is\":%d,\"rev\":%u,\"slots\":[%s],\"aux\":[%s]", def->name, pos.x,
                        pos.y, pos.z, is_container ? 1 : 0, state.multiplayer.containers.last_revision( def->kind, pos ),
                        slots.c_str( ), aux.c_str( ) );
        return;
    }
    if ( strcmp( cmd, "mode" ) == 0 && argc == 2 ) {
        g_force_survival = strcasecmp( argv[ 1 ], "survival" ) == 0;
        state.game_mode = g_force_survival ? GameMode_Survival : GameMode_Creative;
        return;
    }
    if ( strcmp( cmd, "peaceful" ) == 0 && argc == 2 ) {
        if ( !dbgcmd::peaceful( state, argv[ 1 ], &err ) ) {
            cmd_error( state, line );
        }
        return;
    }
    if ( strcmp( cmd, "time" ) == 0 && argc == 2 ) {
        dbgcmd::set_time( state, atol( argv[ 1 ] ) );
        return;
    }
    if ( strcmp( cmd, "dump" ) == 0 ) {
        mp_test::dump_state( state, argc >= 2 ? argv[ 1 ] : nullptr );
        return;
    }
    if ( strcmp( cmd, "quit" ) == 0 ) {
        mp_test::event( &state, "quit" );
        state.input.exitGame = true;
        return;
    }
    cmd_error( state, line );
}

// --- Per-tick driver --------------------------------------------------------

void mp_test::init( ) {
    if ( g_inited ) {
        return;
    }
    g_inited = true;
    const char *dump_path = env_get( "REPGAME_MP_DUMP" );
    if ( dump_path == nullptr || dump_path[ 0 ] == '\0' ) {
        return;
    }
    g_dump = fopen( dump_path, "a" );
    const int n = env_int( "REPGAME_MP_DUMP_EVERY", 0 );
    if ( n > 0 ) {
        g_dump_every = n;
    }
}

void mp_test::tick( RepGameState &state ) {
    if ( !g_inited ) {
        init( );
    }
    if ( g_dump == nullptr ) {
        return;
    }

    // Drain newly appended complete lines from the ctl file (opened lazily —
    // the driver may create it after the client starts).
    if ( g_ctl == nullptr ) {
        if ( const char *ctl_path = env_get( "REPGAME_MP_CTL" ) ) {
            g_ctl = fopen( ctl_path, "r" );
        }
    }
    if ( g_ctl != nullptr ) {
        fseek( g_ctl, g_ctl_off, SEEK_SET );
        clearerr( g_ctl );
        char buf[ 512 ];
        long off = g_ctl_off;
        while ( fgets( buf, sizeof( buf ), g_ctl ) != nullptr ) {
            const size_t len = strlen( buf );
            if ( len > 0 && buf[ len - 1 ] != '\n' && !feof( g_ctl ) ) {
                break; // line longer than the buffer — leave it for next tick
            }
            if ( len > 0 && buf[ len - 1 ] != '\n' ) {
                break; // partial line still being written — retry next tick
            }
            off += static_cast<long>( len );
            buf[ strcspn( buf, "\r\n" ) ] = '\0';
            if ( buf[ 0 ] != '\0' ) {
                run_cmd( state, buf );
            }
        }
        g_ctl_off = off;
    }

    if ( g_force_survival ) {
        state.game_mode = GameMode_Survival;
        state.input.no_clip = false;
        state.input.player_flying = false;
    }

    // `ready`: connected + WELCOME + the camera's chunk is live + at least
    // one CHUNK_ROLE grant was applied.
    if ( !g_ready_sent ) {
        const NetEntities &net = state.multiplayer.entities;
        const glm::ivec3 cam_block( glm::floor( state.camera.pos ) );
        if ( net.connected( ) && net.role_count( ) > 0 && state.world.get_loaded_block( cam_block ).id != LAST_BLOCK_ID ) {
            g_ready_sent = true;
            event( &state, "ready", "\"session\":\"%u\"", net.session( ) );
        }
    }

    if ( state.tick_number % g_dump_every == 0 ) {
        dump_state( state );
    }
}
