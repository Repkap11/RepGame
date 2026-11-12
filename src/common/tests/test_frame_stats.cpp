// rep_tests coverage for FrameTimeStats (frame_stats.*): the per-metric
// trailing windows behind the overlay / FPS-log avg + 1%/0.1% low numbers.

#include "test_util.hpp"

#include "common/utils/frame_stats.hpp"

static int check_close( const char *name, const float got, const float expected, const float tol = 0.05f ) {
    const bool ok = fabsf( got - expected ) <= tol;
    pr_test( "  frame_stats: %-28s %s (got %.2f, want %.2f)", name, ok ? "PASS" : "FAIL", got, expected );
    return !ok;
}

static int check_range( const char *name, const float got, const float lo, const float hi ) {
    const bool ok = got >= lo && got <= hi;
    pr_test( "  frame_stats: %-28s %s (got %.2f, want %.2f..%.2f)", name, ok ? "PASS" : "FAIL", got, lo, hi );
    return !ok;
}

static void add_frames( FrameTimeStats &stats, const int n, const float dt ) {
    for ( int i = 0; i < n; i++ ) {
        stats.add_frame( dt );
    }
}

int test_frame_stats( ) {
    int fails = 0;

    // Uniform 10ms frames: every metric is 100 FPS.
    {
        FrameTimeStats stats;
        add_frames( stats, 1000, 0.010f );
        const FrameTimeStats::Summary s = stats.compute( );
        fails += check_close( "uniform avg", s.avg_fps, 100.0f );
        fails += check_close( "uniform 1% low", s.low1_fps, 100.0f );
        fails += check_close( "uniform 0.1% low", s.low01_fps, 100.0f );
        fails += check_close( "uniform ms", s.avg_ms, 10.0f );
    }

    // Recent hitch: still inside the 1% and 0.1% windows.
    // 2s suffix = 191 frames (50@10ms + hitch + 140@10ms), k=2.
    {
        FrameTimeStats stats;
        add_frames( stats, 600, 0.010f );
        stats.add_frame( 0.100f );
        add_frames( stats, 50, 0.010f );
        const FrameTimeStats::Summary s = stats.compute( );
        fails += check_close( "recent hitch 0.1% low", s.low01_fps, 10.0f );
        fails += check_close( "recent hitch 1% low", s.low1_fps, 2.0f / ( 0.1f + 0.01f ) );
        fails += check_range( "recent hitch avg", s.avg_fps, 80.0f, 95.0f );
    }

    // The hitch has aged out of the 2s 1% window but not the 5s 0.1% one.
    {
        FrameTimeStats stats;
        add_frames( stats, 600, 0.010f );
        stats.add_frame( 0.100f );
        add_frames( stats, 300, 0.010f );
        const FrameTimeStats::Summary s = stats.compute( );
        fails += check_close( "aged hitch 1% low", s.low1_fps, 100.0f );
        fails += check_close( "aged hitch 0.1% low", s.low01_fps, 10.0f );
        fails += check_close( "aged hitch avg", s.avg_fps, 100.0f );
    }

    // The hitch has aged out of retention entirely (5s of 10ms frames
    // since): all lows recover, ~500 samples retained.
    {
        FrameTimeStats stats;
        add_frames( stats, 600, 0.010f );
        stats.add_frame( 0.100f );
        add_frames( stats, 900, 0.010f );
        const FrameTimeStats::Summary s = stats.compute( );
        fails += check_close( "expired hitch 0.1% low", s.low01_fps, 100.0f );
        fails += check_range( "expired hitch frames", s.frames, 499.0f, 502.0f );
    }

    // avg tracks only the last ~1s: after a stretch of 50ms frames all
    // metrics read 20 FPS.
    {
        FrameTimeStats stats;
        add_frames( stats, 1000, 0.010f );
        add_frames( stats, 50, 0.050f );
        const FrameTimeStats::Summary s = stats.compute( );
        fails += check_close( "slow tail avg", s.avg_fps, 20.0f, 0.5f );
        fails += check_close( "slow tail 1% low", s.low1_fps, 20.0f );
        fails += check_close( "slow tail 0.1% low", s.low01_fps, 20.0f );
    }

    // FPS drop: 5s at 1000fps then 5s at 20fps — retention shrinks to the
    // recent 50ms frames, not the old 1ms history.
    {
        FrameTimeStats stats;
        add_frames( stats, 5000, 0.001f );
        add_frames( stats, 100, 0.050f );
        const FrameTimeStats::Summary s = stats.compute( );
        fails += check_range( "fps drop frames", s.frames, 99.0f, 101.0f );
        fails += check_close( "fps drop avg", s.avg_fps, 20.0f );
    }

    // Empty stays zero; non-positive dt is ignored.
    {
        FrameTimeStats stats;
        FrameTimeStats::Summary s = stats.compute( );
        stats.add_frame( 0.0f );
        stats.add_frame( -1.0f );
        s = stats.compute( );
        const bool ok = s.frames == 0 && s.avg_fps == 0 && s.low1_fps == 0 && s.low01_fps == 0 && s.avg_ms == 0;
        pr_test( "  frame_stats: %-28s %s", "empty + bad dt", ok ? "PASS" : "FAIL" );
        fails += !ok;
    }

    return fails;
}
