// rep_tests coverage for the PROFILE_SCOPE timers (profile.hpp): a timed
// scope lands in snapshot_and_reset, a disabled scope records nothing.

#include "test_util.hpp"

#include <thread>

#include "common/utils/profile.hpp"

int test_profile( ) {
    int fails = 0;

    const bool was_enabled = profile::g_enabled;

    // Disabled: a scope records nothing.
    profile::set_enabled( false );
    {
        static const int slot = profile::register_slot( "test_profile" );
        const profile::Scope scope( slot );
        std::this_thread::sleep_for( std::chrono::microseconds( 1 ) );
    }
    const auto empty = profile::snapshot_and_reset( );
    fails += !empty.empty( );
    pr_test( "  profile: disabled scope records nothing %s", empty.empty( ) ? "PASS" : "FAIL" );

    // Enabled: one timed scope, snapshot, reset.
    profile::set_enabled( true );
    {
        static const int slot = profile::register_slot( "test_profile" );
        const profile::Scope scope( slot );
        std::this_thread::sleep_for( std::chrono::microseconds( 50 ) );
    }
    const auto snap = profile::snapshot_and_reset( );
    const auto it = snap.find( "test_profile" );
    fails += it == snap.end( ) || it->second.n != 1;
    pr_test( "  profile: n=1 after one scope        %s", it != snap.end( ) && it->second.n == 1 ? "PASS" : "FAIL" );
    fails += it == snap.end( ) || it->second.total_ns == 0;
    pr_test( "  profile: us>0 after timed scope     %s", it != snap.end( ) && it->second.total_ns > 0 ? "PASS" : "FAIL" );
    const auto cleared = profile::snapshot_and_reset( );
    fails += !cleared.empty( );
    pr_test( "  profile: reset clears slots         %s", cleared.empty( ) ? "PASS" : "FAIL" );

    profile::set_enabled( was_enabled );
    return fails;
}
