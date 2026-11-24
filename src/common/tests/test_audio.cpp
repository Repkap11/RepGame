// SFX voice-budget / retrigger-throttle policy tests — the helpers live in
// include/common/audio.hpp outside REPGAME_AUDIO so they run on every build.
#include "test_util.hpp"
#include "common/audio.hpp"

// audio_sfx_pick_victim: -1 room left, -2 drop the new play (not louder
// than the quietest), i >= 0 fade-stop the quietest (first index on ties).
int test_audio_sfx_budget( ) {
    int failures = 0;
    auto check = [ & ]( const char *name, int actual, int expected ) {
        const bool ok = actual == expected;
        pr_test( "  %-55s got=%-3d want=%-3d  %s", name, actual, expected, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };
    const float quiet[] = { 0.5f };
    check( "room left -> play", audio_sfx_pick_victim( quiet, 1, 0.1f, 4 ), -1 );
    check( "empty -> play", audio_sfx_pick_victim( quiet, 0, 0.1f, 4 ), -1 );
    const float full[] = { 0.5f, 0.9f, 0.2f, 0.7f };
    check( "full, new quieter -> drop", audio_sfx_pick_victim( full, 4, 0.1f, 4 ), -2 );
    check( "full, new == quietest -> drop", audio_sfx_pick_victim( full, 4, 0.2f, 4 ), -2 );
    check( "full, new louder -> evict min idx 2", audio_sfx_pick_victim( full, 4, 0.3f, 4 ), 2 );
    const float tied[] = { 0.2f, 0.9f, 0.2f, 0.7f };
    check( "tie on min -> first index", audio_sfx_pick_victim( tied, 4, 0.5f, 4 ), 0 );
    check( "louder than all -> evict min", audio_sfx_pick_victim( full, 4, 1.0f, 4 ), 2 );
    return failures;
}

// audio_sfx_throttled: same-pool restart within the window is dropped
// unless it's louder; last_start_ms == 0 means never started.
int test_audio_sfx_throttle( ) {
    int failures = 0;
    auto check = [ & ]( const char *name, bool actual, bool expected ) {
        const bool ok = actual == expected;
        pr_test( "  %-55s got=%d want=%d  %s", name, ( int )actual, ( int )expected, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };
    check( "never started -> not throttled", audio_sfx_throttled( 0, 1.0f, 100, 0.5f, 40 ), false );
    check( "inside window, quieter -> throttled", audio_sfx_throttled( 100, 0.5f, 120, 0.3f, 40 ), true );
    check( "inside window, equal -> throttled", audio_sfx_throttled( 100, 0.5f, 120, 0.5f, 40 ), true );
    check( "inside window, louder -> not throttled", audio_sfx_throttled( 100, 0.5f, 120, 0.8f, 40 ), false );
    check( "window elapsed -> not throttled", audio_sfx_throttled( 100, 0.5f, 141, 0.3f, 40 ), false );
    check( "window boundary -> throttled", audio_sfx_throttled( 100, 0.5f, 140, 0.3f, 40 ), true );
    check( "clock went backwards -> not throttled", audio_sfx_throttled( 100, 0.5f, 90, 0.3f, 40 ), false );
    return failures;
}
