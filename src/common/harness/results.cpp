// Structured run-results writer — see results.hpp and docs/debug-tools.md.

#include "common/harness/results.hpp"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <vector>

#include "common/RepGame.hpp"
#include "common/utils/profile.hpp"
#include "common/block_definitions.hpp"
#include "common/harness/run_policy.hpp"
#include "common/light.hpp"
#include "common/Logging.hpp"

static FILE *g_file = nullptr;
static bool g_inited = false;
static std::mutex g_mutex;
static std::string g_exit_reason;
static std::vector<std::string> g_shots;
static int g_asserts_total = 0;
static int g_asserts_failed = 0;

static const char *build_string( ) {
#if defined( __SANITIZE_ADDRESS__ )
    return "asan";
#elif defined( __SANITIZE_THREAD__ )
    return "tsan";
#elif defined( __OPTIMIZE__ )
    return "release";
#else
    return "debug";
#endif
}

std::string results::json_escape( const char *s ) {
    std::string out;
    if ( s == nullptr ) {
        return out;
    }
    for ( const char *p = s; *p != '\0'; p++ ) {
        const unsigned char c = static_cast<unsigned char>( *p );
        if ( c < 0x20 && c != '\n' && c != '\r' && c != '\t' ) {
            char u[ 8 ];
            snprintf( u, sizeof( u ), "\\u%04x", c );
            out += u;
            continue;
        }
        switch ( *p ) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += *p;
        }
    }
    return out;
}

// printf into a string sized to fit — a truncated record is invalid JSON.
static std::string vstrfmt( const char *fmt, va_list ap ) {
    va_list ap2;
    va_copy( ap2, ap );
    const int n = vsnprintf( nullptr, 0, fmt, ap2 );
    va_end( ap2 );
    std::string out;
    if ( n > 0 ) {
        out.resize( static_cast<size_t>( n ) + 1 );
        vsnprintf( &out[ 0 ], out.size( ), fmt, ap );
        out.resize( static_cast<size_t>( n ) );
    }
    return out;
}

static std::string strfmt( const char *fmt, ... ) {
    va_list ap;
    va_start( ap, fmt );
    std::string out = vstrfmt( fmt, ap );
    va_end( ap );
    return out;
}

// Locked append of one formatted line ( flushed per record so a crash loses
// at most the in-flight line).
static void write_line( const std::string &line ) {
    std::lock_guard<std::mutex> lock( g_mutex );
    if ( g_file == nullptr ) {
        return;
    }
    fwrite( line.data( ), 1, line.size( ), g_file );
    fputc( '\n', g_file );
    fflush( g_file );
}

static bool numeric( const std::string &v ) {
    if ( v.empty( ) ) {
        return false;
    }
    char *end = nullptr;
    strtod( v.c_str( ), &end );
    return end == v.c_str( ) + v.size( );
}

std::string results::light_json_from_dbg( const char *dbg ) {
    std::string out = "{";
    bool first = true;
    const char *prefix = "";
    for ( const char *p = dbg; p != nullptr && *p != '\0'; ) {
        if ( p[ 0 ] == '|' ) {
            // The "| us:" section repeats counter names (fin=...) with µs
            // values — prefix to keep them distinct.
            prefix = "us_";
            p++;
            continue;
        }
        if ( !isalpha( static_cast<unsigned char>( p[ 0 ] ) ) && p[ 0 ] != '_' ) {
            p++;
            continue;
        }
        const char *key_start = p;
        while ( isalnum( static_cast<unsigned char>( *p ) ) || *p == '_' ) {
            p++;
        }
        if ( *p != '=' ) {
            continue;
        }
        const std::string key = key_start == p ? "" : std::string( key_start, p - key_start );
        p++;
        const char *val_start = p;
        while ( *p != '\0' && *p != ' ' && *p != '\t' && *p != '(' && *p != ')' && *p != '[' && *p != ']' && *p != '|' ) {
            p++;
        }
        const std::string val( val_start, p - val_start );
        const std::string full_key = std::string( prefix ) + key;
        if ( out.find( "\"" + full_key + "\":" ) != std::string::npos ) {
            // light_dbg_stats repeats names (n=/fill= under fin() and again as
            // totals): keep the first occurrence so the object has no dup keys.
            continue;
        }
        out += first ? "\"" : ",\"";
        out += full_key;
        out += "\":";
        if ( numeric( val ) ) {
            out += val;
        } else {
            out += "\"";
            out += json_escape( val.c_str( ) );
            out += "\"";
        }
        first = false;
    }
    out += "}";
    return out;
}

std::string results::format_metric( long tick, int fly_phase, int rd, const glm::dvec3 &pos, const LoaderDebugStats &s, const std::string &light_json,
                                    const std::string &prof_json ) {
    char buf[ 2048 ];
    snprintf( buf, sizeof( buf ),
              "{\"type\":\"metric\",\"tick\":%ld,\"fly_phase\":%d,\"rd\":%d,\"pos\":[%.4f,%.4f,%.4f],"
              "\"loader\":{\"terrain_queued\":%d,\"terrain_results\":%d,\"chunks_loading\":%d,\"chunks_drawable\":%d,"
              "\"chunks_occluded\":%d,\"light_jobs\":%d,\"light_pending\":%d,\"light_claims\":%d,\"light_dirty\":%d,"
              "\"light_bfs_active\":%d,\"light_bfs_priority\":%d,\"light_bfs_starve\":%d,\"light_seeds\":%lld,"
              "\"light_dropped\":%lld,\"light_stale_count\":%d,\"light_jlag_us\":%lld,\"light_njlag_us\":%lld,"
              "\"async_submitted\":%lld,\"async_published\":%lld,\"async_discarded\":%lld,"
              "\"async_promoted\":%lld,\"async_in_flight\":%d},"
              "\"light\":",
              tick, fly_phase, rd, pos.x, pos.y, pos.z, s.terrain_queued, s.terrain_results, s.chunks_loading, s.chunks_drawable,
              s.chunks_occluded, s.light_jobs, s.light_pending, s.light_claims, s.light_dirty, s.light_bfs_active,
              s.light_bfs_priority, s.light_bfs_starve, s.light_seeds, s.light_dropped, s.light_stale_count,
              s.light_jlag_us, s.light_njlag_us,
              s.async_submitted, s.async_published, s.async_discarded, s.async_promoted, s.async_in_flight );
    std::string out = std::string( buf ) + light_json;
    if ( !prof_json.empty( ) ) {
        out += ",\"prof\":";
        out += prof_json;
    }
    return out + "}";
}

std::string results::format_event( long tick, const char *name, const std::string &detail_json ) {
    std::string out = "{\"type\":\"event\",\"tick\":" + std::to_string( tick ) + ",\"name\":\"" + json_escape( name ) + "\"";
    if ( !detail_json.empty( ) ) {
        out += ",\"detail\":{" + detail_json + "}";
    }
    out += "}";
    return out;
}

std::string results::format_probe( long tick, const glm::ivec3 &pos, int block, const char *name, int rotation, int sky, int blocklight ) {
    return strfmt( "{\"type\":\"probe\",\"tick\":%ld,\"pos\":[%d,%d,%d],\"block\":%d,\"name\":\"%s\",\"rotation\":%d,"
                   "\"sky\":%d,\"blocklight\":%d}",
                   tick, pos.x, pos.y, pos.z, block, json_escape( name ).c_str( ), rotation, sky, blocklight );
}

std::string results::format_assert( long tick, int line, const char *cmd, bool ok, const char *expected, const char *actual ) {
    return strfmt( "{\"type\":\"assert\",\"tick\":%ld,\"line\":%d,\"cmd\":\"%s\",\"ok\":%s,\"expected\":\"%s\",\"actual\":\"%s\"}",
                   tick, line, json_escape( cmd ).c_str( ), ok ? "true" : "false", json_escape( expected ).c_str( ),
                   json_escape( actual ).c_str( ) );
}

std::string results::format_summary( long ticks, const char *exit_reason, int asserts_total, int asserts_failed, long settle_tick, const std::string &shots_json ) {
    return strfmt( "{\"type\":\"summary\",\"tick\":%ld,\"ticks\":%ld,\"exit_reason\":\"%s\",\"asserts\":{\"total\":%d,\"failed\":%d},"
                   "\"shots\":%s,\"settle_tick\":%ld}",
                   ticks, ticks, json_escape( exit_reason ).c_str( ), asserts_total, asserts_failed,
                   shots_json.empty( ) ? "[]" : shots_json.c_str( ), settle_tick );
}

bool results::active( ) {
    return g_file != nullptr;
}

bool results::metric_due( long tick ) {
    return active( ) && run_policy( ).results_every > 0 && tick % run_policy( ).results_every == 0;
}

void results::init( const char *world_name ) {
    if ( g_inited ) {
        return;
    }
    g_inited = true;
    const char *path = run_policy( ).results_path;
    if ( path == nullptr || path[ 0 ] == '\0' ) {
        return;
    }
    g_file = fopen( path, "w" );
    if ( g_file == nullptr ) {
        pr_debug( "REPGAME_RESULTS: cannot open '%s'", path );
        return;
    }
    profile::set_enabled( run_policy( ).profile );
    const RunPolicy &p = run_policy( );
    char policy[ 2048 ];
    snprintf( policy, sizeof( policy ),
              "\"capture\":{\"enabled\":%d,\"views\":%zu,\"settle\":%ld,\"timeout\":%ld,\"view_ticks\":%ld,"
              "\"quality\":%d,\"world_time\":%ld},"
              "\"fly\":{\"enabled\":%d,\"metric_every\":%ld,\"exit_tick\":%ld,\"settle_ticks\":%ld,"
              "\"settle_timeout\":%ld,\"fly_ticks\":%ld,\"speed\":%.3f},"
              "\"walktest\":%d,\"walktest_ticks\":%ld,\"screenshot_tick\":%ld,"
              "\"capture_periodic\":%d,\"capture_anomaly\":%d,\"results_every\":%ld",
              p.capture.enabled ? 1 : 0, p.capture.views.size( ), p.capture.settle_ticks, p.capture.timeout_ticks,
              p.capture.view_ticks, static_cast<int>( p.capture.quality ), p.capture.world_time,
              p.fly.enabled ? 1 : 0, p.fly.metric_every, p.fly.exit_tick, p.fly.settle_ticks, p.fly.settle_timeout,
              p.fly.fly_ticks, p.fly.speed, p.walktest ? 1 : 0, p.walktest_ticks, p.screenshot_tick,
              p.capture_periodic, p.capture_anomaly, p.results_every );
    char header[ 2600 ];
    snprintf( header, sizeof( header ),
              "{\"type\":\"header\",\"schema\":1,\"git\":\"%s\",\"build\":\"%s\",\"world\":\"%s\",\"policy\":{%s},\"start_unix\":%ld}",
              json_escape( p.git_sha != nullptr ? p.git_sha : "" ).c_str( ), build_string( ), json_escape( world_name ).c_str( ), policy,
              static_cast<long>( time( nullptr ) ) );
    write_line( header );
}

void results::metric( const RepGameState &state, const LoaderDebugStats &s, const char *light_dbg ) {
    if ( !active( ) ) {
        return;
    }
    write_line( format_metric( state.tick_number, run_policy( ).fly.phase, state.input.render_distance, state.camera.pos, s,
                               light_json_from_dbg( light_dbg ), profile::json_and_reset( ) ) );
}

void results::event( long tick, const char *name, const char *fmt, ... ) {
    if ( !active( ) ) {
        return;
    }
    std::string detail;
    if ( fmt != nullptr && fmt[ 0 ] != '\0' ) {
        va_list ap;
        va_start( ap, fmt );
        detail = vstrfmt( fmt, ap );
        va_end( ap );
    }
    if ( strcmp( name, "capture_shot" ) == 0 || strcmp( name, "screenshot" ) == 0 ) {
        // Collect shot filenames for the summary record; the "file" detail
        // field holds the display image path.
        const std::string needle = "\"file\":\"";
        const size_t at = detail.find( needle );
        if ( at != std::string::npos ) {
            const size_t end = detail.find( '"', at + needle.size( ) );
            g_shots.push_back( detail.substr( at + needle.size( ), end - at - needle.size( ) ) );
        }
    }
    write_line( format_event( tick, name, detail ) );
}

void results::probe( const RepGameState &state, ChunkLoader &loader, const glm::ivec3 &pos ) {
    if ( !active( ) ) {
        return;
    }
    const BlockState bs = state.world.get_loaded_block( pos );
    const char *n = bs.id == LAST_BLOCK_ID ? "unloaded" : block_definition_get_name( bs.id );
    write_line( format_probe( state.tick_number, pos, static_cast<int>( bs.id ), n != nullptr ? n : "?", bs.rotation,
                              loader.light_probe( pos, LIGHT_CHANNEL_SKY ), loader.light_probe( pos, LIGHT_CHANNEL_BLOCK ) ) );
}

void results::assert_( const RepGameState &state, int line, const char *cmd, bool ok, const char *expected, const char *actual ) {
    g_asserts_total++;
    if ( !ok ) {
        g_asserts_failed++;
    }
    if ( !active( ) ) {
        return;
    }
    write_line( format_assert( state.tick_number, line, cmd, ok, expected, actual ) );
}

void results::note_exit_reason( const char *reason ) {
    if ( g_exit_reason.empty( ) ) {
        g_exit_reason = reason;
    }
}

void results::summary( const RepGameState &state ) {
    if ( !active( ) ) {
        return;
    }
    event( state.tick_number, "quit" );
    std::string shots = "[";
    for ( size_t i = 0; i < g_shots.size( ); i++ ) {
        shots += i == 0 ? "\"" : ",\"";
        shots += json_escape( g_shots[ i ].c_str( ) );
        shots += "\"";
    }
    shots += "]";
    const long settle_tick = run_policy( ).fly.phase > 0 ? run_policy( ).fly.fly_start_tick : 0;
    write_line( format_summary( state.tick_number, g_exit_reason.empty( ) ? "quit" : g_exit_reason.c_str( ), g_asserts_total,
                                g_asserts_failed, settle_tick, shots ) );
}
