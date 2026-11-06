#include "common/harness/run_policy.hpp"

#include <string>
#include <stdio.h>
#include <string.h>
#include <cmath>

#include "common/utils/env_config.hpp"
#include "common/Logging.hpp"

static void capture_parse( RunPolicy &p ) {
    const char *env = env_get( "REPGAME_CAPTURE" );
    if ( env == nullptr || env[ 0 ] == '\0' ) {
        return;
    }
    std::string spec = env;
    size_t start = 0;
    while ( start <= spec.size( ) ) {
        const size_t end = spec.find( ';', start );
        const std::string view_str = spec.substr( start, end == std::string::npos ? std::string::npos : end - start );
        if ( view_str.empty( ) ) {
            break;
        }
        CaptureView view = { 0, 0, 0, 0, 0, 0 };
        if ( sscanf( view_str.c_str( ), "%lf,%lf,%lf,%f,%f,%d", &view.x, &view.y, &view.z, &view.yaw, &view.pitch, &view.flags ) >= 5 ) {
            p.capture.views.push_back( view );
        }
        if ( end == std::string::npos ) {
            break;
        }
        start = end + 1;
    }
    if ( p.capture.views.empty( ) ) {
        return;
    }
    p.capture.enabled = true;
    if ( const char *v = env_get( "REPGAME_CAPTURE_QUALITY" ) ) {
        p.capture.quality = static_cast<WorldDrawQuality>( atoi( v ) );
    }
    p.capture.world_time = env_long( "REPGAME_CAPTURE_TIME", p.capture.world_time );
    p.capture.settle_ticks = env_long( "REPGAME_CAPTURE_SETTLE", p.capture.settle_ticks );
    p.capture.timeout_ticks = env_long( "REPGAME_CAPTURE_TIMEOUT", p.capture.timeout_ticks );
    p.capture.view_ticks = env_long( "REPGAME_CAPTURE_VIEW_TICKS", p.capture.view_ticks );
    pr_debug( "Capture mode: %zu view(s), quality=%d time=%ld settle=%ld timeout=%ld view_ticks=%ld", p.capture.views.size( ),
              static_cast<int>( p.capture.quality ), p.capture.world_time, p.capture.settle_ticks, p.capture.timeout_ticks, p.capture.view_ticks );
}

static void fly_parse( RunPolicy &p ) {
    const char *env = env_get( "REPGAME_FLY" );
    if ( env == nullptr || env[ 0 ] == '\0' ) {
        return;
    }
    double x, y, z, speed = 8.0;
    float yaw, pitch;
    if ( sscanf( env, "%lf,%lf,%lf,%f,%f,%lf", &x, &y, &z, &yaw, &pitch, &speed ) < 5 ) {
        pr_debug( "REPGAME_FLY: bad spec '%s' (want x,y,z,yaw,pitch[,speed])", env );
        return;
    }
    p.fly.enabled = true;
    p.fly.start = glm::dvec3( x, y, z );
    p.fly.yaw = yaw;
    p.fly.pitch = pitch;
    p.fly.speed = speed;
    // Same look-vector convention as build_camera_view.
    p.fly.dir = glm::normalize( glm::dvec3( sin( yaw * ( M_PI / 180 ) ), -tan( pitch * ( M_PI / 180 ) ), -cos( yaw * ( M_PI / 180 ) ) ) );
    p.fly.exit_tick = env_long( "REPGAME_FLY_TICKS", 0 );
    p.fly.metric_every = env_long( "REPGAME_FLY_METRICS", 1 );
    p.fly.settle_ticks = env_long( "REPGAME_FLY_SETTLE", 0 );
    p.fly.settle_timeout = env_long( "REPGAME_FLY_SETTLE_TIMEOUT", 30 * UPS_RATE );
    p.fly.fly_ticks = env_long( "REPGAME_FLY_FLY_TICKS", -1 );
    if ( const char *v = env_get( "REPGAME_FLY_RD" ) ) {
        char buf[ 256 ];
        snprintf( buf, sizeof( buf ), "%s", v );
        for ( char *tok = strtok( buf, "," ); tok && p.fly.rd_count < FlyState::RD_SCHEDULE_MAX; tok = strtok( nullptr, "," ) ) {
            long t;
            int r;
            if ( sscanf( tok, "%ld:%d", &t, &r ) != 2 ) {
                pr_debug( "REPGAME_FLY_RD: bad entry '%s' (want tick:radius)", tok );
                break;
            }
            p.fly.rd_ticks[ p.fly.rd_count ] = t;
            p.fly.rd_radius[ p.fly.rd_count ] = r;
            p.fly.rd_count++;
        }
    }
    pr_debug( "Fly mode: start=(%.1f,%.1f,%.1f) yaw=%.1f pitch=%.1f speed=%.2f metrics=%ld exit=%ld settle=%ld/%ld fly=%ld", x, y,
              z, yaw, pitch, speed, p.fly.metric_every, p.fly.exit_tick, p.fly.settle_ticks, p.fly.settle_timeout,
              p.fly.fly_ticks );
}

static RunPolicy run_policy_parse( ) {
    RunPolicy p;
    capture_parse( p );
    fly_parse( p );
    p.walktest = env_is_set( "REPGAME_WALKTEST" );
    p.walktest_ticks = env_long( "REPGAME_WALKTEST_TICKS", 0 );
    p.teleport = env_get( "REPGAME_TELEPORT" );
    p.screenshot_tick = env_long( "REPGAME_SCREENSHOT_TICK", 0 );
    // Periodic/anomaly readback scans work without REPGAME_CAPTURE too.
    p.capture_periodic = env_int( "REPGAME_CAPTURE_PERIODIC", 0 );
    p.capture_anomaly = env_int( "REPGAME_CAPTURE_ANOMALY", 0 );
    p.script.path = env_get( "REPGAME_SCRIPT" );
    p.script.enabled = p.script.path != nullptr && p.script.path[ 0 ] != '\0';
    p.script.keep = env_int( "REPGAME_SCRIPT_KEEP", 0 ) != 0;
    if ( const char *v = env_get( "REPGAME_DEBUG_CRASH" ) ) {
        const char *at = strchr( v, '@' );
        std::string mode( v, at ? ( size_t )( at - v ) : strlen( v ) );
        if ( mode == "segv" ) {
            p.debug_crash = 1;
        } else if ( mode == "abort" ) {
            p.debug_crash = 2;
        }
        if ( at ) {
            p.debug_crash_tick = atol( at + 1 );
        }
    }
    p.results_path = env_get( "REPGAME_RESULTS" );
    p.results_every = env_long( "REPGAME_RESULTS_EVERY", 20 );
    p.git_sha = env_get( "REPGAME_GIT" );
    p.profile = env_int( "REPGAME_PROFILE", 0 ) != 0;
    return p;
}

RunPolicy &run_policy( ) {
    static RunPolicy policy = run_policy_parse( );
    return policy;
}
