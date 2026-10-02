
#include "common/rep_tests.hpp"

#include "common/RepGame.hpp"

#include <cmath>

#include "common/perlin_noise.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/utils/collision.hpp"

typedef struct Test {
    const char *name;
    int ( *test_func )( );
} Test;

#define MK_TEST( name ) { #name, &test_##name }

int test_ecs( ) {
    ECS ecs = ECS( );
    return ecs.test_ecs( );
}

// Builds a World backed by a single loaded chunk at the origin (no GL needed:
// chunk.blocks is a plain array and Chunk::init's GL work is lazy). Positions
// outside that chunk read LAST_BLOCK_ID like an unloaded chunk.
void test_setup_world( World &world ) {
    ChunkLoader &cl = world.chunkLoader;
    cl.chunkArray = new Chunk[ MAX_LOADED_CHUNKS ]( );
    Chunk &chunk = cl.chunkArray[ chunk_slot_from_pos( glm::ivec3( 0, 0, 0 ) ) ];
    chunk.chunk_pos = glm::ivec3( 0, 0, 0 );
    chunk.chunk_mod = glm::ivec3( 0, 0, 0 );
    chunk.is_loading = 0;
    chunk.blocks = new BlockState[ CHUNK_BLOCK_SIZE ]( );
}

// Checks the power level at a position, printing a line per check. Returns the
// number of failures.
static int check_power( const World &world, const char *name, const glm::ivec3 &pos, int expected, int expected_max = -1 ) {
    const int power = world.get_loaded_block( pos ).current_redstone_power;
    const bool ok = expected_max < 0 ? power == expected : ( power >= expected && power <= expected_max );
    pr_test( "  %-55s power=%-2d expected=%s%d  %s", name, power, expected_max < 0 ? "" : ">=", expected, ok ? "PASS" : "FAIL" );
    return ok ? 0 : 1;
}

// Checks the block id at a position. Returns the number of failures.
static int check_id( const World &world, const char *name, const glm::ivec3 &pos, BlockID expected ) {
    const BlockID id = world.get_loaded_block( pos ).id;
    const bool ok = id == expected;
    pr_test( "  %-55s id=%-3d expected=%-3d  %s", name, id, expected, ok ? "PASS" : "FAIL" );
    return ok ? 0 : 1;
}

// Redstone mechanics, focused on the redstone torch:
//
//        dust(D2) lamp2  dust(G_d)  y=13
//  torch(T2) B dust(D) piston lamp glass  y=12   T2 attached to B's -x face
//           T1  S       dust(D3)   T3      y=11
//           S0                     S2      y=10
//
//   lamp3 sits at (9,12,11), beside the powered piston and the OFF torch.
//
// Minecraft semantics being verified:
//  - A torch strongly ("hard") powers the block directly above it: B=15.
//  - A hard-powered block powers adjacent dust: D=14, D2=14.
//  - A torch attached to a powered block turns off: T2=0.
//  - A powered block (weak or strong) activates adjacent mechanisms: the
//    piston extends a head and the lamp lights, both storing "activated" (1).
//  - A powered mechanism does NOT emit: dust beside the lit lamp stays 0.
//  - Only solid blocks hold power: glass above T3 stays 0, so dust on top
//    of the glass is not powered either.
//  - Powered opaque-but-non-solid blocks transmit nothing: the powered
//    piston does not activate the lamp beside it. A powered mechanism is
//    activated, not emitting: the lit lamp does not activate lamp2 above it,
//    so two lamps can't latch each other once their source is gone.
int test_redstone( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    RepGameState gs;
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 0, 0, 0 );
    test_setup_world( gs.world );
    World &world = gs.world;

    // Static terrain: written directly, like a chunk load.
    const BlockState stone = { STONE, 0, 0, STONE };
    world.set_loaded_block( glm::ivec3( 10, 10, 10 ), stone ); // S0: torch support
    world.set_loaded_block( glm::ivec3( 10, 12, 10 ), stone ); // B: block above the torch
    world.set_loaded_block( glm::ivec3( 11, 11, 10 ), stone ); // support under D
    world.set_loaded_block( glm::ivec3( 10, 11, 8 ), stone );  // support under D3
    world.set_loaded_block( glm::ivec3( 13, 10, 10 ), stone ); // S2: T3 support

    // Dust staircases at x=20-27: glass treads carry power up but not down
    // (the gap above a lower dust, not the side block, decides the diagonal
    // edge); stone treads carry both ways.
    const BlockState glass = { GLASS, 0, 0, GLASS };
    world.set_loaded_block( glm::ivec3( 20, 10, 10 ), stone ); // s1 tread
    world.set_loaded_block( glm::ivec3( 21, 11, 10 ), glass ); // s2 tread
    world.set_loaded_block( glm::ivec3( 22, 12, 10 ), glass ); // s3 tread
    world.set_loaded_block( glm::ivec3( 25, 10, 10 ), stone ); // s4 tread
    world.set_loaded_block( glm::ivec3( 26, 11, 10 ), stone ); // s5 tread

    // Blocked staircases: the corner cell above the lower dust gates the
    // diagonal edge. A solid corner blocks it entirely; a glass corner stays
    // open. All inside the 32-wide test chunk.
    world.set_loaded_block( glm::ivec3( 28, 10, 10 ), stone ); // b1 tread
    world.set_loaded_block( glm::ivec3( 29, 11, 10 ), stone ); // stair (b2 support)
    world.set_loaded_block( glm::ivec3( 28, 12, 10 ), stone ); // solid corner above b1
    world.set_loaded_block( glm::ivec3( 28, 10, 14 ), stone ); // b3 tread
    world.set_loaded_block( glm::ivec3( 29, 11, 14 ), stone ); // stair (b4 support)
    world.set_loaded_block( glm::ivec3( 28, 12, 14 ), glass ); // glass corner above b3

    // Redstone components placed through the real event path so power updates
    // propagate exactly as in gameplay.
    auto place = [ & ]( const glm::ivec3 &pos, const BlockState &block_state ) {
        gs.blockUpdateQueue.addBlockUpdate( std::make_shared<PlayerBlockPlacedEvent>( gs.tick_number, pos, block_state, false ) );
    };
    place( glm::ivec3( 10, 11, 10 ), { REDSTONE_TORCH, 0, 0, REDSTONE_TORCH } );  // T1: floor torch
    place( glm::ivec3( 11, 12, 10 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS } ); // D: dust beside B
    place( glm::ivec3( 10, 13, 10 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS } ); // D2: dust on top of B
    place( glm::ivec3( 9, 12, 10 ), { REDSTONE_TORCH, 6, 0, REDSTONE_TORCH } );  // T2: attached to B
    place( glm::ivec3( 10, 12, 11 ), { PISTON, 0, 0, PISTON } );                // piston beside B, faces +z
    place( glm::ivec3( 10, 12, 9 ), { REDSTONE_LAMP, 0, 0, REDSTONE_LAMP } );   // lamp beside B
    place( glm::ivec3( 10, 12, 8 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS } ); // D3: dust beside lamp
    place( glm::ivec3( 13, 11, 10 ), { REDSTONE_TORCH, 0, 0, REDSTONE_TORCH } ); // T3: floor torch under glass
    place( glm::ivec3( 13, 12, 10 ), { GLASS, 0, 0, GLASS } );                  // glass above T3: can't be powered
    place( glm::ivec3( 13, 13, 10 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS } ); // G_d: dust on glass
    place( glm::ivec3( 10, 13, 9 ), { REDSTONE_LAMP, 0, 0, REDSTONE_LAMP } );   // lamp2 above lit lamp
    place( glm::ivec3( 9, 12, 11 ), { REDSTONE_LAMP, 0, 0, REDSTONE_LAMP } );   // lamp3 beside powered piston

    // Glass staircase: source at the bottom must climb it.
    place( glm::ivec3( 20, 11, 10 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS } ); // s1
    place( glm::ivec3( 21, 12, 10 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS } ); // s2
    place( glm::ivec3( 22, 13, 10 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS } ); // s3
    place( glm::ivec3( 20, 11, 11 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK } ); // source beside s1

    // Stone staircase: source at the top must descend it.
    place( glm::ivec3( 25, 11, 10 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS } ); // s4
    place( glm::ivec3( 26, 12, 10 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS } ); // s5
    place( glm::ivec3( 26, 12, 11 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK } ); // source beside s5

    // Blocked staircases.
    place( glm::ivec3( 28, 11, 10 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS } ); // b1
    place( glm::ivec3( 29, 12, 10 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS } ); // b2 (blocked)
    place( glm::ivec3( 28, 11, 11 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK } ); // source beside b1
    place( glm::ivec3( 28, 11, 14 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS } ); // b3
    place( glm::ivec3( 29, 12, 14 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS } ); // b4 (glass corner)
    place( glm::ivec3( 28, 11, 15 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK } ); // source beside b3

    // Latch regression: two adjacent lamps must not sustain each other once
    // the source is removed — a powered mechanism is activated, not emitting.
    place( glm::ivec3( 28, 11, 18 ), { REDSTONE_LAMP, 0, 0, REDSTONE_LAMP } );   // M1 (sourced)
    place( glm::ivec3( 29, 11, 18 ), { REDSTONE_LAMP, 0, 0, REDSTONE_LAMP } );   // M2 (only neighbor is M1)
    place( glm::ivec3( 28, 11, 19 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK } ); // source beside M1

    for ( int i = 0; i < 40; i++ ) {
        gs.tick_number++;
        gs.blockUpdateQueue.processAllBlockUpdates( gs, gs.tick_number );
    }

    int failures = 0;
    failures += check_power( world, "torch T1 (floor torch)", glm::ivec3( 10, 11, 10 ), 15 );
    failures += check_power( world, "B: block above torch (hard powered)", glm::ivec3( 10, 12, 10 ), 15 );
    failures += check_power( world, "D: dust beside hard powered block", glm::ivec3( 11, 12, 10 ), 14 );
    failures += check_power( world, "D2: dust on top of hard powered block", glm::ivec3( 10, 13, 10 ), 14 );
    failures += check_power( world, "T2: torch attached to powered block", glm::ivec3( 9, 12, 10 ), 0 );
    failures += check_power( world, "piston beside powered block", glm::ivec3( 10, 12, 11 ), 1 );
    failures += check_power( world, "lamp beside powered block", glm::ivec3( 10, 12, 9 ), 1 );
    failures += check_id( world, "piston head extended into (10,12,12)", glm::ivec3( 10, 12, 12 ), PISTON_HEAD );
    failures += check_power( world, "D3: dust beside powered lamp (no emit)", glm::ivec3( 10, 12, 8 ), 0 );
    failures += check_power( world, "glass above torch (non-solid, no power)", glm::ivec3( 13, 12, 10 ), 0 );
    failures += check_power( world, "dust on glass (no power through glass)", glm::ivec3( 13, 13, 10 ), 0 );
    failures += check_power( world, "lamp2 above powered lamp (no relay)", glm::ivec3( 10, 13, 9 ), 0 );
    failures += check_power( world, "lamp3 beside powered piston (no relay)", glm::ivec3( 9, 12, 11 ), 0 );

    failures += check_power( world, "s1: dust at glass staircase base", glm::ivec3( 20, 11, 10 ), 14 );
    failures += check_power( world, "s2: dust climbing glass stair 1", glm::ivec3( 21, 12, 10 ), 13 );
    failures += check_power( world, "s3: dust climbing glass stair 2", glm::ivec3( 22, 13, 10 ), 12 );
    failures += check_power( world, "s5: dust on stone stair top (sourced)", glm::ivec3( 26, 12, 10 ), 14 );
    failures += check_power( world, "s4: dust descending stone stair", glm::ivec3( 25, 11, 10 ), 13 );

    failures += check_power( world, "b1: dust below blocked corner", glm::ivec3( 28, 11, 10 ), 14 );
    failures += check_power( world, "b2: dust past solid corner (blocked)", glm::ivec3( 29, 12, 10 ), 0 );
    failures += check_power( world, "b3: dust below glass corner", glm::ivec3( 28, 11, 14 ), 14 );
    failures += check_power( world, "b4: dust past glass corner (open)", glm::ivec3( 29, 12, 14 ), 13 );

    failures += check_power( world, "M1: lamp beside source", glm::ivec3( 28, 11, 18 ), 1 );
    failures += check_power( world, "M2: lamp beside powered lamp (no chain)", glm::ivec3( 29, 11, 18 ), 0 );

    // Teardown phase: breaking the source must unpower the whole glass
    // staircase — a missed diagonal update would leave it stuck on. Blocking
    // the corner above s4 must cut the stone staircase's downward feed.
    place( glm::ivec3( 20, 11, 11 ), BLOCK_STATE_AIR );
    place( glm::ivec3( 25, 12, 10 ), stone );
    place( glm::ivec3( 28, 11, 19 ), BLOCK_STATE_AIR );
    for ( int i = 0; i < 40; i++ ) {
        gs.tick_number++;
        gs.blockUpdateQueue.processAllBlockUpdates( gs, gs.tick_number );
    }
    failures += check_power( world, "s1 after source removed", glm::ivec3( 20, 11, 10 ), 0 );
    failures += check_power( world, "s2 after source removed", glm::ivec3( 21, 12, 10 ), 0 );
    failures += check_power( world, "s3 after source removed", glm::ivec3( 22, 13, 10 ), 0 );
    failures += check_power( world, "s4 after corner blocked", glm::ivec3( 25, 11, 10 ), 0 );
    failures += check_power( world, "M1 after source removed (no latch)", glm::ivec3( 28, 11, 18 ), 0 );
    failures += check_power( world, "M2 after source removed (no latch)", glm::ivec3( 29, 11, 18 ), 0 );
    return failures;
}

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
    RepGameState gs;
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 0, 0, 0 );
    test_setup_world( gs.world );
    World &world = gs.world;

    const BlockState stone = { STONE, 0, 0, STONE };
    const BlockState slab = { STONE_BRICK_SLAB, 0, 0, STONE_BRICK_SLAB };
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

constexpr Test all_tests[] = { //
    MK_TEST( ecs ),            //
    MK_TEST( redstone ),       //
    MK_TEST( sneak ),          //
    { nullptr, nullptr }

};

int rep_tests_start( ) {
    pr_test( "\nStarting Rep Tests" );

    int i = 0;
    int failed = 0;
    int another_test = 1;
    do {
        const Test *test = &all_tests[ i ];
        i++;
        another_test = test->name && test->test_func;
        if ( another_test ) {
            if ( const int result = test->test_func( ) ) {
                failed = 1;
                pr_test( "Test \"%s\" failed with %d", test->name, result );
            } else {
                pr_test( "Test \"%s\" passed", test->name );
            }
        }
    } while ( another_test );
    if ( failed ) {
        pr_test( "A Rep Tests Failed!!!\n" );
    } else {
        pr_test( "All Rep Tests passed\n" );
    }
    return failed;
}
