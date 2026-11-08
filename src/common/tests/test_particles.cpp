// Heart-particle lifecycle: expiry must survive a spawn stamp slightly
// ahead of the draw's u_Time (mid-frame spawn vs frame-start clock capture)
// while still handling the real 3600 s clock rollover.
#include "common/heart_particles.hpp"
#include "common/Logging.hpp" // pr_test

int test_particles( ) {
    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool actual, bool expected ) {
        const bool ok = actual == expected;
        pr_test( "  %-55s actual=%d expected=%d  %s", name, (int)actual, (int)expected, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };

    // The bug that made hearts invisible live: spawn stamped mid-frame is
    // ~1ms ahead of the frame-start u_Time — that is "not yet born", not a
    // clock rollover, so the particle must live.
    check_bool( "expiry: mid-frame spawn (stamp 1ms ahead)", HeartParticles::expired( 10.0f, 10.001f, 0.8f ), false );
    // Same-frame draw of a frame-time-stamped particle: age 0, alive.
    check_bool( "expiry: same-frame stamp", HeartParticles::expired( 10.0f, 10.0f, 0.8f ), false );
    // Normal mid-life and end-of-life draws.
    check_bool( "expiry: mid-life", HeartParticles::expired( 10.4f, 10.0f, 0.8f ), false );
    check_bool( "expiry: past life", HeartParticles::expired( 10.9f, 10.0f, 0.8f ), true );
    // Genuine wraparound: stamped at 3599.9, drawn at 0.1 is age 0.2 — alive.
    check_bool( "expiry: real wrap, still young", HeartParticles::expired( 0.1f, 3599.9f, 0.8f ), false );
    // Wrapped far enough to expire: stamp 3599.5 at 1.0 is age 1.5 > 0.8.
    check_bool( "expiry: real wrap, expired", HeartParticles::expired( 1.0f, 3599.5f, 0.8f ), true );
    // The u_Time clock itself stays inside the wrap period.
    const float now = HeartParticles::now( );
    check_bool( "now: inside [0, 3600)", now >= 0.0f && now < 3600.0f, true );

    return failures;
}
