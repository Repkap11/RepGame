#include "common/harness/inspect.hpp"

#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

#include "common/RepGame.hpp" // REPGAME_PATH_DIVIDOR via the platform header

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/block_definitions.hpp"
#include "common/chunk.hpp"
#include "common/chest.hpp"
#include "common/item.hpp"
#include "common/mob_types.hpp"
#include "common/player_data.hpp"
#include "common/utils/file_utils.hpp"
#include "common/utils/map_storage.hpp"

namespace {

constexpr glm::ivec3 CHUNK_DIM{ CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z };

int chunk_coord( const int v, const int dim ) {
    return static_cast<int>( std::floor( static_cast<double>( v ) / dim ) );
}

const char *block_name_of( const BlockID id ) {
    if ( id == AIR || id == LAST_BLOCK_ID ) {
        return "AIR";
    }
    const char *n = block_id_defined( id ) ? block_definition_get_name( id ) : nullptr;
    return n != nullptr ? n : "?";
}

// fmt into a std::string (bounded 256-ish usages only).
std::string jf( const char *fmt, ... ) {
    char buf[ 4096 ];
    va_list ap;
    va_start( ap, fmt );
    vsnprintf( buf, sizeof( buf ), fmt, ap );
    va_end( ap );
    return buf;
}

// Load a saved chunk's cell array. loaded=false when no chunk file exists —
// cells are left as the caller made them (callers pre-fill with AIR).
bool load_cells( MapStorage &ms, const glm::ivec3 &off, BlockState *cells ) {
    int dirty = 0;
    return ms.load_blocks( off, cells, dirty, false ) != 0;
}

const BlockState &cell_at( const BlockState *cells, const glm::ivec3 &local ) {
    return cells[ Chunk::get_index_from_coords( local ) ];
}

struct DirScan {
    std::vector<glm::ivec3> chests, mobs, items;
};

// Find {chest,mobs,items}_<cx>_<cy>_<cz> files under dir.
DirScan scan_entity_files( const char *dir ) {
    DirScan out;
    DIR *d = opendir( dir );
    if ( d == nullptr ) {
        return out;
    }
    const struct dirent *e;
    while ( ( e = readdir( d ) ) != nullptr ) {
        int a, b, c;
        if ( sscanf( e->d_name, "chest_%d_%d_%d", &a, &b, &c ) == 3 ) {
            out.chests.emplace_back( a, b, c );
        } else if ( sscanf( e->d_name, "mobs_%d_%d_%d", &a, &b, &c ) == 3 ) {
            out.mobs.emplace_back( a, b, c );
        } else if ( sscanf( e->d_name, "items_%d_%d_%d", &a, &b, &c ) == 3 ) {
            out.items.emplace_back( a, b, c );
        }
    }
    closedir( d );
    return out;
}

} // namespace

std::string inspect::resolve_world_dir( const char *arg ) {
    struct stat st;
    const bool looks_like_path = strchr( arg, '/' ) != nullptr;
    if ( looks_like_path ) {
        return ( stat( arg, &st ) == 0 && S_ISDIR( st.st_mode ) ) ? arg : "";
    }
    char *base = getRepGamePath( );
    std::string dir = std::string( base ) + REPGAME_PATH_DIVIDOR + arg;
    free( base );
    return ( stat( dir.c_str( ), &st ) == 0 && S_ISDIR( st.st_mode ) ) ? dir : "";
}

inspect::Result inspect::block( const char *dir, const int x, const int y, const int z ) {
    MapStorage ms;
    ms.init_readonly( dir );
    const glm::ivec3 cpos{ chunk_coord( x, CHUNK_SIZE_X ), chunk_coord( y, CHUNK_SIZE_Y ), chunk_coord( z, CHUNK_SIZE_Z ) };
    const glm::ivec3 local = glm::ivec3( x, y, z ) - cpos * CHUNK_DIM;
    Result r;
    if ( !ms.check_if_chunk_exists( cpos ) ) {
        r.json = jf( "{\"pos\":[%d,%d,%d],\"chunk\":[%d,%d,%d],\"local\":[%d,%d,%d],\"id\":null,\"name\":null,\"saved\":false}",
                     x, y, z, cpos.x, cpos.y, cpos.z, local.x, local.y, local.z );
        r.human = jf( "block (%d,%d,%d): no saved chunk_%d_%d_%d — generated terrain, not stored",
                      x, y, z, cpos.x, cpos.y, cpos.z );
        return r;
    }
    std::vector<BlockState> cells( CHUNK_BLOCK_SIZE, BLOCK_STATE_AIR );
    if ( !load_cells( ms, cpos, cells.data( ) ) ) {
        r.ok = false;
        r.json = jf( "{\"error\":\"chunk file unreadable\",\"chunk\":[%d,%d,%d]}", cpos.x, cpos.y, cpos.z );
        r.human = "chunk file unreadable";
        return r;
    }
    const BlockState &s = cell_at( cells.data( ), local );
    r.json = jf( "{\"pos\":[%d,%d,%d],\"chunk\":[%d,%d,%d],\"local\":[%d,%d,%d],\"id\":%d,\"name\":\"%s\","
                 "\"rotation\":%d,\"redstone_power\":%d,\"waterlogged\":%s,\"saved\":true}",
                 x, y, z, cpos.x, cpos.y, cpos.z, local.x, local.y, local.z, static_cast<int>( s.id ), block_name_of( s.id ),
                 static_cast<int>( s.rotation ), s.current_redstone_power, s.waterlogged ? "true" : "false" );
    r.human = jf( "block (%d,%d,%d) chunk (%d,%d,%d) local (%d,%d,%d): %s id=%d rot=%d power=%d%s",
                  x, y, z, cpos.x, cpos.y, cpos.z, local.x, local.y, local.z, block_name_of( s.id ),
                  static_cast<int>( s.id ), static_cast<int>( s.rotation ), s.current_redstone_power,
                  s.waterlogged ? " waterlogged" : "" );
    return r;
}

inspect::Result inspect::slice( const char *dir, const int y, const int x1, const int z1, const int x2, const int z2 ) {
    Result r;
    const int w = x2 - x1 + 1;
    const int h = z2 - z1 + 1;
    if ( w <= 0 || h <= 0 || w > 256 || h > 256 ) {
        r.ok = false;
        r.json = jf( "{\"error\":\"slice must be 1..256 blocks per side, got %dx%d\"}", w, h );
        r.human = "slice must be 1..256 blocks per side";
        return r;
    }
    MapStorage ms;
    ms.init_readonly( dir );
    const int cy = chunk_coord( y, CHUNK_SIZE_Y );
    // Load every chunk the rect touches, keyed by (cx,cz).
    std::map<std::pair<int, int>, std::vector<BlockState>> cache;
    auto cells_for = [&]( const int wx, const int wz ) -> const BlockState * {
        const int cx = chunk_coord( wx, CHUNK_SIZE_X );
        const int cz = chunk_coord( wz, CHUNK_SIZE_Z );
        const auto key = std::make_pair( cx, cz );
        auto it = cache.find( key );
        if ( it == cache.end( ) ) {
            std::vector<BlockState> cells( CHUNK_BLOCK_SIZE, BLOCK_STATE_AIR );
            load_cells( ms, glm::ivec3( cx, cy, cz ), cells.data( ) );
            it = cache.emplace( key, std::move( cells ) ).first;
        }
        return it->second.data( );
    };
    // One char per block name (' ' for AIR).
    std::map<std::string, char> legend;
    legend["AIR"] = ' ';
    char next_char = 'a';
    auto char_for = [&]( const std::string &name ) {
        auto it = legend.find( name );
        if ( it != legend.end( ) ) {
            return it->second;
        }
        char c = next_char;
        if ( c == 'z' ) {
            next_char = 'A';
        } else if ( c == 'Z' ) {
            next_char = '0';
        } else if ( c == '9' ) {
            next_char = '!';
        } else {
            next_char = static_cast<char>( c + 1 );
        }
        legend[name] = c;
        return c;
    };
    std::string rows_json, rows_text;
    for ( int wz = z1; wz <= z2; wz++ ) {
        std::string row;
        for ( int wx = x1; wx <= x2; wx++ ) {
            const int cx = chunk_coord( wx, CHUNK_SIZE_X );
            const int cz = chunk_coord( wz, CHUNK_SIZE_Z );
            const BlockState *cells = cells_for( wx, wz );
            const glm::ivec3 local{ wx - cx * CHUNK_SIZE_X, y - cy * CHUNK_SIZE_Y, wz - cz * CHUNK_SIZE_Z };
            const BlockState &s = cell_at( cells, local );
            row += char_for( block_name_of( s.id ) );
        }
        rows_json += rows_json.empty() ? "" : ",";
        rows_json += "\"" + row + "\"";
        rows_text += "    " + row + "\n";
    }
    std::string legend_json, legend_text;
    for ( const auto &kv : legend ) {
        legend_json += legend_json.empty() ? "" : ",";
        legend_json += jf( "\"%c\":\"%s\"", kv.second, kv.first.c_str( ) );
        if ( kv.second != ' ' ) {
            legend_text += jf( "    %c = %s\n", kv.second, kv.first.c_str( ) );
        }
    }
    r.json = jf( "{\"y\":%d,\"x1\":%d,\"z1\":%d,\"x2\":%d,\"z2\":%d,\"legend\":{%s},\"rows\":[%s]}",
                 y, x1, z1, x2, z2, legend_json.c_str( ), rows_json.c_str( ) );
    r.human = jf( "slice y=%d x %d..%d z %d..%d\n%slegend:\n%s", y, x1, x2, z1, z2, rows_text.c_str( ), legend_text.c_str( ) );
    return r;
}

inspect::Result inspect::column( const char *dir, const int x, const int z, const int y0, const int y1 ) {
    Result r;
    if ( y1 < y0 ) {
        r.ok = false;
        r.json = jf( "{\"error\":\"y1 < y0\"}" );
        r.human = "y1 < y0";
        return r;
    }
    MapStorage ms;
    ms.init_readonly( dir );
    const int cx = chunk_coord( x, CHUNK_SIZE_X );
    const int cz = chunk_coord( z, CHUNK_SIZE_Z );
    std::map<int, std::vector<BlockState>> cache;
    std::string runs_json, runs_text;
    int run_start = y0;
    BlockID run_id = static_cast<BlockID>( -1 );
    auto flush = [&]( const int upto ) {
        if ( run_id == static_cast<BlockID>( -1 ) ) {
            return;
        }
        runs_json += runs_json.empty() ? "" : ",";
        runs_json += jf( "{\"y0\":%d,\"y1\":%d,\"name\":\"%s\",\"id\":%d}", run_start, upto - 1,
                         block_name_of( run_id ), static_cast<int>( run_id ) );
        runs_text += jf( "    %4d..%4d %s\n", run_start, upto - 1, block_name_of( run_id ) );
    };
    for ( int y = y0; y <= y1; y++ ) {
        const int cy = chunk_coord( y, CHUNK_SIZE_Y );
        auto it = cache.find( cy );
        if ( it == cache.end( ) ) {
            std::vector<BlockState> cells( CHUNK_BLOCK_SIZE, BLOCK_STATE_AIR );
            load_cells( ms, glm::ivec3( cx, cy, cz ), cells.data( ) );
            it = cache.emplace( cy, std::move( cells ) ).first;
        }
        const glm::ivec3 local{ x - cx * CHUNK_SIZE_X, y - cy * CHUNK_SIZE_Y, z - cz * CHUNK_SIZE_Z };
        const BlockID id = cell_at( it->second.data( ), local ).id;
        if ( id != run_id ) {
            flush( y );
            run_id = id;
            run_start = y;
        }
    }
    flush( y1 + 1 );
    r.json = jf( "{\"x\":%d,\"z\":%d,\"y0\":%d,\"y1\":%d,\"runs\":[%s]}", x, z, y0, y1, runs_json.c_str( ) );
    r.human = jf( "column x=%d z=%d y %d..%d\n%s", x, z, y0, y1, runs_text.c_str( ) );
    return r;
}

inspect::Result inspect::chunk( const char *dir, const int cx, const int cy, const int cz ) {
    Result r;
    const glm::ivec3 off{ cx, cy, cz };
    MapStorage ms;
    ms.init_readonly( dir );
    char file_name[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_CHUNK, ms.get_map_name( ), cx, cy, cz );
    struct stat st;
    if ( stat( file_name, &st ) != 0 ) {
        r.json = jf( "{\"chunk\":[%d,%d,%d],\"saved\":false}", cx, cy, cz );
        r.human = jf( "chunk (%d,%d,%d): no file — generated terrain, not stored", cx, cy, cz );
        return r;
    }
    std::vector<BlockState> cells( CHUNK_BLOCK_SIZE, BLOCK_STATE_AIR );
    std::map<std::string, long> counts;
    long non_air = 0;
    if ( load_cells( ms, off, cells.data( ) ) ) {
        for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
            const BlockID id = cells[ i ].id;
            if ( id == AIR ) {
                continue;
            }
            non_air++;
            counts[ block_name_of( id ) ]++;
        }
    }
    std::vector<std::pair<std::string, long>> top( counts.begin( ), counts.end( ) );
    std::sort( top.begin( ), top.end( ), []( const auto &a, const auto &b ) { return a.second > b.second; } );
    std::string counts_json, counts_text;
    for ( size_t i = 0; i < top.size( ) && i < 20; i++ ) {
        counts_json += counts_json.empty() ? "" : ",";
        counts_json += jf( "\"%s\":%ld", top[ i ].first.c_str( ), top[ i ].second );
        counts_text += jf( "    %6ld %s\n", top[ i ].second, top[ i ].first.c_str( ) );
    }
    r.json = jf( "{\"chunk\":[%d,%d,%d],\"saved\":true,\"size\":%ld,\"non_air\":%ld,\"counts\":{%s}}",
                 cx, cy, cz, static_cast<long>( st.st_size ), non_air, counts_json.c_str( ) );
    r.human = jf( "chunk (%d,%d,%d): %ld bytes, %ld non-air cells\ncounts:\n%s",
                  cx, cy, cz, static_cast<long>( st.st_size ), non_air, counts_text.c_str( ) );
    return r;
}

inspect::Result inspect::entities( const char *dir, const double *near, const double radius ) {
    Result r;
    MapStorage ms;
    ms.init_readonly( dir );
    const DirScan scan = scan_entity_files( dir );
    auto within = [&]( const double x, const double y, const double z ) {
        if ( near == nullptr ) {
            return true;
        }
        const double dx = x - near[ 0 ], dy = y - near[ 1 ], dz = z - near[ 2 ];
        return dx * dx + dy * dy + dz * dz <= radius * radius;
    };
    std::string list_json, list_text;
    auto emit = [&]( const char *kind, const std::string &body_json, const std::string &line ) {
        list_json += list_json.empty() ? "" : ",";
        list_json += jf( "{\"kind\":\"%s\",%s}", kind, body_json.c_str( ) );
        list_text += "    " + line + "\n";
    };
    for ( const glm::ivec3 &off : scan.chests ) {
        ChestContentsMap chests;
        ms.load_chests( off, chests );
        for ( const auto &kv : chests ) {
            const glm::ivec3 &p = kv.first;
            if ( !within( p.x, p.y, p.z ) ) {
                continue;
            }
            std::string items;
            for ( int i = 0; i < CHEST_SLOTS; i++ ) {
                const InventorySlot &s = kv.second.slots[ i ];
                if ( s.item_id == LAST_BLOCK_ID || s.quantity <= 0 ) {
                    continue;
                }
                items += jf( "%s%sx%d", items.empty() ? "" : ",", item_name( s.item_id ), s.quantity );
            }
            emit( "chest", jf( "\"pos\":[%d,%d,%d],\"items\":\"%s\"", p.x, p.y, p.z, items.c_str( ) ),
                  jf( "chest (%d,%d,%d): %s", p.x, p.y, p.z, items.empty() ? "empty" : items.c_str( ) ) );
        }
    }
    for ( const glm::ivec3 &off : scan.mobs ) {
        std::vector<MobFileRecord> recs;
        ms.load_mobs( off, recs );
        for ( const MobFileRecord &m : recs ) {
            if ( !within( m.x, m.y, m.z ) ) {
                continue;
            }
            const char *name = m.type < MOB_TYPE_COUNT && mob_types[ m.type ].name != nullptr ? mob_types[ m.type ].name : "?";
            emit( "mob", jf( "\"type\":\"%s\",\"pos\":[%.3f,%.3f,%.3f],\"health\":%.1f,\"net_id\":%llu",
                             name, m.x, m.y, m.z, m.health, static_cast<unsigned long long>( m.net_id ) ),
                  jf( "mob %s (%.1f,%.1f,%.1f) hp=%.1f net_id=%llu", name, m.x, m.y, m.z, m.health,
                      static_cast<unsigned long long>( m.net_id ) ) );
        }
    }
    for ( const glm::ivec3 &off : scan.items ) {
        std::vector<ItemFileRecord> recs;
        ms.load_items( off, recs );
        for ( const ItemFileRecord &it : recs ) {
            if ( !within( it.x, it.y, it.z ) ) {
                continue;
            }
            emit( "item", jf( "\"name\":\"%s\",\"count\":%d,\"pos\":[%.3f,%.3f,%.3f]",
                              item_name( it.item ), it.count, it.x, it.y, it.z ),
                  jf( "item %s x%d (%.1f,%.1f,%.1f)", item_name( it.item ), it.count, it.x, it.y, it.z ) );
        }
    }
    r.json = jf( "{\"entities\":[%s]}", list_json.c_str( ) );
    r.human = list_text.empty() ? std::string( "no saved entities\n" ) : "entities:\n" + list_text;
    return r;
}

inspect::Result inspect::player( const char *dir ) {
    Result r;
    MapStorage ms;
    ms.init_readonly( dir );
    PlayerData pd;
    const bool has_player = ms.read_player_data( pd ) != 0;
    int64_t world_time = 0;
    const bool has_time = ms.read_world_time( world_time ) != 0;
    char type_path[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( type_path, CHUNK_NAME_MAX_LENGTH, "%s/world_type", dir );
    std::string world_type;
    if ( FILE *f = fopen( type_path, "rb" ) ) {
        char tok[ 64 ] = { 0 };
        if ( fscanf( f, "%63s", tok ) == 1 ) {
            world_type = tok;
        }
        fclose( f );
    }
    auto slots_json = [&]( const InventorySlot *slots, const int n ) {
        std::string out;
        for ( int i = 0; i < n; i++ ) {
            if ( slots[ i ].item_id == LAST_BLOCK_ID || slots[ i ].quantity <= 0 ) {
                continue;
            }
            out += out.empty() ? "" : ",";
            out += jf( "{\"slot\":%d,\"name\":\"%s\",\"count\":%d}", i, item_name( slots[ i ].item_id ), slots[ i ].quantity );
        }
        return "[" + out + "]";
    };
    r.json = jf( "{\"player_dat\":%s,\"pos\":[%.4f,%.4f,%.4f],\"angle_h\":%.2f,\"angle_v\":%.2f,"
                 "\"flying\":%s,\"no_clip\":%s,\"quality\":%d,\"game_mode\":\"%s\",\"render_distance\":%d,"
                 "\"min_ambient\":%.3f,\"world_time\":%lld,\"world_type\":\"%s\","
                 "\"hotbar\":%s,\"survival\":%s}",
                 has_player ? "true" : "false", pd.world_x, pd.world_y, pd.world_z, pd.angle_H, pd.angle_V,
                 pd.flying ? "true" : "false", pd.no_clip ? "true" : "false", pd.worldDrawQuality,
                 pd.game_mode == GameMode_Survival ? "survival" : "creative", pd.render_distance,
                 pd.min_ambient, static_cast<long long>( has_time ? world_time : pd.world_time ),
                 world_type.c_str( ), slots_json( pd.hotbar_inventory, HOTBAR_WIDTH * HOTBAR_HEIGHT ).c_str( ),
                 slots_json( pd.survival_inventory, SURVIVAL_INVENTORY_WIDTH * SURVIVAL_INVENTORY_HEIGHT ).c_str( ) );
    r.human = jf( "player.dat: %s\n  pos (%.2f,%.2f,%.2f) yaw %.1f pitch %.1f\n  %s%s quality %d rd %d\n"
                  "  world_time %lld world_type %s",
                  has_player ? "present" : "absent", pd.world_x, pd.world_y, pd.world_z, pd.angle_H, pd.angle_V,
                  pd.game_mode == GameMode_Survival ? "survival" : "creative",
                  pd.flying ? " flying" : "", pd.worldDrawQuality, pd.render_distance,
                  static_cast<long long>( has_time ? world_time : pd.world_time ),
                  world_type.empty() ? "(none)" : world_type.c_str( ) );
    return r;
}

namespace {

void usage( ) {
    fprintf( stderr,
             "usage: RepGame inspect <world> <sub> ...\n"
             "  world: a world name under $HOME/.repgame, or a path to a world dir\n"
             "  block x y z                          one saved block (saved:false = generated, not stored)\n"
             "  slice y x1 z1 x2 z2 [--png out.png]  horizontal layer, ASCII grid + legend (max 256x256)\n"
             "  column x z [--y0 a --y1 b]           vertical column, run-length encoded\n"
             "  chunk cx cy cz                       file size + per-block counts (top 20)\n"
             "  entities [--near x y z --radius r]   chests, mobs, items from save files\n"
             "  player                               player.dat + world.dat + world_type\n"
             "  --json prints exactly one JSON object. Light is not persisted; use\n"
             "  `probe_light` in a rep-dbg script to inspect light in a live world.\n" );
}

// Parse --json anywhere and remove it. `--png out.png` is consumed by the
// Python side; accepted (and ignored) here so a direct CLI call stays valid.
struct Args {
    bool json = false;
    std::vector<std::string> pos;
};

Args parse_args( const int argc, char **argv ) {
    Args a;
    for ( int i = 0; i < argc; i++ ) {
        if ( strcmp( argv[ i ], "--json" ) == 0 ) {
            a.json = true;
        } else if ( strcmp( argv[ i ], "--png" ) == 0 && i + 1 < argc ) {
            i++;
        } else {
            a.pos.emplace_back( argv[ i ] );
        }
    }
    return a;
}

} // namespace

int inspect::run( const int argc, char **argv ) {
    const Args a = parse_args( argc, argv );
    if ( a.pos.size( ) < 2 ) {
        usage( );
        return 4;
    }
    const std::string dir = resolve_world_dir( a.pos[ 0 ].c_str( ) );
    if ( dir.empty( ) ) {
        fprintf( stderr, "inspect: no such world/dir: %s\n", a.pos[ 0 ].c_str( ) );
        return 4;
    }
    block_definitions_initilize_definitions( nullptr );

    // pr_debug writes to stdout — send it to stderr for this command so
    // --json output is exactly one object.
    fflush( stdout );
    const int saved_out = dup( STDOUT_FILENO );
    dup2( STDERR_FILENO, STDOUT_FILENO );

    inspect::Result r;
    const std::string &sub = a.pos[ 1 ];
    auto arg = [&]( const int i ) { return atoi( a.pos[ i ].c_str( ) ); };
    if ( sub == "block" && a.pos.size( ) == 5 ) {
        r = inspect::block( dir.c_str( ), arg( 2 ), arg( 3 ), arg( 4 ) );
    } else if ( sub == "slice" && a.pos.size( ) == 7 ) {
        r = inspect::slice( dir.c_str( ), arg( 2 ), arg( 3 ), arg( 4 ), arg( 5 ), arg( 6 ) );
    } else if ( sub == "column" && a.pos.size( ) >= 4 ) {
        int y0 = -4096, y1 = 4096;
        for ( size_t i = 4; i + 1 < a.pos.size( ); i++ ) {
            if ( a.pos[ i ] == "--y0" ) {
                y0 = atoi( a.pos[ ++i ].c_str( ) );
            } else if ( a.pos[ i ] == "--y1" ) {
                y1 = atoi( a.pos[ ++i ].c_str( ) );
            }
        }
        r = inspect::column( dir.c_str( ), arg( 2 ), arg( 3 ), y0, y1 );
    } else if ( sub == "chunk" && a.pos.size( ) == 5 ) {
        r = inspect::chunk( dir.c_str( ), arg( 2 ), arg( 3 ), arg( 4 ) );
    } else if ( sub == "entities" ) {
        double near[ 3 ];
        bool use_near = false;
        double radius = 32.0;
        for ( size_t i = 2; i + 1 < a.pos.size( ); i++ ) {
            if ( a.pos[ i ] == "--near" && i + 3 < a.pos.size( ) ) {
                near[ 0 ] = atof( a.pos[ i + 1 ].c_str( ) );
                near[ 1 ] = atof( a.pos[ i + 2 ].c_str( ) );
                near[ 2 ] = atof( a.pos[ i + 3 ].c_str( ) );
                use_near = true;
                i += 3;
            } else if ( a.pos[ i ] == "--radius" ) {
                radius = atof( a.pos[ ++i ].c_str( ) );
            }
        }
        r = inspect::entities( dir.c_str( ), use_near ? near : nullptr, radius );
    } else if ( sub == "player" && a.pos.size( ) == 2 ) {
        r = inspect::player( dir.c_str( ) );
    } else {
        usage( );
        dup2( saved_out, STDOUT_FILENO );
        close( saved_out );
        return 4;
    }

    fflush( stdout );
    dup2( saved_out, STDOUT_FILENO );
    close( saved_out );
    if ( a.json ) {
        puts( r.json.c_str( ) );
    } else {
        puts( r.human.c_str( ) );
    }
    return r.ok ? 0 : 1;
}
