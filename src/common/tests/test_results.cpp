// rep_tests coverage for the results writer's pure JSON record formatters
// (include/common/harness/results.hpp) — pins the results.jsonl schema.

#include "test_util.hpp"

#include "common/harness/results.hpp"

static int check_str( const char *name, const std::string &got, const std::string &expected ) {
    const bool ok = got == expected;
    pr_test( "  %-20s %s", name, ok ? "PASS" : "FAIL" );
    if ( !ok ) {
        pr_test( "    got:      %s", got.c_str( ) );
        pr_test( "    expected: %s", expected.c_str( ) );
    }
    return ok ? 0 : 1;
}

int test_results( ) {
    int fails = 0;

    LoaderDebugStats s = {};
    s.terrain_queued = 1;
    s.terrain_results = 2;
    s.chunks_loading = 3;
    s.chunks_drawable = 40;
    s.chunks_occluded = 5;
    s.light_jobs = 6;
    s.light_pending = 7;
    s.light_claims = 8;
    s.light_dirty = 9;
    s.light_bfs_active = 10;
    s.light_bfs_priority = 11;
    s.light_bfs_starve = 12;
    s.light_seeds = 13;
    s.light_dropped = 14;
    s.light_stale_count = 15;
    s.light_jlag_us = 16;
    s.light_njlag_us = 17;
    const std::string metric =
        results::format_metric( 100, 1, 6, glm::dvec3( 1.5, 80.25, -2.0 ), s, "{\"loop\":9}" );
    fails += check_str( "metric", metric,
                        "{\"type\":\"metric\",\"tick\":100,\"fly_phase\":1,\"rd\":6,\"pos\":[1.5000,80.2500,-2.0000],"
                        "\"loader\":{\"terrain_queued\":1,\"terrain_results\":2,\"chunks_loading\":3,\"chunks_drawable\":40,"
                        "\"chunks_occluded\":5,\"light_jobs\":6,\"light_pending\":7,\"light_claims\":8,\"light_dirty\":9,"
                        "\"light_bfs_active\":10,\"light_bfs_priority\":11,\"light_bfs_starve\":12,\"light_seeds\":13,"
                        "\"light_dropped\":14,\"light_stale_count\":15,\"light_jlag_us\":16,\"light_njlag_us\":17},"
                        "\"light\":{\"loop\":9}}" );

    fails += check_str( "event bare", results::format_event( 42, "fly_settled", "" ),
                        "{\"type\":\"event\",\"tick\":42,\"name\":\"fly_settled\"}" );
    fails += check_str( "event detail", results::format_event( 43, "capture_shot", "\"view\":2,\"timeout\":0" ),
                        "{\"type\":\"event\",\"tick\":43,\"name\":\"capture_shot\",\"detail\":{\"view\":2,\"timeout\":0}}" );

    fails += check_str( "probe", results::format_probe( 7, glm::ivec3( 1, 64, -3 ), 5, "stone", 2, 15, 0 ),
                        "{\"type\":\"probe\",\"tick\":7,\"pos\":[1,64,-3],\"block\":5,\"name\":\"stone\",\"rotation\":2,"
                        "\"sky\":15,\"blocklight\":0}" );

    fails += check_str( "assert", results::format_assert( 8, 12, "assert_light 1 2 3 sky >= 12", false, ">=12", "9" ),
                        "{\"type\":\"assert\",\"tick\":8,\"line\":12,\"cmd\":\"assert_light 1 2 3 sky >= 12\",\"ok\":false,"
                        "\"expected\":\">=12\",\"actual\":\"9\"}" );

    fails += check_str( "summary", results::format_summary( 400, "fly_ticks", 3, 1, 60, "[\"screenshots/capture_0_display.tga\"]" ),
                        "{\"type\":\"summary\",\"tick\":400,\"ticks\":400,\"exit_reason\":\"fly_ticks\","
                        "\"asserts\":{\"total\":3,\"failed\":1},\"shots\":[\"screenshots/capture_0_display.tga\"],"
                        "\"settle_tick\":60}" );

    // light dbg text: counter section keeps plain keys, the "| us:" section
    // is prefixed; "12/34" stays a string; nested parens flatten to keys.
    fails += check_str( "light_json", results::light_json_from_dbg( "loop=5 pq=12/34 jobs=7 | us: fin=9(lfin=4[fill=2]) remesh=3" ),
                        "{\"loop\":5,\"pq\":\"12/34\",\"jobs\":7,\"us_fin\":9,\"us_lfin\":4,\"us_fill\":2,\"us_remesh\":3}" );
    return fails;
}
