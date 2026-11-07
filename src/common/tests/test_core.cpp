// ecs / vr projection / min_ambient / angle-wrap / teleport-parse tests —
// split out of rep_tests.cpp.
#include "test_util.hpp"
#include "common/utils/angles.hpp"
#include "common/harness/run_policy.hpp"
int test_ecs( ) {
    ECS ecs = ECS( );
    return ecs.test_ecs( );
}

// A symmetric ±45° horizontal/vertical FOV through the off-axis OpenXR
// projection helper must equal glm::perspective(90°, 1, n, f).
int test_vr_projection( ) {
    const float n = 0.1f, f = 1000.0f;
    const float half = glm::radians( 45.0f );
    const glm::mat4 got = RepGame::projection_from_fov( -half, half, half, -half, n, f );
    const glm::mat4 want = glm::perspective( glm::radians( 90.0f ), 1.0f, n, f );
    float worst = 0.0f;
    for ( int c = 0; c < 4; c++ ) {
        for ( int r = 0; r < 4; r++ ) {
            worst = glm::max( worst, fabsf( got[ c ][ r ] - want[ c ][ r ] ) );
        }
    }
    pr_test( "  %-55s worst=%g  %s", "symmetric fov == glm::perspective", worst, worst < 1e-5f ? "PASS" : "FAIL" );
    return worst < 1e-5f ? 0 : 1;
}

// Pause-menu stepper for the min_ambient light floor: snaps to the
// MIN_AMBIENT_STEP grid (off-grid inputs snap first) and clamps to
// [0, MIN_AMBIENT_MAX].
int test_min_ambient_step( ) {
    int failures = 0;
    auto check_near = [ & ]( const char *name, float actual, float expected ) {
        const bool ok = fabsf( actual - expected ) <= 1e-6f;
        pr_test( "  %-55s value=%-9.4f expected=%-9.4f  %s", name, actual, expected, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };
    check_near( "min_ambient_step: 5% +1 -> 10%", min_ambient_step( 0.05f, 1 ), 0.10f );
    check_near( "min_ambient_step: 5% -1 -> 0%", min_ambient_step( 0.05f, -1 ), 0.0f );
    check_near( "min_ambient_step: 0% -1 clamps at 0%", min_ambient_step( 0.0f, -1 ), 0.0f );
    check_near( "min_ambient_step: 100% +1 clamps at 100%", min_ambient_step( 1.0f, 1 ), 1.0f );
    check_near( "min_ambient_step: off-grid 7% +1 -> 10%", min_ambient_step( 0.07f, 1 ), 0.10f );
    return failures;
}

// wrap_degrees_360 lands in [0,360) at the boundaries; wrap_degrees_180 lands
// in [-180,180] (shortest-path deltas).
int test_angle_wrap( ) {
    int failures = 0;
    auto check_near = [ & ]( const char *name, float actual, float expected ) {
        const bool ok = fabsf( actual - expected ) <= 1e-6f;
        pr_test( "  %-55s value=%-9.4f expected=%-9.4f  %s", name, actual, expected, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };
    check_near( "wrap360: 0", wrap_degrees_360( 0.0f ), 0.0f );
    check_near( "wrap360: 360 -> 0", wrap_degrees_360( 360.0f ), 0.0f );
    check_near( "wrap360: -0.5 -> 359.5", wrap_degrees_360( -0.5f ), 359.5f );
    check_near( "wrap360: 719.5 -> 359.5", wrap_degrees_360( 719.5f ), 359.5f );
    check_near( "wrap360: -540 -> 180", wrap_degrees_360( -540.0f ), 180.0f );
    check_near( "wrap180: 0", wrap_degrees_180( 0.0f ), 0.0f );
    check_near( "wrap180: 180", wrap_degrees_180( 180.0f ), 180.0f );
    check_near( "wrap180: -180", wrap_degrees_180( -180.0f ), -180.0f );
    check_near( "wrap180: 540 -> 180", wrap_degrees_180( 540.0f ), 180.0f );
    check_near( "wrap180: -540 -> -180", wrap_degrees_180( -540.0f ), -180.0f );
    check_near( "wrap180: 719.5 -> -0.5", wrap_degrees_180( 719.5f ), -0.5f );
    return failures;
}

// REPGAME_TELEPORT spec parsing: >=4 fields valid; spin/pitch/sprint default
// to 0, walk defaults to 1 (also for missing/invalid specs).
int test_teleport_parse( ) {
    int failures = 0;
    auto check = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };
    TeleportSpec tp = parse_teleport( "1,2,3,4" );
    check( "4 fields: valid", tp.valid && tp.pos == glm::dvec3( 1, 2, 3 ) && tp.angle == 4.0f );
    check( "4 fields: defaults", tp.spin == 0.0f && tp.pitch == 0.0f && !tp.sprint && tp.walk == 1 );
    tp = parse_teleport( "1,2,3,4,1,0.5" );
    check( "6 fields: sprint + spin", tp.valid && tp.sprint && tp.spin == 0.5f && tp.pitch == 0.0f && tp.walk == 1 );
    tp = parse_teleport( "1,2,3,4,0,0.5,-10" );
    check( "7 fields: pitch, walk default", tp.valid && tp.pitch == -10.0f && tp.walk == 1 );
    tp = parse_teleport( "0.5,80,0.5,45,1,0.5,-10,0" );
    check( "8 fields: walk=0", tp.valid && tp.walk == 0 && tp.pos == glm::dvec3( 0.5, 80, 0.5 ) && tp.angle == 45.0f );
    tp = parse_teleport( "1,2,3" );
    check( "3 fields: invalid, walk still 1", !tp.valid && tp.walk == 1 );
    tp = parse_teleport( nullptr );
    check( "nullptr: invalid, walk still 1", !tp.valid && tp.walk == 1 );
    return failures;
}

