// ecs / vr projection / min_ambient tests — split out of rep_tests.cpp.
#include "test_util.hpp"
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

