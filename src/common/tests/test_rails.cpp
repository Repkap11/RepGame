// Rail tests: shape encoding, connection resolution (straights, curves,
// sticky junctions, slopes), powered-rail chain power, and support loss —
// split out of rep_tests.cpp.
#include "test_util.hpp"
#include "common/rail.hpp"
#include "common/block_update_events/BlockNextToChangeEvent.hpp"
#include "common/utils/ray_traversal.hpp"

// Checks the resolved RailShape at a position, printing a line per check.
static int check_shape( const World &world, const char *name, const glm::ivec3 &pos, RailShape expected ) {
    const BlockState st = world.get_loaded_block( pos );
    const RailShape got = rail_shape_of( st );
    const bool ok = got == expected;
    pr_test( "  %-55s %s (got %d want %d)", name, ok ? "PASS" : "FAIL", (int)got, (int)expected );
    return ok ? 0 : 1;
}

static int check_bool( int &failures, const char *name, bool cond ) {
    pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
    if ( !cond ) {
        failures++;
    }
    return failures;
}

int test_rails( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    int failures = 0;

    // --- encoding round-trip -------------------------------------------------
    // Shapes 0-5 store rotation=shape/display=id; curves store
    // display=RAIL_CORNER/rotation=shape-6. Powered rails can't curve —
    // asking for a curve lands on NS instead.
    for ( int s = 0; s < RAIL_SHAPE_COUNT; s++ ) {
        const RailShape shape = static_cast<RailShape>( s );
        const BlockState st = rail_state_with_shape( BlockState{ RAIL, 0, 0, RAIL, 0 }, shape );
        check_bool( failures, "rail: encode/decode round-trip", rail_shape_of( st ) == shape );
    }
    {
        const BlockState corner = rail_state_with_shape( BlockState{ RAIL, 0, 0, RAIL, 0 }, RAIL_NE );
        check_bool( failures, "rail: curve hides in RAIL_CORNER display",
                    corner.id == RAIL && corner.display_id == RAIL_CORNER && corner.rotation == 2 );
        const BlockState asc = rail_state_with_shape( BlockState{ RAIL, 0, 0, RAIL, 0 }, RAIL_ASC_W );
        check_bool( failures, "rail: ascend stores rotation=shape", asc.display_id == RAIL && asc.rotation == RAIL_ASC_W );
        const BlockState powered_rail = rail_state_with_shape( BlockState{ POWERED_RAIL, 0, 0, POWERED_RAIL, 0 }, RAIL_SE );
        check_bool( failures, "rail: powered rail never encodes a curve", powered_rail.display_id == POWERED_RAIL && rail_shape_of( powered_rail ) == RAIL_NS );
    }
    {
        // Endpoint table sanity: ASC_E climbs toward +x, a SE curve exits
        // south and east, and rail_render_yaw maps the four uphill dirs
        // onto the shader nibble (0=-z, 1=+x, 2=+z, 3=-x).
        glm::ivec3 dirs[ 2 ];
        int dy[ 2 ];
        rail_endpoints( RAIL_ASC_E, dirs, dy );
        check_bool( failures, "rail: ASC_E climbs toward +x",
                    dirs[ 0 ] == glm::ivec3( 1, 0, 0 ) && dy[ 0 ] == 1 && dirs[ 1 ] == glm::ivec3( -1, 0, 0 ) && dy[ 1 ] == 0 );
        rail_endpoints( RAIL_SE, dirs, dy );
        check_bool( failures, "rail: SE exits +z and +x",
                    dirs[ 0 ] == glm::ivec3( 0, 0, 1 ) && dirs[ 1 ] == glm::ivec3( 1, 0, 0 ) && dy[ 0 ] == 0 && dy[ 1 ] == 0 );
        // Curve render yaw was measured from top-down captures: the turned
        // tile at yaw 0 draws an N+W arc, so SE/SW/NW/NE need 2/3/0/1 —
        // independent of the stored rotation (SE still stores rotation 3).
        check_bool( failures, "rail: curve render yaws measured",
                    rail_render_yaw( RAIL_SE ) == 2 && rail_render_yaw( RAIL_SW ) == 3 && rail_render_yaw( RAIL_NW ) == 0 &&
                        rail_render_yaw( RAIL_NE ) == 1 );
        const BlockState se = rail_state_with_shape( BlockState{ RAIL, 0, 0, RAIL, 0 }, RAIL_SE );
        check_bool( failures, "rail: SE stored rotation unchanged", se.display_id == RAIL_CORNER && se.rotation == 3 );
        check_bool( failures, "rail: render yaw encodes uphill dirs",
                    rail_render_yaw( RAIL_ASC_N ) == 0 && rail_render_yaw( RAIL_ASC_E ) == 1 &&
                        rail_render_yaw( RAIL_ASC_S ) == 2 && rail_render_yaw( RAIL_ASC_W ) == 3 );
        check_bool( failures, "rail: rail_is_ascending covers all four",
                    rail_is_ascending( RAIL_ASC_N ) && rail_is_ascending( RAIL_ASC_S ) &&
                        rail_is_ascending( RAIL_ASC_E ) && rail_is_ascending( RAIL_ASC_W ) && !rail_is_ascending( RAIL_NS ) );
    }

    // --- world ---------------------------------------------------------------
    RepGameState &gs = *new RepGameState( ); // leaked on purpose — see test_redstone
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 0, 0, 0 );
    test_setup_world( gs.world );
    World &world = gs.world;
    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    // Stone floor at y=10 across the test area; rails sit at y=11.
    for ( int x = 2; x <= 30; x++ ) {
        for ( int z = 2; z <= 28; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 10, z ), stone );
        }
    }

    auto place = [ & ]( const glm::ivec3 &pos, const BlockState &block_state ) {
        gs.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( gs.tick_number, pos, block_state, false );
    };
    auto run = [ & ]( const int ticks ) {
        for ( int i = 0; i < ticks; i++ ) {
            gs.tick_number++;
            repgame_drain_block_updates( gs );
        }
    };
    const BlockState rail = { RAIL, 0, 0, RAIL, 0 };
    const BlockState powered_rail = { POWERED_RAIL, 0, 0, POWERED_RAIL, 0 };

    // Straight line along x: the middle rail reads east-west.
    place( glm::ivec3( 4, 11, 4 ), rail );
    place( glm::ivec3( 5, 11, 4 ), rail );
    place( glm::ivec3( 6, 11, 4 ), rail );

    // L corner at (10,4): arms south (+z) and east (+x) -> RAIL_SE.
    place( glm::ivec3( 10, 11, 4 ), rail );
    place( glm::ivec3( 10, 11, 5 ), rail );
    place( glm::ivec3( 11, 11, 4 ), rail );

    // T-junction: N+S arms first, then the E arm. The junction keeps its
    // north-south track (sticky) instead of re-picking on the third link.
    place( glm::ivec3( 14, 11, 3 ), rail );
    place( glm::ivec3( 14, 11, 5 ), rail );
    place( glm::ivec3( 14, 11, 4 ), rail );

    // Slope: a rail on a stone step at (21,12,8) pulls the rail at
    // (20,11,8) up into RAIL_ASC_E.
    world.set_loaded_block( glm::ivec3( 21, 11, 8 ), stone );
    place( glm::ivec3( 21, 12, 8 ), rail );
    place( glm::ivec3( 20, 11, 8 ), rail );

    // Powered rails never curve: S + E links resolve to a straight.
    place( glm::ivec3( 24, 11, 4 ), powered_rail );
    place( glm::ivec3( 24, 11, 5 ), powered_rail );
    place( glm::ivec3( 25, 11, 4 ), powered_rail );

    // Awkward build order: both arms of an L before the corner cell — the
    // corner still converges to RAIL_SE once placed.
    place( glm::ivec3( 10, 11, 11 ), rail );
    place( glm::ivec3( 11, 11, 10 ), rail );
    place( glm::ivec3( 10, 11, 10 ), rail );

    // Line-end acceptance: an N-S pair at (20,4)-(20,5), then a rail placed
    // east of the south end at (21,5). The end rail has only its one real
    // connection, so it must accept the newcomer and curve N+E (RAIL_NE).
    place( glm::ivec3( 20, 11, 4 ), rail );
    place( glm::ivec3( 20, 11, 5 ), rail );

    run( 10 );
    // Now extend the T-junction: the middle keeps RAIL_NS.
    place( glm::ivec3( 15, 11, 4 ), rail );
    // ...and join the line end: (20,5) curves onto the new east arm.
    place( glm::ivec3( 21, 11, 5 ), rail );
    run( 10 );

    failures += check_shape( world, "line: middle rail runs E-W", glm::ivec3( 5, 11, 4 ), RAIL_EW );
    failures += check_shape( world, "line: end rail runs E-W", glm::ivec3( 4, 11, 4 ), RAIL_EW );
    failures += check_shape( world, "L: corner is RAIL_SE", glm::ivec3( 10, 11, 4 ), RAIL_SE );
    failures += check_id( world, "L: corner displays RAIL_CORNER", glm::ivec3( 10, 11, 4 ), RAIL );
    check_bool( failures, "L: corner display_id is RAIL_CORNER",
                world.get_loaded_block( glm::ivec3( 10, 11, 4 ) ).display_id == RAIL_CORNER );
    failures += check_shape( world, "T: junction stays N-S after E arm", glm::ivec3( 14, 11, 4 ), RAIL_NS );
    failures += check_shape( world, "line end curves onto late east arm", glm::ivec3( 20, 11, 5 ), RAIL_NE );
    failures += check_shape( world, "slope: rail below step ascends E", glm::ivec3( 20, 11, 8 ), RAIL_ASC_E );
    failures += check_shape( world, "slope: rail on step runs E-W", glm::ivec3( 21, 12, 8 ), RAIL_EW );
    {
        const BlockState g = world.get_loaded_block( glm::ivec3( 24, 11, 4 ) );
        check_bool( failures, "powered: no curve at a corner",
                    g.display_id == POWERED_RAIL && ( rail_shape_of( g ) == RAIL_NS || rail_shape_of( g ) == RAIL_EW ) );
    }
    failures += check_shape( world, "L placed arms-first converges", glm::ivec3( 10, 11, 10 ), RAIL_SE );

    // Ascending rails pick on an 8px box; flat/curved stay the 1px plate.
    // A horizontal ray at cell.y + 0.3 hits the ASC rail at (20,11,8) but
    // passes clean over the flat rails at (5-6,11,4).
    {
        glm::ivec3 out( -1, -1, -1 );
        int which_face = -1;
        check_bool( failures, "ray: ascending rail hit at +0.3",
                    RayTraversal::find_block_from_to( world, glm::dvec3( 19.0, 11.3, 8.5 ), glm::dvec3( 21.0, 11.3, 8.5 ), out, &which_face ) &&
                        out == glm::ivec3( 20, 11, 8 ) );
        check_bool( failures, "ray: flat rail missed at +0.3",
                    !RayTraversal::find_block_from_to( world, glm::dvec3( 4.5, 11.3, 4.5 ), glm::dvec3( 7.5, 11.3, 4.5 ), out, &which_face ) );
    }

    // Placer yaw decides a lone rail's axis (same buckets as
    // getPlacedRotation): facing E/W (yaw 90) lays E-W, facing N (0) N-S.
    {
        const BlockState ew = rail_resolve_shape( world, glm::ivec3( 28, 11, 27 ), rail, 90 );
        const BlockState ns = rail_resolve_shape( world, glm::ivec3( 28, 11, 27 ), rail, 0 );
        check_bool( failures, "placer yaw 90 lays E-W", rail_shape_of( ew ) == RAIL_EW );
        check_bool( failures, "placer yaw 0 lays N-S", rail_shape_of( ns ) == RAIL_NS );
    }

    // --- powered rail chain power ---------------------------------------------
    // Ten powered rails along x at z=16; a redstone block beside the first
    // activates it directly. Rails within 8 connected hops power; the rail
    // 9 hops out stays dark (and must not feed anything back).
    for ( int x = 4; x <= 13; x++ ) {
        place( glm::ivec3( x, 11, 16 ), powered_rail );
    }
    place( glm::ivec3( 4, 11, 17 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );
    run( 20 );
    failures += check_power( world, "powered chain: rail next to source", glm::ivec3( 4, 11, 16 ), 1 );
    failures += check_power( world, "powered chain: rail 8 hops out", glm::ivec3( 12, 11, 16 ), 1 );
    failures += check_power( world, "powered chain: rail 9 hops out unpowered", glm::ivec3( 13, 11, 16 ), 0 );

    // Removing the source must unpower the whole chain — powered rails are
    // activated, not emitting, so nothing can latch itself on.
    place( glm::ivec3( 4, 11, 17 ), BLOCK_STATE_AIR );
    run( 20 );
    failures += check_power( world, "powered chain: head unpowered after source gone", glm::ivec3( 4, 11, 16 ), 0 );
    failures += check_power( world, "powered chain: tail unpowered after source gone", glm::ivec3( 12, 11, 16 ), 0 );

    // A self-referencing knot: two powered rails whose only power path is
    // through each other never turn on — the BFS only counts direct
    // activation, never stored rail power.
    place( glm::ivec3( 20, 11, 20 ), powered_rail );
    place( glm::ivec3( 21, 11, 20 ), powered_rail );
    run( 20 );
    failures += check_power( world, "powered knot: no source, no latch", glm::ivec3( 20, 11, 20 ), 0 );
    failures += check_power( world, "powered knot: neighbour no latch", glm::ivec3( 21, 11, 20 ), 0 );

    // --- support loss ----------------------------------------------------------
    // A rail needs a colliding block below: mining the floor pops it.
    place( glm::ivec3( 4, 11, 24 ), rail );
    run( 10 );
    failures += check_id( world, "rail: sits on its support", glm::ivec3( 4, 11, 24 ), RAIL );
    place( glm::ivec3( 4, 10, 24 ), BLOCK_STATE_AIR );
    run( 10 );
    failures += check_id( world, "rail: pops when support is mined", glm::ivec3( 4, 11, 24 ), AIR );

    return failures;
}
