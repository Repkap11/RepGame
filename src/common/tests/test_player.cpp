// Player movement/interaction tests — split out of rep_tests.cpp.
#include "test_util.hpp"
// Sneak edge protection: Collision::check_move with edge_guard must keep a
// standing player from walking off a drop — the AABB clamps at the edge —
// while still allowing step-downs within STEP_HEIGHT (slabs/stairs) and
// leaving normal (non-sneak) movement untouched.
//
//   stone platform, tops at y=11    slab floor, tops at y=10.5
//   x=10..12 (west edge at x=10)    x=13..15        z=10..12
int test_sneak( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    // Heap, not stack: RepGameState embeds megabytes of light queues (the
    // game keeps it in static storage via globalGameState) — a stack local
    // overflows the default 8MB stack. Leaked on purpose: the process exits
    // right after the tests and destructors would touch GL state.
    RepGameState &gs = *new RepGameState( );
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 0, 0, 0 );
    test_setup_world( gs.world );
    World &world = gs.world;

    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    const BlockState slab = { STONE_BRICK_SLAB, 0, 0, STONE_BRICK_SLAB, 0 };
    for ( int x = 10; x <= 12; x++ ) {
        for ( int z = 10; z <= 12; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 10, z ), stone );
        }
    }
    for ( int x = 13; x <= 15; x++ ) {
        for ( int z = 10; z <= 12; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 10, z ), slab );
        }
    }

    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };
    auto check_near = [ & ]( const char *name, double actual, double expected, double tol ) {
        const bool ok = fabs( actual - expected ) <= tol;
        pr_test( "  %-55s value=%-9.4f expected=%-9.4f  %s", name, actual, expected, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };

    // Applies `step` through check_move for `ticks` ticks, like
    // process_movement does (check_move clamps vec, then pos advances).
    auto walk = [ & ]( glm::dvec3 &pos, const glm::dvec3 &step, int ticks, int &standing, double height, double eye_height, bool edge_guard ) {
        for ( int i = 0; i < ticks; i++ ) {
            glm::dvec3 vec = step;
            Collision::check_move( world, vec, pos, &standing, height, eye_height, edge_guard );
            pos += vec;
        }
    };

    // Sneak-walk toward the west edge (x=10, void below): the player stops
    // once the hitbox's trailing edge reaches the platform edge (max overhang,
    // Minecraft-style) and never falls — an epsilon of overlap keeps them
    // standing.
    glm::dvec3 pos( 11.5, 11.0 + PLAYER_SNEAK_EYE_HEIGHT, 11.5 );
    int standing = 0;
    walk( pos, glm::dvec3( -0.05, -0.002, 0.0 ), 40, standing, PLAYER_SNEAK_HEIGHT, PLAYER_SNEAK_EYE_HEIGHT, true );
    check_bool( "sneak-walk to void edge: still standing", standing == 1 );
    check_bool( "sneak-walk to void edge: stopped at edge", pos.x > 9.69 && pos.x < 9.75 );
    check_near( "sneak-walk to void edge: no fall", pos.y, 11.0 + PLAYER_SNEAK_EYE_HEIGHT, 0.01 );

    // Control: the same walk without edge guard falls off the edge.
    pos = glm::dvec3( 11.5, 11.0 + PLAYER_EYE_HEIGHT, 11.5 );
    walk( pos, glm::dvec3( -0.05, -0.3, 0.0 ), 60, standing, PLAYER_HEIGHT, PLAYER_EYE_HEIGHT, false );
    check_bool( "normal walk off the edge: fully past it", pos.x < 9.7 );
    check_bool( "normal walk off the edge: fell", pos.y < 11.0 + PLAYER_EYE_HEIGHT - 1.0 );

    // Sneak-walk east onto the half-lower slab floor: the 0.5 step-down is
    // within STEP_HEIGHT, so edge guard permits it and the player settles.
    pos = glm::dvec3( 11.5, 11.0 + PLAYER_SNEAK_EYE_HEIGHT, 11.5 );
    walk( pos, glm::dvec3( 0.05, -0.1, 0.0 ), 60, standing, PLAYER_SNEAK_HEIGHT, PLAYER_SNEAK_EYE_HEIGHT, true );
    check_bool( "sneak-walk onto slabs: past the platform edge", pos.x > 13.3 );
    check_bool( "sneak-walk onto slabs: still standing", standing == 1 );
    check_near( "sneak-walk onto slabs: settled on slab top", pos.y, 10.5 + PLAYER_SNEAK_EYE_HEIGHT, 0.1 );

    // collides_at gates standing up: a 1-block gap over the platform collides
    // with the standing box, a 2-block gap fits.
    world.set_loaded_block( glm::ivec3( 11, 12, 11 ), stone );
    check_bool( "collides_at: standing box under 1-block gap", Collision::collides_at( world, glm::dvec3( 11.5, 11.0 + PLAYER_EYE_HEIGHT, 11.5 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
    world.set_loaded_block( glm::ivec3( 11, 12, 11 ), BLOCK_STATE_AIR );
    world.set_loaded_block( glm::ivec3( 11, 13, 11 ), stone );
    check_bool( "collides_at: standing box under 2-block gap", !Collision::collides_at( world, glm::dvec3( 11.5, 11.0 + PLAYER_EYE_HEIGHT, 11.5 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
    return failures;
}

// Torch pick box (torch_pick_shape): 4px wide on x/z so torches are easier to
// target than the 2px drawn stick; wall mounts (rotation 4-7) hug their wall.
// Rays are cast horizontally at the stick's mid-height.
int test_torch_pick( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    RepGameState &gs = *new RepGameState( );
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 0, 0, 0 );
    test_setup_world( gs.world );
    World &world = gs.world;

    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    // Floor torch on a support; wall torch attached to the -x face of a
    // support (rotation 4 = attached LEFT, box pushed to x+0).
    world.set_loaded_block( glm::ivec3( 10, 10, 10 ), stone );
    world.set_loaded_block( glm::ivec3( 10, 11, 10 ), { TORCH, 0, 0, TORCH, 0 } );
    world.set_loaded_block( glm::ivec3( 19, 11, 10 ), stone );
    world.set_loaded_block( glm::ivec3( 20, 11, 10 ), { TORCH, 4, 0, TORCH, 0 } );

    int failures = 0;
    const double mid_y = 11.0 + 5.0 / 16.0;
    auto ray = [ & ]( const char *name, const glm::dvec3 &from, const glm::dvec3 &to, const glm::ivec3 &expect, bool expect_hit ) {
        glm::ivec3 out( -1, -1, -1 );
        int which_face = -1;
        RayTraversal::find_block_from_to( world, nullptr, from, to, out, &which_face, 0, 1, 0, 0 );
        const bool ok = ( out == expect ) == expect_hit;
        pr_test( "  %-55s hit=(%d,%d,%d)  %s", name, out.x, out.y, out.z, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };
    // The floor torch's pick box spans x/z 6..10 px: inside it but outside
    // the old 2px stick (7..9 px) still hits; just outside misses.
    ray( "floor torch: x + 6.5px inside new pick box", glm::dvec3( 10.40625, mid_y, 9.5 ), glm::dvec3( 10.40625, mid_y, 11.5 ), glm::ivec3( 10, 11, 10 ), true );
    ray( "floor torch: x + 5.5px outside pick box", glm::dvec3( 10.34375, mid_y, 9.5 ), glm::dvec3( 10.34375, mid_y, 11.5 ), glm::ivec3( 10, 11, 10 ), false );
    // Wall torch pushed to the cell's -x edge: pick box spans x 0..4 px.
    ray( "wall torch (rot 4): x + 3.5px inside pick box", glm::dvec3( 20.21875, mid_y, 9.5 ), glm::dvec3( 20.21875, mid_y, 11.5 ), glm::ivec3( 20, 11, 10 ), true );

    return failures;
}

// A right-click placement the event would silently reject must be detected
// before the event is queued (the caller plays sfx and consumes a hotbar
// block whenever change_block doesn't return LAST_BLOCK_ID).
int test_player_placement_valid( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    RepGameState &gs = *new RepGameState( );
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 0, 0, 0 );
    test_setup_world( gs.world );
    World &world = gs.world;

    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    const BlockState torch = { TORCH, 0, 0, TORCH, 0 };
    world.set_loaded_block( glm::ivec3( 10, 10, 10 ), stone ); // torch support
    world.set_loaded_block( glm::ivec3( 10, 11, 10 ), torch ); // occupied torch cell
    world.set_loaded_block( glm::ivec3( 12, 10, 10 ), stone ); // support for the valid cell

    int failures = 0;
    auto check = [ & ]( const char *name, const glm::ivec3 &pos, const BlockState &block_state, bool expected ) {
        const bool got = player_placement_valid( world, pos, block_state );
        const bool ok = got == expected;
        pr_test( "  %-55s valid=%d expected=%d  %s", name, got, expected, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };
    check( "torch into occupied torch cell", glm::ivec3( 10, 11, 10 ), torch, false );
    check( "stone into occupied torch cell", glm::ivec3( 10, 11, 10 ), stone, false );
    check( "torch into occupied solid cell", glm::ivec3( 12, 10, 10 ), torch, false );
    check( "torch into empty cell with support", glm::ivec3( 12, 11, 10 ), torch, true );
    check( "torch into empty cell without support", glm::ivec3( 15, 11, 10 ), torch, false );
    check( "stone into empty cell", glm::ivec3( 14, 11, 10 ), stone, true );
    check( "stone into unloaded cell", glm::ivec3( 100, 10, 10 ), stone, false );

    return failures;
}

