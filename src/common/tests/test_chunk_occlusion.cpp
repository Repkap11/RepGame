// rep_tests coverage for ChunkOcclState (common/chunk_occlusion.hpp): the
// position/rotation/full verdict validity rules behind the occlusion hide
// gate. Position generations invalidate everything; rotation generations
// only invalidate verdicts earned while the chunk wasn't fully inside the
// frustum.

#include "test_util.hpp"

#include "common/chunk_occlusion.hpp"

int test_chunk_occlusion( ) {
    int fails = 0;

    // a. Full-frustum fails are rotation-invariant at the same position.
    {
        ChunkOcclState s;
        s.in_full = 1;
        s.mark_probe_issued( 1, 1 );
        s.record( false, 1, 1 );
        s.mark_probe_issued( 1, 1 );
        s.record( false, 1, 1 );
        fails += !s.hides( 1, 1 );
        fails += !s.hides( 1, 5 );
        pr_test( "  occl: full fails hide under any rotation       %s", fails == 0 ? "PASS" : "FAIL" );
    }

    // b. Partial fails only hide under the rotation they were earned in.
    {
        ChunkOcclState s;
        s.in_full = 0;
        s.mark_probe_issued( 1, 1 );
        s.record( false, 1, 1 );
        s.mark_probe_issued( 1, 1 );
        s.record( false, 1, 1 );
        fails += !s.hides( 1, 1 );
        fails += s.hides( 1, 2 );
        pr_test( "  occl: partial fails bound to their rotation    %s", s.hides( 1, 1 ) && !s.hides( 1, 2 ) ? "PASS" : "FAIL" );
    }

    // c. A partial probe issued under an older rotation is dropped.
    {
        ChunkOcclState s;
        s.in_full = 0;
        s.mark_probe_issued( 1, 1 );
        s.record( false, 1, 2 );
        fails += s.fails != 0;
        pr_test( "  occl: stale-rotation partial result dropped    %s", s.fails == 0 ? "PASS" : "FAIL" );
    }

    // d. A partial streak under rot 1 doesn't carry into rot 2: a new
    // partial fail there starts a fresh count of 1.
    {
        ChunkOcclState s;
        s.in_full = 0;
        s.mark_probe_issued( 1, 1 );
        s.record( false, 1, 1 );
        s.mark_probe_issued( 1, 1 );
        s.record( false, 1, 1 );
        fails += !s.hides( 1, 1 );
        s.mark_probe_issued( 1, 2 );
        s.record( false, 1, 2 );
        fails += s.fails != 1;
        fails += s.hides( 1, 2 );
        pr_test( "  occl: partial streak restarts on rotation      %s", s.fails == 1 && !s.hides( 1, 2 ) ? "PASS" : "FAIL" );
    }

    // e. A passed probe resets the streak and un-hides.
    {
        ChunkOcclState s;
        s.in_full = 1;
        s.mark_probe_issued( 1, 1 );
        s.record( false, 1, 1 );
        s.mark_probe_issued( 1, 1 );
        s.record( false, 1, 1 );
        fails += !s.hides( 1, 1 );
        s.mark_probe_issued( 1, 1 );
        s.record( true, 1, 1 );
        fails += s.hides( 1, 1 );
        pr_test( "  occl: pass resets the streak                   %s", !s.hides( 1, 1 ) ? "PASS" : "FAIL" );
    }

    // f. Position generation invalidates everything: a probe issued under
    // gen 1 is ignored once the position moved, and a gen-1 streak can't
    // hide at gen 2.
    {
        ChunkOcclState s;
        s.in_full = 1;
        s.mark_probe_issued( 1, 1 );
        s.record( false, 1, 1 );
        s.mark_probe_issued( 1, 1 );
        s.record( false, 1, 1 );
        fails += !s.hides( 1, 1 );
        fails += s.hides( 2, 1 );
        s.mark_probe_issued( 1, 1 );
        s.record( false, 2, 1 );
        fails += s.fails != 2 || s.hides( 2, 1 );
        pr_test( "  occl: position move invalidates all verdicts   %s", !s.hides( 2, 1 ) ? "PASS" : "FAIL" );
    }

    // g. After reset_streak a single full fail leaves fails==1 —
    // OCCL_HIDE_FRAMES still absorbs one stale result.
    {
        ChunkOcclState s;
        s.in_full = 1;
        s.mark_probe_issued( 1, 1 );
        s.record( false, 1, 1 );
        s.reset_streak( );
        s.mark_probe_issued( 1, 1 );
        s.record( false, 1, 1 );
        fails += s.fails != 1;
        fails += s.hides( 1, 1 );
        pr_test( "  occl: reset streak needs OCCL_HIDE_FRAMES      %s", s.fails == 1 && !s.hides( 1, 1 ) ? "PASS" : "FAIL" );
    }

    // h. A full streak keeps hiding under a new rotation even when a
    // partial fail lands under it.
    {
        ChunkOcclState s;
        s.in_full = 1;
        s.mark_probe_issued( 1, 1 );
        s.record( false, 1, 1 );
        s.mark_probe_issued( 1, 1 );
        s.record( false, 1, 1 );
        fails += !s.hides( 1, 1 );
        s.in_full = 0;
        s.mark_probe_issued( 1, 2 );
        s.record( false, 1, 2 );
        fails += !s.hides( 1, 2 );
        pr_test( "  occl: full streak survives partial new-rot     %s", s.hides( 1, 2 ) ? "PASS" : "FAIL" );
    }

    return fails;
}
