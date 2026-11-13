// Slash commands for the chat box - the dispatcher plus every handler.
// World actions reuse the dbgcmd:: implementations shared with the
// mp-test and script drivers; social commands ride the CHAT wire message.
// Command output lands in the chat log as client-only INFO/ERROR lines —
// nothing here touches the wire directly except send_chat/send_player_name.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <vector>

#include "common/RepGame.hpp"
#include "common/chat.hpp"
#include "common/constants.hpp"
#include "common/item.hpp"
#include "common/harness/debug_commands.hpp"
#include "common/world.hpp"

static void chat_info( RepGameState &state, const std::string &text ) {
    state.chat.push( CHAT_KIND_INFO, "", "", text, state.tick_number );
}

static void chat_err( RepGameState &state, const std::string &text ) {
    state.chat.push( CHAT_KIND_ERROR, "", "", text, state.tick_number );
}

using ChatCmdFn = void ( * )( RepGameState &state, const std::vector<std::string> &args );

struct ChatCmd {
    const char *name;
    const char *aliases; // comma-separated, may be empty
    const char *usage;   // includes the leading '/'
    const char *help;
    ChatCmdFn fn;
};

// Defined at the bottom, after every handler; cmd_help iterates it too.
extern const ChatCmd CHAT_COMMANDS_TABLE[ 16 ];

// One coordinate token: a plain number, '~' (the player's component), or
// '~N'/'~-N' (player-relative offset). `base` is the feet-relative origin
// for the axis.
static bool chat_parse_coord( const std::string &tok, const double base, double &out ) {
    if ( tok == "~" ) {
        out = base;
        return true;
    }
    if ( tok[ 0 ] == '~' ) {
        char *end = nullptr;
        const double off = strtod( tok.c_str( ) + 1, &end );
        if ( end == tok.c_str( ) + 1 || *end != '\0' ) {
            return false;
        }
        out = base + off;
        return true;
    }
    char *end = nullptr;
    const double v = strtod( tok.c_str( ), &end );
    if ( end == tok.c_str( ) || *end != '\0' ) {
        return false;
    }
    out = v;
    return true;
}

// The player's feet position (camera.pos is the eye).
static glm::dvec3 chat_feet( const RepGameState &state ) {
    return glm::dvec3( state.camera.pos.x, state.camera.pos.y - state.camera.eye_height, state.camera.pos.z );
}

// Teleport so the feet land on `feet_target`, zeroing motion state so the
// player doesn't keep a velocity across the jump.
static void chat_teleport( RepGameState &state, const glm::dvec3 &feet_target ) {
    const glm::dvec3 eye( feet_target.x, feet_target.y + state.camera.eye_height, feet_target.z );
    dbgcmd::goto_( state, eye );
    state.camera.y_speed = 0.0;
    state.camera.horizontal_vel = glm::dvec2( 0.0 );
    state.vitals.fall_dist = 0.0;
}

// Joined remainder of args (chat text after the name/command args).
static std::string chat_tail( const std::vector<std::string> &args, const size_t first ) {
    std::string text;
    for ( size_t i = first; i < args.size( ); i++ ) {
        text += i == first ? "" : " ";
        text += args[ i ];
    }
    return text;
}

// Three position args starting at `i` -> block coords, '~'-relative to
// the feet position.
static bool chat_parse_pos3( RepGameState &state, const std::vector<std::string> &args, const size_t i, glm::dvec3 &out ) {
    const glm::dvec3 feet = chat_feet( state );
    return chat_parse_coord( args[ i ], feet.x, out.x ) && chat_parse_coord( args[ i + 1 ], feet.y, out.y ) &&
           chat_parse_coord( args[ i + 2 ], feet.z, out.z );
}

// ---- handlers -----------------------------------------------------------

static void cmd_help( RepGameState &state, const std::vector<std::string> &args ) {
    if ( args.size( ) >= 2 ) {
        for ( const ChatCmd &c : CHAT_COMMANDS_TABLE ) {
            bool match = strcasecmp( args[ 1 ].c_str( ), c.name ) == 0;
            if ( !match ) {
                for ( const char *p = c.aliases; *p && !match; ) {
                    const char *comma = strchr( p, ',' );
                    const size_t len = comma ? static_cast<size_t>( comma - p ) : strlen( p );
                    match = strlen( args[ 1 ].c_str( ) ) == len && strncasecmp( args[ 1 ].c_str( ), p, len ) == 0;
                    p += len + ( comma ? 1 : 0 );
                }
            }
            if ( match ) {
                chat_info( state, std::string( c.usage ) + " - " + c.help );
                return;
            }
        }
        chat_err( state, "No such command /" + args[ 1 ] );
        return;
    }
    std::string names = "Commands:";
    for ( const ChatCmd &c : CHAT_COMMANDS_TABLE ) {
        names += " /";
        names += c.name;
        if ( c.aliases[ 0 ] != '\0' ) {
            names += "(/";
            for ( const char *p = c.aliases; *p; p++ ) {
                names += *p == ',' ? ",/" : std::string( 1, *p );
            }
            names += ")";
        }
    }
    chat_info( state, names );
    chat_info( state, "/help <command> for usage." );
}

static void cmd_clear( RepGameState &state, const std::vector<std::string> &args ) {
    if ( args.size( ) != 1 ) {
        chat_err( state, "Usage: /clear" );
        return;
    }
    state.chat.clear( );
}

static void cmd_pos( RepGameState &state, const std::vector<std::string> &args ) {
    if ( args.size( ) != 1 ) {
        chat_err( state, "Usage: /pos" );
        return;
    }
    const glm::dvec3 feet = chat_feet( state );
    const glm::ivec3 block( static_cast<int>( floor( feet.x ) ), static_cast<int>( floor( feet.y ) ),
                            static_cast<int>( floor( feet.z ) ) );
    const glm::ivec3 chunk = glm::floor( glm::vec3( block ) / CHUNK_SIZE_F );
    // angle_H: 0 faces -z (north); name the nearest compass direction.
    const float yaw = fmodf( fmodf( state.camera.angle_H, 360.0f ) + 360.0f, 360.0f );
    static const char *dirs[ 8 ] = { "north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west" };
    const char *facing = dirs[ static_cast<int>( floorf( ( yaw + 22.5f ) / 45.0f ) ) % 8 ];
    char buf[ 160 ];
    snprintf( buf, sizeof( buf ), "pos %.1f %.1f %.1f - block %d %d %d, chunk %d %d %d, facing %s (yaw %.0f)",
              feet.x, feet.y, feet.z, block.x, block.y, block.z, chunk.x, chunk.y, chunk.z, facing, yaw );
    chat_info( state, buf );
}

static void cmd_tp( RepGameState &state, const std::vector<std::string> &args ) {
    if ( args.size( ) == 2 ) {
        int32_t player_id = -1;
        if ( !state.multiplayer.is_active( ) || !state.multiplayer.find_player( args[ 1 ], player_id ) ) {
            chat_err( state, "No player named " + args[ 1 ] );
            return;
        }
        glm::dvec3 feet;
        if ( !state.world.avatar_position_of( player_id, feet ) ) {
            chat_err( state, "No position for " + args[ 1 ] + " yet" );
            return;
        }
        chat_teleport( state, feet );
        return;
    }
    if ( args.size( ) != 4 ) {
        chat_err( state, "Usage: /tp <x> <y> <z> or /tp <player>" );
        return;
    }
    glm::dvec3 feet;
    if ( !chat_parse_pos3( state, args, 1, feet ) ) {
        chat_err( state, "Usage: /tp <x> <y> <z> or /tp <player>" );
        return;
    }
    chat_teleport( state, feet );
}

static void cmd_give( RepGameState &state, const std::vector<std::string> &args ) {
    if ( args.size( ) != 2 && args.size( ) != 3 ) {
        chat_err( state, "Usage: /give <item> [count]" );
        return;
    }
    char *end = nullptr;
    const long count = args.size( ) == 3 ? strtol( args[ 2 ].c_str( ), &end, 10 ) : 1;
    if ( args.size( ) == 3 && ( end == args[ 2 ].c_str( ) || *end != '\0' || count <= 0 ) ) {
        chat_err( state, "Usage: /give <item> [count]" );
        return;
    }
    std::string err;
    if ( !dbgcmd::give( state, args[ 1 ].c_str( ), static_cast<int>( count ), &err ) ) {
        chat_err( state, err );
        return;
    }
    chat_info( state, "Gave " + std::to_string( count ) + " " + args[ 1 ] );
}

static void cmd_gamemode( RepGameState &state, const std::vector<std::string> &args ) {
    if ( args.size( ) != 2 ) {
        chat_err( state, "Usage: /gamemode <survival|creative>" );
        return;
    }
    const char *a = args[ 1 ].c_str( );
    const char *mode = ( strcasecmp( a, "s" ) == 0 || strcasecmp( a, "survival" ) == 0 || strcmp( a, "0" ) == 0 )
                           ? "survival"
                           : ( strcasecmp( a, "c" ) == 0 || strcasecmp( a, "creative" ) == 0 || strcmp( a, "1" ) == 0 )
                                 ? "creative"
                                 : nullptr;
    if ( mode == nullptr ) {
        chat_err( state, "Usage: /gamemode <survival|creative>" );
        return;
    }
    std::string err;
    if ( !dbgcmd::mode( state, mode, nullptr, &err ) ) {
        chat_err( state, err );
        return;
    }
    chat_info( state, std::string( "Game mode: " ) + mode );
}

static void cmd_time( RepGameState &state, const std::vector<std::string> &args ) {
    if ( args.size( ) == 1 || args == std::vector<std::string>{ "time", "query" } ) {
        char buf[ 64 ];
        snprintf( buf, sizeof( buf ), "The time is %ld", state.world_time );
        chat_info( state, buf );
        return;
    }
    if ( args.size( ) != 3 || ( args[ 1 ] != "set" && args[ 1 ] != "add" ) ) {
        chat_err( state, "Usage: /time <set|add> <ticks|day|noon|night|midnight>" );
        return;
    }
    const bool add = args[ 1 ] == "add";
    long value = 0;
    const std::string &v = args[ 2 ];
    // Named times keep the current day index: day/noon = D/4,
    // night/midnight = 3D/4 (D = DAY_LENGTH_TICKS).
    long named = -1;
    if ( v == "day" || v == "noon" ) {
        named = DAY_LENGTH_TICKS / 4;
    } else if ( v == "night" || v == "midnight" ) {
        named = 3 * DAY_LENGTH_TICKS / 4;
    }
    if ( named >= 0 ) {
        if ( add ) {
            value = state.world_time + named;
        } else {
            value = ( state.world_time / DAY_LENGTH_TICKS ) * DAY_LENGTH_TICKS + named;
        }
    } else {
        char *end = nullptr;
        const long ticks = strtol( v.c_str( ), &end, 10 );
        if ( end == v.c_str( ) || *end != '\0' ) {
            chat_err( state, "Usage: /time <set|add> <ticks|day|noon|night|midnight>" );
            return;
        }
        value = add ? state.world_time + ticks : ticks;
    }
    dbgcmd::set_time( state, value );
    char buf[ 64 ];
    snprintf( buf, sizeof( buf ), "Set the time to %ld", state.world_time );
    chat_info( state, buf );
}

static void cmd_fly( RepGameState &state, const std::vector<std::string> &args ) {
    if ( args.size( ) == 1 ) {
        state.input.player_flying = !state.input.player_flying;
    } else if ( args.size( ) == 2 ) {
        std::string err;
        if ( !dbgcmd::fly( state, args[ 1 ].c_str( ), &err ) ) {
            chat_err( state, err );
            return;
        }
    } else {
        chat_err( state, "Usage: /fly [on|off]" );
        return;
    }
    chat_info( state, state.input.player_flying ? "Flying on" : "Flying off" );
}

static void cmd_peaceful( RepGameState &state, const std::vector<std::string> &args ) {
    if ( args.size( ) != 2 ) {
        chat_err( state, "Usage: /peaceful <on|off>" );
        return;
    }
    std::string err;
    if ( !dbgcmd::peaceful( state, args[ 1 ].c_str( ), &err ) ) {
        chat_err( state, err );
        return;
    }
    chat_info( state, state.peaceful ? "Peaceful on" : "Peaceful off" );
}

static void cmd_setblock( RepGameState &state, const std::vector<std::string> &args ) {
    if ( args.size( ) != 5 ) {
        chat_err( state, "Usage: /setblock <x> <y> <z> <block>" );
        return;
    }
    glm::dvec3 pos;
    if ( !chat_parse_pos3( state, args, 1, pos ) ) {
        chat_err( state, "Usage: /setblock <x> <y> <z> <block>" );
        return;
    }
    std::string err;
    if ( !dbgcmd::setblock( state, glm::ivec3( static_cast<int>( floor( pos.x ) ), static_cast<int>( floor( pos.y ) ),
                                             static_cast<int>( floor( pos.z ) ) ),
                            args[ 4 ].c_str( ), &err ) ) {
        chat_err( state, err );
    }
}

static void cmd_fill( RepGameState &state, const std::vector<std::string> &args ) {
    if ( args.size( ) != 8 ) {
        chat_err( state, "Usage: /fill <x1> <y1> <z1> <x2> <y2> <z2> <block>" );
        return;
    }
    glm::dvec3 a, b;
    if ( !chat_parse_pos3( state, args, 1, a ) || !chat_parse_pos3( state, args, 4, b ) ) {
        chat_err( state, "Usage: /fill <x1> <y1> <z1> <x2> <y2> <z2> <block>" );
        return;
    }
    std::string err;
    if ( !dbgcmd::fill( state, glm::ivec3( static_cast<int>( floor( a.x ) ), static_cast<int>( floor( a.y ) ),
                                         static_cast<int>( floor( a.z ) ) ),
                        glm::ivec3( static_cast<int>( floor( b.x ) ), static_cast<int>( floor( b.y ) ),
                                    static_cast<int>( floor( b.z ) ) ),
                        args[ 7 ].c_str( ), &err ) ) {
        chat_err( state, err );
    }
}

static void cmd_summon( RepGameState &state, const std::vector<std::string> &args ) {
    if ( args.size( ) != 2 && args.size( ) != 5 ) {
        chat_err( state, "Usage: /summon <mob> [x y z]" );
        return;
    }
    glm::dvec3 pos;
    if ( args.size( ) == 5 ) {
        if ( !chat_parse_pos3( state, args, 2, pos ) ) {
            chat_err( state, "Usage: /summon <mob> [x y z]" );
            return;
        }
    } else {
        // Three blocks ahead along the camera look, at feet level.
        pos = chat_feet( state ) + glm::dvec3( state.camera.look ) * 3.0;
    }
    std::string err;
    if ( !dbgcmd::mob( state, args[ 1 ].c_str( ), pos, &err ) ) {
        chat_err( state, err );
    }
}

static void cmd_name( RepGameState &state, const std::vector<std::string> &args ) {
    if ( args.size( ) != 2 ) {
        chat_err( state, "Usage: /name <new name>" );
        return;
    }
    if ( !chat_valid_name( args[ 1 ] ) ) {
        chat_err( state, "Invalid name - 1-16 chars of [A-Za-z0-9_]" );
        return;
    }
    // Persist the requested name (player.dat saves state.player_name on
    // exit); the server may dedupe it to a different effective spelling.
    state.player_name = args[ 1 ];
    if ( state.multiplayer.is_active( ) ) {
        state.multiplayer.send_player_name( state.player_name );
        chat_info( state, "Requested the name " + state.player_name );
    } else {
        chat_info( state, "Name set to " + state.player_name );
    }
}

static void cmd_list( RepGameState &state, const std::vector<std::string> &args ) {
    if ( args.size( ) != 1 ) {
        chat_err( state, "Usage: /list" );
        return;
    }
    if ( !state.multiplayer.is_active( ) ) {
        chat_info( state, "Players online (1): " + state.player_name );
        return;
    }
    std::string names;
    int count = 0;
    const std::string *self = state.multiplayer.name_of( state.multiplayer.self_player_id );
    names += self != nullptr ? *self : state.player_name;
    count++;
    for ( const auto &pair : state.multiplayer.player_names ) {
        if ( pair.first == state.multiplayer.self_player_id ) {
            continue;
        }
        names += ", " + pair.second;
        count++;
    }
    chat_info( state, "Players online (" + std::to_string( count ) + "): " + names );
}

static void cmd_msg( RepGameState &state, const std::vector<std::string> &args ) {
    if ( args.size( ) < 3 ) {
        chat_err( state, "Usage: /msg <player> <message...>" );
        return;
    }
    if ( !state.multiplayer.is_active( ) ) {
        chat_err( state, "Not connected to a server" );
        return;
    }
    const std::string text = chat_tail( args, 2 );
    if ( text.empty( ) ) {
        chat_err( state, "Usage: /msg <player> <message...>" );
        return;
    }
    // The server resolves the target (case-insensitive, plus its
    // canonical spelling in the echo) and reports "No player named X".
    state.multiplayer.send_chat( CHAT_KIND_WHISPER, args[ 1 ], text );
}

static void cmd_me( RepGameState &state, const std::vector<std::string> &args ) {
    const std::string text = chat_tail( args, 1 );
    if ( text.empty( ) ) {
        chat_err( state, "Usage: /me <action...>" );
        return;
    }
    if ( state.multiplayer.is_active( ) ) {
        state.multiplayer.send_chat( CHAT_KIND_EMOTE, "", text );
    } else {
        state.chat.push( CHAT_KIND_EMOTE, state.player_name, "", text, state.tick_number );
    }
}

const ChatCmd CHAT_COMMANDS_TABLE[ 16 ] = {
    { "help", "", "/help [command]", "List commands, or one command's usage.", cmd_help },
    { "clear", "", "/clear", "Clear the chat log.", cmd_clear },
    { "pos", "", "/pos", "Where you are: feet position, block, chunk, facing.", cmd_pos },
    { "tp", "teleport", "/tp <x> <y> <z> | /tp <player>", "Teleport to feet coords or a player ('~' = current).", cmd_tp },
    { "give", "", "/give <item> [count]", "Add items to the hotbar like a pickup.", cmd_give },
    { "gamemode", "gm", "/gamemode <survival|creative>", "Switch game mode (s/c/0/1 also work).", cmd_gamemode },
    { "time", "", "/time <set|add> <ticks|day|noon|night|midnight>", "Set or add to the world clock.", cmd_time },
    { "fly", "", "/fly [on|off]", "Toggle flight.", cmd_fly },
    { "peaceful", "", "/peaceful <on|off>", "Toggle hostile-mob immunity + regen.", cmd_peaceful },
    { "setblock", "", "/setblock <x> <y> <z> <block>", "Place one block ('~' = current pos).", cmd_setblock },
    { "fill", "", "/fill <x1> <y1> <z1> <x2> <y2> <z2> <block>", "Fill an inclusive box (<= 65536 cells).", cmd_fill },
    { "summon", "", "/summon <mob> [x y z]", "Spawn a mob (default: 3 blocks ahead).", cmd_summon },
    { "name", "", "/name <new name>", "Set your display name (persisted).", cmd_name },
    { "list", "", "/list", "Who is online.", cmd_list },
    { "msg", "w,tell", "/msg <player> <message...>", "Whisper to one player.", cmd_msg },
    { "me", "", "/me <action...>", "Emote: * <name> <action>.", cmd_me },
};

void chat_run_command( RepGameState &state, const std::string &cmdline ) {
    std::vector<std::string> args;
    size_t i = 0;
    while ( i < cmdline.size( ) ) {
        while ( i < cmdline.size( ) && cmdline[ i ] == ' ' ) {
            i++;
        }
        const size_t start = i;
        while ( i < cmdline.size( ) && cmdline[ i ] != ' ' ) {
            i++;
        }
        if ( i > start ) {
            args.push_back( cmdline.substr( start, i - start ) );
        }
    }
    if ( args.empty( ) ) {
        return;
    }
    for ( const ChatCmd &c : CHAT_COMMANDS_TABLE ) {
        if ( strcasecmp( args[ 0 ].c_str( ), c.name ) == 0 ) {
            c.fn( state, args );
            return;
        }
        for ( const char *p = c.aliases; *p; ) {
            const char *comma = strchr( p, ',' );
            const size_t len = comma ? static_cast<size_t>( comma - p ) : strlen( p );
            const bool match = args[ 0 ].size( ) == len && strncasecmp( args[ 0 ].c_str( ), p, len ) == 0;
            if ( match ) {
                c.fn( state, args );
                return;
            }
            p += len + ( comma ? 1 : 0 );
        }
    }
    chat_err( state, "Unknown command /" + args[ 0 ] + " - try /help" );
}
