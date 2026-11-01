
#include "common/rep_tests.hpp"

#include "common/RepGame.hpp"

#include <cmath>

#include "common/perlin_noise.hpp"
#include "common/map_gen.hpp"
#include "common/structure_gen.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/block_update_events/DoorEvent.hpp"
#include "common/block_update_events/TntExplodeEvent.hpp"
#include "common/utils/collision.hpp"
#include "common/mobs.hpp"
#include "common/net/packet.hpp"
#include "common/net_entities.hpp"
#include "common/multiplayer.hpp"
#include "common/mob_uv.hpp"
#include "common/creative_order_gen.hpp"
#include "common/item.hpp"
#include "common/utils/map_storage.hpp"
#include "common/utils/ray_traversal.hpp"

#include <stdlib.h>
#include <string.h>

#if defined( REPGAME_WINDOWS )
static int setenv( const char *name, const char *value, int overwrite ) {
    if ( !overwrite && getenv( name ) ) {
        return 0;
    }
    return _putenv_s( name, value );
}
static int unsetenv( const char *name ) {
    return _putenv_s( name, "" );
}
#endif

typedef struct Test {
    const char *name;
    int ( *test_func )( );
} Test;

#define MK_TEST( name ) { #name, &test_##name }

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

// Builds a World backed by a single loaded chunk at the origin (no GL needed:
// chunk.cells is a plain array and Chunk::init's GL work is lazy). Positions
// outside that chunk read LAST_BLOCK_ID like an unloaded chunk.
void test_setup_world( World &world ) {
    ChunkLoader &cl = world.chunkLoader;
    cl.chunkArray = new Chunk[ MAX_LOADED_CHUNKS ]( );
    Chunk &chunk = cl.chunkArray[ chunk_slot_from_pos( glm::ivec3( 0, 0, 0 ) ) ];
    chunk.chunk_pos = glm::ivec3( 0, 0, 0 );
    chunk.chunk_mod = glm::ivec3( 0, 0, 0 );
    chunk.load_state = CHUNK_LIVE;
    chunk.cells = chunk_alloc_cells( );
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
    // Heap, not stack: RepGameState embeds megabytes of light queues (the
    // game keeps it in static storage via globalGameState) — a stack local
    // overflows the default 8MB stack. Leaked on purpose: the process exits
    // right after the tests and destructors would touch GL state.
    RepGameState &gs = *new RepGameState( );
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 0, 0, 0 );
    test_setup_world( gs.world );
    World &world = gs.world;

    // Static terrain: written directly, like a chunk load.
    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    world.set_loaded_block( glm::ivec3( 10, 10, 10 ), stone ); // S0: torch support
    world.set_loaded_block( glm::ivec3( 10, 12, 10 ), stone ); // B: block above the torch
    world.set_loaded_block( glm::ivec3( 11, 11, 10 ), stone ); // support under D
    world.set_loaded_block( glm::ivec3( 10, 11, 8 ), stone );  // support under D3
    world.set_loaded_block( glm::ivec3( 13, 10, 10 ), stone ); // S2: T3 support

    // Dust staircases at x=20-27: glass treads carry power up but not down
    // (the gap above a lower dust, not the side block, decides the diagonal
    // edge); stone treads carry both ways.
    const BlockState glass = { GLASS, 0, 0, GLASS, 0 };
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
    place( glm::ivec3( 10, 11, 10 ), { REDSTONE_TORCH, 0, 0, REDSTONE_TORCH, 0 } );  // T1: floor torch
    place( glm::ivec3( 11, 12, 10 ), { REDSTONE_WIRE_CROSS, 0, 0, REDSTONE_WIRE_CROSS, 0 } ); // D: dust beside B
    place( glm::ivec3( 10, 13, 10 ), { REDSTONE_WIRE_CROSS, 0, 0, REDSTONE_WIRE_CROSS, 0 } ); // D2: dust on top of B
    place( glm::ivec3( 9, 12, 10 ), { REDSTONE_TORCH, 6, 0, REDSTONE_TORCH, 0 } );  // T2: attached to B
    place( glm::ivec3( 10, 12, 11 ), { PISTON, 0, 0, PISTON, 0 } );                // piston beside B, faces +z
    place( glm::ivec3( 10, 12, 9 ), { REDSTONE_LAMP, 0, 0, REDSTONE_LAMP, 0 } );   // lamp beside B
    place( glm::ivec3( 10, 12, 8 ), { REDSTONE_WIRE_CROSS, 0, 0, REDSTONE_WIRE_CROSS, 0 } ); // D3: dust beside lamp
    place( glm::ivec3( 13, 11, 10 ), { REDSTONE_TORCH, 0, 0, REDSTONE_TORCH, 0 } ); // T3: floor torch under glass
    place( glm::ivec3( 13, 12, 10 ), { GLASS, 0, 0, GLASS, 0 } );                  // glass above T3: can't be powered
    place( glm::ivec3( 13, 13, 10 ), { REDSTONE_WIRE_CROSS, 0, 0, REDSTONE_WIRE_CROSS, 0 } ); // G_d: dust on glass
    place( glm::ivec3( 10, 13, 9 ), { REDSTONE_LAMP, 0, 0, REDSTONE_LAMP, 0 } );   // lamp2 above lit lamp
    place( glm::ivec3( 9, 12, 11 ), { REDSTONE_LAMP, 0, 0, REDSTONE_LAMP, 0 } );   // lamp3 beside powered piston

    // Glass staircase: source at the bottom must climb it.
    place( glm::ivec3( 20, 11, 10 ), { REDSTONE_WIRE_CROSS, 0, 0, REDSTONE_WIRE_CROSS, 0 } ); // s1
    place( glm::ivec3( 21, 12, 10 ), { REDSTONE_WIRE_CROSS, 0, 0, REDSTONE_WIRE_CROSS, 0 } ); // s2
    place( glm::ivec3( 22, 13, 10 ), { REDSTONE_WIRE_CROSS, 0, 0, REDSTONE_WIRE_CROSS, 0 } ); // s3
    place( glm::ivec3( 20, 11, 11 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } ); // source beside s1

    // Stone staircase: source at the top must descend it.
    place( glm::ivec3( 25, 11, 10 ), { REDSTONE_WIRE_CROSS, 0, 0, REDSTONE_WIRE_CROSS, 0 } ); // s4
    place( glm::ivec3( 26, 12, 10 ), { REDSTONE_WIRE_CROSS, 0, 0, REDSTONE_WIRE_CROSS, 0 } ); // s5
    place( glm::ivec3( 26, 12, 11 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } ); // source beside s5

    // Blocked staircases.
    place( glm::ivec3( 28, 11, 10 ), { REDSTONE_WIRE_CROSS, 0, 0, REDSTONE_WIRE_CROSS, 0 } ); // b1
    place( glm::ivec3( 29, 12, 10 ), { REDSTONE_WIRE_CROSS, 0, 0, REDSTONE_WIRE_CROSS, 0 } ); // b2 (blocked)
    place( glm::ivec3( 28, 11, 11 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } ); // source beside b1
    place( glm::ivec3( 28, 11, 14 ), { REDSTONE_WIRE_CROSS, 0, 0, REDSTONE_WIRE_CROSS, 0 } ); // b3
    place( glm::ivec3( 29, 12, 14 ), { REDSTONE_WIRE_CROSS, 0, 0, REDSTONE_WIRE_CROSS, 0 } ); // b4 (glass corner)
    place( glm::ivec3( 28, 11, 15 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } ); // source beside b3

    // Latch regression: two adjacent lamps must not sustain each other once
    // the source is removed — a powered mechanism is activated, not emitting.
    place( glm::ivec3( 28, 11, 18 ), { REDSTONE_LAMP, 0, 0, REDSTONE_LAMP, 0 } );   // M1 (sourced)
    place( glm::ivec3( 29, 11, 18 ), { REDSTONE_LAMP, 0, 0, REDSTONE_LAMP, 0 } );   // M2 (only neighbor is M1)
    place( glm::ivec3( 28, 11, 19 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } ); // source beside M1

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
    failures += check_id( world, "piston head extended into (10,12,12)", glm::ivec3( 10, 12, 12 ), PISTON_ARM_COLLISION );
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

// Item id space (Phase 1 non-block items): block items share the BlockID
// value, pure items live at ITEM_ID_BASE+, LAST_BLOCK_ID stays the empty
// sentinel. Covers item_id_valid/item_is_block/item_max_stack, the wire
// slot format, and the generated block drop table.
int test_items( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };

    check_bool( "item: block id is a block item", item_is_block( GRASS ) );
    check_bool( "item: pure item is not a block item", !item_is_block( STICK ) );
    check_bool( "item: defined block is valid", item_id_valid( GRASS ) );
    check_bool( "item: defined item is valid", item_id_valid( STICK ) && item_id_valid( APPLE ) );
    check_bool( "item: ITEM_ID_END is invalid", !item_id_valid( ITEM_ID_END ) );
    check_bool( "item: sentinel is invalid", !item_id_valid( LAST_BLOCK_ID ) );
    check_bool( "item: block max stack", item_max_stack( GRASS ) == MAX_STACK_SIZE );
    check_bool( "item: pure item max stack default", item_max_stack( STICK ) == 64 );
    check_bool( "item: icon for pure item", item_icon_texture( STICK ) != item_icon_texture( DIAMOND ) );
    check_bool( "item: pure item is flat icon", !item_icon_is_isometric( STICK ) );
    check_bool( "item: item_name", item_name( STICK ) != nullptr && item_name( GRASS ) != nullptr );

    // Wire slot round trip: a real item survives; an undefined item-range id
    // is normalized to empty.
    {
        NetChestSetSlotPayload out_p = { 1, 2, 3, 4, { STICK, 7 } };
        PacketWriter w;
        net_serialize_chest_set_slot( w, out_p );
        std::vector<uint8_t> buf = w.take_buf( );
        PacketReader r( buf.data( ), buf.size( ) );
        NetChestSetSlotPayload in_p;
        memset( &in_p, 0, sizeof( in_p ) );
        check_bool( "net slot: deserialize", net_deserialize_chest_set_slot( r, in_p ) );
        check_bool( "net slot: item id survives", in_p.slot_value.item_id == STICK && in_p.slot_value.quantity == 7 );

        NetChestSetSlotPayload bad_p = { 1, 2, 3, 4, { ITEM_ID_END, 7 } };
        PacketWriter w2;
        net_serialize_chest_set_slot( w2, bad_p );
        std::vector<uint8_t> buf2 = w2.take_buf( );
        PacketReader r2( buf2.data( ), buf2.size( ) );
        NetChestSetSlotPayload in_p2;
        memset( &in_p2, 0, sizeof( in_p2 ) );
        check_bool( "net slot: deserialize bad id", net_deserialize_chest_set_slot( r2, in_p2 ) );
        check_bool( "net slot: undefined id clamps empty", in_p2.slot_value.item_id == LAST_BLOCK_ID && in_p2.slot_value.quantity == 0 );
    }

    // Block drop table (deterministic rng): table entries roll, no-table
    // blocks drop themselves, drops:[] drops nothing.
    {
        uint32_t rng = 12345;
        ItemStack drops[ 4 ];
        check_bool( "drops: stone -> 1 cobblestone", block_roll_drops( STONE, &rng, drops, 4 ) == 1 && drops[ 0 ].item_id == COBBLESTONE && drops[ 0 ].count == 1 );
        rng = 12345;
        check_bool( "drops: glass -> nothing", block_roll_drops( GLASS, &rng, drops, 4 ) == 0 );
        rng = 12345;
        check_bool( "drops: diamond ore -> diamond", block_roll_drops( DIAMOND_ORE, &rng, drops, 4 ) == 1 && drops[ 0 ].item_id == DIAMOND && drops[ 0 ].count == 1 );
        rng = 12345;
        check_bool( "drops: unlisted block -> itself", block_roll_drops( DIRT, &rng, drops, 4 ) == 1 && drops[ 0 ].item_id == DIRT && drops[ 0 ].count == 1 );
        // Lapis ore rolls 4-8 lapis.
        rng = 999;
        int n = block_roll_drops( LAPIS_ORE, &rng, drops, 4 );
        check_bool( "drops: lapis ore -> 4-8 lapis", n == 1 && drops[ 0 ].item_id == LAPIS_LAZULI && drops[ 0 ].count >= 4 && drops[ 0 ].count <= 8 );
        // Gravel's two <1.0 chances are mutually exclusive: exactly one item
        // per roll, either flint or gravel, over many seeds.
        int gravel_ok = 1, saw_flint = 0, saw_gravel = 0;
        for ( uint32_t seed = 1; seed <= 500; seed++ ) {
            rng = seed * 2654435761u;
            const int m = block_roll_drops( GRAVEL, &rng, drops, 4 );
            if ( m != 1 || ( drops[ 0 ].item_id != FLINT && drops[ 0 ].item_id != GRAVEL ) ) {
                gravel_ok = 0;
            } else if ( drops[ 0 ].item_id == FLINT ) {
                saw_flint = 1;
            } else {
                saw_gravel = 1;
            }
        }
        check_bool( "drops: gravel rolls exactly one item", gravel_ok && saw_flint && saw_gravel );
        // Oak leaves: sapling/stick/apple are mutually exclusive too.
        int leaves_ok = 1;
        for ( uint32_t seed = 1; seed <= 500; seed++ ) {
            rng = seed * 40503u;
            if ( block_roll_drops( OAK_LEAVES, &rng, drops, 4 ) > 1 ) {
                leaves_ok = 0;
            }
        }
        check_bool( "drops: oak leaves at most one item", leaves_ok );
    }
    return failures;
}

// Fills the hotbar + survival inventory fields headlessly (init() does GL
// work; friend access mirrors test_mobs).
void test_setup_inventories( RepGameState &gs ) {
    gs.hotbar.width = HOTBAR_WIDTH;
    gs.hotbar.height = HOTBAR_HEIGHT;
    gs.hotbar.num_blocks_max = HOTBAR_WIDTH * HOTBAR_HEIGHT;
    gs.hotbar.slots = static_cast<InventorySlot *>( calloc( gs.hotbar.num_blocks_max, sizeof( InventorySlot ) ) );
    for ( int i = 0; i < gs.hotbar.num_blocks_max; i++ ) {
        gs.hotbar.slots[ i ].item_id = LAST_BLOCK_ID;
        gs.hotbar.slots[ i ].quantity = 0;
    }
    gs.hotbar.selected_slot = 0;
    InventoryRenderer &ir = gs.hotbar.inventory_renderer;
    ir.width = HOTBAR_WIDTH;
    ir.height = HOTBAR_HEIGHT;
    ir.num_blocks_max = HOTBAR_WIDTH * HOTBAR_HEIGHT;
    ir.screen_width = 800;
    ir.screen_height = 600;
    ir.inv_items_x = 0;
    ir.inv_items_y = 0;
    ir.inv_cell_stride = 1.0f;
    ir.inv_block_size = 1;
    ir.inv_block_offset = 0;
    ir.slots_entities = static_cast<entt::entity *>( calloc( ir.num_blocks_max, sizeof( entt::entity ) ) );
    for ( int i = 0; i < ir.num_blocks_max; i++ ) {
        ir.slots_entities[ i ] = entt::null;
    }

    SurvivalInventory &si = gs.survival_inventory;
    si.width = SURVIVAL_INVENTORY_WIDTH;
    si.height = SURVIVAL_INVENTORY_HEIGHT;
    si.num_slots = SURVIVAL_INVENTORY_WIDTH * SURVIVAL_INVENTORY_HEIGHT;
    si.slots = static_cast<InventorySlot *>( calloc( si.num_slots, sizeof( InventorySlot ) ) );
    for ( int i = 0; i < si.num_slots; i++ ) {
        si.slots[ i ].item_id = LAST_BLOCK_ID;
        si.slots[ i ].quantity = 0;
    }
    InventoryRenderer &sir = si.inventory_renderer;
    sir.width = SURVIVAL_INVENTORY_WIDTH;
    sir.height = SURVIVAL_INVENTORY_HEIGHT;
    sir.num_blocks_max = si.num_slots;
    sir.screen_width = 800;
    sir.screen_height = 600;
    sir.inv_items_x = 0;
    sir.inv_items_y = 0;
    sir.inv_cell_stride = 1.0f;
    sir.inv_block_size = 1;
    sir.inv_block_offset = 0;
    sir.slots_entities = static_cast<entt::entity *>( calloc( sir.num_blocks_max, sizeof( entt::entity ) ) );
    for ( int i = 0; i < sir.num_blocks_max; i++ ) {
        sir.slots_entities[ i ] = entt::null;
    }
}

// Dropped item entities: falling/resting, water float, merging, despawn,
// chunk freeze, pickup remainder, cap eviction.
int test_item_entities( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    RepGameState &gs = *new RepGameState( );
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 1.5, 60.0, 1.5 ); // far away: no pickup
    gs.camera.player_height = PLAYER_HEIGHT;
    gs.camera.eye_height = PLAYER_EYE_HEIGHT;
    test_setup_world( gs.world );
    test_setup_inventories( gs );
    World &world = gs.world;

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

    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    const BlockState water = { WATER, 0, 0, WATER, 0 };
    // Stone floor at y=10 (top surface 11) over a generous area, plus a
    // walled water column at (25, *, 25) standing 5 deep on the floor.
    for ( int x = 5; x <= 30; x++ ) {
        for ( int z = 5; z <= 30; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 10, z ), stone );
        }
    }
    for ( int y = 11; y <= 15; y++ ) {
        world.set_loaded_block( glm::ivec3( 25, y, 25 ), water );
        // Contain the column so it can't pour out.
        world.set_loaded_block( glm::ivec3( 24, y, 25 ), stone );
        world.set_loaded_block( glm::ivec3( 26, y, 25 ), stone );
        world.set_loaded_block( glm::ivec3( 25, y, 24 ), stone );
        world.set_loaded_block( glm::ivec3( 25, y, 26 ), stone );
    }

    // --- fall + rest --------------------------------------------------------
    {
        ItemEntities items;
        items.spawn( STICK, 3, glm::dvec3( 10.5, 14.0, 10.5 ), glm::dvec3( 0.0 ), 0 );
        for ( int i = 0; i < 200 && !items.debug_items( )[ 0 ].grounded; i++ ) {
            gs.tick_number++;
            items.tick( gs );
        }
        const ItemEntities::ItemEntity it = items.debug_items( )[ 0 ];
        check_bool( "item: lands and grounds", it.grounded == 1 );
        check_near( "item: rests at floor top", it.pos.y, 11.0, 0.02 );
    }

    // --- water float ---------------------------------------------------------
    {
        ItemEntities items;
        items.spawn( STICK, 1, glm::dvec3( 25.5, 11.2, 25.5 ), glm::dvec3( 0.0 ), 0 );
        double top = -1;
        for ( int i = 0; i < 400; i++ ) {
            gs.tick_number++;
            items.tick( gs );
            top = items.debug_items( )[ 0 ].pos.y;
        }
        // Buoyancy + rise cap should leave it bobbing near the water surface
        // (top water cell spans 15..16): resting pos near 16.
        check_near( "item: floats to water surface", top, 16.0, 0.6 );
    }

    // --- merge ---------------------------------------------------------------
    {
        ItemEntities items;
        items.spawn( STICK, 10, glm::dvec3( 10.4, 11.0, 10.4 ), glm::dvec3( 0.0 ), 0 );
        items.spawn( STICK, 10, glm::dvec3( 10.6, 11.0, 10.6 ), glm::dvec3( 0.0 ), 0 );
        items.spawn( APPLE, 5, glm::dvec3( 10.5, 11.0, 10.9 ), glm::dvec3( 0.0 ), 0 );
        for ( int i = 0; i < 25; i++ ) {
            gs.tick_number++;
            items.tick( gs );
        }
        int sticks = 0, apples = 0, entities = 0;
        for ( const auto &d : items.debug_items( ) ) {
            entities++;
            if ( d.item == STICK ) {
                sticks += d.count;
            } else if ( d.item == APPLE ) {
                apples += d.count;
            }
        }
        check_bool( "item: same items merge", entities == 2 && sticks == 20 && apples == 5 );
    }

    // --- merge respects max_stack --------------------------------------------
    {
        ItemEntities items;
        items.spawn( STICK, 60, glm::dvec3( 10.4, 11.0, 10.4 ), glm::dvec3( 0.0 ), 0 );
        items.spawn( STICK, 60, glm::dvec3( 10.6, 11.0, 10.6 ), glm::dvec3( 0.0 ), 0 );
        for ( int i = 0; i < 25; i++ ) {
            gs.tick_number++;
            items.tick( gs );
        }
        int total = 0, entities = 0, over = 0;
        for ( const auto &d : items.debug_items( ) ) {
            entities++;
            total += d.count;
            if ( d.count > item_max_stack( STICK ) ) {
                over++;
            }
        }
        check_bool( "item: merge capped at max_stack", entities == 2 && total == 120 && over == 0 );
    }

    // --- despawn -------------------------------------------------------------
    {
        ItemEntities items;
        items.spawn( STICK, 1, glm::dvec3( 10.5, 11.0, 10.5 ), glm::dvec3( 0.0 ), 0 );
        check_bool( "item: alive before despawn", items.count( ) == 1 );
        for ( int i = 0; i < ItemEntities::ITEM_DESPAWN_TICKS + 5; i++ ) {
            gs.tick_number++;
            items.tick( gs );
        }
        check_bool( "item: despawns at 5 minutes", items.count( ) == 0 );
    }

    // --- frozen in a non-live chunk -------------------------------------------
    {
        ItemEntities items;
        items.spawn( STICK, 1, glm::dvec3( 1000.5, 60.0, 1000.5 ), glm::dvec3( 0.0, 0.5, 0.0 ), 0 );
        for ( int i = 0; i < 40; i++ ) {
            gs.tick_number++;
            items.tick( gs );
        }
        const ItemEntities::ItemEntity it = items.debug_items( )[ 0 ];
        check_bool( "item: frozen in unloaded chunk", it.pos == glm::dvec3( 1000.5, 60.0, 1000.5 ) && it.age_ticks == 0 );
    }

    // --- pickup into a full inventory leaves the remainder ---------------------
    {
        test_setup_inventories( gs ); // fresh empty inventories
        check_bool( "give: empty inventory takes all", RepGame::give_item_to_player( gs, STICK, 3 ) == 0 );
        check_bool( "give: went to hotbar", gs.hotbar.findSlotWithItem( STICK, false ) >= 0 );
        for ( int i = 0; i < gs.hotbar.num_blocks_max; i++ ) {
            gs.hotbar.slots[ i ].item_id = COBBLESTONE;
            gs.hotbar.slots[ i ].quantity = 64;
        }
        for ( int i = 0; i < gs.survival_inventory.num_slots; i++ ) {
            gs.survival_inventory.slots[ i ].item_id = COBBLESTONE;
            gs.survival_inventory.slots[ i ].quantity = 64;
        }
        check_bool( "give: full inventory returns all", RepGame::give_item_to_player( gs, APPLE, 5 ) == 5 );
    }

    // --- pickup: empty inventory collects, full keeps the remainder ------------
    {
        test_setup_inventories( gs );
        ItemEntities items;
        // Feet at y=11 on the floor; the item spawns inside the feet AABB.
        gs.camera.pos = glm::dvec3( 10.5, 11.0 + PLAYER_EYE_HEIGHT, 10.5 );
        items.spawn( STICK, 7, glm::dvec3( 10.5, 11.0, 10.5 ), glm::dvec3( 0.0 ), 0 );
        gs.tick_number++;
        items.tick( gs );
        check_bool( "item: picked up into empty hotbar", items.count( ) == 0 && gs.hotbar.findSlotWithItem( STICK, false ) >= 0 );
        // Full inventory: the entity keeps its whole stack.
        for ( int i = 0; i < gs.hotbar.num_blocks_max; i++ ) {
            gs.hotbar.slots[ i ].item_id = COBBLESTONE;
            gs.hotbar.slots[ i ].quantity = 64;
        }
        for ( int i = 0; i < gs.survival_inventory.num_slots; i++ ) {
            gs.survival_inventory.slots[ i ].item_id = COBBLESTONE;
            gs.survival_inventory.slots[ i ].quantity = 64;
        }
        items.spawn( APPLE, 4, glm::dvec3( 10.5, 11.0, 10.5 ), glm::dvec3( 0.0 ), 0 );
        gs.tick_number++;
        items.tick( gs );
        check_bool( "item: full inventory keeps the stack", items.count( ) == 1 && items.debug_items( )[ 0 ].count == 4 );
        gs.camera.pos = glm::dvec3( 1.5, 60.0, 1.5 ); // back out of pickup range
    }

    // --- cap eviction ----------------------------------------------------------
    {
        ItemEntities items;
        for ( int i = 0; i < static_cast<int>( ItemEntities::MAX_ITEM_ENTITIES ) + 5; i++ ) {
            items.spawn( STICK, 1, glm::dvec3( 10.5 + ( i % 8 ) * 0.1, 11.0, 10.5 + ( i % 8 ) * 0.1 ), glm::dvec3( 0.0 ), 0 );
        }
        check_bool( "item: cap holds", items.count( ) == ItemEntities::MAX_ITEM_ENTITIES );
        // The 5 oldest were evicted: lowest surviving id is 6.
        uint32_t min_id = UINT32_MAX;
        for ( const auto &d : items.debug_items( ) ) {
            min_id = std::min( min_id, d.id );
        }
        check_bool( "item: oldest evicted first", min_id == 6 );
    }

    // --- persistence: record round trip, invalid records, adopt/persist ------
    {
        const char *orig_home = getenv( "HOME" );
        char orig_home_buf[ 512 ] = { 0 };
        if ( orig_home != nullptr ) {
            strncpy( orig_home_buf, orig_home, sizeof( orig_home_buf ) - 1 );
        }
        setenv( "HOME", "/tmp/repgame_test_items_home", 1 );
        MapStorage storage;
        storage.init( "TestItems" );

        // Record round trip.
        ItemFileRecord rec = { 0, 0, 0.0, 0.0, 0.0, 0.0f, 0.0f, 0.0f, 0 };
        rec.item = STICK;
        rec.count = 5;
        rec.x = 3.5;
        rec.y = 12.0;
        rec.z = 4.5;
        rec.vx = 0.1f;
        rec.vy = -0.2f;
        rec.vz = 0.3f;
        rec.age_ticks = 1234;
        storage.persist_items( glm::ivec3( 0, 0, 0 ), { rec } );
        std::vector<ItemFileRecord> out;
        check_bool( "persist: item file round trips", storage.load_items( glm::ivec3( 0, 0, 0 ), out ) == 1 && out.size( ) == 1 && out[ 0 ].item == STICK && out[ 0 ].count == 5 && out[ 0 ].age_ticks == 1234 && fabs( out[ 0 ].x - 3.5 ) < 1e-9 && fabs( out[ 0 ].vy + 0.2f ) < 1e-6 );

        // Invalid id / zero count are skipped.
        ItemFileRecord bad_id = rec;
        bad_id.item = 999999u;
        ItemFileRecord bad_count = rec;
        bad_count.count = 0;
        storage.persist_items( glm::ivec3( 0, 0, 0 ), { bad_id, bad_count, rec } );
        check_bool( "persist: invalid records skipped", storage.load_items( glm::ivec3( 0, 0, 0 ), out ) == 1 && out.size( ) == 1 && out[ 0 ].item == STICK );

        // A larger on-disk record_size still parses the shared prefix.
        {
            char file_name[ CHUNK_NAME_MAX_LENGTH ];
            snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_ITEMS, storage.get_map_name( ), 0, 0, 0 );
            FILE *fp = fopen( file_name, "wb" );
            const uint32_t big_size = sizeof( ItemFileRecord ) + 8;
            uint32_t one = 1;
            fwrite( &big_size, sizeof( uint32_t ), 1, fp );
            fwrite( &one, sizeof( uint32_t ), 1, fp );
            fwrite( &rec, sizeof( ItemFileRecord ), 1, fp );
            const char pad[ 8 ] = { 0 };
            fwrite( pad, 1, sizeof( pad ), fp );
            fclose( fp );
            check_bool( "persist: larger record_size parses prefix", storage.load_items( glm::ivec3( 0, 0, 0 ), out ) == 1 && out.size( ) == 1 && out[ 0 ].item == STICK && out[ 0 ].count == 5 );
        }
        // A tiny record_size leaves count 0 -> skipped.
        {
            char file_name[ CHUNK_NAME_MAX_LENGTH ];
            snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_ITEMS, storage.get_map_name( ), 0, 0, 0 );
            FILE *fp = fopen( file_name, "wb" );
            const uint32_t tiny_size = 4;
            uint32_t one = 1;
            fwrite( &tiny_size, sizeof( uint32_t ), 1, fp );
            fwrite( &one, sizeof( uint32_t ), 1, fp );
            fwrite( &rec.item, sizeof( uint32_t ), 1, fp );
            fclose( fp );
            check_bool( "persist: tiny record_size skipped", storage.load_items( glm::ivec3( 0, 0, 0 ), out ) == 1 && out.empty( ) );
        }

        // Adopt drains pending records into live entities; persist writes
        // them back out for the owned chunk.
        ItemEntities items;
        items.init_storage( &storage );
        Chunk chunk;
        chunk.chunk_pos = glm::ivec3( 0, 0, 0 );
        chunk.pending_items.push_back( rec );
        chunk.items_file_present = 1;
        items.adopt_chunk( chunk );
        check_bool( "persist: adopt spawns record", items.count( ) == 1 && chunk.pending_items.empty( ) );
        if ( items.count( ) == 1 ) {
            const ItemEntities::ItemEntity it = items.debug_items( )[ 0 ];
            check_bool( "persist: adopt restores fields", it.item == STICK && it.count == 5 && it.age_ticks == 1234 && it.pickup_delay == 0 && fabs( it.vel.y + 0.2 ) < 1e-6 );
        }
        items.persist_all( );
        check_bool( "persist: persist_all writes item", storage.load_items( glm::ivec3( 0, 0, 0 ), out ) == 1 && out.size( ) == 1 && out[ 0 ].item == STICK && out[ 0 ].count == 5 );
        // Non-persistent entities (REPGAME_ITEM_TEST) never reach the file.
        items.spawn( APPLE, 2, glm::dvec3( 4.0, 12.0, 4.0 ), glm::dvec3( 0.0 ), 0, 0 );
        items.persist_all( );
        check_bool( "persist: non-persistent excluded", storage.load_items( glm::ivec3( 0, 0, 0 ), out ) == 1 && out.size( ) == 1 );

        if ( orig_home != nullptr ) {
            setenv( "HOME", orig_home_buf, 1 );
        } else {
            unsetenv( "HOME" );
        }
    }

    return failures;
}

// Pre-2047 save migration: 1249 was the empty-slot marker, and ids that are
// now undefined or past LAST_BLOCK_ID must load as empty slots / AIR.
int test_save_migration( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };

    check_bool( "slot: sentinel 1249 loads empty", save_slot_id_is_empty( LEGACY_EMPTY_BLOCK_ID ) );
    uint32_t undefined_id = LAST_BLOCK_ID;
    for ( uint32_t i = 0; i < LAST_BLOCK_ID; i++ ) {
        if ( i != LEGACY_EMPTY_BLOCK_ID && !block_id_defined( i ) ) {
            undefined_id = i;
            break;
        }
    }
    check_bool( "slot: an undefined id exists below LAST_BLOCK_ID", undefined_id < LAST_BLOCK_ID );
    check_bool( "slot: undefined id loads empty", undefined_id < LAST_BLOCK_ID && save_slot_id_is_empty( undefined_id ) );
    check_bool( "slot: out-of-range id loads empty", save_slot_id_is_empty( LAST_BLOCK_ID + 42 ) );
    check_bool( "slot: real block survives", !save_slot_id_is_empty( GRASS ) );
    check_bool( "slot: pure item survives", !save_slot_id_is_empty( STICK ) );
    check_bool( "slot: undefined item id loads empty", save_slot_id_is_empty( ITEM_ID_END ) );
    check_bool( "chunk: sentinel 1249 loads AIR", save_chunk_block_id( LEGACY_EMPTY_BLOCK_ID ) == AIR );
    check_bool( "chunk: out-of-range id loads AIR", save_chunk_block_id( LAST_BLOCK_ID + 42 ) == AIR );
    check_bool( "chunk: real block survives", save_chunk_block_id( GRASS ) == GRASS );
    check_bool( "sentinel 1249 is not a defined block", !block_id_defined( LEGACY_EMPTY_BLOCK_ID ) );
    return failures;
}

// Creative inventory ordering: generated CREATIVE_BLOCKS must contain only
// defined pickable blocks with no duplicates, must include the material
// representatives, and must keep every legacy block with a unique look.
int test_creative( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };

    unsigned char seen[ LAST_BLOCK_ID ] = { 0 };
    unsigned char seen_items[ ITEM_ID_END - ITEM_ID_BASE ] = { 0 };
    int dupes = 0, bad = 0, item_bad = 0;
    for ( unsigned int i = 0; i < CREATIVE_BLOCK_COUNT; i++ ) {
        const ItemID iid = CREATIVE_BLOCKS[ i ];
        if ( !item_is_block( iid ) ) {
            // Pure items tail the list: defined and non-duplicated.
            if ( !item_id_defined( iid ) || seen_items[ iid - ITEM_ID_BASE ] ) {
                item_bad++;
                continue;
            }
            seen_items[ iid - ITEM_ID_BASE ] = 1;
            continue;
        }
        const BlockID bid = static_cast<BlockID>( iid );
        if ( seen[ bid ] ) {
            dupes++;
            continue;
        }
        seen[ bid ] = 1;
        if ( !block_id_defined( bid ) || !block_definition_get_definition( bid )->is_pickable ) {
            bad++;
        }
    }
    check_bool( "creative: no duplicate ids", dupes == 0 );
    check_bool( "creative: all entries defined and pickable", bad == 0 );
    check_bool( "creative: item tail defined and unique", item_bad == 0 );
    check_bool( "creative: STICK item listed", seen_items[ STICK - ITEM_ID_BASE ] != 0 );

    auto contains = [ & ]( BlockID bid ) { return seen[ bid ] != 0; };
    check_bool( "creative: ACACIA_PLANKS listed", contains( ACACIA_PLANKS ) );
    check_bool( "creative: ACACIA_FENCE listed", contains( ACACIA_FENCE ) );
    check_bool( "creative: FENCE (oak fence) listed", contains( FENCE ) );
    check_bool( "creative: FENCE_GATE (oak gate) listed", contains( FENCE_GATE ) );
    check_bool( "creative: OAK_FENCE hidden behind FENCE", !contains( OAK_FENCE ) );
    check_bool( "creative: ACACIA_STAIRS listed", contains( ACACIA_STAIRS ) );
    check_bool( "creative: ACACIA_DOUBLE_SLAB hidden", !contains( ACACIA_DOUBLE_SLAB ) );
    check_bool( "creative: GRASS listed", contains( GRASS ) );
    check_bool( "creative: STONE listed", contains( STONE ) );
    check_bool( "creative: TNT listed", contains( TNT ) );
    check_bool( "creative: OAK_LEAVES listed", contains( OAK_LEAVES ) );
    check_bool( "creative: COBBLESTONE_SLAB listed", contains( COBBLESTONE_SLAB ) );
    check_bool( "creative: STONE_BUTTON listed", contains( STONE_BUTTON ) );
    check_bool( "creative: WHITE_WOOL listed", contains( WHITE_WOOL ) );
    check_bool( "creative: WOOL (legacy name) hidden", !contains( WOOL ) );
    check_bool( "creative: WHITE_CONCRETE_POWDER listed", contains( WHITE_CONCRETE_POWDER ) );
    check_bool( "creative: ACACIA_LEAVES listed", contains( ACACIA_LEAVES ) );
    check_bool( "creative: OAK_LOG listed", contains( OAK_LOG ) );
    check_bool( "creative: LIGHT_BLUE_STAINED_GLASS listed", contains( LIGHT_BLUE_STAINED_GLASS ) );
    check_bool( "creative: GLASS_PALE_BLUE hidden", !contains( GLASS_PALE_BLUE ) );
    return failures;
}

// Fences and fence gates: connection rules, box geometry, the right-click
// toggle rotation, the generated definitions, creative listing, and the
// block-world showcase layout.
int test_fences( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };

    // Every emitted box must stay inside the 0..16 cell.
    auto boxes_in_bounds = [ & ]( const BlockBox *boxes, int n ) {
        for ( int i = 0; i < n; i++ ) {
            for ( int a = 0; a < 3; a++ ) {
                if ( boxes[ i ].offset[ a ] < 0 || boxes[ i ].scale[ a ] <= 0 || boxes[ i ].offset[ a ] + boxes[ i ].scale[ a ] > 16 ) {
                    return false;
                }
            }
        }
        return true;
    };

    // --- fence_boxes -----------------------------------------------------
    BlockBox boxes[ FENCE_GATE_MAX_BOXES ];
    check_bool( "fence: no connections gives just the post",
                fence_boxes( 0, boxes ) == 1 &&                                                            //
                    boxes[ 0 ].scale[ 0 ] == 4 && boxes[ 0 ].scale[ 1 ] == 16 && boxes[ 0 ].scale[ 2 ] == 4 && //
                    boxes[ 0 ].offset[ 0 ] == 6 && boxes[ 0 ].offset[ 1 ] == 0 && boxes[ 0 ].offset[ 2 ] == 6 );
    check_bool( "fence: one side gives post + 2 rails", fence_boxes( FENCE_CONNECT_POS_X, boxes ) == 3 );
    check_bool( "fence: +x lower rail x 10..16 y 6..9",
                boxes[ 1 ].offset[ 0 ] == 10 && boxes[ 1 ].scale[ 0 ] == 6 && //
                    boxes[ 1 ].offset[ 1 ] == 6 && boxes[ 1 ].scale[ 1 ] == 3 && //
                    boxes[ 1 ].offset[ 2 ] == 7 && boxes[ 1 ].scale[ 2 ] == 2 );
    check_bool( "fence: +x upper rail y 12..15",
                boxes[ 2 ].offset[ 1 ] == 12 && boxes[ 2 ].scale[ 1 ] == 3 );
    check_bool( "fence: rail ends skip both x faces",
                boxes[ 1 ].skip_faces == ( ( 1u << FACE_LEFT ) | ( 1u << FACE_RIGHT ) ) );
    check_bool( "fence: opposite sides merge into 3 boxes",
                fence_boxes( FENCE_CONNECT_POS_X | FENCE_CONNECT_NEG_X, boxes ) == 3 && //
                    boxes[ 1 ].offset[ 0 ] == 0 && boxes[ 1 ].scale[ 0 ] == 16 );
    check_bool( "fence: all four sides give 5 boxes", fence_boxes( 0xF, boxes ) == 5 );
    bool all_bounds = true;
    for ( unsigned conn = 0; conn < 16; conn++ ) {
        const int n = fence_boxes( conn, boxes );
        all_bounds &= n == 1 + 2 * ( ( conn & ( FENCE_CONNECT_POS_X | FENCE_CONNECT_NEG_X ) ) != 0 ) + 2 * ( ( conn & ( FENCE_CONNECT_POS_Z | FENCE_CONNECT_NEG_Z ) ) != 0 );
        all_bounds &= boxes_in_bounds( boxes, n );
    }
    check_bool( "fence: every connection mask stays in bounds", all_bounds );

    // --- fence_connects_to -------------------------------------------------
    const Block *oak = block_definition_get_definition( OAK_FENCE );
    const Block *nether = block_definition_get_definition( NETHER_BRICK_FENCE );
    check_bool( "fence: oak connects to spruce fence", fence_connects_to( oak, SPRUCE_FENCE, 0, true ) );
    check_bool( "fence: nether connects to nether", fence_connects_to( nether, NETHER_BRICK_FENCE, 0, true ) );
    check_bool( "fence: oak connects to solid stone", fence_connects_to( oak, STONE, 0, true ) );
    check_bool( "fence: oak does not connect to nether", !fence_connects_to( oak, NETHER_BRICK_FENCE, 0, true ) );
    check_bool( "fence: oak does not connect to glass", !fence_connects_to( oak, GLASS, 0, true ) );
    check_bool( "fence: oak does not connect to air", !fence_connects_to( oak, AIR, 0, true ) );
    check_bool( "fence: gate rot 0 connects along x", fence_connects_to( oak, FENCE_GATE, 0, true ) );
    check_bool( "fence: gate rot 0 not along z", !fence_connects_to( oak, FENCE_GATE, 0, false ) );
    check_bool( "fence: gate rot 1 connects along z", fence_connects_to( oak, FENCE_GATE, 1, false ) );
    check_bool( "fence: gate rot 4 (open) still along x", fence_connects_to( oak, FENCE_GATE, BLOCK_ROTATE_GATE_OPEN, true ) );

    // --- fence_gate_boxes (definition space, spans x) ----------------------
    check_bool( "gate: closed gives 5 boxes", fence_gate_boxes( false, boxes ) == 5 );
    check_bool( "gate: closed posts at x 0..2 and 14..16",
                boxes[ 0 ].offset[ 0 ] == 0 && boxes[ 0 ].scale[ 0 ] == 2 && boxes[ 1 ].offset[ 0 ] == 14 && //
                    boxes[ 0 ].offset[ 1 ] == 5 && boxes[ 0 ].scale[ 1 ] == 11 && boxes[ 0 ].offset[ 2 ] == 7 && boxes[ 0 ].scale[ 2 ] == 2 );
    check_bool( "gate: closed rail x 2..14 y 6..9",
                boxes[ 2 ].offset[ 0 ] == 2 && boxes[ 2 ].scale[ 0 ] == 12 && boxes[ 2 ].offset[ 1 ] == 6 && boxes[ 2 ].scale[ 1 ] == 3 );
    check_bool( "gate: closed upright x 6..10 y 6..15",
                boxes[ 4 ].offset[ 0 ] == 6 && boxes[ 4 ].scale[ 0 ] == 4 && boxes[ 4 ].offset[ 1 ] == 6 && boxes[ 4 ].scale[ 1 ] == 9 );
    check_bool( "gate: closed in bounds", boxes_in_bounds( boxes, 5 ) );
    check_bool( "gate: open gives 8 boxes", fence_gate_boxes( true, boxes ) == 8 );
    check_bool( "gate: open leaves fold to z<7",
                boxes[ 2 ].offset[ 2 ] == 3 && boxes[ 2 ].scale[ 2 ] == 4 && //
                    boxes[ 4 ].offset[ 2 ] == 1 && boxes[ 4 ].scale[ 2 ] == 2 && boxes[ 4 ].offset[ 1 ] == 6 && boxes[ 4 ].scale[ 1 ] == 9 );
    check_bool( "gate: open in bounds", boxes_in_bounds( boxes, 8 ) );

    // --- fence_gate_toggle_rotation ----------------------------------------
    check_bool( "gate toggle: closed 0 + player 0 -> open 0", fence_gate_toggle_rotation( 0, 0 ) == 4 );
    check_bool( "gate toggle: closed 0 + player 2 -> open 2", fence_gate_toggle_rotation( 0, 2 ) == 6 );
    check_bool( "gate toggle: closed 0 + player 1 -> open 0", fence_gate_toggle_rotation( 0, 1 ) == 4 );
    check_bool( "gate toggle: open 2 -> closed 2", fence_gate_toggle_rotation( 6, 0 ) == 2 );

    // --- generated definitions ----------------------------------------------
    check_bool( "def: OAK_FENCE is_fence + post box",
                oak->is_fence && !oak->is_fence_gate &&           //
                    oak->scale.x == 4 && oak->scale.y == 16 && oak->scale.z == 4 && //
                    oak->offset.x == 6 && oak->offset.y == 0 && oak->offset.z == 6 );
    const Block *spruce_gate = block_definition_get_definition( SPRUCE_FENCE_GATE );
    check_bool( "def: SPRUCE_FENCE_GATE is_fence_gate + rotates",
                spruce_gate->is_fence_gate && !spruce_gate->is_fence && spruce_gate->rotate_on_placement && //
                    spruce_gate->scale.x == 16 && spruce_gate->scale.y == 11 && spruce_gate->scale.z == 4 && //
                    spruce_gate->offset.x == 0 && spruce_gate->offset.y == 5 && spruce_gate->offset.z == 6 );
    check_bool( "def: NETHER_BRICK_FENCE is_fence", nether->is_fence );

    // Creative listing: the oak fence/gate show up as the legacy FENCE and
    // FENCE_GATE ids (they win the dedupe now that OAK_FENCE shares the fence
    // model signature — there is no separate OAK_FENCE_GATE block).
    unsigned char in_creative[ LAST_BLOCK_ID ] = { 0 };
    for ( unsigned int i = 0; i < CREATIVE_BLOCK_COUNT; i++ ) {
        // Skip the pure items that tail the creative list.
        if ( item_is_block( CREATIVE_BLOCKS[ i ] ) ) {
            in_creative[ CREATIVE_BLOCKS[ i ] ] = 1;
        }
    }
    check_bool( "creative: FENCE (oak) listed", in_creative[ FENCE ] != 0 );
    check_bool( "creative: FENCE_GATE (oak) listed", in_creative[ FENCE_GATE ] != 0 );

    // --- block-world showcase (z<0, y=1) ------------------------------------
    check_bool( "showcase: gate inline in oak run", MapGen::block_world_id( -6, 1, -2 ) == FENCE_GATE );
    check_bool( "showcase: oak fence beside gate", MapGen::block_world_id( -7, 1, -2 ) == OAK_FENCE && MapGen::block_world_id( -5, 1, -2 ) == OAK_FENCE );
    check_bool( "showcase: cross center", MapGen::block_world_id( 0, 1, -5 ) == OAK_FENCE );
    check_bool( "showcase: cross arm -z", MapGen::block_world_id( 0, 1, -4 ) == OAK_FENCE );
    check_bool( "showcase: lone post", MapGen::block_world_id( 4, 1, -4 ) == OAK_FENCE );
    check_bool( "showcase: planks connector", MapGen::block_world_id( 7, 1, -4 ) == OAK_PLANKS );
    check_bool( "showcase: oak vs nether pair", MapGen::block_world_id( 6, 1, -8 ) == OAK_FENCE && MapGen::block_world_id( 7, 1, -8 ) == NETHER_BRICK_FENCE );
    check_bool( "showcase: nether run", MapGen::block_world_id( 10, 1, -8 ) == NETHER_BRICK_FENCE );
    check_bool( "showcase: air around layout", MapGen::block_world_id( -2, 1, -4 ) == AIR );

    // --- fence_collision_boxes ----------------------------------------------
    check_bool( "collision: no connections gives just the post",
                fence_collision_boxes( 0, boxes ) == 1 &&                                                            //
                    boxes[ 0 ].scale[ 0 ] == 4 && boxes[ 0 ].scale[ 1 ] == 24 && boxes[ 0 ].scale[ 2 ] == 4 && //
                    boxes[ 0 ].offset[ 0 ] == 6 && boxes[ 0 ].offset[ 1 ] == 0 && boxes[ 0 ].offset[ 2 ] == 6 );
    check_bool( "collision: one side gives post + 1 bar",
                fence_collision_boxes( FENCE_CONNECT_POS_X, boxes ) == 2 &&                                     //
                    boxes[ 1 ].offset[ 0 ] == 10 && boxes[ 1 ].scale[ 0 ] == 6 &&                             //
                    boxes[ 1 ].scale[ 1 ] == 24 &&                                                            //
                    boxes[ 1 ].offset[ 2 ] == 6 && boxes[ 1 ].scale[ 2 ] == 4 );
    check_bool( "collision: opposite sides merge into one bar",
                fence_collision_boxes( FENCE_CONNECT_POS_X | FENCE_CONNECT_NEG_X, boxes ) == 2 && //
                    boxes[ 1 ].offset[ 0 ] == 0 && boxes[ 1 ].scale[ 0 ] == 16 );
    check_bool( "collision: all four sides give 3 boxes", fence_collision_boxes( 0xF, boxes ) == 3 );
    // Collision boxes stay in the cell horizontally but stand 24px tall
    // (1.5 blocks) on purpose.
    auto collision_in_bounds = [ & ]( unsigned mask ) {
        const int n = fence_collision_boxes( mask, boxes );
        for ( int i = 0; i < n; i++ ) {
            if ( boxes[ i ].offset[ 0 ] < 0 || boxes[ i ].offset[ 0 ] + boxes[ i ].scale[ 0 ] > 16 ||
                 boxes[ i ].offset[ 2 ] < 0 || boxes[ i ].offset[ 2 ] + boxes[ i ].scale[ 2 ] > 16 ||
                 boxes[ i ].offset[ 1 ] != 0 || boxes[ i ].scale[ 1 ] != 24 ) {
                return false;
            }
        }
        return true;
    };
    check_bool( "collision: every box within cell xz, 1.5 tall", collision_in_bounds( 0xF ) && collision_in_bounds( 0 ) && collision_in_bounds( FENCE_CONNECT_POS_X | FENCE_CONNECT_POS_Z ) );

    // World-level: two adjacent fences wall off the gap between their posts.
    {
        RepGameState &gs2 = *new RepGameState( );
        gs2.tick_number = 0;
        gs2.camera.pos = glm::dvec3( 0, 0, 0 );
        test_setup_world( gs2.world );
        World &world = gs2.world;
        const BlockState stone = { STONE, 0, 0, STONE, 0 };
        const BlockState fence = { OAK_FENCE, 0, 0, OAK_FENCE, 0 };
        for ( int x = 19; x <= 22; x++ ) {
            for ( int z = 19; z <= 22; z++ ) {
                world.set_loaded_block( glm::ivec3( x, 10, z ), stone );
            }
        }
        const double eye_y = 11.0 + PLAYER_EYE_HEIGHT;
        world.set_loaded_block( glm::ivec3( 20, 11, 20 ), fence );
        world.set_loaded_block( glm::ivec3( 21, 11, 20 ), fence );
        check_bool( "collision: gap between connected posts blocked",
                    Collision::collides_at( world, glm::dvec3( 21.0, eye_y, 20.5 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
        world.set_loaded_block( glm::ivec3( 21, 11, 20 ), BLOCK_STATE_AIR );
        check_bool( "collision: lone post leaves the gap open",
                    !Collision::collides_at( world, glm::dvec3( 21.0, eye_y, 20.5 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
        world.set_loaded_block( glm::ivec3( 21, 11, 20 ), fence );
        glm::dvec3 pos( 21.0, eye_y, 19.5 );
        int standing = 0;
        for ( int i = 0; i < 30; i++ ) {
            glm::dvec3 vec( 0.0, -0.05, 0.05 );
            Collision::check_move( world, vec, pos, &standing, PLAYER_HEIGHT, PLAYER_EYE_HEIGHT, false );
            pos += vec;
        }
        check_bool( "collision: walking through the gap gets stopped", pos.z < 20.2 );

        // 1.5-block height: feet 1.2 above the fence cell overlap the
        // (extended) box even though that box's top sits in the next cell
        // down — exercising the extra scan layer below the AABB.
        check_bool( "collision: feet at fence+1.2 over the post collide",
                    Collision::collides_at( world, glm::dvec3( 20.5, 11.0 + 1.2 + PLAYER_EYE_HEIGHT, 20.5 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
        check_bool( "collision: feet above fence+1.5 don't collide",
                    !Collision::collides_at( world, glm::dvec3( 20.5, 11.0 + 1.5 + 0.01 + PLAYER_EYE_HEIGHT, 20.5 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );

        // Jump-over: standing next to the row, jumping while pushing across
        // the fence line never reaches the far side (jump apex ~1.22 < 1.5).
        pos = glm::dvec3( 21.0, eye_y, 19.5 );
        double vy = JUMP_STRENGTH;
        for ( int i = 0; i < 80; i++ ) {
            glm::dvec3 vec( 0.0, vy, 0.05 );
            Collision::check_move( world, vec, pos, &standing, PLAYER_HEIGHT, PLAYER_EYE_HEIGHT, false );
            pos += vec;
            vy = pos.y > 11.0 + PLAYER_EYE_HEIGHT ? vy - GRAVITY_STRENGTH : vy;
        }
        check_bool( "collision: jumping over the fence row stays on near side", pos.z < 20.5 );

        // Closed gate collides at fence height; open gate doesn't at all.
        world.set_loaded_block( glm::ivec3( 20, 11, 24 ), { FENCE_GATE, 0, 0, FENCE_GATE, 0 } );
        check_bool( "collision: closed gate blocks at fence+1.2",
                    Collision::collides_at( world, glm::dvec3( 20.5, 11.0 + 1.2 + PLAYER_EYE_HEIGHT, 24.5 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
        check_bool( "collision: closed gate blocks the bottom gap",
                    Collision::collides_at( world, glm::dvec3( 20.5, eye_y, 24.5 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
        world.set_loaded_block( glm::ivec3( 20, 11, 24 ), { FENCE_GATE, BLOCK_ROTATE_GATE_OPEN, 0, FENCE_GATE, 0 } );
        check_bool( "collision: open gate lets the player through",
                    !Collision::collides_at( world, glm::dvec3( 20.5, eye_y, 24.5 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
        world.set_loaded_block( glm::ivec3( 20, 11, 24 ), BLOCK_STATE_AIR );

        // A player dropped onto a fence stands on top of it at fence+1.5.
        pos = glm::dvec3( 20.5, 11.0 + 2.2 + PLAYER_EYE_HEIGHT, 20.5 );
        standing = 0;
        for ( int i = 0; i < 40 && standing == 0; i++ ) {
            glm::dvec3 vec( 0.0, -0.05, 0.0 );
            Collision::check_move( world, vec, pos, &standing, PLAYER_HEIGHT, PLAYER_EYE_HEIGHT, false );
            pos += vec;
        }
        check_bool( "collision: dropped player lands on the fence", standing == 1 );
        const double feet = pos.y - PLAYER_EYE_HEIGHT;
        pr_test( "  %-55s value=%-9.4f expected=%-9.4f  %s", "collision: standing on fence top at +1.5", feet, 12.5, fabs( feet - 12.5 ) < 0.05 ? "PASS" : "FAIL" );
        if ( fabs( feet - 12.5 ) >= 0.05 ) {
            failures++;
        }
    }
    return failures;
}

// Block-world debug layout (REPGAME_BLOCK_WORLD path — block_world_id itself
// is env-independent): sandstone floor, grid order = CREATIVE_BLOCKS then
// remaining defined ids ascending, one air cell between neighbours.
int test_block_world( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };

    check_bool( "bw: floor is sandstone at y=0", MapGen::block_world_id( 0, 0, 0 ) == SANDSTONE );
    check_bool( "bw: floor is sandstone far away", MapGen::block_world_id( -500, 0, 700 ) == SANDSTONE );
    check_bool( "bw: air above floor", MapGen::block_world_id( 2, 2, 2 ) == AIR );
    check_bool( "bw: sandstone below floor", MapGen::block_world_id( 2, -1, 2 ) == SANDSTONE );
    check_bool( "bw: grid slot 0 is CREATIVE_BLOCKS[0]", MapGen::block_world_id( 2, 1, 2 ) == CREATIVE_BLOCKS[ 0 ] );
    check_bool( "bw: grid slot 1 is CREATIVE_BLOCKS[1]", MapGen::block_world_id( 4, 1, 2 ) == CREATIVE_BLOCKS[ 1 ] );
    check_bool( "bw: slot 32 wraps to next row", MapGen::block_world_id( 2, 1, 4 ) == CREATIVE_BLOCKS[ 32 ] );
    check_bool( "bw: air gap between slots", MapGen::block_world_id( 3, 1, 2 ) == AIR );
    check_bool( "bw: air gap between rows", MapGen::block_world_id( 2, 1, 3 ) == AIR );
    check_bool( "bw: air before grid start", MapGen::block_world_id( 1, 1, 2 ) == AIR );
    check_bool( "bw: air past row width", MapGen::block_world_id( 2 + 64, 1, 2 ) == AIR );

    // Every defined non-AIR id must appear on the grid exactly once.
    unsigned char expected[ LAST_BLOCK_ID ] = { 0 };
    unsigned char seen[ LAST_BLOCK_ID ] = { 0 };
    int expected_count = 0;
    for ( unsigned int i = 0; i < CREATIVE_BLOCK_COUNT; i++ ) {
        // The creative list ends with pure items, which the block world skips.
        if ( item_is_block( CREATIVE_BLOCKS[ i ] ) ) {
            expected[ CREATIVE_BLOCKS[ i ] ] = 1;
        }
    }
    for ( unsigned int id = 1; id < (unsigned int)LAST_BLOCK_ID; id++ ) {
        if ( !expected[ id ] && block_id_defined( id ) ) {
            expected[ id ] = 1;
        }
    }
    expected[ AIR ] = 0;
    for ( unsigned int id = 0; id < (unsigned int)LAST_BLOCK_ID; id++ ) {
        expected_count += expected[ id ];
    }
    int seen_count = 0, dupes = 0, strays = 0;
    for ( int k = 0; k < expected_count + 64; k++ ) {
        const BlockID id = MapGen::block_world_id( 2 + 2 * ( k % 32 ), 1, 2 + 2 * ( k / 32 ) );
        if ( id == AIR ) {
            continue;
        }
        if ( !expected[ id ] ) {
            strays++;
        } else if ( seen[ id ] ) {
            dupes++;
        } else {
            seen[ id ] = 1;
            seen_count++;
        }
    }
    check_bool( "bw: every defined non-AIR id placed once", seen_count == expected_count && dupes == 0 && strays == 0 );
    return failures;
}

// Terrain generator invariants, checked through the public single-cell API
// (MapGen::gen_block_id is the same code the CUDA/HIP kernels share via
// map_gen_fields.hpp). Seed is fixed so every check is deterministic.
int test_mapgen( ) {
    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };

    const float max_h = MapGen::maxTerrainHeight( );
    int lava = 0, ores = 0, overhangs = 0, above_max = 0, bedrock_gaps = 0;
    int grass = 0, sand_like = 0, snow_like = 0;
    for ( int x = -256; x < 256; x += 2 ) {
        for ( int z = -256; z < 256; z += 2 ) {
            const float h = MapGen::calculateTerrainHeight( x, z );
            int top_solid = -10000;
            for ( int y = -110; y <= (int)max_h; y++ ) {
                const BlockID b = MapGen::gen_block_id( x, y, z );
                if ( y <= BEDROCK_LEVEL - 4 && b != BEDROCK ) {
                    bedrock_gaps++;
                }
                if ( b == LAVA ) {
                    lava++;
                }
                if ( b == COAL_ORE || b == IRON_ORE || b == GOLD_ORE ) {
                    ores++;
                }
                if ( b != AIR && b != WATER ) {
                    if ( (float)y > max_h ) {
                        above_max++;
                    }
                    top_solid = y;
                }
            }
            if ( top_solid > (int)ceilf( h ) + 2 &&
                 MapGen::gen_block_id( x, (int)ceilf( h ) + 1, z ) == AIR &&
                 MapGen::gen_block_id( x, (int)ceilf( h ) + 2, z ) == AIR ) {
                overhangs++;
            }
            if ( top_solid > -10000 ) {
                const BlockID surf = MapGen::gen_block_id( x, top_solid, z );
                if ( surf == GRASS || surf == PODZOL || surf == MYCELIUM ) grass++;
                if ( surf == SAND || surf == RED_SAND || surf == GRAVEL ) sand_like++;
                if ( surf == SNOW || surf == SNOWY_GRASS ) snow_like++;
            }
        }
    }
    // Mountains are sparse, so the snowline needs a wider (cheap) survey: the
    // heightmap alone finds tall columns, then a short y-scan finds the top
    // block. No per-y scan over the whole area. The same pass verifies
    // overhangs: they intentionally only form in rough/mountain terrain now
    // (flat land stays walkably smooth), so mountain columns are where they
    // must exist.
    for ( int x = -2048; x < 2048; x += 8 ) {
        for ( int z = -2048; z < 2048; z += 8 ) {
            const float h = MapGen::calculateTerrainHeight( x, z );
            if ( h <= MOUNTAIN_ROCK_LINE ) {
                continue;
            }
            int top_solid = -10000;
            for ( int y = (int)h + (int)OVERHANG_MAX_RISE; y >= (int)h - 5; y-- ) {
                const BlockID b = MapGen::gen_block_id( x, y, z );
                if ( b != AIR && b != WATER ) {
                    top_solid = y;
                    if ( b == SNOW || b == SNOWY_GRASS ) {
                        snow_like++;
                    }
                    break;
                }
            }
            if ( top_solid > (int)ceilf( h ) + 2 &&
                 MapGen::gen_block_id( x, (int)ceilf( h ) + 1, z ) == AIR &&
                 MapGen::gen_block_id( x, (int)ceilf( h ) + 2, z ) == AIR ) {
                overhangs++;
            }
        }
    }

    check_bool( "nothing solid above maxTerrainHeight", above_max == 0 );
    check_bool( "bedrock floor has no gaps", bedrock_gaps == 0 );
#if REPGAME_MAP_GEN_LEGACY
    // The legacy generator has no lava layer or overhangs, and snow only
    // appears on rare tall mountain caps — none are guaranteed in a survey.
    (void)lava;
    (void)overhangs;
    (void)snow_like;
#else
    check_bool( "lava layer exists below LAVA_LEVEL", lava > 0 );
    check_bool( "terrain overhangs exist", overhangs > 0 );
    check_bool( "snowy surfaces exist", snow_like > 0 );
#endif
    check_bool( "ores generate", ores > 0 );
    check_bool( "grassy surfaces exist", grass > 0 );
    check_bool( "sandy surfaces exist", sand_like > 0 );
    return failures;
}

// Waterlogging: the per-cell `waterlogged` flag vs the per-type
// `waterloggable` property, and the water-expansion loop regression — an
// expansion into an already-waterlogged cell used to re-queue forever,
// spawning debris each cycle until the event queue overflowed.
int test_waterlog( ) {
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
    world.set_loaded_block( glm::ivec3( 10, 9, 10 ), stone );  // floor under coral
    world.set_loaded_block( glm::ivec3( 20, 9, 20 ), stone );  // floor under dry coral
    world.set_loaded_block( glm::ivec3( 11, 10, 10 ), { WATER, 0, 0, WATER, 0 } );
    world.set_loaded_block( glm::ivec3( 10, 10, 10 ), { WATER, 0, 0, WATER, 0 } );

    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };

    // Coral written into a water cell becomes waterlogged; on dry land it
    // does not (the original "always waterlogged" bug).
    world.set_loaded_block( glm::ivec3( 10, 10, 10 ), { KELP, 0, 0, KELP, 0 } );
    const BlockState wet = world.get_loaded_block( glm::ivec3( 10, 10, 10 ) );
    check_bool( "plant placed in water keeps id", wet.id == KELP );
    check_bool( "plant placed in water is waterlogged", wet.waterlogged == 1 );
    world.set_loaded_block( glm::ivec3( 20, 10, 20 ), { KELP, 0, 0, KELP, 0 } );
    const BlockState dry = world.get_loaded_block( glm::ivec3( 20, 10, 20 ) );
    check_bool( "plant placed on land is not waterlogged", dry.id == KELP && dry.waterlogged == 0 );

    // Water expanding onto a waterloggable block waterlogs it instead of
    // replacing it; into an already-waterlogged cell it must not fire at
    // all — that was the infinite re-queue/debris loop.
    auto place = [ & ]( const glm::ivec3 &pos, const BlockState &block_state ) {
        gs.blockUpdateQueue.addBlockUpdate( std::make_shared<PlayerBlockPlacedEvent>( gs.tick_number, pos, block_state, false ) );
    };
    place( glm::ivec3( 10, 10, 10 ), { WATER, 0, 0, WATER, 0 } ); // water "onto" the wet coral
    place( glm::ivec3( 12, 10, 10 ), { STONE, 0, 0, STONE, 0 } ); // a change beside the water
    for ( int i = 0; i < 60; i++ ) {
        gs.tick_number++;
        gs.blockUpdateQueue.processAllBlockUpdates( gs, gs.tick_number );
    }
    // With the loop, ~14 neighbor updates per expansion re-arm every 20
    // ticks and the queue explodes past the 100k-event guard (exit). The
    // state must also hold: coral stays waterlogged, neighbor stays water.
    const BlockState after = world.get_loaded_block( glm::ivec3( 10, 10, 10 ) );
    check_bool( "waterlogged coral survives water expansion", after.id == KELP && after.waterlogged == 1 );
    failures += check_id( world, "water cell beside coral stays water", glm::ivec3( 11, 10, 10 ), WATER );

    // Breaking a waterlogged block leaves its water behind.
    world.set_loaded_block( glm::ivec3( 10, 10, 10 ), BLOCK_STATE_AIR );
    failures += check_id( world, "breaking waterlogged plant leaves water", glm::ivec3( 10, 10, 10 ), WATER );
    return failures;
}

// Flood-fill lighting (src/common/light.cpp): torch emission + decay/removal
// on the block channel, and the column cascade + occlusion on the sky
// channel. Runs without GL — propagation works purely on Chunk::light[].
int test_lighting( ) {
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
    ChunkLoader &cl = world.chunkLoader;
    cl.chunk_center = glm::ivec3( 0, 0, 0 );
    Chunk &chunk = cl.chunkArray[ chunk_slot_from_pos( glm::ivec3( 0, 0, 0 ) ) ];
    chunk.light = static_cast<unsigned char *>( calloc( CHUNK_BLOCK_SIZE, 1 ) );

    int failures = 0;
    auto check_light = [ & ]( const char *name, const glm::ivec3 &pos, int channel, int expected ) {
        const int v = cl.light_get( pos, channel );
        const bool ok = v == expected;
        pr_test( "  %-55s light=%-2d expected=%-2d  %s", name, v, expected, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };

    // --- Block channel: torch emits 14, decays 1 per BFS step.
    world.set_loaded_block( glm::ivec3( 10, 10, 10 ), { TORCH, 0, 0, TORCH, 0 } );
    cl.light_process_queue( 1000000 );
    check_light( "torch cell", glm::ivec3( 10, 10, 10 ), LIGHT_CHANNEL_BLOCK, 14 );
    check_light( "adjacent to torch", glm::ivec3( 11, 10, 10 ), LIGHT_CHANNEL_BLOCK, 13 );
    check_light( "above torch", glm::ivec3( 10, 11, 10 ), LIGHT_CHANNEL_BLOCK, 13 );
    check_light( "diagonal two steps", glm::ivec3( 11, 11, 10 ), LIGHT_CHANNEL_BLOCK, 12 );
    check_light( "past light range", glm::ivec3( 25, 10, 10 ), LIGHT_CHANNEL_BLOCK, 0 );

    // An opaque wall blocks the direct path; light detours over it.
    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    world.set_loaded_block( glm::ivec3( 12, 10, 10 ), stone );
    cl.light_process_queue( 1000000 );
    check_light( "behind wall detours (5 steps)", glm::ivec3( 13, 10, 10 ), LIGHT_CHANNEL_BLOCK, 9 );

    // Breaking the torch removes its light entirely.
    world.set_loaded_block( glm::ivec3( 10, 10, 10 ), BLOCK_STATE_AIR );
    cl.light_process_queue( 1000000 );
    check_light( "torch cell after break", glm::ivec3( 10, 10, 10 ), LIGHT_CHANNEL_BLOCK, 0 );
    check_light( "adjacent after break", glm::ivec3( 11, 10, 10 ), LIGHT_CHANNEL_BLOCK, 0 );
    check_light( "detoured cell after break", glm::ivec3( 13, 10, 10 ), LIGHT_CHANNEL_BLOCK, 0 );

    // Block stacked directly on a torch: the air cells beside the block
    // (what its +/-z faces sample) must stay lit around the obstruction —
    // two steps from the torch via the cells beside it.
    world.set_loaded_block( glm::ivec3( 10, 10, 10 ), { TORCH, 0, 0, TORCH, 0 } );
    world.set_loaded_block( glm::ivec3( 10, 11, 10 ), stone );
    cl.light_process_queue( 1000000 );
    check_light( "beside block +z over torch", glm::ivec3( 10, 11, 11 ), LIGHT_CHANNEL_BLOCK, 12 );
    check_light( "beside block -z over torch", glm::ivec3( 10, 11, 9 ), LIGHT_CHANNEL_BLOCK, 12 );
    check_light( "beside block +x over torch", glm::ivec3( 11, 11, 10 ), LIGHT_CHANNEL_BLOCK, 12 );
    check_light( "above block over torch", glm::ivec3( 10, 12, 10 ), LIGHT_CHANNEL_BLOCK, 10 );
    world.set_loaded_block( glm::ivec3( 10, 11, 10 ), BLOCK_STATE_AIR );
    world.set_loaded_block( glm::ivec3( 10, 10, 10 ), BLOCK_STATE_AIR );
    cl.light_process_queue( 1000000 );

    // Leaves are light-permeable but absorbing: entering one costs 1 +
    // light_absorb (7 at absorb 6), so torchlight inside a leaf dims hard,
    // and the far side settles at the best wraparound path (glass costs -1).
    world.set_loaded_block( glm::ivec3( 10, 20, 10 ), { TORCH, 0, 0, TORCH, 0 } );
    world.set_loaded_block( glm::ivec3( 12, 20, 10 ), { OAK_LEAVES, 0, 0, OAK_LEAVES, 0 } );
    cl.light_process_queue( 1000000 );
    check_light( "air cell beside torch", glm::ivec3( 11, 20, 10 ), LIGHT_CHANNEL_BLOCK, 13 );
    check_light( "leaf cell holds light", glm::ivec3( 12, 20, 10 ), LIGHT_CHANNEL_BLOCK, 6 );
    check_light( "past leaf is dimmer", glm::ivec3( 13, 20, 10 ), LIGHT_CHANNEL_BLOCK, 9 );
    world.set_loaded_block( glm::ivec3( 12, 20, 10 ), BLOCK_STATE_AIR );
    cl.light_process_queue( 1000000 );
    check_light( "past cleared leaf recovers", glm::ivec3( 13, 20, 10 ), LIGHT_CHANNEL_BLOCK, 11 );
    world.set_loaded_block( glm::ivec3( 10, 20, 10 ), BLOCK_STATE_AIR );
    cl.light_process_queue( 1000000 );

    // Regression: an overhang present BEFORE finalize must still get lateral
    // skylight — the interior band scan seeds lit cells facing deeper-dark
    // neighbor columns. Writing the block directly (not set_loaded_block)
    // keeps it out of the recheck path, which would seed around it and mask
    // the scan; light_finalize_chunk is the path terrain features take.
    chunk.store_state( chunk.get_index_from_coords( 5, 30, 5 ), stone );
    cl.light_ensure_columns( chunk );
    cl.light_compute_one_column( chunk, 5, 5 );

    // --- Sky channel: finalize runs the per-column cascade. All-air chunk
    // opens every column, so skylight reaches the bottom at full strength.
    cl.light_finalize_chunk( chunk );
    cl.light_process_queue( 1000000 );
    check_light( "open column top", glm::ivec3( 20, 31, 20 ), LIGHT_CHANNEL_SKY, 15 );
    check_light( "open column bottom", glm::ivec3( 20, 0, 20 ), LIGHT_CHANNEL_SKY, 15 );

    // A blocker near the top of a column closes it: the column loses its
    // vertical sky feed and re-settles at side-lit 14, while the neighbor
    // column keeps its full 15. (y=30, not 31: the top interior cell needs
    // the chunk above for halo fixup, which this test world lacks.)
    world.set_loaded_block( glm::ivec3( 20, 30, 20 ), stone );
    cl.light_process_queue( 1000000 );
    check_light( "under sky blocker", glm::ivec3( 20, 29, 20 ), LIGHT_CHANNEL_SKY, 14 );
    check_light( "deep under sky blocker", glm::ivec3( 20, 0, 20 ), LIGHT_CHANNEL_SKY, 14 );
    check_light( "open neighbor column", glm::ivec3( 21, 30, 20 ), LIGHT_CHANNEL_SKY, 15 );
    check_light( "under finalize-time canopy", glm::ivec3( 5, 29, 5 ), LIGHT_CHANNEL_SKY, 14 );
    check_light( "deep under finalize-time canopy", glm::ivec3( 5, 0, 5 ), LIGHT_CHANNEL_SKY, 14 );

    // Finalize-path light floor: a 9x9 slab floor written via store_state
    // (terrain-style, no recheck) caps its columns — the interior boundary
    // scan must re-seed above each cap or the slab cells stay dim.
    const BlockState slab = { BRICK_SLAB, 0, 0, BRICK_SLAB, 0 };
    cl.light_ensure_columns( chunk );
    for ( int fx = 22; fx <= 30; fx++ ) {
        for ( int fz = 22; fz <= 30; fz++ ) {
            chunk.store_state( chunk.get_index_from_coords( fx, 25, fz ), slab );
            cl.light_compute_one_column( chunk, fx, fz );
        }
    }

    // Regression: the worker's optimistic prefill sets sky_open_above=1
    // before finalize, so the cascade fills nothing (cells_filled==0) — the
    // interior boundary scan must still seed canopy-shaded cells or they
    // stay dark until an unrelated edit. light_fill_chunk is the path the
    // terrain worker takes (the canopy stone is still in cells[]).
    light_fill_chunk( chunk );
    cl.light_finalize_chunk( chunk );
    cl.light_process_queue( 1000000 );
    check_light( "under canopy after optimistic refill", glm::ivec3( 5, 29, 5 ), LIGHT_CHANNEL_SKY, 14 );
    check_light( "deep under canopy after optimistic refill", glm::ivec3( 5, 0, 5 ), LIGHT_CHANNEL_SKY, 14 );

    // --- Light floors (blocks_light_down): bottom-aligned partial blocks
    // hold light but never pass it downward and cap the sky column, so a
    // slab roof shades while its sides and top stay lit.

    // Finalize-path floor (the 9x9 above): the interior slab cell reaches
    // full sky via the cap reseed; the cell under the floor center is
    // shaded to 15 - (distance to the nearest open column) = 15 - 5 = 10.
    check_light( "finalize slab floor interior sky", glm::ivec3( 26, 25, 26 ), LIGHT_CHANNEL_SKY, 15 );
    check_light( "under finalize slab floor", glm::ivec3( 26, 24, 26 ), LIGHT_CHANNEL_SKY, 10 );

    // Shade mask (contact shadows): light_fill_chunk stamps bit=1 only for
    // blocks_light_down cells — set on the slab floor, clear on opaque
    // stone and air. (Mask exists only under light_mipmaps_enabled; tests
    // run with it on.)
    auto shade_bit = [ & ]( const glm::ivec3 &p ) -> int {
        if ( !chunk.light_shade_mask ) {
            return -1;
        }
        const int i = chunk.get_index_from_coords( p );
        return ( chunk.light_shade_mask[ i >> 3 ] >> ( i & 7 ) ) & 1;
    };
    check_bool( "shade mask built by fill", chunk.light_shade_mask != nullptr );
    check_bool( "shade bit set on slab floor cell", shade_bit( glm::ivec3( 26, 25, 26 ) ) == 1 );
    check_bool( "shade bit clear on stone", shade_bit( glm::ivec3( 5, 30, 5 ) ) == 0 );
    check_bool( "shade bit clear on air", shade_bit( glm::ivec3( 28, 28, 28 ) ) == 0 );
    for ( int fx = 22; fx <= 30; fx++ ) {
        for ( int fz = 22; fz <= 30; fz++ ) {
            chunk.store_state( chunk.get_index_from_coords( fx, 25, fz ), BLOCK_STATE_AIR );
            cl.light_compute_one_column( chunk, fx, fz );
        }
    }
    cl.light_process_queue( 1000000 );

    // Edit path: a slab placed into an open-sky column caps the column fill
    // but still holds full sky itself; the cell below is lit laterally.
    // Placing into the lit column also proves the remove pass clears the 15
    // that used to live under it.
    world.set_loaded_block( glm::ivec3( 16, 25, 16 ), slab );
    cl.light_process_queue( 1000000 );
    check_light( "placed slab cell holds sky", glm::ivec3( 16, 25, 16 ), LIGHT_CHANNEL_SKY, 15 );
    check_light( "above placed slab stays sky", glm::ivec3( 16, 26, 16 ), LIGHT_CHANNEL_SKY, 15 );
    check_light( "under placed slab is lateral-lit", glm::ivec3( 16, 24, 16 ), LIGHT_CHANNEL_SKY, 14 );
    check_bool( "shade bit set on placed slab", shade_bit( glm::ivec3( 16, 25, 16 ) ) == 1 );
    world.set_loaded_block( glm::ivec3( 16, 25, 16 ), BLOCK_STATE_AIR );
    cl.light_process_queue( 1000000 );
    check_light( "under cleared slab is sky again", glm::ivec3( 16, 24, 16 ), LIGHT_CHANNEL_SKY, 15 );
    check_bool( "shade bit cleared on slab break", shade_bit( glm::ivec3( 16, 25, 16 ) ) == 0 );

    // Stale-alpha guard: an edit that flips only the shade bit — slab onto
    // open-sky stone, every light value stays the same — must still mark
    // its texel in the dirty box or the GPU keeps a stale A: no contact
    // shadow appears on place, and a ghost one stays after a break.
    world.set_loaded_block( glm::ivec3( 8, 24, 28 ), stone );
    cl.light_process_queue( 1000000 );
    chunk.light_dirty_box.store( LIGHT_BOX_EMPTY, std::memory_order_relaxed );
    world.set_loaded_block( glm::ivec3( 8, 25, 28 ), slab );
    cl.light_process_queue( 1000000 );
    {
        const uint64_t box = chunk.light_dirty_box.load( std::memory_order_relaxed );
        const glm::ivec3 texel( 8 + 1, 25 + 1, 28 + 1 ); // block local + halo
        check_bool( "slab-on-stone edit marks its texel",
                    box != LIGHT_BOX_EMPTY && static_cast<int>( box & 0xFF ) <= texel.x &&
                        texel.x <= static_cast<int>( ( box >> 24 ) & 0xFF ) &&
                        static_cast<int>( ( box >> 8 ) & 0xFF ) <= texel.y &&
                        texel.y <= static_cast<int>( ( box >> 32 ) & 0xFF ) &&
                        static_cast<int>( ( box >> 16 ) & 0xFF ) <= texel.z &&
                        texel.z <= static_cast<int>( ( box >> 40 ) & 0xFF ) );
    }
    chunk.light_dirty_box.store( LIGHT_BOX_EMPTY, std::memory_order_relaxed );
    world.set_loaded_block( glm::ivec3( 8, 25, 28 ), BLOCK_STATE_AIR );
    cl.light_process_queue( 1000000 );
    check_bool( "slab break also marks its texel", chunk.light_dirty_box.load( std::memory_order_relaxed ) != LIGHT_BOX_EMPTY );
    world.set_loaded_block( glm::ivec3( 8, 24, 28 ), BLOCK_STATE_AIR );
    cl.light_process_queue( 1000000 );

    // Wide 7x7 roof: the center has no lateral path shorter than 4 steps
    // to an open column, so the cell below it settles at 15 - 4 = 11.
    for ( int rx = 24; rx <= 30; rx++ ) {
        for ( int rz = 10; rz <= 16; rz++ ) {
            world.set_loaded_block( glm::ivec3( rx, 25, rz ), slab );
        }
    }
    cl.light_process_queue( 1000000 );
    check_light( "roof center slab cell sky", glm::ivec3( 27, 25, 13 ), LIGHT_CHANNEL_SKY, 15 );
    check_light( "under roof center", glm::ivec3( 27, 24, 13 ), LIGHT_CHANNEL_SKY, 11 );
    for ( int rx = 24; rx <= 30; rx++ ) {
        for ( int rz = 10; rz <= 16; rz++ ) {
            world.set_loaded_block( glm::ivec3( rx, 25, rz ), BLOCK_STATE_AIR );
        }
    }
    cl.light_process_queue( 1000000 );

    // Block channel: a torch directly above a slab lights the slab cell to
    // 13, but the cell under the slab can only be reached around the side —
    // torch 14 -> side 13 -> down 12 -> under-side 11 -> under-slab 10.
    world.set_loaded_block( glm::ivec3( 16, 22, 16 ), { TORCH, 0, 0, TORCH, 0 } );
    world.set_loaded_block( glm::ivec3( 16, 21, 16 ), slab );
    cl.light_process_queue( 1000000 );
    check_light( "slab cell over-detoured torch", glm::ivec3( 16, 21, 16 ), LIGHT_CHANNEL_BLOCK, 13 );
    check_light( "under slab only gets side path", glm::ivec3( 16, 20, 16 ), LIGHT_CHANNEL_BLOCK, 10 );
    world.set_loaded_block( glm::ivec3( 16, 21, 16 ), BLOCK_STATE_AIR );
    world.set_loaded_block( glm::ivec3( 16, 22, 16 ), BLOCK_STATE_AIR );
    cl.light_process_queue( 1000000 );

    // Derived flag sanity: bottom-aligned partials are light floors; full
    // blocks, absorbing leaves, and plates are not.
    check_bool( "BRICK_SLAB is a light floor", block_definition_get_definition( BRICK_SLAB )->blocks_light_down && !block_definition_get_definition( BRICK_SLAB )->casts_shadow );
    check_bool( "GRASS_PATH is a light floor", block_definition_get_definition( GRASS_PATH )->blocks_light_down && !block_definition_get_definition( GRASS_PATH )->casts_shadow );
    check_bool( "STONE is not a light floor", !block_definition_get_definition( STONE )->blocks_light_down );
    check_bool( "OAK_LEAVES is not a light floor", !block_definition_get_definition( OAK_LEAVES )->blocks_light_down );
    check_bool( "pressure plate is not a light floor", !block_definition_get_definition( STONE_PRESSURE_PLATE )->blocks_light_down );
    return failures;
}

// Doors: the rotation encoding, generated definitions, pair
// placement/breaking, the swinging slab hitbox, right-click toggle, the
// hinge rule for double doors, and MC redstone edge semantics.
int test_doors( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };

    // --- door_rotations / door_facing round-trip ---------------------------
    bool round_trip = true;
    for ( int f = 0; f < 4; f++ ) {
        for ( int open = 0; open < 2; open++ ) {
            for ( int hinge = 0; hinge < 2; hinge++ ) {
                unsigned char lower_rot, upper_rot;
                door_rotations( (unsigned char)f, open != 0, hinge != 0, lower_rot, upper_rot );
                round_trip &= door_facing( lower_rot, upper_rot ) == f;
                round_trip &= ( ( lower_rot & BLOCK_ROTATE_DOOR_FLAG ) != 0 ) == ( open != 0 );
                round_trip &= ( ( upper_rot & BLOCK_ROTATE_DOOR_FLAG ) != 0 ) == ( hinge != 0 );
                round_trip &= ( lower_rot & 3 ) == door_yaw( (unsigned char)f, open != 0, hinge != 0 );
                round_trip &= ( upper_rot & 3 ) == ( lower_rot & 3 );
            }
        }
    }
    check_bool( "door: rotations round-trip all 16 states", round_trip );
    check_bool( "door: F=0 open hinge-left yaws +1", door_yaw( 0, true, false ) == 1 );
    check_bool( "door: F=0 open hinge-right yaws -1", door_yaw( 0, true, true ) == 3 );
    check_bool( "door: block_yaw strips the flag bit", block_yaw( block_definition_get_definition( WOODEN_DOOR_LOWER ), 5 ) == 1 );

    // --- generated definitions ---------------------------------------------
    const BlockID door_lowers[] = {
        WOODEN_DOOR_LOWER, IRON_DOOR_LOWER, SPRUCE_DOOR_LOWER, BIRCH_DOOR_LOWER, JUNGLE_DOOR_LOWER, ACACIA_DOOR_LOWER, DARK_OAK_DOOR_LOWER,
        BAMBOO_DOOR, CHERRY_DOOR, COPPER_DOOR, CRIMSON_DOOR, EXPOSED_COPPER_DOOR, MANGROVE_DOOR, OXIDIZED_COPPER_DOOR, PALE_OAK_DOOR,
        POPLAR_DOOR, WARPED_DOOR, WAXED_COPPER_DOOR, WAXED_EXPOSED_COPPER_DOOR, WAXED_OXIDIZED_COPPER_DOOR, WAXED_WEATHERED_COPPER_DOOR,
        WEATHERED_COPPER_DOOR,
    };
    bool defs_ok = true;
    for ( BlockID id : door_lowers ) {
        const Block *b = block_definition_get_definition( id );
        if ( !b->is_door || b->is_door_upper || b->door_partner == LAST_BLOCK_ID ) {
            defs_ok = false;
            continue;
        }
        const Block *partner = block_definition_get_definition( b->door_partner );
        defs_ok &= partner->is_door && partner->is_door_upper && partner->door_partner == id && !partner->is_pickable;
        defs_ok &= b->collides_with_player && b->is_redstone_mechanism && b->scale.z == 3 && b->offset.z == 13;
    }
    check_bool( "def: all 22 lowers pair with a non-pickable upper", defs_ok );
    check_bool( "def: iron door is redstone-only",
                block_definition_get_definition( IRON_DOOR_LOWER )->door_redstone_only &&
                    block_definition_get_definition( IRON_DOOR_UPPER )->door_redstone_only &&
                    !block_definition_get_definition( WOODEN_DOOR_LOWER )->door_redstone_only );
    check_bool( "def: door_item_id maps upper to lower", door_item_id( WOODEN_DOOR_UPPER ) == WOODEN_DOOR_LOWER );

    // --- world ---------------------------------------------------------------
    RepGameState &gs = *new RepGameState( ); // leaked on purpose — see test_tnt
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 0, 0, 0 );
    test_setup_world( gs.world );
    World &world = gs.world;
    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    const BlockState oak_door = { WOODEN_DOOR_LOWER, 0, 0, WOODEN_DOOR_LOWER, 0 };
    const BlockState redstone_block = { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 };
    // Floor at y=10 with a hole at (15,10,15) and a blocked upper cell at
    // (18,12,15).
    for ( int x = 4; x <= 24; x++ ) {
        for ( int z = 4; z <= 24; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 10, z ), stone );
        }
    }
    world.set_loaded_block( glm::ivec3( 15, 10, 15 ), BLOCK_STATE_AIR );
    world.set_loaded_block( glm::ivec3( 18, 12, 15 ), stone );

    auto place = [ & ]( const glm::ivec3 &pos, const BlockState &block_state ) {
        gs.blockUpdateQueue.addBlockUpdate( std::make_shared<PlayerBlockPlacedEvent>( gs.tick_number, pos, block_state, false ) );
    };
    auto run = [ & ]( const int ticks ) {
        for ( int i = 0; i < ticks; i++ ) {
            gs.tick_number++;
            gs.blockUpdateQueue.processAllBlockUpdates( gs, gs.tick_number );
        }
    };
    auto toggle = [ & ]( const glm::ivec3 &pos ) {
        gs.blockUpdateQueue.addBlockUpdate( std::make_shared<DoorEvent>( gs.tick_number, pos, DoorEvent::TOGGLE ) );
    };
    auto is_open = [ & ]( const glm::ivec3 &pos ) {
        return ( world.get_loaded_block( pos ).rotation & BLOCK_ROTATE_DOOR_FLAG ) != 0;
    };

    // A lower on a solid floor writes both halves; a missing floor or a
    // blocked upper cell places nothing.
    place( glm::ivec3( 10, 11, 10 ), oak_door );
    place( glm::ivec3( 15, 11, 15 ), oak_door );
    place( glm::ivec3( 18, 11, 15 ), oak_door );
    run( 2 );
    failures += check_id( world, "placed: lower written", glm::ivec3( 10, 11, 10 ), WOODEN_DOOR_LOWER );
    failures += check_id( world, "placed: upper written", glm::ivec3( 10, 12, 10 ), WOODEN_DOOR_UPPER );
    failures += check_id( world, "no floor: nothing placed", glm::ivec3( 15, 11, 15 ), AIR );
    failures += check_id( world, "blocked upper: nothing placed", glm::ivec3( 18, 11, 15 ), AIR );

    // Uppers stay is_pickable=false (creative exclusion) but must still be
    // targetable: a horizontal pick ray through the upper slab hits it.
    glm::ivec3 pick_out( -1, -1, -1 );
    int pick_face = -1;
    RayTraversal::find_block_from_to( world, nullptr, glm::dvec3( 10.5, 12.5, 9.5 ), glm::dvec3( 10.5, 12.5, 11.5 ), pick_out, &pick_face, 0, 1, 0, 0 );
    check_bool( "pick: ray hits the upper half", pick_out == glm::ivec3( 10, 12, 10 ) );

    // A second door on the first's right becomes the pair's hinge-right
    // member (matching facing required).
    place( glm::ivec3( 11, 11, 10 ), oak_door );
    run( 2 );
    check_bool( "double door: right member gets hinge right", ( world.get_loaded_block( glm::ivec3( 11, 12, 10 ) ).rotation & BLOCK_ROTATE_DOOR_FLAG ) != 0 );
    check_bool( "double door: left member stays hinge left", ( world.get_loaded_block( glm::ivec3( 10, 12, 10 ) ).rotation & BLOCK_ROTATE_DOOR_FLAG ) == 0 );

    // The closed F=0 slab sits at the cell's +z edge (z .8125..1); opened
    // (hinge-left) it swings to the x 0..0.1875 strip.
    const double eye_y = 11.0 + PLAYER_EYE_HEIGHT;
    check_bool( "collision: closed door blocks +z edge", Collision::collides_at( world, glm::dvec3( 10.5, eye_y, 10.9 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
    check_bool( "collision: closed door leaves -z side free", !Collision::collides_at( world, glm::dvec3( 10.5, eye_y, 10.2 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
    toggle( glm::ivec3( 10, 11, 10 ) );
    run( 2 );
    check_bool( "toggle: lower open bit set", is_open( glm::ivec3( 10, 11, 10 ) ) );
    check_bool( "toggle: upper yaw follows", ( world.get_loaded_block( glm::ivec3( 10, 12, 10 ) ).rotation & 3 ) == door_yaw( 0, true, false ) );
    check_bool( "collision: open door blocks the x 0..3 strip", Collision::collides_at( world, glm::dvec3( 10.05, eye_y, 10.5 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
    check_bool( "collision: open door frees the +z edge", !Collision::collides_at( world, glm::dvec3( 10.5, eye_y, 10.9 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
    toggle( glm::ivec3( 10, 12, 10 ) ); // clicking the upper toggles too
    run( 2 );
    check_bool( "toggle: closes again", !is_open( glm::ivec3( 10, 11, 10 ) ) );

    // Breaking either half pops the other through the survive check.
    place( glm::ivec3( 11, 12, 10 ), BLOCK_STATE_AIR );
    run( 2 );
    failures += check_id( world, "breaking upper removes lower", glm::ivec3( 11, 11, 10 ), AIR );
    place( glm::ivec3( 10, 11, 10 ), BLOCK_STATE_AIR );
    run( 2 );
    failures += check_id( world, "breaking lower removes upper", glm::ivec3( 10, 12, 10 ), AIR );

    // Redstone edge semantics: a source next to the lower opens the door.
    place( glm::ivec3( 20, 11, 20 ), oak_door );
    place( glm::ivec3( 21, 11, 20 ), redstone_block );
    run( 10 );
    check_bool( "redstone: rising edge opens", is_open( glm::ivec3( 20, 11, 20 ) ) );

    // A hand toggle while powered survives — no edge, no re-open.
    toggle( glm::ivec3( 20, 11, 20 ) );
    run( 10 );
    check_bool( "redstone: hand-close while powered stays closed", !is_open( glm::ivec3( 20, 11, 20 ) ) );

    // Falling then rising edges close and re-open it.
    place( glm::ivec3( 21, 11, 20 ), BLOCK_STATE_AIR );
    run( 10 );
    check_bool( "redstone: stays closed after power removed", !is_open( glm::ivec3( 20, 11, 20 ) ) );
    place( glm::ivec3( 21, 11, 20 ), redstone_block );
    run( 10 );
    check_bool( "redstone: rising edge re-opens", is_open( glm::ivec3( 20, 11, 20 ) ) );
    place( glm::ivec3( 21, 11, 20 ), BLOCK_STATE_AIR );
    run( 10 );
    check_bool( "redstone: falling edge closes", !is_open( glm::ivec3( 20, 11, 20 ) ) );

    // Powering only the upper half also opens the door.
    place( glm::ivec3( 20, 13, 20 ), redstone_block );
    run( 10 );
    check_bool( "redstone: powering the upper opens", is_open( glm::ivec3( 20, 11, 20 ) ) );
    return failures;
}

// TNT: a powered TNT ignites — the cell goes air while the primed entity
// (a renderer-side cube, not a block) flashes — and the fuse event blasts a
// sphere that destroys stone, spares bedrock, and chain-ignites a second
// TNT which launches away and detonates on a shorter fuse.
int test_tnt( ) {
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
    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };
    auto tick = [ & ]( ) {
        gs.tick_number++;
        world.bodies.tick( gs );
        gs.blockUpdateQueue.processAllBlockUpdates( gs, gs.tick_number );
    };

    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    const BlockState bedrock = { BEDROCK, 0, 0, BEDROCK, 0 };
    // Stone floor on an indestructible bedrock slab.
    for ( int x = 6; x <= 16; x++ ) {
        for ( int z = 6; z <= 16; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 9, z ), bedrock );
            world.set_loaded_block( glm::ivec3( x, 10, z ), stone );
        }
    }
    // Chain-ignition target inside the blast radius but NOT adjacent to the
    // redstone block (which would ignite it directly). Launches +x (away
    // from the blast center) and lands ~4-5 blocks out, over the platform.
    world.set_loaded_block( glm::ivec3( 13, 11, 10 ), { TNT, 0, 0, TNT, 0 } );
    // Far TNT behind the launch direction: survives both blasts.
    world.set_loaded_block( glm::ivec3( 8, 11, 16 ), { TNT, 0, 0, TNT, 0 } );
    // Stone well outside every blast radius.
    world.set_loaded_block( glm::ivec3( 25, 10, 25 ), stone );

    // A 3x3, 3-deep pit (air for y=8..10 over a stone floor at y=7) beside
    // the platform: a TNT ignited at y=11 over it must fall in and explode
    // at the pit floor, not at the ignite cell.
    for ( int x = 19; x <= 21; x++ ) {
        for ( int z = 19; z <= 21; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 7, z ), stone );
        }
    }
    // Rim stone: only survives if the blast lands at the pit floor (y=8);
    // a blast at the ignite cell (y=11) is 3.6 blocks away and destroys it.
    world.set_loaded_block( glm::ivec3( 18, 11, 18 ), stone );

    auto place = [ & ]( const glm::ivec3 &pos, const BlockState &block_state ) {
        gs.blockUpdateQueue.addBlockUpdate( std::make_shared<PlayerBlockPlacedEvent>( gs.tick_number, pos, block_state, false ) );
    };
    place( glm::ivec3( 10, 11, 10 ), { TNT, 0, 0, TNT, 0 } );
    place( glm::ivec3( 11, 11, 10 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );
    // Primed TNT over the pit, ignited by an adjacent redstone block.
    place( glm::ivec3( 20, 11, 20 ), { TNT, 0, 0, TNT, 0 } );
    place( glm::ivec3( 20, 11, 21 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );

    // Ignition propagates within a few ticks — the powered TNT cell turns to
    // air long before the fuse event lands.
    for ( int i = 0; i < 10; i++ ) {
        tick( );
    }
    failures += check_id( world, "ignited TNT cell is air", glm::ivec3( 10, 11, 10 ), AIR );
    failures += check_id( world, "chain TNT not yet blasted", glm::ivec3( 13, 11, 10 ), TNT );
    failures += check_id( world, "platform intact before blast", glm::ivec3( 10, 10, 10 ), STONE );
    check_bool( "two primed TNT bodies live", world.bodies.count_tnt( ) == 2 );

    // A primed TNT resting on the floor beside the main blast gets shoved by
    // it. Spawned directly (its cell was already air) with a long fuse.
    const glm::dvec3 shoved_spawn( 10.5, 11.5, 14.5 );
    const uint32_t shoved = world.bodies.spawn_primed_tnt( gs, shoved_spawn, glm::dvec3( 0.0 ), TNT, TNT_FUSE_TICKS * 20, gs.tick_number );
    check_bool( "shove-test primed TNT spawned", shoved != 0 );

    // Outlast the main fuse plus the longest possible chain fuse.
    const long wait = TNT_FUSE_TICKS + TNT_CHAIN_FUSE_MIN_TICKS + TNT_CHAIN_FUSE_SPREAD_TICKS + 40;
    for ( long i = 0; i < wait; i++ ) {
        tick( );
    }
    failures += check_id( world, "blast cell stays air", glm::ivec3( 10, 11, 10 ), AIR );
    failures += check_id( world, "stone under blast destroyed", glm::ivec3( 10, 10, 10 ), AIR );
    failures += check_id( world, "bedrock survives blast", glm::ivec3( 10, 9, 10 ), BEDROCK );
    failures += check_id( world, "chained TNT detonated", glm::ivec3( 13, 11, 10 ), AIR );
    failures += check_id( world, "far TNT untouched", glm::ivec3( 8, 11, 16 ), TNT );
    failures += check_id( world, "distant stone untouched", glm::ivec3( 25, 10, 25 ), STONE );
    failures += check_id( world, "pit rim stone survives (blast was at pit floor)", glm::ivec3( 18, 11, 18 ), STONE );
    failures += check_id( world, "pit floor block destroyed by blast", glm::ivec3( 20, 7, 20 ), AIR );

    glm::dvec3 shoved_pos = shoved_spawn;
    world.bodies.take( shoved, &shoved_pos );
    check_bool( "primed TNT pushed by nearby blast", glm::distance( shoved_pos, shoved_spawn ) > 0.05 );
    check_bool( "no primed TNT bodies remain", world.bodies.count_tnt( ) == 0 );
    return failures;
}

// Falling blocks: gravity blocks released by a support change become FALLING
// bodies that land and write themselves back. Cover columns, a 1-wide gap
// drop, water drag, anvil + concrete powder, blast behavior (in-world sand is
// destroyed; already-falling bodies are pushed), unloaded cells below, and
// landing on a non-replaceable torch cell.
int test_falling_blocks( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    RepGameState &gs = *new RepGameState( ); // leaked on purpose — see test_tnt
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 0, 0, 0 );
    test_setup_world( gs.world );
    World &world = gs.world;
    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };
    // Each game tick: the bodies integrate, then queued block events fire.
    auto tick = [ & ]( ) {
        gs.tick_number++;
        world.bodies.tick( gs );
        gs.blockUpdateQueue.processAllBlockUpdates( gs, gs.tick_number );
    };
    auto run = [ & ]( const int ticks ) {
        for ( int i = 0; i < ticks; i++ ) {
            tick( );
        }
    };
    auto place = [ & ]( const glm::ivec3 &pos, const BlockState &block_state ) {
        gs.blockUpdateQueue.addBlockUpdate( std::make_shared<PlayerBlockPlacedEvent>( gs.tick_number, pos, block_state, false ) );
    };

    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    const BlockState sand = { SAND, 0, 0, SAND, 0 };

    // --- Column: three stacked sand collapse onto a stone floor. ---
    for ( int x = 9; x <= 12; x++ ) {
        for ( int z = 9; z <= 11; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 10, z ), stone );
        }
    }
    world.set_loaded_block( glm::ivec3( 10, 16, 10 ), sand );
    world.set_loaded_block( glm::ivec3( 10, 17, 10 ), sand );
    place( glm::ivec3( 10, 15, 10 ), sand );
    run( 4 * UPS_RATE );
    failures += check_id( world, "column: sand landed lowest", glm::ivec3( 10, 11, 10 ), SAND );
    failures += check_id( world, "column: sand stacked at 12", glm::ivec3( 10, 12, 10 ), SAND );
    failures += check_id( world, "column: sand stacked at 13", glm::ivec3( 10, 13, 10 ), SAND );
    failures += check_id( world, "column: air above the stack", glm::ivec3( 10, 14, 10 ), AIR );
    failures += check_id( world, "column: air at the top", glm::ivec3( 10, 17, 10 ), AIR );
    check_bool( "column: no bodies remain", world.bodies.count_falling( ) == 0 );

    // --- Gap: sand over a 1x1 hole drops one level into the gap cell. ---
    for ( int x = 4; x <= 8; x++ ) {
        for ( int z = 9; z <= 11; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 10, z ), stone );
        }
    }
    world.set_loaded_block( glm::ivec3( 7, 10, 10 ), BLOCK_STATE_AIR ); // the gap cell
    world.set_loaded_block( glm::ivec3( 7, 9, 10 ), stone );            // under the gap
    place( glm::ivec3( 7, 11, 10 ), sand );
    run( 2 * UPS_RATE );
    failures += check_id( world, "gap: sand fell into the hole", glm::ivec3( 7, 10, 10 ), SAND );
    check_bool( "gap: no bodies remain", world.bodies.count_falling( ) == 0 );

    // --- Stacked release: breaking the support under stacked sand must not
    // let an upper body dip into the block a lower body just wrote (the
    // "falls into the just-landed block, then pops up" bug). Each tick, no
    // falling body may sit below the top of the highest written sand cell. ---
    auto stack_test = [ & ]( const char *name, const glm::ivec3 &base, const int count ) {
        world.set_loaded_block( base + glm::ivec3( 0, -1, 0 ), stone ); // floor
        world.set_loaded_block( base, stone );
        for ( int i = 0; i < count; i++ ) {
            world.set_loaded_block( base + glm::ivec3( 0, 1 + i, 0 ), sand );
        }
        place( base, BLOCK_STATE_AIR ); // break the support
        bool dipped = false;
        for ( int t = 0; t < 4 * UPS_RATE; t++ ) {
            tick( );
            int top_y = -1;
            for ( int i = 0; i < count; i++ ) {
                if ( world.get_loaded_block( base + glm::ivec3( 0, i, 0 ) ).id == SAND ) {
                    top_y = base.y + i;
                }
            }
            if ( top_y < 0 ) {
                continue;
            }
            for ( const auto &db : world.bodies.debug_bodies( ) ) {
                if ( db.kind == BlockBodies::BODY_FALLING && db.min.y < top_y + 1.0 - 0.03 ) {
                    dipped = true;
                }
            }
        }
        char label[ 96 ];
        snprintf( label, sizeof( label ), "%s: body never dips below landed sand", name );
        check_bool( label, !dipped );
        for ( int i = 0; i < count; i++ ) {
            snprintf( label, sizeof( label ), "%s: sand at %d", name, base.y + i );
            failures += check_id( world, label, base + glm::ivec3( 0, i, 0 ), SAND );
        }
        snprintf( label, sizeof( label ), "%s: air above the stack", name );
        failures += check_id( world, label, base + glm::ivec3( 0, count, 0 ), AIR );
        snprintf( label, sizeof( label ), "%s: no bodies remain", name );
        check_bool( label, world.bodies.count_falling( ) == 0 );
    };
    stack_test( "stack2", glm::ivec3( 34, 10, 34 ), 2 );
    stack_test( "stack3", glm::ivec3( 37, 10, 37 ), 3 );

    // --- Water: a drop through water is slower than the same drop in air. ---
    world.set_loaded_block( glm::ivec3( 20, 10, 20 ), stone );
    for ( int y = 11; y <= 15; y++ ) {
        world.set_loaded_block( glm::ivec3( 20, y, 20 ), { WATER, 0, 0, WATER, 0 } );
    }
    world.set_loaded_block( glm::ivec3( 24, 10, 20 ), stone );
    place( glm::ivec3( 20, 16, 20 ), sand );
    int ticks_water = 0;
    for ( ; ticks_water < 24 * UPS_RATE; ticks_water++ ) {
        tick( );
        if ( world.bodies.count_falling( ) == 0 && world.get_loaded_block( glm::ivec3( 20, 11, 20 ) ).id == SAND ) {
            break;
        }
    }
    place( glm::ivec3( 24, 16, 20 ), sand );
    int ticks_air = 0;
    for ( ; ticks_air < 24 * UPS_RATE; ticks_air++ ) {
        tick( );
        if ( world.bodies.count_falling( ) == 0 && world.get_loaded_block( glm::ivec3( 24, 11, 20 ) ).id == SAND ) {
            break;
        }
    }
    failures += check_id( world, "water: sand landed at the bottom", glm::ivec3( 20, 11, 20 ), SAND );
    failures += check_id( world, "air: sand landed at the bottom", glm::ivec3( 24, 11, 20 ), SAND );
    pr_test( "  %-55s water=%d air=%d", "water drop slower than air drop", ticks_water, ticks_air );
    check_bool( "water drop slower than air drop", ticks_water > ticks_air );

    // --- Anvil + concrete powder also fall. ---
    world.set_loaded_block( glm::ivec3( 26, 10, 26 ), stone );
    world.set_loaded_block( glm::ivec3( 28, 10, 26 ), stone );
    place( glm::ivec3( 26, 12, 26 ), { ANVIL, 0, 0, ANVIL, 0 } );
    place( glm::ivec3( 28, 12, 26 ), { CONCRETEPOWDER, 0, 0, CONCRETEPOWDER, 0 } );
    run( 2 * UPS_RATE );
    failures += check_id( world, "anvil fell", glm::ivec3( 26, 11, 26 ), ANVIL );
    failures += check_id( world, "concrete powder fell", glm::ivec3( 28, 11, 26 ), CONCRETEPOWDER );

    // --- Explosion: in-world gravity blocks are destroyed, not flung; a
    // body already in flight near the blast is pushed. ---
    const glm::ivec3 blast( 12, 11, 12 );
    for ( int x = 9; x <= 15; x++ ) {
        for ( int z = 9; z <= 15; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 9, z ), { BEDROCK, 0, 0, BEDROCK, 0 } );
            world.set_loaded_block( glm::ivec3( x, 10, z ), stone );
        }
    }
    // Sand column straddling the sphere: cells up to y=15 are inside the
    // blast (destroyed), y=16 sits outside and only loses support.
    for ( int y = 11; y <= 16; y++ ) {
        world.set_loaded_block( glm::ivec3( 13, y, 12 ), sand );
    }
    // An already-falling body in mid-air beside the blast — left un-landed so
    // the impulse can be measured on it.
    const uint32_t flyer = world.bodies.spawn_falling( glm::ivec3( 14, 15, 14 ), sand, glm::dvec3( 0.0 ) );
    check_bool( "blast: mid-air body spawned", flyer != 0 );
    gs.blockUpdateQueue.addBlockUpdate( std::make_shared<TntExplodeEvent>( gs.tick_number, blast, 0, false ) );
    // One tick: the explosion event runs, but the outside-sand FallingBlockEvent
    // (+2 ticks) hasn't fired yet — any body now would have come from the blast.
    tick( );
    failures += check_id( world, "blast: sand inside radius destroyed", glm::ivec3( 13, 11, 12 ), AIR );
    failures += check_id( world, "blast: sand inside radius destroyed (top)", glm::ivec3( 13, 14, 12 ), AIR );
    check_bool( "blast: no bodies from in-radius blocks", world.bodies.count_falling( ) == 1 );
    // The impulse pushed the flyer away from the blast center.
    const glm::dvec3 blast_center = glm::dvec3( blast ) + glm::dvec3( 0.5 );
    glm::dvec3 flyer_min( 0.0 ), flyer_vel( 0.0 );
    for ( const auto &db : world.bodies.debug_bodies( ) ) {
        if ( db.id == flyer ) {
            flyer_min = db.min;
            flyer_vel = db.vel;
        }
    }
    const glm::dvec3 push_away = ( flyer_min + glm::dvec3( 0.5 ) ) - blast_center;
    check_bool( "blast: falling body pushed away", glm::dot( flyer_vel, push_away ) > 0.0 );
    // The outside sand then falls through the normal FallingBlockEvent path.
    run( 4 * UPS_RATE );
    check_bool( "blast: no bodies remain", world.bodies.count_falling( ) == 0 );
    failures += check_id( world, "blast: support-lost sand landed in crater", glm::ivec3( 13, 10, 12 ), SAND );
    failures += check_id( world, "blast: second outside sand stacked on it", glm::ivec3( 13, 11, 12 ), SAND );

    // --- Unloaded below: a cell whose below is unloaded never falls. ---
    // (Writes can't go through set_loaded_block at a chunk face — the halo
    // fixup needs the missing neighbor — so the sand is written directly
    // into chunk storage and a neighbor placement triggers the check.)
    Chunk &chunk = world.chunkLoader.chunkArray[ chunk_slot_from_pos( glm::ivec3( 0, 0, 0 ) ) ];
    chunk.set_block( glm::ivec3( 5, 0, 10 ), sand );
    place( glm::ivec3( 5, 1, 10 ), stone );
    run( 30 );
    failures += check_id( world, "unloaded below: sand stays", glm::ivec3( 5, 0, 10 ), SAND );
    check_bool( "unloaded below: no body spawned", world.bodies.count_falling( ) == 0 );

    // --- Torch: a falling block into a torch cell breaks into debris. ---
    world.set_loaded_block( glm::ivec3( 10, 10, 5 ), stone );
    world.set_loaded_block( glm::ivec3( 10, 11, 5 ), { TORCH, 0, 0, TORCH, 0 } );
    place( glm::ivec3( 10, 12, 5 ), sand );
    run( 2 * UPS_RATE );
    check_bool( "torch: cell has no sand", world.get_loaded_block( glm::ivec3( 10, 11, 5 ) ).id != SAND );
    check_bool( "torch: no body remains", world.bodies.count_falling( ) == 0 );
    return failures;
}

// Structure-gen border mirroring: a chunk's +-1 halo cells must equal the
// interior cells they mirror in the neighbor. Terrain trivially agrees (both
// sides derive it from the same pure function); tree parts only agree when
// the placement rules are chunk-independent — on the old legacy generator a
// border-spanning tree was clipped to the trunk's owning chunk and the halo
// stayed bare. Two y slabs are generated so y-border mirroring is checked
// the same way.
int test_structure_border( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };

    auto gen_chunk = []( const glm::ivec3 &pos ) {
        Chunk *c = new Chunk( ); // freed by the caller's loop
        c->chunk_pos = pos;
        c->cells = chunk_alloc_cells( );
        c->extras = nullptr;
        c->light_air_mask = nullptr;
        c->light_shade_mask = nullptr;
        MapGen::load_block_c( c );
        StructureGen::place( *c );
        return c;
    };
    auto free_chunk = []( Chunk *c ) {
        chunk_free_cells( c->cells );
        chunk_free_extras( c->extras );
        chunk_free_air_mask( c->light_air_mask );
        chunk_free_air_mask( c->light_shade_mask );
        delete c;
    };

    // A tree part in a halo plane can only have come from structure gen —
    // terrain never emits OAK_LEAVES/OAK_LOG — so counting them proves trees reach
    // borders, and the face comparisons then prove they mirror correctly.
    int border_tree_cells = 0;
    int mismatches = 0;
    for ( int attempt = 0; attempt < 10 && border_tree_cells == 0; attempt++ ) {
        const int rcx = -40 + attempt * 19;
        const int rcz = -40 + attempt * 13;
        const int wx0 = rcx * CHUNK_SIZE_X + CHUNK_SIZE_X / 2;
        const int wz0 = rcz * CHUNK_SIZE_Z + CHUNK_SIZE_Z / 2;
        const float h = MapGen::calculateTerrainHeight( wx0, wz0 );
#if REPGAME_MAP_GEN_LEGACY
        // Target the legacy forest field (perlin_noise2d > 0.7 somewhere in
        // the region) so the grid is actually in tree country.
        bool forest = false;
        for ( int x = wx0 - CHUNK_SIZE_X; x < wx0 + 2 * CHUNK_SIZE_X && !forest; x += 5 ) {
            for ( int z = wz0 - CHUNK_SIZE_Z; z < wz0 + 2 * CHUNK_SIZE_Z && !forest; z += 5 ) {
                if ( perlin_noise2d( x, z, 0.02f, 3, MAP_SEED + 6 ) > 0.7f ) {
                    forest = true;
                }
            }
        }
        if ( !forest ) {
            continue;
        }
#else
        if ( h < WATER_LEVEL + 3.0f ) {
            continue; // open water — trees need land
        }
#endif
        const int cy = (int)floorf( h / (float)CHUNK_SIZE_Y );

        Chunk *grid[ 3 ][ 2 ][ 3 ] = {};
        for ( int gx = 0; gx < 3; gx++ ) {
            for ( int gy = 0; gy < 2; gy++ ) {
                for ( int gz = 0; gz < 3; gz++ ) {
                    grid[ gx ][ gy ][ gz ] = gen_chunk( glm::ivec3( rcx + gx, cy + gy, rcz + gz ) );
                }
            }
        }

        // Every shared face, both directions: the outer halo cell of each
        // chunk vs the mirrored interior edge of its neighbor.
        for ( int gx = 0; gx < 3; gx++ ) {
            for ( int gy = 0; gy < 2; gy++ ) {
                for ( int gz = 0; gz < 3; gz++ ) {
                    const Chunk *a = grid[ gx ][ gy ][ gz ];
                    if ( gx < 2 ) {
                        const Chunk *b = grid[ gx + 1 ][ gy ][ gz ];
                        for ( int ly = -1; ly <= CHUNK_SIZE_Y; ly++ ) {
                            for ( int lz = -1; lz <= CHUNK_SIZE_Z; lz++ ) {
                                if ( a->get_block( glm::ivec3( CHUNK_SIZE_X, ly, lz ) ).id != b->get_block( glm::ivec3( 0, ly, lz ) ).id ) {
                                    mismatches++;
                                }
                                if ( a->get_block( glm::ivec3( CHUNK_SIZE_X - 1, ly, lz ) ).id != b->get_block( glm::ivec3( -1, ly, lz ) ).id ) {
                                    mismatches++;
                                }
                            }
                        }
                    }
                    if ( gz < 2 ) {
                        const Chunk *b = grid[ gx ][ gy ][ gz + 1 ];
                        for ( int ly = -1; ly <= CHUNK_SIZE_Y; ly++ ) {
                            for ( int lx = -1; lx <= CHUNK_SIZE_X; lx++ ) {
                                if ( a->get_block( glm::ivec3( lx, ly, CHUNK_SIZE_Z ) ).id != b->get_block( glm::ivec3( lx, ly, 0 ) ).id ) {
                                    mismatches++;
                                }
                                if ( a->get_block( glm::ivec3( lx, ly, CHUNK_SIZE_Z - 1 ) ).id != b->get_block( glm::ivec3( lx, ly, -1 ) ).id ) {
                                    mismatches++;
                                }
                            }
                        }
                    }
                    if ( gy < 1 ) {
                        const Chunk *b = grid[ gx ][ gy + 1 ][ gz ];
                        for ( int lx = -1; lx <= CHUNK_SIZE_X; lx++ ) {
                            for ( int lz = -1; lz <= CHUNK_SIZE_Z; lz++ ) {
                                if ( a->get_block( glm::ivec3( lx, CHUNK_SIZE_Y, lz ) ).id != b->get_block( glm::ivec3( lx, 0, lz ) ).id ) {
                                    mismatches++;
                                }
                                if ( a->get_block( glm::ivec3( lx, CHUNK_SIZE_Y - 1, lz ) ).id != b->get_block( glm::ivec3( lx, -1, lz ) ).id ) {
                                    mismatches++;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Count tree parts sitting on x/z halo planes.
        for ( int gx = 0; gx < 3; gx++ ) {
            for ( int gy = 0; gy < 2; gy++ ) {
                for ( int gz = 0; gz < 3; gz++ ) {
                    const Chunk *c = grid[ gx ][ gy ][ gz ];
                    for ( int ly = -1; ly <= CHUNK_SIZE_Y; ly++ ) {
                        for ( int lz = -1; lz <= CHUNK_SIZE_Z; lz++ ) {
                            for ( int lx : { -1, CHUNK_SIZE_X } ) {
                                const BlockID id = c->get_block( glm::ivec3( lx, ly, lz ) ).id;
                                if ( id == OAK_LEAVES || id == OAK_LOG || id == GLOWSTONE ) {
                                    border_tree_cells++;
                                }
                            }
                            for ( int lx = -1; lx <= CHUNK_SIZE_X; lx++ ) {
                                for ( int lz2 : { -1, CHUNK_SIZE_Z } ) {
                                    const BlockID id = c->get_block( glm::ivec3( lx, ly, lz2 ) ).id;
                                    if ( id == OAK_LEAVES || id == OAK_LOG || id == GLOWSTONE ) {
                                        border_tree_cells++;
                                    }
                                }
                            }
                        }
                    }
                    free_chunk( grid[ gx ][ gy ][ gz ] );
                }
            }
        }
    }
    check_bool( "trees reach chunk borders", border_tree_cells > 0 );
    check_bool( "halo cells mirror neighbor interiors", mismatches == 0 );
    return failures;
}

// Passive-mob checks on the phase-1 rig plus phase-2 hitting/persistence:
// physics (drop settle, auto-jump, slab step-up), the mob raycast, damage /
// knockback / drops, the mobs-file record format, the face-rect unwrap and
// the generated pig geometry.
int test_mobs( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    RepGameState &gs = *new RepGameState( );
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 0, 0, 0 );
    test_setup_world( gs.world );
    World &world = gs.world;
    const glm::dvec3 player_pos( 1.5, 50.0, 1.5 ); // far away: no look-at/sounds

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

    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    // Stone floor at y=10 (top surface 11) over a generous area.
    for ( int x = 5; x <= 30; x++ ) {
        for ( int z = 5; z <= 15; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 10, z ), stone );
        }
    }

    // --- 1: a pig dropped over stone settles standing at floor+1 -----------
    {
        MobManager mobs;
        check_bool( "spawn pig", mobs.spawn( MOB_PIG, glm::dvec3( 10.5, 14.0, 10.5 ), 0.0f, 1 ) );
        for ( int i = 0; i < 150 && !mobs.mobs[ 0 ].standing; i++ ) {
            mobs.tick( world, player_pos );
        }
        check_bool( "dropped pig: standing", mobs.mobs[ 0 ].standing == 1 );
        check_near( "dropped pig: settled at floor+1", mobs.mobs[ 0 ].pos.y, 11.0, 0.01 );
    }

    // --- 2: 1-high wall auto-jumps; 2-high wall stops ----------------------
    {
        MobManager mobs;
        mobs.spawn( MOB_PIG, glm::dvec3( 10.5, 11.0, 10.5 ), 90.0f, 1 );
        MobManager::Mob &mob = mobs.mobs[ 0 ];
        // Walk east toward a wall at x=14 (cells 14, z 9..11).
        for ( int z = 9; z <= 11; z++ ) {
            world.set_loaded_block( glm::ivec3( 14, 11, z ), stone );
        }
        mob.ai_state = 1;
        mob.ai_ticks = 100000;
        mob.target = glm::dvec3( 20.0, 11.0, 10.5 );
        for ( int i = 0; i < 1200 && mob.pos.x < 14.6; i++ ) {
            mobs.tick( world, player_pos );
        }
        check_bool( "1-high wall: auto-jumped past", mob.pos.x > 14.6 );

        // Second pig against a 2-high wall at x=14: the hop can't clear it.
        mobs.mobs.clear( );
        world.set_loaded_block( glm::ivec3( 14, 12, 9 ), stone );
        world.set_loaded_block( glm::ivec3( 14, 12, 10 ), stone );
        world.set_loaded_block( glm::ivec3( 14, 12, 11 ), stone );
        mobs.spawn( MOB_PIG, glm::dvec3( 10.5, 11.0, 10.5 ), 90.0f, 1 );
        MobManager::Mob &mob2 = mobs.mobs[ 0 ];
        mob2.ai_state = 1;
        mob2.ai_ticks = 100000;
        mob2.target = glm::dvec3( 20.0, 11.0, 10.5 );
        for ( int i = 0; i < 1200; i++ ) {
            mobs.tick( world, player_pos );
        }
        check_bool( "2-high wall: stopped", mob2.pos.x < 14.0 && mob2.pos.y < 12.0 );
        for ( int z = 9; z <= 11; z++ ) {
            world.set_loaded_block( glm::ivec3( 14, 11, z ), BLOCK_STATE_AIR );
            world.set_loaded_block( glm::ivec3( 14, 12, z ), BLOCK_STATE_AIR );
        }
    }

    // --- 3: half slab step-up ----------------------------------------------
    {
        MobManager mobs;
        const BlockState slab = { STONE_BRICK_SLAB, 0, 0, STONE_BRICK_SLAB, 0 };
        for ( int x = 15; x <= 20; x++ ) {
            for ( int z = 8; z <= 12; z++ ) {
                world.set_loaded_block( glm::ivec3( x, 11, z ), slab );
            }
        }
        mobs.spawn( MOB_PIG, glm::dvec3( 10.5, 11.0, 10.5 ), 90.0f, 1 );
        MobManager::Mob &mob = mobs.mobs[ 0 ];
        mob.ai_state = 1;
        mob.ai_ticks = 100000;
        mob.target = glm::dvec3( 24.0, 11.5, 10.5 );
        for ( int i = 0; i < 1200 && mob.pos.x < 15.6; i++ ) {
            mobs.tick( world, player_pos );
        }
        check_bool( "0.5 slab: stepped up onto it", mob.pos.x > 15.6 );
        check_near( "0.5 slab: standing at 11.5", mob.pos.y, 11.5, 0.05 );
    }

    // --- 4: raycast hits nearer mob; block-nearer wins the click -----------
    {
        MobManager mobs;
        mobs.spawn( MOB_PIG, glm::dvec3( 10.5, 11.0, 10.5 ), 0.0f, 1 );
        // Pin the mob (no AI walk) so the AABB is exactly at spawn.
        mobs.mobs[ 0 ].ai_state = 0;
        mobs.mobs[ 0 ].ai_ticks = 100000;
        const glm::dvec3 origin( 8.5, 11.4, 10.5 );
        const glm::dvec3 dir( 1.0, 0.0, 0.0 );
        double dist = 0.0;
        const int hit = mobs.raycast( origin, dir, 6.0, &dist );
        // hitbox_w 0.9 -> front face at 10.5 - 0.45 = 10.05 -> dist 1.55
        check_bool( "raycast: hit mob", hit == 0 );
        check_near( "raycast: entry distance", dist, 10.05 - 8.5, 0.01 );
        // A block cell nearer than the mob takes the click (the slab-entry
        // comparison process_mouse_events makes): cell x=9 enters at 0.5.
        world.set_loaded_block( glm::ivec3( 9, 11, 10 ), stone );
        check_bool( "nearer block beats the mob", mobs.resolve_target( origin, dir, 6.0, 1, glm::ivec3( 9, 11, 10 ), &dist ) < 0 );
        world.set_loaded_block( glm::ivec3( 9, 11, 10 ), BLOCK_STATE_AIR );
        // Outside reach the ray misses entirely.
        check_bool( "raycast: miss beyond reach", mobs.raycast( origin, dir, 1.0, &dist ) < 0 );
        // A mob farther along the same ray loses to the near one.
        mobs.spawn( MOB_PIG, glm::dvec3( 14.5, 11.0, 10.5 ), 0.0f, 1 );
        check_bool( "raycast: nearer mob wins", mobs.raycast( origin, dir, 6.0, &dist ) == 0 );
    }

    // --- 4b: a mob under the crosshair suppresses the block selection ------
    {
        MobManager &mobs = gs.world.mobs;
        mobs.mobs.clear( );
        mobs.spawn( MOB_PIG, glm::dvec3( 10.5, 11.0, 10.5 ), 0.0f, 1 );
        mobs.mobs[ 0 ].ai_state = 0;
        mobs.mobs[ 0 ].ai_ticks = 100000;
        const glm::dvec3 origin( 8.5, 11.4, 10.5 );
        gs.block_selection.ray_origin = origin;
        gs.block_selection.ray_dir = glm::dvec3( 1.0, 0.0, 0.0 );

        // (a) Pig between the camera and a wall: mob wins, selection clears.
        world.set_loaded_block( glm::ivec3( 14, 11, 10 ), stone );
        gs.block_selection.selectionInBounds = 1;
        gs.block_selection.floatSelectionInBounds = 1;
        gs.block_selection.pos_destroy = glm::ivec3( 14, 11, 10 );
        RepGame::resolve_target_mob( gs, origin );
        check_bool( "target mob: pig before wall wins", gs.block_selection.target_mob == 0 );
        check_bool( "target mob: selection cleared", gs.block_selection.selectionInBounds == 0 && gs.block_selection.floatSelectionInBounds == 0 );

        // (b) Pig behind a nearer block: the block keeps the selection.
        world.set_loaded_block( glm::ivec3( 9, 11, 10 ), stone );
        gs.block_selection.selectionInBounds = 1;
        gs.block_selection.pos_destroy = glm::ivec3( 9, 11, 10 );
        RepGame::resolve_target_mob( gs, origin );
        check_bool( "target mob: nearer block wins", gs.block_selection.target_mob < 0 );
        check_bool( "target mob: selection kept", gs.block_selection.selectionInBounds == 1 );
        world.set_loaded_block( glm::ivec3( 9, 11, 10 ), BLOCK_STATE_AIR );
        world.set_loaded_block( glm::ivec3( 14, 11, 10 ), BLOCK_STATE_AIR );

        // (c) Pig standing on the selected block, looked down on from above:
        // the ray enters the pig's hitbox (~1.1) before the cell's top face
        // (2.0), so the slab test gives it to the pig.
        const glm::dvec3 above( 10.5, 13.0, 10.5 );
        gs.block_selection.ray_origin = above;
        gs.block_selection.ray_dir = glm::dvec3( 0.0, -1.0, 0.0 );
        gs.block_selection.selectionInBounds = 1;
        gs.block_selection.pos_destroy = glm::ivec3( 10, 10, 10 );
        RepGame::resolve_target_mob( gs, above );
        check_bool( "target mob: pig on selected block wins", gs.block_selection.target_mob == 0 );
        check_bool( "target mob: ground selection cleared", gs.block_selection.selectionInBounds == 0 );

        // (d) No pig: the selection is unchanged.
        mobs.mobs.clear( );
        gs.block_selection.selectionInBounds = 1;
        gs.block_selection.pos_destroy = glm::ivec3( 10, 10, 10 );
        RepGame::resolve_target_mob( gs, above );
        check_bool( "target mob: none without pig", gs.block_selection.target_mob < 0 );
        check_bool( "target mob: selection unchanged", gs.block_selection.selectionInBounds == 1 );
    }

    // --- 5: damage, knockback, death, drops --------------------------------
    {
        // Headless hotbar: init() does GL work via the renderer, so the test
        // fills the plain fields directly (friend access).
        gs.hotbar.width = HOTBAR_WIDTH;
        gs.hotbar.height = HOTBAR_HEIGHT;
        gs.hotbar.num_blocks_max = HOTBAR_WIDTH * HOTBAR_HEIGHT;
        gs.hotbar.slots = static_cast<InventorySlot *>( calloc( gs.hotbar.num_blocks_max, sizeof( InventorySlot ) ) );
        for ( int i = 0; i < gs.hotbar.num_blocks_max; i++ ) {
            gs.hotbar.slots[ i ].item_id = LAST_BLOCK_ID;
            gs.hotbar.slots[ i ].quantity = 0;
        }
        gs.hotbar.selected_slot = 0;
        // InventoryRenderer::init's GL work is skipped too; only the fields
        // singleItemRender touches are filled.
        InventoryRenderer &ir = gs.hotbar.inventory_renderer;
        ir.width = HOTBAR_WIDTH;
        ir.height = HOTBAR_HEIGHT;
        ir.num_blocks_max = HOTBAR_WIDTH * HOTBAR_HEIGHT;
        ir.screen_width = 800;
        ir.screen_height = 600;
        ir.inv_items_x = 0;
        ir.inv_items_y = 0;
        ir.inv_cell_stride = 1.0f;
        ir.inv_block_size = 1;
        ir.inv_block_offset = 0;
        ir.slots_entities = static_cast<entt::entity *>( calloc( ir.num_blocks_max, sizeof( entt::entity ) ) );
        for ( int i = 0; i < ir.num_blocks_max; i++ ) {
            ir.slots_entities[ i ] = entt::null;
        }
        MobManager mobs;
        mobs.spawn( MOB_PIG, glm::dvec3( 10.5, 11.0, 10.5 ), 0.0f, 1 );
        const glm::dvec3 attacker( 8.5, 11.5, 10.5 );
        check_bool( "hit 1: survives", !mobs.hurt( 0, 4.0f, attacker ) );
        check_bool( "hit: red flash set", mobs.mobs[ 0 ].hurt_ticks > 0 );
        check_bool( "hit: panic set", mobs.mobs[ 0 ].panic_ticks > 0 );
        check_bool( "hit: knockback away", mobs.mobs[ 0 ].kb.x > 0.0 );
        check_bool( "hit 2: survives", !mobs.hurt( 0, 4.0f, attacker ) );
        check_bool( "hit 3: kills (survival)", mobs.hurt( 0, 4.0f, attacker ) );
        check_bool( "death: tip-over started", mobs.mobs[ 0 ].death_ticks > 0 );
        // Survival drop path: the killed pig's drops go to the player.
        const MobTypeDef &pig_def = mob_types[ MOB_PIG ];
        check_bool( "pig drops: PORKCHOP 1-3", pig_def.drops[ 0 ].item == PORKCHOP && pig_def.drops[ 0 ].min == 1 && pig_def.drops[ 0 ].max == 3 );
        RepGame::give_item_to_player( gs, pig_def.drops[ 0 ].item, pig_def.drops[ 0 ].min );
        check_bool( "drop: PORKCHOP in inventory", gs.hotbar.findSlotWithItem( PORKCHOP, false ) >= 0 );
        // Creative: one hit kills; the caller simply doesn't run the drop.
        MobManager mobs2;
        mobs2.spawn( MOB_PIG, glm::dvec3( 10.5, 11.0, 10.5 ), 0.0f, 1 );
        check_bool( "creative hit: kills in 1", mobs2.hurt( 0, 1000.0f, attacker ) );
    }

    // --- 6: mobs file round trip, empty marker, unknown type ---------------
    {
        // Point the map root at a throwaway dir under /tmp (getRepGamePath
        // reads $HOME): no writes land in a real world.
        const char *orig_home = getenv( "HOME" );
        char orig_home_buf[ 512 ] = { 0 };
        if ( orig_home != nullptr ) {
            strncpy( orig_home_buf, orig_home, sizeof( orig_home_buf ) - 1 );
        }
        setenv( "HOME", "/tmp/repgame_test_mobs_home", 1 );
        MapStorage storage;
        storage.init( "TestMobs" );
        const glm::ivec3 chunk_pos( 0, 0, 0 );
        std::vector<MobFileRecord> in;
        MobFileRecord r = { 0, 0, 0.0, 0.0, 0.0, 0.0f, 0.0f, 0u };
        r.type = MOB_PIG;
        r.variant = 1;
        r.x = 10.5;
        r.y = 11.0;
        r.z = 10.25;
        r.yaw = 45.0f;
        r.health = 7.5f;
        in.push_back( r );
        storage.persist_mobs( chunk_pos, in );
        std::vector<MobFileRecord> out;
        check_bool( "mob file: present", storage.load_mobs( chunk_pos, out ) == 1 );
        check_bool( "mob file: one record", out.size( ) == 1 );
        if ( out.size( ) == 1 ) {
            const MobFileRecord &o = out[ 0 ];
            check_bool( "mob file: fields round trip",
                        o.type == MOB_PIG && o.variant == 1 && o.x == 10.5 && o.y == 11.0 && o.z == 10.25 && o.yaw == 45.0f && o.health == 7.5f );
        }
        // Empty file: still "present" (the populated marker) with 0 records.
        storage.persist_mobs( glm::ivec3( 0, 0, 1 ), std::vector<MobFileRecord>( ) );
        out.clear( );
        check_bool( "mob file: empty still present", storage.load_mobs( glm::ivec3( 0, 0, 1 ), out ) == 1 );
        check_bool( "mob file: empty has 0 records", out.empty( ) );
        // Unknown mob types are skipped on load.
        MobFileRecord bad = r;
        bad.type = MOB_TYPE_COUNT + 3;
        storage.persist_mobs( glm::ivec3( 0, 0, 2 ), std::vector<MobFileRecord>( 1, bad ) );
        out.clear( );
        check_bool( "mob file: unknown type skipped", storage.load_mobs( glm::ivec3( 0, 0, 2 ), out ) == 1 && out.empty( ) );
        if ( orig_home != nullptr ) {
            setenv( "HOME", orig_home_buf, 1 );
        } else {
            unsetenv( "HOME" );
        }
    }

    // --- 7: face-rect unwrap for the pig head and body ---------------------
    {
        auto rect_ok = [ & ]( const char *name, unsigned int face, float u, float v, float w, float h, float d, bool mirror, const glm::vec4 &want ) {
            const glm::vec4 got = mob_face_rect( face, u, v, w, h, d, mirror );
            check_bool( name, got == want );
        };
        // Pig head cube: uv (0,0), size (8,8,8).
        rect_ok( "head FACE_BACK (front) rect", FACE_BACK, 0, 0, 8, 8, 8, false, glm::vec4( 8, 8, 8, 8 ) );
        rect_ok( "head FACE_TOP rect", FACE_TOP, 0, 0, 8, 8, 8, false, glm::vec4( 8, 0, 8, 8 ) );
        rect_ok( "head FACE_FRONT (back) rect", FACE_FRONT, 0, 0, 8, 8, 8, false, glm::vec4( 24, 8, 8, 8 ) );
        rect_ok( "head FACE_RIGHT rect", FACE_RIGHT, 0, 0, 8, 8, 8, false, glm::vec4( 0, 8, 8, 8 ) );
        rect_ok( "head FACE_RIGHT mirrored", FACE_RIGHT, 0, 0, 8, 8, 8, true, glm::vec4( 16, 8, 8, 8 ) );
        rect_ok( "head FACE_LEFT mirrored", FACE_LEFT, 0, 0, 8, 8, 8, true, glm::vec4( 0, 8, 8, 8 ) );
        // Pig body cube: uv (28,8), size (10,16,8) — legs mirror the same rect.
        rect_ok( "body FACE_BACK (front) rect", FACE_BACK, 28, 8, 10, 16, 8, false, glm::vec4( 36, 16, 10, 16 ) );
        rect_ok( "body FACE_TOP rect", FACE_TOP, 28, 8, 10, 16, 8, false, glm::vec4( 36, 8, 10, 8 ) );
        rect_ok( "body FACE_LEFT rect", FACE_LEFT, 28, 8, 10, 16, 8, false, glm::vec4( 46, 16, 8, 16 ) );
    }

    // --- 8: generated pig model shape --------------------------------------
    {
        const MobModelDef &model = mob_models[ MOB_MODEL_PIG ];
        check_bool( "pig: 6 bones", model.bone_count == 6 );
        check_bool( "pig: 7 cubes", model.cube_count == 7 );
        // The snout is the second cube on the head bone (4x3x1 at uv 16,16).
        const MobBoneDef *head = nullptr;
        for ( int i = 0; i < model.bone_count; i++ ) {
            if ( strcmp( model.bones[ i ].name, "head" ) == 0 ) {
                head = &model.bones[ i ];
            }
        }
        check_bool( "pig: head bone found", head != nullptr );
        if ( head != nullptr ) {
            check_bool( "pig: snout on head", head->cube_count == 2 &&
                                                model.cubes[ head->cube_start + 1 ].size[ 0 ] == 4.0f &&
                                                model.cubes[ head->cube_start + 1 ].size[ 1 ] == 3.0f &&
                                                model.cubes[ head->cube_start + 1 ].size[ 2 ] == 1.0f );
        }
    }

    // --- 9: generated player model shape -----------------------------------
    {
        const MobModelDef &model = mob_models[ MOB_MODEL_PLAYER ];
        check_bool( "player: 12 cubes", model.cube_count == 12 );
        bool ordered = true;
        bool found[ 6 ] = { false };
        const char *want[ 6 ] = { "head", "body", "leftArm", "rightArm", "leftLeg", "rightLeg" };
        int overlays = 0;
        for ( int i = 0; i < model.bone_count; i++ ) {
            const MobBoneDef &b = model.bones[ i ];
            if ( b.parent >= i ) {
                ordered = false;
            }
            for ( int w = 0; w < 6; w++ ) {
                found[ w ] = found[ w ] || strcmp( b.name, want[ w ] ) == 0;
            }
            if ( strstr( b.name, "Sleeve" ) != nullptr || strstr( b.name, "Pants" ) != nullptr || strcmp( b.name, "hat" ) == 0 || strcmp( b.name, "jacket" ) == 0 ) {
                overlays++;
            }
        }
        bool all_found = true;
        for ( int w = 0; w < 6; w++ ) {
            all_found = all_found && found[ w ];
        }
        check_bool( "player: parents precede children", ordered );
        check_bool( "player: head/body/arms/legs present", all_found );
        check_bool( "player: 6 overlay bones", overlays == 6 );
    }

    // --- 10: camera-rotation -> (yaw, pitch) round trip ---------------------
    {
        const float Hs[ 6 ] = { 0.0f, 45.0f, 90.0f, 180.0f, 270.0f, 359.0f };
        const float Vs[ 4 ] = { -60.0f, 0.0f, 30.0f, 89.0f };
        bool ok = true;
        for ( float H : Hs ) {
            for ( float V : Vs ) {
                // RepGame::build_camera_view's rotation (replicated: it's a
                // private const member).
                glm::mat4 rot = glm::rotate( glm::mat4( 1.0f ), glm::radians( -H + 180.0f ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
                rot = glm::rotate( rot, glm::radians( V ), glm::vec3( 1.0f, 0.0f, 0.0f ) );
                float yaw = -1.0f, pitch = -999.0f;
                mob_look_from_camera_rotation( rot, &yaw, &pitch );
                float dyaw = fabsf( yaw - H );
                if ( dyaw > 180.0f ) {
                    dyaw = 360.0f - dyaw;
                }
                if ( dyaw > 1e-3f || fabsf( pitch - V ) > 1e-3f ) {
                    pr_test( "    rot round-trip H=%g V=%g -> yaw=%g pitch=%g", H, V, yaw, pitch );
                    ok = false;
                }
            }
        }
        check_bool( "camera rotation: yaw/pitch round trip", ok );
    }

    // --- 10b: head-look forward direction ------------------------------------
    // Replicates update_render's math: the head's world rotation is
    // RY(-body_yaw) * RX(anim.x) * RY(anim.y) (bone_local_matrix multiplies
    // Rx*Ry*Rz, so anim.y hits the model -Z first, then anim.x), and the
    // model's face is -Z.
    {
        const auto head_forward = []( float body_yaw, float head_yaw, float head_pitch ) {
            const glm::vec3 anim = mob_head_anim( head_yaw, head_pitch );
            glm::mat4 m = glm::rotate( glm::mat4( 1.0f ), glm::radians( -body_yaw ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
            m = glm::rotate( m, glm::radians( anim.x ), glm::vec3( 1.0f, 0.0f, 0.0f ) );
            m = glm::rotate( m, glm::radians( anim.y ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
            return glm::vec3( m * glm::vec4( 0.0f, 0.0f, -1.0f, 0.0f ) );
        };
        const glm::vec3 up = head_forward( 0.0f, 0.0f, 30.0f );
        check_bool( "head look: +pitch looks up", up.y > 0.4f );
        const glm::vec3 turned = head_forward( 0.0f, 40.0f, 0.0f );
        // +yaw forward is (sin yaw, 0, -cos yaw): +40 turns toward +X.
        check_bool( "head look: +yaw turns toward +yaw", turned.x > 0.6f );
        const glm::vec3 body_turned = head_forward( 90.0f, -40.0f, 0.0f );
        check_near( "head look: net yaw tracks body", body_turned.z, -cosf( glm::radians( 50.0f ) ), 1e-3f );
    }

    // --- 11: remote (network-driven) mobs -----------------------------------
    {
        MobManager mobs;
        const uint32_t uid = mobs.spawn_remote( MOB_PLAYER );
        check_bool( "remote: spawn_remote gives uid", uid != 0 && mobs.mobs.size( ) == 1 );
        // First pose is a teleport-sized jump -> snaps to eye - eye_h.
        mobs.set_remote_pose( uid, glm::dvec3( 10.5, 11.0 + PLAYER_EYE_HEIGHT, 10.5 ), 0.0f, 0.0f );
        for ( int i = 0; i < 30; i++ ) {
            mobs.tick( world, player_pos );
        }
        check_near( "remote: feet converge to eye - 1.62", mobs.mobs[ 0 ].pos.y, 11.0, 1e-3 );

        // Walking 0.2 blocks/tick: leg_amp rises, walk_dist accumulates, and
        // stopping decays the swing back to ~0.
        for ( int i = 0; i < 40; i++ ) {
            const double tx = mobs.mobs[ 0 ].target.x + 0.2;
            mobs.set_remote_pose( uid, glm::dvec3( tx, mobs.mobs[ 0 ].target.y + PLAYER_EYE_HEIGHT, 10.5 ), 90.0f, 0.0f );
            mobs.tick( world, player_pos );
        }
        check_bool( "remote: walk amp rises", mobs.mobs[ 0 ].leg_amp > 0.5f );
        check_bool( "remote: walk dist accumulates", mobs.mobs[ 0 ].walk_dist > 0.0f );
        for ( int i = 0; i < 60; i++ ) {
            mobs.tick( world, player_pos );
        }
        check_bool( "remote: amp decays when stopped", mobs.mobs[ 0 ].leg_amp < 0.05f );

        // Stationary body, look 120° off: the body drags so the head stays
        // within 50°.
        mobs.set_remote_pose( uid, glm::dvec3( mobs.mobs[ 0 ].pos.x, mobs.mobs[ 0 ].pos.y + PLAYER_EYE_HEIGHT, mobs.mobs[ 0 ].pos.z ), mobs.mobs[ 0 ].yaw + 120.0f, 0.0f );
        mobs.tick( world, player_pos );
        check_bool( "remote: head yaw clamped to 50", fabsf( mobs.mobs[ 0 ].head_yaw ) <= 50.01f );

        // Remote mobs are invisible to the crosshair raycast...
        const glm::dvec3 origin( mobs.mobs[ 0 ].pos.x - 3.0, mobs.mobs[ 0 ].pos.y + 0.9, mobs.mobs[ 0 ].pos.z );
        double dist = 0.0;
        check_bool( "remote: raycast misses", mobs.raycast( origin, glm::dvec3( 1.0, 0.0, 0.0 ), 6.0, &dist ) < 0 );

        // ...never persisted...
        const char *orig_home = getenv( "HOME" );
        char orig_home_buf[ 512 ] = { 0 };
        if ( orig_home != nullptr ) {
            strncpy( orig_home_buf, orig_home, sizeof( orig_home_buf ) - 1 );
        }
        setenv( "HOME", "/tmp/repgame_test_mobs_home", 1 );
        MapStorage storage;
        storage.init( "TestMobs" );
        mobs.init_storage( &storage );
        mobs.owned_chunks.push_back( { glm::ivec3( 0, 0, 0 ), 0, 0 } );
        mobs.spawn( MOB_PIG, glm::dvec3( 10.5, 11.0, 10.5 ), 0.0f, 1 );
        mobs.persist_all( );
        std::vector<MobFileRecord> out;
        check_bool( "remote: no mobs record", storage.load_mobs( glm::ivec3( 0, 0, 0 ), out ) == 1 && out.size( ) == 1 && out[ 0 ].type == MOB_PIG );
        if ( orig_home != nullptr ) {
            setenv( "HOME", orig_home_buf, 1 );
        } else {
            unsetenv( "HOME" );
        }

        // ...and spawn_remote works with REPGAME_MOBS=0 semantics.
        MobManager off;
        off.enabled = false;
        check_bool( "remote: spawn works when disabled", off.spawn_remote( MOB_PLAYER ) != 0 );
    }

    // --- 11b: remote sneak (eye_height) --------------------------------------
    {
        // Wire round-trip: eye_height survives the player payload.
        NetPlayerPayload out;
        memset( &out, 0, sizeof( out ) );
        out.x = 1.0f;
        out.eye_height = PLAYER_SNEAK_EYE_HEIGHT;
        PacketWriter w;
        net_serialize_player( w, out );
        std::vector<uint8_t> buf = w.take_buf( );
        PacketReader r( buf.data( ), buf.size( ) );
        NetPlayerPayload in;
        memset( &in, 0, sizeof( in ) );
        check_bool( "remote: payload round trips", net_deserialize_player( r, in ) );
        check_near( "remote: payload carries eye_height", in.eye_height, PLAYER_SNEAK_EYE_HEIGHT, 1e-6f );

        // Sneaking eye height: feet land on the ground, not 0.35 under it.
        MobManager mobs;
        const uint32_t uid = mobs.spawn_remote( MOB_PLAYER );
        const double ground = 11.0;
        mobs.set_remote_pose( uid, glm::dvec3( 10.5, ground + PLAYER_SNEAK_EYE_HEIGHT, 10.5 ), 0.0f, 0.0f, PLAYER_SNEAK_EYE_HEIGHT );
        for ( int i = 0; i < 30; i++ ) {
            mobs.tick( world, player_pos );
        }
        check_near( "remote sneak: feet on ground", mobs.mobs[ 0 ].pos.y, ground, 1e-3 );
        check_bool( "remote sneak: sneak converges to 1", mobs.mobs[ 0 ].sneak > 0.95f );

        // Sneaking must not tilt the head: the sneak pose is a model-space
        // pre-multiply (head/arms don't inherit body's lean), so the head
        // bone's rotation is identical standing vs crouched — only its
        // model-space y drops by the 2.5px sneak offset.
        {
            MobManager::Mob stand_mob = mobs.mobs[ 0 ];
            stand_mob.head_yaw = 40.0f;
            stand_mob.head_pitch = 30.0f;
            MobManager::Mob sneak_mob = stand_mob;
            stand_mob.sneak = stand_mob.prev_sneak = 0.0f;
            sneak_mob.sneak = sneak_mob.prev_sneak = 1.0f;
            const MobTypeDef &pdef = mob_types[ MOB_PLAYER ];
            const MobModelDef &pmodel = mob_models[ pdef.model ];
            int head_i = -1;
            for ( int i = 0; i < pmodel.bone_count; i++ ) {
                if ( strcmp( pmodel.bones[ i ].name, pdef.bone_head ) == 0 ) {
                    head_i = i;
                }
            }
            check_bool( "remote sneak: head bone found", head_i >= 0 );
            glm::mat4 w0[ 32 ], w1[ 32 ];
            MobManager::bone_worlds( stand_mob, pdef, 1.0f, w0 );
            MobManager::bone_worlds( sneak_mob, pdef, 1.0f, w1 );
            float rot_diff = 0.0f;
            for ( int c = 0; c < 3; c++ ) {
                for ( int r = 0; r < 3; r++ ) {
                    rot_diff = fmaxf( rot_diff, fabsf( w1[ head_i ][ c ][ r ] - w0[ head_i ][ c ][ r ] ) );
                }
            }
            check_near( "remote sneak: head orientation unchanged", rot_diff, 0.0f, 1e-5f );
            const glm::vec3 dt = glm::vec3( w1[ head_i ][ 3 ] - w0[ head_i ][ 3 ] );
            check_bool( "remote sneak: head drops 2.5px", fabsf( dt.x ) < 1e-4f && fabsf( dt.z ) < 1e-4f && fabsf( dt.y + 2.5f ) < 1e-4f );
        }

        // Standing again: sneak eases back to 0 and feet stay put.
        mobs.set_remote_pose( uid, glm::dvec3( 10.5, ground + PLAYER_EYE_HEIGHT, 10.5 ), 0.0f, 0.0f, PLAYER_EYE_HEIGHT );
        for ( int i = 0; i < 60; i++ ) {
            mobs.tick( world, player_pos );
        }
        check_bool( "remote sneak: sneak converges to 0", mobs.mobs[ 0 ].sneak < 0.05f );
        check_near( "remote sneak: feet stay on ground", mobs.mobs[ 0 ].pos.y, ground, 1e-3 );

        // Garbage eye heights can't produce NaN feet.
        mobs.set_remote_pose( uid, glm::dvec3( 10.5, ground + 1.0, 10.5 ), 0.0f, 0.0f, NAN );
        mobs.tick( world, player_pos );
        check_bool( "remote sneak: NaN eye height falls back", std::isfinite( mobs.mobs[ 0 ].pos.y ) && mobs.mobs[ 0 ].sneak_target == 0.0f );
        mobs.set_remote_pose( uid, glm::dvec3( 10.5, ground + 9.0, 10.5 ), 0.0f, 0.0f, 9.0f );
        mobs.tick( world, player_pos );
        check_bool( "remote sneak: out-of-range eye height falls back", std::isfinite( mobs.mobs[ 0 ].pos.y ) && mobs.mobs[ 0 ].sneak_target == 0.0f );
    }

    // --- 12: every named bone in every MobTypeDef exists in its model -------
    {
        bool all_ok = true;
        for ( int t = 0; t < MOB_TYPE_COUNT; t++ ) {
            const MobTypeDef &def = mob_types[ t ];
            const MobModelDef &model = mob_models[ def.model ];
            auto has_bone = [ & ]( const char *name ) {
                for ( int i = 0; i < model.bone_count; i++ ) {
                    if ( strcmp( model.bones[ i ].name, name ) == 0 ) {
                        return true;
                    }
                }
                return false;
            };
            auto check_one = [ & ]( const char *kind, const char *name ) {
                if ( name != NULL && !has_bone( name ) ) {
                    pr_test( "    %s: %s bone '%s' missing", def.name, kind, name );
                    all_ok = false;
                }
            };
            check_one( "head", def.bone_head );
            check_one( "body", def.bone_body );
            for ( int i = 0; i < 4; i++ ) {
                check_one( "leg", def.bone_legs[ i ] );
            }
            for ( int i = 0; i < 2; i++ ) {
                check_one( "arm", def.bone_arms[ i ] );
                check_one( "wing", def.bone_wings[ i ] );
            }
        }
        check_bool( "types: all def bone names exist in models", all_ok );
    }

    // --- 13: every new non-remote type spawns and settles at floor+1 --------
    {
        const MobType new_types[] = { MOB_COW, MOB_CHICKEN, MOB_SHEEP, MOB_ZOMBIE, MOB_CREEPER };
        for ( MobType t : new_types ) {
            MobManager mobs;
            mobs.spawner_enabled = false;
            char name[ 64 ];
            snprintf( name, sizeof( name ), "%s: settles standing at floor+1", mob_types[ t ].name );
            check_bool( name, mobs.spawn( t, glm::dvec3( 10.5, 14.0, 10.5 ), 0.0f, 1 ) );
            for ( int i = 0; i < 300 && !mobs.mobs[ 0 ].standing; i++ ) {
                mobs.tick( world, player_pos );
            }
            snprintf( name, sizeof( name ), "%s: standing", mob_types[ t ].name );
            check_bool( name, mobs.mobs[ 0 ].standing == 1 && fabs( mobs.mobs[ 0 ].pos.y - 11.0 ) < 0.01 );
        }
    }

    // --- 14: the chicken's fall cap glides it down ---------------------------
    {
        MobManager mobs;
        mobs.spawner_enabled = false;
        mobs.spawn( MOB_CHICKEN, glm::dvec3( 10.5, 16.0, 10.5 ), 0.0f, 1 );
        double min_vy = 0.0;
        for ( int i = 0; i < 500 && !mobs.mobs[ 0 ].standing; i++ ) {
            const double y0 = mobs.mobs[ 0 ].pos.y;
            mobs.tick( world, player_pos );
            min_vy = fmin( min_vy, mobs.mobs[ 0 ].pos.y - y0 );
        }
        check_bool( "chicken: never falls faster than fall_cap", min_vy >= -mob_types[ MOB_CHICKEN ].fall_cap - 1e-6 && mobs.mobs[ 0 ].standing == 1 );
    }

    // --- 15: hostile helpers ----------------------------------------------
    {
        check_bool( "burn: noon + open sky", mob_burns_in_daylight( 1.0f, 1.0f ) );
        check_bool( "burn: daylight > 0.5 + sky >= 0.95", mob_burns_in_daylight( 0.51f, 0.95f ) );
        check_bool( "burn: shaded head does not", !mob_burns_in_daylight( 1.0f, 0.94f ) );
        check_bool( "burn: dusk does not", !mob_burns_in_daylight( 0.5f, 1.0f ) );
        check_bool( "burn: night does not", !mob_burns_in_daylight( 0.0f, 1.0f ) );
        check_bool( "spawn light: dark night cell ok", hostile_spawn_light_ok( 1.0f, 0.0f, 0.0f ) );
        check_bool( "spawn light: moonlit sky*daylight low ok", hostile_spawn_light_ok( 1.0f, 0.0f, 0.2f ) );
        check_bool( "spawn light: torch-lit cell fails", !hostile_spawn_light_ok( 0.0f, 0.5f, 0.0f ) );
        check_bool( "spawn light: day sky fails", !hostile_spawn_light_ok( 1.0f, 0.0f, 1.0f ) );
        check_bool( "spawn light: exactly 7/15 fails", !hostile_spawn_light_ok( 7.0f / 15.0f, 0.0f, 1.0f ) );
    }

    // --- 16: zombie chase + melee hit ---------------------------------------
    {
        const long midnight = 3 * DAY_LENGTH_TICKS / 4; // daylight 0: no burning
        MobManager mobs;
        mobs.spawner_enabled = false;
        mobs.spawn( MOB_ZOMBIE, glm::dvec3( 10.5, 11.0, 10.5 ), 0.0f, 1 );
        mobs.mobs[ 0 ].ai_state = 0;
        mobs.mobs[ 0 ].ai_ticks = 100000; // pin the wander fallback
        const glm::dvec3 pfeet( 16.5, 11.0, 10.5 );
        const glm::dvec3 ppos = pfeet + glm::dvec3( 0.0, PLAYER_EYE_HEIGHT, 0.0 );
        MobTickInput in = { true, midnight, PLAYER_EYE_HEIGHT };
        MobTickOutput out;
        double d0 = fabs( mobs.mobs[ 0 ].pos.x - pfeet.x );
        int hit_tick = -1;
        for ( int i = 0; i < 3 * UPS_RATE; i++ ) {
            mobs.tick( world, ppos, &in, &out );
            if ( !out.player_hits.empty( ) && hit_tick < 0 ) {
                hit_tick = i;
                break;
            }
        }
        check_bool( "zombie: targeting acquired", mobs.mobs[ 0 ].targeting == 1 );
        check_bool( "zombie: closed the distance", fabs( mobs.mobs[ 0 ].pos.x - pfeet.x ) < d0 - 1.0 );
        check_bool( "zombie: melee hit within ~3 s", hit_tick >= 0 && !out.player_hits.empty() && out.player_hits[ 0 ].damage == mob_types[ MOB_ZOMBIE ].attack_damage );

        // A non-targetable player: no approach, no hits.
        MobManager mobs2;
        mobs2.spawner_enabled = false;
        mobs2.spawn( MOB_ZOMBIE, glm::dvec3( 10.5, 11.0, 10.5 ), 0.0f, 1 );
        mobs2.mobs[ 0 ].ai_state = 0;
        mobs2.mobs[ 0 ].ai_ticks = 100000;
        MobTickInput in2 = { false, midnight, PLAYER_EYE_HEIGHT };
        MobTickOutput out2;
        const double x0 = mobs2.mobs[ 0 ].pos.x;
        for ( int i = 0; i < 3 * UPS_RATE; i++ ) {
            mobs2.tick( world, ppos, &in2, &out2 );
        }
        check_bool( "zombie: untargetable gives no hits", out2.player_hits.empty( ) );
        check_bool( "zombie: untargetable never approaches", fabs( mobs2.mobs[ 0 ].pos.x - x0 ) < 1.0 );
    }

    // --- 17: creeper fuse -> explosion; defuse on retreat --------------------
    {
        const long midnight = 3 * DAY_LENGTH_TICKS / 4;
        MobManager mobs;
        mobs.spawner_enabled = false;
        mobs.spawn( MOB_CREEPER, glm::dvec3( 10.5, 11.0, 10.5 ), 0.0f, 1 );
        mobs.mobs[ 0 ].ai_state = 0;
        mobs.mobs[ 0 ].ai_ticks = 100000;
        const glm::dvec3 ppos = glm::dvec3( 12.0, 11.0, 10.5 ) + glm::dvec3( 0.0, PLAYER_EYE_HEIGHT, 0.0 );
        MobTickInput in = { true, midnight, PLAYER_EYE_HEIGHT };
        MobTickOutput out;
        int fuse_seen = -1, boom_tick = -1;
        for ( int i = 0; i < 4 * UPS_RATE; i++ ) {
            mobs.tick( world, ppos, &in, &out );
            if ( !mobs.mobs.empty( ) && mobs.mobs[ 0 ].fuse_ticks > 0 && fuse_seen < 0 ) {
                fuse_seen = i;
            }
            if ( !out.explosions.empty( ) ) {
                boom_tick = i;
                break;
            }
        }
        check_bool( "creeper: fuse lit near the player", fuse_seen >= 0 );
        check_bool( "creeper: explodes after ~1.5 s", boom_tick - fuse_seen >= ( int ) ( 1.5f * UPS_RATE ) - 2 && !out.explosions.empty( ) );
        check_bool( "creeper: removed by the explosion", mobs.mobs.empty( ) );

        // Walking away mid-fuse: the fuse counts back down, no explosion.
        MobManager mobs2;
        mobs2.spawner_enabled = false;
        mobs2.spawn( MOB_CREEPER, glm::dvec3( 10.5, 11.0, 10.5 ), 0.0f, 1 );
        mobs2.mobs[ 0 ].ai_state = 0;
        mobs2.mobs[ 0 ].ai_ticks = 100000;
        MobTickOutput out2;
        glm::dvec3 ppos2 = ppos;
        for ( int i = 0; i < 30; i++ ) {
            mobs2.tick( world, ppos2, &in, &out2 );
        }
        check_bool( "creeper: fuse started", !mobs2.mobs.empty( ) && mobs2.mobs[ 0 ].fuse_ticks > 0 );
        ppos2 += glm::dvec3( 10.0, 0.0, 0.0 ); // 10+ blocks away mid-fuse
        for ( int i = 0; i < 300; i++ ) {
            mobs2.tick( world, ppos2, &in, &out2 );
        }
        check_bool( "creeper: defused past 7 blocks", out2.explosions.empty( ) && !mobs2.mobs.empty( ) && mobs2.mobs[ 0 ].fuse_ticks == 0 );
    }

    // --- 18: despawnable mobs are never persisted ----------------------------
    {
        const char *orig_home = getenv( "HOME" );
        char orig_home_buf[ 512 ] = { 0 };
        if ( orig_home != nullptr ) {
            strncpy( orig_home_buf, orig_home, sizeof( orig_home_buf ) - 1 );
        }
        setenv( "HOME", "/tmp/repgame_test_mobs_home", 1 );
        MapStorage storage;
        storage.init( "TestMobs" );
        MobManager mobs;
        mobs.init_storage( &storage );
        // A fresh chunk (x 186..247) so earlier sections' files can't leak in:
        // the despawnable zombie writes nothing, the persistent pig does.
        mobs.owned_chunks.push_back( { glm::ivec3( 3, 0, 0 ), 0, 0 } );
        mobs.spawn( MOB_ZOMBIE, glm::dvec3( 190.5, 11.0, 10.5 ), 0.0f, 1 );
        mobs.mobs[ 0 ].despawnable = 1;
        mobs.spawn( MOB_PIG, glm::dvec3( 190.5, 11.0, 10.5 ), 0.0f, 1 );
        mobs.persist_all( );
        std::vector<MobFileRecord> out;
        check_bool( "despawnable: only the persistent mob is written",
                    storage.load_mobs( glm::ivec3( 3, 0, 0 ), out ) == 1 && out.size( ) == 1 && out[ 0 ].type == MOB_PIG );
        if ( orig_home != nullptr ) {
            setenv( "HOME", orig_home_buf, 1 );
        } else {
            unsetenv( "HOME" );
        }
    }

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

// Crafting recipe matching (src/common/crafting.cpp + generated
// recipes_gen.inc): shaped bounding-box semantics with mirroring, shapeless
// backtracking assignment, and the starter recipe table.
// Round-trip every entity-sync payload (protocol v5): serialize then
// deserialize must reproduce the input, and a truncated buffer must fail.
int test_net_entities( ) {
    int failures = 0;
    const auto report = [ &failures ]( const char *name, bool ok ) {
        pr_test( "  %-55s %s", name, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };

    { // WELCOME
        NetWelcomePayload out{ 7 };
        PacketWriter w;
        net_serialize_welcome( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetWelcomePayload in{ 0 };
        report( "net entities: WELCOME round trip", net_deserialize_welcome( r, in ) && in.session_id == 7 );
    }
    { // VIEW
        NetViewPayload out{ -3, 2, 40, 5, 2 };
        PacketWriter w;
        net_serialize_view( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetViewPayload in{ 0, 0, 0, 0, 0 };
        report( "net entities: VIEW round trip", net_deserialize_view( r, in ) && in.cx == -3 && in.cy == 2 && in.cz == 40 && in.radius_xz == 5 && in.radius_y == 2 );
    }

    // One fully-populated record reused by the record-bearing messages.
    NetEntityRecord rec;
    rec.id = ( static_cast<NetEntityId>( 3 ) << 32 ) | 17;
    rec.kind = NetEntityKind::BODY;
    rec.x = -12.5;
    rec.y = 70.25;
    rec.z = 1e9 + 0.5;
    rec.vx = 0.1f;
    rec.vy = -0.2f;
    rec.vz = 0.3f;
    rec.yaw = 1.25f;
    rec.type = 2;
    rec.count = -4;
    rec.health = 13.5f;
    rec.age_ticks = 12345;
    rec.pickup_delay = 8;
    rec.flags = 0xdeadbeef;
    rec.block_state = { TNT, 3, 7, TNT, 1 };
    rec.fuse_ticks = 60;
    const auto record_eq = [ & ]( const NetEntityRecord &a, const NetEntityRecord &b ) {
        return a.id == b.id && a.kind == b.kind && a.x == b.x && a.y == b.y && a.z == b.z &&             //
               a.vx == b.vx && a.vy == b.vy && a.vz == b.vz && a.yaw == b.yaw && a.type == b.type &&   //
               a.count == b.count && a.health == b.health && a.age_ticks == b.age_ticks &&           //
               a.pickup_delay == b.pickup_delay && a.flags == b.flags && a.fuse_ticks == b.fuse_ticks && //
               BlockStates_equal( a.block_state, b.block_state );
    };
    { // ENTITY_SPAWN / record round trip
        PacketWriter w;
        net_write_entity_record( w, rec );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetEntityRecord in;
        report( "net entities: ENTITY_SPAWN record round trip", net_read_entity_record( r, in ) && record_eq( rec, in ) );
        PacketReader rt( w.buf( ).data( ), w.buf( ).size( ) - 1 );
        NetEntityRecord junk;
        report( "net entities: record truncated fails", !net_read_entity_record( rt, junk ) );
    }
    { // CHUNK_ROLE with two records
        NetChunkRolePayload out;
        out.cx = 1;
        out.cy = -2;
        out.cz = 3;
        out.role = NET_ROLE_OWNER;
        out.populated = 1;
        out.records.push_back( rec );
        NetEntityRecord rec2 = rec;
        rec2.id = 99;
        rec2.kind = NetEntityKind::ITEM;
        out.records.push_back( rec2 );
        PacketWriter w;
        net_serialize_chunk_role( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetChunkRolePayload in;
        report( "net entities: CHUNK_ROLE round trip",
                net_deserialize_chunk_role( r, in ) && in.cx == 1 && in.cy == -2 && in.cz == 3 && in.role == NET_ROLE_OWNER && in.populated == 1 &&
                    in.records.size( ) == 2 && record_eq( in.records[ 0 ], rec ) && record_eq( in.records[ 1 ], rec2 ) );
        PacketReader rt( w.buf( ).data( ), w.buf( ).size( ) - 1 );
        NetChunkRolePayload junk;
        report( "net entities: CHUNK_ROLE truncated fails", !net_deserialize_chunk_role( rt, junk ) );
        // Over-cap list is rejected.
        PacketWriter w2;
        w2.write_i32( 0 );
        w2.write_i32( 0 );
        w2.write_i32( 0 );
        w2.write_u8( NET_ROLE_VIEWER );
        w2.write_u8( 0 );
        w2.write_u32( NET_ENTITY_MAX_LIST + 1 );
        PacketReader r2( w2.buf( ).data( ), w2.buf( ).size( ) );
        NetChunkRolePayload junk2;
        report( "net entities: CHUNK_ROLE over-cap fails", !net_deserialize_chunk_role( r2, junk2 ) );
    }
    { // ENTITY_STATE batch
        NetEntityStatePayload out;
        NetEntityStateEntry e;
        e.id = 42;
        e.kind = static_cast<uint8_t>( NetEntityKind::ITEM );
        e.x = 1.5;
        e.y = -2.5;
        e.z = 3.5;
        e.vx = 0.01f;
        e.vy = -0.02f;
        e.vz = 0.03f;
        e.yaw = 0.5f;
        e.count = 7;
        e.health = 4.5f;
        e.flags = 9;
        out.entries.push_back( e );
        out.entries.push_back( e );
        out.entries[ 1 ].id = 43;
        PacketWriter w;
        net_serialize_entity_state( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetEntityStatePayload in;
        const NetEntityStateEntry &g = in.entries.size( ) == 2 ? in.entries[ 0 ] : e;
        report( "net entities: ENTITY_STATE round trip",
                net_deserialize_entity_state( r, in ) && in.entries.size( ) == 2 && g.id == 42 && g.kind == static_cast<uint8_t>( NetEntityKind::ITEM ) &&
                    g.x == 1.5 && g.y == -2.5 && g.z == 3.5 && g.vx == 0.01f && g.vy == -0.02f && g.vz == 0.03f && g.yaw == 0.5f && g.count == 7 && g.health == 4.5f && g.flags == 9 );
        PacketReader rt( w.buf( ).data( ), w.buf( ).size( ) - 1 );
        NetEntityStatePayload junk;
        report( "net entities: ENTITY_STATE truncated fails", !net_deserialize_entity_state( rt, junk ) );
    }
    { // ENTITY_REMOVE
        NetEntityRemovePayload out{ 0xABCDEF0123456789ull, NET_REMOVE_MERGED };
        PacketWriter w;
        net_serialize_entity_remove( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetEntityRemovePayload in{ 0, 0 };
        report( "net entities: ENTITY_REMOVE round trip", net_deserialize_entity_remove( r, in ) && in.id == out.id && in.reason == NET_REMOVE_MERGED );
    }
    { // ENTITY_TRANSFER
        NetEntityTransferPayload out{ rec, 1 };
        PacketWriter w;
        net_serialize_entity_transfer( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetEntityTransferPayload in;
        report( "net entities: ENTITY_TRANSFER round trip", net_deserialize_entity_transfer( r, in ) && in.adopt == 1 && record_eq( in.record, rec ) );
    }
    { // ENTITY_ACTION
        NetEntityActionPayload out{ 77, NET_ACTION_HIT, 4.5f, 1.0, 2.0, 3.0, 1 };
        PacketWriter w;
        net_serialize_entity_action( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetEntityActionPayload in{ 0, 0, 0.0f, 0.0, 0.0, 0.0, 0 };
        report( "net entities: ENTITY_ACTION round trip",
                net_deserialize_entity_action( r, in ) && in.id == 77 && in.action == NET_ACTION_HIT && in.damage == 4.5f && in.ax == 1.0 && in.ay == 2.0 && in.az == 3.0 && in.attacker_survival == 1 );
    }
    { // PICKUP_REQUEST / PICKUP_RESULT
        NetPickupRequestPayload out{ 55, 12 };
        PacketWriter w;
        net_serialize_pickup_request( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetPickupRequestPayload in{ 0, 0 };
        report( "net entities: PICKUP_REQUEST round trip", net_deserialize_pickup_request( r, in ) && in.id == 55 && in.room == 12 );

        NetPickupResultPayload out2{ 55, STICK, 12 };
        PacketWriter w2;
        net_serialize_pickup_result( w2, out2 );
        PacketReader r2( w2.buf( ).data( ), w2.buf( ).size( ) );
        NetPickupResultPayload in2{ 0, 0, 0 };
        report( "net entities: PICKUP_RESULT round trip", net_deserialize_pickup_result( r2, in2 ) && in2.id == 55 && in2.item == STICK && in2.granted == 12 );
    }
    { // PLAYER_HIT
        NetPlayerHitPayload out{ 6, -1.5, 2.5, -3.5, 3.25f };
        PacketWriter w;
        net_serialize_player_hit( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetPlayerHitPayload in{ 0, 0.0, 0.0, 0.0, 0.0f };
        report( "net entities: PLAYER_HIT round trip",
                net_deserialize_player_hit( r, in ) && in.target_player_id == 6 && in.fx == -1.5 && in.fy == 2.5 && in.fz == -3.5 && in.damage == 3.25f );
    }
    { // EXPLOSION
        NetExplosionPayload out{ 10.5, 64.0, -20.25, 4.0f };
        PacketWriter w;
        net_serialize_explosion( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetExplosionPayload in{ 0.0, 0.0, 0.0, 0.0f };
        report( "net entities: EXPLOSION round trip", net_deserialize_explosion( r, in ) && in.x == 10.5 && in.y == 64.0 && in.z == -20.25 && in.radius == 4.0f );
    }
    { // NetPlayerPayload flags survive.
        NetPlayerPayload out;
        memset( &out, 0, sizeof( out ) );
        out.flags = NET_PLAYER_FLAG_TARGETABLE;
        PacketWriter w;
        net_serialize_player( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetPlayerPayload in;
        memset( &in, 0, sizeof( in ) );
        report( "net entities: player flags round trip", net_deserialize_player( r, in ) && in.flags == NET_PLAYER_FLAG_TARGETABLE );
    }
    { // CHUNK_POPULATED
        NetChunkPopulatedPayload out{ -3, 0, 7 };
        PacketWriter w;
        net_serialize_chunk_populated( w, out );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetChunkPopulatedPayload in{ 0, 0, 0 };
        report( "net entities: CHUNK_POPULATED round trip", net_deserialize_chunk_populated( r, in ) && in.cx == -3 && in.cy == 0 && in.cz == 7 );
        PacketReader rt( w.buf( ).data( ), w.buf( ).size( ) - 1 );
        NetChunkPopulatedPayload junk{ 0, 0, 0 };
        report( "net entities: CHUNK_POPULATED truncated rejects", !net_deserialize_chunk_populated( rt, junk ) );
    }
    return failures;
}

int test_crafting( ) {
    int failures = 0;
    const auto report = [ &failures ]( const char *name, bool ok ) {
        pr_test( "  %-55s %s", name, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };
    const auto expect = [ &report ]( const char *name, const ItemID *grid, int dim, ItemID want, int want_count ) {
        const int idx = crafting_find_recipe( grid, dim );
        report( name, idx >= 0 && CRAFT_RECIPES[ idx ].result == want && CRAFT_RECIPES[ idx ].count == want_count );
    };
    const auto expect_none = [ &report ]( const char *name, const ItemID *grid, int dim ) {
        report( name, crafting_find_recipe( grid, dim ) < 0 );
    };

    // Table sanity: non-empty, every result is a real item.
    bool sane = CRAFT_RECIPE_COUNT > 0;
    for ( int i = 0; i < CRAFT_RECIPE_COUNT; i++ ) {
        sane = sane && item_id_valid( CRAFT_RECIPES[ i ].result );
    }
    report( "recipe table non-empty, results valid", sane );

    // Log -> planks: a single OAK_LOG in any cell crafts OAK_PLANKS x4, in
    // both the 2x2 and 3x3 grids. STRIPPED_OAK_LOG works too.
    for ( int dim = 2; dim <= 3; dim++ ) {
        for ( int i = 0; i < dim * dim; i++ ) {
            ItemID grid[ 9 ];
            for ( ItemID &g : grid ) {
                g = LAST_BLOCK_ID;
            }
            grid[ i ] = OAK_LOG;
            char name[ 80 ];
            snprintf( name, sizeof( name ), "OAK_LOG cell %d in %dx%d -> OAK_PLANKS x4", i, dim, dim );
            expect( name, grid, dim, OAK_PLANKS, 4 );
        }
        ItemID grid[ 9 ];
        for ( ItemID &g : grid ) {
            g = LAST_BLOCK_ID;
        }
        grid[ 0 ] = STRIPPED_OAK_LOG;
        char name[ 80 ];
        snprintf( name, sizeof( name ), "STRIPPED_OAK_LOG in %dx%d -> OAK_PLANKS x4", dim, dim );
        expect( name, grid, dim, OAK_PLANKS, 4 );
    }

    // Sticks: a vertical plank pair. Left column in 2x2, right column in
    // 3x3, and mixed wood types all match; a horizontal pair does not.
    {
        ItemID grid[ 4 ] = { OAK_PLANKS, LAST_BLOCK_ID, OAK_PLANKS, LAST_BLOCK_ID };
        expect( "sticks, left column 2x2", grid, 2, STICK, 4 );
    }
    {
        ItemID grid[ 9 ] = { LAST_BLOCK_ID, LAST_BLOCK_ID, OAK_PLANKS,
                             LAST_BLOCK_ID, LAST_BLOCK_ID, SPRUCE_PLANKS,
                             LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID };
        expect( "sticks, right column 3x3 (mixed planks)", grid, 3, STICK, 4 );
    }
    {
        ItemID grid[ 4 ] = { OAK_PLANKS, OAK_PLANKS, LAST_BLOCK_ID, LAST_BLOCK_ID };
        expect_none( "sticks, horizontal pair 2x2 -> no match", grid, 2 );
    }

    // Crafting table: a mixed-planks 2x2 in the 2x2 grid, and an offset
    // 2x2 block in the 3x3 grid.
    {
        ItemID grid[ 4 ] = { OAK_PLANKS, SPRUCE_PLANKS, BIRCH_PLANKS, OAK_PLANKS };
        expect( "crafting table, mixed planks 2x2", grid, 2, CRAFTING_TABLE, 1 );
    }
    {
        ItemID grid[ 9 ] = { LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID,
                             LAST_BLOCK_ID, OAK_PLANKS, SPRUCE_PLANKS,
                             LAST_BLOCK_ID, BIRCH_PLANKS, OAK_PLANKS };
        expect( "crafting table, offset 2x2 in 3x3", grid, 3, CRAFTING_TABLE, 1 );
    }

    // Chest: the 3x3 ring matches (result is the container block), a filled
    // centre does not, and the ring can't fit the 2x2 grid.
    {
        ItemID ring[ 9 ] = { OAK_PLANKS, OAK_PLANKS, OAK_PLANKS,
                             OAK_PLANKS, LAST_BLOCK_ID, OAK_PLANKS,
                             OAK_PLANKS, OAK_PLANKS, OAK_PLANKS };
        expect( "chest ring in 3x3 -> SINGLE_CHEST_LATCH", ring, 3, SINGLE_CHEST_LATCH, 1 );
        ItemID filled[ 9 ] = { OAK_PLANKS, OAK_PLANKS, OAK_PLANKS,
                               OAK_PLANKS, OAK_PLANKS, OAK_PLANKS,
                               OAK_PLANKS, OAK_PLANKS, OAK_PLANKS };
        expect_none( "chest ring with filled centre -> no match", filled, 3 );
        // A 3x3 recipe can never match a 2x2 grid (the bounding box can't
        // exceed the grid).
        int chest_idx = -1;
        for ( int i = 0; i < CRAFT_RECIPE_COUNT; i++ ) {
            if ( CRAFT_RECIPES[ i ].result == SINGLE_CHEST_LATCH ) {
                chest_idx = i;
            }
        }
        report( "chest recipe exists in the table", chest_idx >= 0 );
        ItemID full2[ 4 ] = { OAK_PLANKS, OAK_PLANKS, OAK_PLANKS, OAK_PLANKS };
        report( "chest recipe can't match in 2x2", chest_idx < 0 || !crafting_recipe_matches( CRAFT_RECIPES[ chest_idx ], full2, 2 ) );
    }

    // Torch: COAL over STICK gives TORCH x4; reversed does not.
    {
        ItemID grid[ 9 ] = { LAST_BLOCK_ID, COAL, LAST_BLOCK_ID,
                             LAST_BLOCK_ID, STICK, LAST_BLOCK_ID,
                             LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID };
        expect( "torch, COAL over STICK", grid, 3, TORCH, 4 );
        ItemID reversed[ 9 ] = { LAST_BLOCK_ID, STICK, LAST_BLOCK_ID,
                                 LAST_BLOCK_ID, COAL, LAST_BLOCK_ID,
                                 LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID };
        expect_none( "torch, STICK over COAL -> no match", reversed, 3 );
    }

    // Mirrored shaped matching: an asymmetric recipe (top-left empty)
    // matches a grid holding the mirrored layout.
    {
        const ItemID opts[] = { OAK_PLANKS };
        CraftRecipe asym = {};
        asym.shaped = 1;
        asym.width = 2;
        asym.height = 2;
        asym.cells[ 0 ] = { nullptr, 0 };   // top-left empty
        asym.cells[ 1 ] = { opts, 1 };      // top-right
        asym.cells[ 2 ] = { opts, 1 };      // bottom-left
        asym.cells[ 3 ] = { opts, 1 };      // bottom-right
        asym.result = TORCH;
        asym.count = 1;
        ItemID plain[ 9 ] = { LAST_BLOCK_ID, OAK_PLANKS, LAST_BLOCK_ID,
                              OAK_PLANKS, OAK_PLANKS, LAST_BLOCK_ID,
                              LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID };
        report( "synthetic shaped, layout as written", crafting_recipe_matches( asym, plain, 3 ) );
        ItemID mirrored[ 9 ] = { OAK_PLANKS, LAST_BLOCK_ID, LAST_BLOCK_ID,
                                 OAK_PLANKS, OAK_PLANKS, LAST_BLOCK_ID,
                                 LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID };
        report( "synthetic shaped, mirrored layout", crafting_recipe_matches( asym, mirrored, 3 ) );
    }

    // Shapeless backtracking: ingredient options [OAK,SPRUCE] then [OAK].
    // The grid lists OAK first, so a greedy assignment would feed OAK to
    // the wide option and strand SPRUCE; backtracking succeeds.
    {
        const ItemID both[] = { OAK_PLANKS, SPRUCE_PLANKS };
        const ItemID oak_only[] = { OAK_PLANKS };
        CraftRecipe shapeless = {};
        shapeless.shaped = 0;
        shapeless.cells[ 0 ] = { both, 2 };
        shapeless.cells[ 1 ] = { oak_only, 1 };
        shapeless.result = TORCH;
        shapeless.count = 1;
        ItemID grid[ 4 ] = { OAK_PLANKS, SPRUCE_PLANKS, LAST_BLOCK_ID, LAST_BLOCK_ID };
        report( "synthetic shapeless, backtracking assignment", crafting_recipe_matches( shapeless, grid, 2 ) );
    }

    // Empty grid matches nothing.
    {
        ItemID grid[ 9 ];
        for ( ItemID &g : grid ) {
            g = LAST_BLOCK_ID;
        }
        expect_none( "empty grid -> -1", grid, 3 );
    }

    // Slabs: a full row of planks. Bottom row of the 3x3 works; the 1x3
    // recipe can't fit the 2x2 grid.
    {
        ItemID grid[ 9 ] = { LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID,
                             LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID,
                             OAK_PLANKS, OAK_PLANKS, OAK_PLANKS };
        expect( "slab, bottom row of 3x3", grid, 3, OAK_SLAB, 6 );
        int slab_idx = -1;
        for ( int i = 0; i < CRAFT_RECIPE_COUNT; i++ ) {
            if ( CRAFT_RECIPES[ i ].result == OAK_SLAB ) {
                slab_idx = i;
            }
        }
        report( "slab recipe exists in the table", slab_idx >= 0 );
        ItemID full2[ 4 ] = { OAK_PLANKS, OAK_PLANKS, OAK_PLANKS, OAK_PLANKS };
        report( "slab recipe can't match in 2x2", slab_idx < 0 || !crafting_recipe_matches( CRAFT_RECIPES[ slab_idx ], full2, 2 ) );
    }

    // Stairs: the L-shape in cobblestone gives STONE_STAIRS, mirrored too.
    {
        ItemID grid[ 9 ] = { COBBLESTONE, LAST_BLOCK_ID, LAST_BLOCK_ID,
                             COBBLESTONE, COBBLESTONE, LAST_BLOCK_ID,
                             COBBLESTONE, COBBLESTONE, COBBLESTONE };
        expect( "stairs, cobblestone L", grid, 3, STONE_STAIRS, 4 );
        ItemID mirrored[ 9 ] = { LAST_BLOCK_ID, LAST_BLOCK_ID, COBBLESTONE,
                                 LAST_BLOCK_ID, COBBLESTONE, COBBLESTONE,
                                 COBBLESTONE, COBBLESTONE, COBBLESTONE };
        expect( "stairs, cobblestone L mirrored", mirrored, 3, STONE_STAIRS, 4 );
    }

    // Tools: mixed plank types still match via the #planks tag; a stone
    // sword accepts either stone in an off-centre column; diamond shovel.
    {
        ItemID grid[ 9 ] = { OAK_PLANKS, BIRCH_PLANKS, SPRUCE_PLANKS,
                             LAST_BLOCK_ID, STICK, LAST_BLOCK_ID,
                             LAST_BLOCK_ID, STICK, LAST_BLOCK_ID };
        expect( "wooden pickaxe, mixed planks", grid, 3, WOODEN_PICKAXE, 1 );
    }
    {
        ItemID grid[ 9 ] = { LAST_BLOCK_ID, COBBLED_DEEPSLATE, LAST_BLOCK_ID,
                             LAST_BLOCK_ID, COBBLED_DEEPSLATE, LAST_BLOCK_ID,
                             LAST_BLOCK_ID, STICK, LAST_BLOCK_ID };
        expect( "stone sword, off-centre column", grid, 3, STONE_SWORD, 1 );
    }
    {
        ItemID grid[ 9 ] = { DIAMOND, LAST_BLOCK_ID, LAST_BLOCK_ID,
                             STICK, LAST_BLOCK_ID, LAST_BLOCK_ID,
                             STICK, LAST_BLOCK_ID, LAST_BLOCK_ID };
        expect( "diamond shovel", grid, 3, DIAMOND_SHOVEL, 1 );
    }
    {
        // Axe ["##","#S"," S"] with mixed planks; the mirrored layout with
        // cobblestone gives the stone axe, not something else.
        ItemID grid[ 9 ] = { OAK_PLANKS, BIRCH_PLANKS, LAST_BLOCK_ID,
                             SPRUCE_PLANKS, STICK, LAST_BLOCK_ID,
                             LAST_BLOCK_ID, STICK, LAST_BLOCK_ID };
        expect( "wooden axe, mixed planks", grid, 3, WOODEN_AXE, 1 );
        ItemID mirrored[ 9 ] = { COBBLESTONE, COBBLESTONE, LAST_BLOCK_ID,
                                 STICK, COBBLESTONE, LAST_BLOCK_ID,
                                 STICK, LAST_BLOCK_ID, LAST_BLOCK_ID };
        expect( "stone axe, mirrored", mirrored, 3, STONE_AXE, 1 );
    }
    {
        // Hoe ["##"," S"," S"] in the right two columns.
        ItemID grid[ 9 ] = { LAST_BLOCK_ID, DIAMOND, DIAMOND,
                             LAST_BLOCK_ID, LAST_BLOCK_ID, STICK,
                             LAST_BLOCK_ID, LAST_BLOCK_ID, STICK };
        expect( "diamond hoe, right columns", grid, 3, DIAMOND_HOE, 1 );
    }
    report( "tools stack to 1", item_max_stack( WOODEN_PICKAXE ) == 1 && item_max_stack( DIAMOND_SWORD ) == 1 );

    // Drag crafting: distribute a held stack over the dragged slots.
    {
        InventorySlot slots[ 9 ];
        for ( InventorySlot &s : slots ) {
            s.item_id = LAST_BLOCK_ID;
            s.quantity = 0;
        }
        InventorySlot held = { OAK_PLANKS, 64 };
        const int targets[] = { 0, 1, 2 };
        crafting_drag_distribute( slots, targets, 3, held, false );
        report( "drag split 64 over 3 slots", slots[ 0 ].quantity == 21 && slots[ 1 ].quantity == 21 && slots[ 2 ].quantity == 21 && held.quantity == 1 );
    }
    {
        InventorySlot slots[ 9 ];
        for ( InventorySlot &s : slots ) {
            s.item_id = LAST_BLOCK_ID;
            s.quantity = 0;
        }
        slots[ 0 ].item_id = OAK_PLANKS;
        slots[ 0 ].quantity = 60;
        InventorySlot held = { OAK_PLANKS, 10 };
        const int targets[] = { 0, 1 };
        crafting_drag_distribute( slots, targets, 2, held, false );
        report( "drag split capped by slot room", slots[ 0 ].quantity == 64 && slots[ 1 ].quantity == 5 && held.quantity == 1 );
    }
    {
        InventorySlot slots[ 9 ];
        for ( InventorySlot &s : slots ) {
            s.item_id = LAST_BLOCK_ID;
            s.quantity = 0;
        }
        InventorySlot held = { OAK_PLANKS, 2 };
        const int targets[] = { 0, 1 };
        crafting_drag_distribute( slots, targets, 2, held, true );
        report( "right-drag one per slot", slots[ 0 ].quantity == 1 && slots[ 1 ].quantity == 1 && held.item_id == LAST_BLOCK_ID && held.quantity == 0 );
    }
    {
        const InventorySlot empty = { LAST_BLOCK_ID, 0 };
        const InventorySlot room = { OAK_PLANKS, 5 };
        const InventorySlot full = { OAK_PLANKS, 64 };
        const InventorySlot other = { SPRUCE_PLANKS, 1 };
        const InventorySlot held = { OAK_PLANKS, 10 };
        report( "drag accepts an empty slot", crafting_drag_accepts( empty, held ) );
        report( "drag accepts same item with room", crafting_drag_accepts( room, held ) );
        report( "drag rejects same item full", !crafting_drag_accepts( full, held ) );
        report( "drag rejects a different item", !crafting_drag_accepts( other, held ) );
    }

    return failures;
}

// An owned synced item that drifts into a foreign-owned chunk must keep
// streaming ENTITY_STATE — ownership keys off net_chunk (the chunk the
// server believes it sits in), which only advances once a state is sent, so
// the server sees the crossing and issues ENTITY_TRANSFER. Before net_chunk
// the local pos was used and the crossing was stranded.
int test_net_item_crossing( ) {
    int failures = 0;
    const auto report = [ &failures ]( const char *name, bool ok ) {
        pr_test( "  %-55s %s", name, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };

    Multiplayer mp;
    NetEntities net( &mp );
    ItemEntities items;
    items.set_multiplayer_mode( true );
    items.test_bind_net( &net );

    const glm::ivec3 chunk_a( 0, 0, 0 ), chunk_b( 1, 0, 0 );
    net.test_set_role( chunk_a, NET_ROLE_OWNER );

    NetEntityRecord rec;
    rec.id = ( static_cast<NetEntityId>( 9 ) << 32 ) | 1;
    rec.kind = NetEntityKind::ITEM;
    rec.x = 30.0;
    rec.y = 40.0;
    rec.z = 10.0;
    rec.type = DIRT;
    rec.count = 4;
    items.net_apply_spawn( rec, /*mirror*/ false );

    std::vector<NetEntityStateEntry> out;
    items.collect_net_state( net, out, 0 );
    report( "crossing: initial state emitted", out.size( ) == 1 );

    // Drag the item into foreign chunk_b: the state still goes out because
    // net_chunk is still chunk_a — the server learns of the crossing.
    items.test_items( )[ 0 ].pos = glm::dvec3( 70.0, 40.0, 10.0 );
    out.clear( );
    items.collect_net_state( net, out, 6 );
    report( "crossing: state emitted while crossing", out.size( ) == 1 && out[ 0 ].x == 70.0 );
    report( "crossing: net_chunk advanced to sent pos", items.test_items( )[ 0 ].net_chunk == chunk_b );

    // Server-belief moved: without a role on chunk_b the item stops
    // streaming — the server now drives it via TRANSFER/adopt=0.
    items.test_items( )[ 0 ].pos.x += 1.0;
    out.clear( );
    items.collect_net_state( net, out, 12 );
    report( "crossing: stops streaming in foreign chunk", out.empty( ) );

    // ENTITY_TRANSFER adopt=0 while still viewed: becomes a mirror.
    items.net_release_entity( rec, /*still_viewed*/ true );
    report( "crossing: adopt=0 becomes mirror", items.test_items( )[ 0 ].remote );

    return failures;
}

int test_net_mobs( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    RepGameState &gs = *new RepGameState( );
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 0, 0, 0 );
    test_setup_world( gs.world );
    World &world = gs.world;
    const glm::dvec3 player_pos( 1.5, 50.0, 1.5 ); // far away: no look-at/sounds

    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };

    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    for ( int x = 5; x <= 30; x++ ) {
        for ( int z = 5; z <= 15; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 10, z ), stone );
        }
    }

    // Mirror: no physics — a mob spawned mid-air stays at its net target
    // instead of falling, and only ENTITY_REMOVE takes it away.
    {
        Multiplayer mp;
        NetEntities net( &mp );
        MobManager mobs;
        mobs.set_multiplayer_mode( true, &net );
        NetEntityRecord rec{};
        rec.id = ( static_cast<NetEntityId>( 7 ) << 32 ) | 9;
        rec.kind = NetEntityKind::MOB;
        rec.x = 10.5;
        rec.y = 40.0;
        rec.z = 10.5;
        rec.type = MOB_PIG;
        rec.health = 10.0f;
        mobs.net_apply_spawn( rec, /*mirror*/ true );
        check_bool( "mirror: spawned from record", mobs.mobs.size( ) == 1 );
        check_bool( "mirror: flagged mirror", mobs.mobs[ 0 ].net_mirror );
        for ( int i = 0; i < 100; i++ ) {
            mobs.tick( world, player_pos );
        }
        check_bool( "mirror: no physics (floats at target)", fabs( mobs.mobs[ 0 ].pos.y - 40.0 ) < 0.01 && !mobs.mobs.empty( ) );

        // DYING rising edge starts the tip-over; nothing removes it locally.
        NetEntityStateEntry e{};
        e.id = rec.id;
        e.kind = static_cast<uint8_t>( NetEntityKind::MOB );
        e.x = rec.x;
        e.y = rec.y;
        e.z = rec.z;
        e.flags = NET_MOB_FLAG_DYING;
        mobs.net_apply_state( e );
        check_bool( "mirror: DYING starts death anim", mobs.mobs[ 0 ].death_ticks > 0 );
        for ( int i = 0; i < 200; i++ ) {
            mobs.tick( world, player_pos );
        }
        check_bool( "mirror: still present without ENTITY_REMOVE", mobs.mobs.size( ) == 1 );
        mobs.net_apply_remove( rec.id );
        check_bool( "mirror: ENTITY_REMOVE removes", mobs.mobs.empty( ) );
    }

    // Hostile targeting: a nearer remote targetable player beats the local
    // one and the melee hit carries that player id for PLAYER_HIT.
    {
        MobManager mobs;
        check_bool( "spawn zombie", mobs.spawn( MOB_ZOMBIE, glm::dvec3( 10.5, 11.0, 10.5 ), 0.0f, 1 ) );
        MobTickInput in{};
        in.player_targetable = true;
        in.world_time = DAY_LENGTH_TICKS / 4;
        in.player_eye_height = PLAYER_EYE_HEIGHT;
        in.remote_targets.push_back( { 7, glm::dvec3( 11.0, 12.5, 10.5 ), 1.62f, true } );
        MobTickOutput out;
        mobs.tick( world, player_pos, &in, &out );
        bool remote_hit = false;
        for ( const MobPlayerHit &hit : out.player_hits ) {
            remote_hit |= hit.target_player_id == 7;
        }
        check_bool( "hostile: melee hit targets remote player", remote_hit );
    }

    // ENTITY_ACTION HIT on an owned mob applies real damage.
    {
        Multiplayer mp;
        NetEntities net( &mp );
        MobManager mobs;
        mobs.set_multiplayer_mode( true, &net );
        check_bool( "action: spawn pig", mobs.spawn( MOB_PIG, glm::dvec3( 10.5, 11.0, 10.5 ), 0.0f, 1 ) );
        MobManager::Mob &mob = mobs.mobs[ 0 ];
        mob.net_id = 42;
        mob.net_chunk = glm::ivec3( 0, 0, 0 );
        net.test_set_role( glm::ivec3( 0, 0, 0 ), NET_ROLE_OWNER );
        NetEntityActionPayload p{};
        p.id = 42;
        p.action = NET_ACTION_HIT;
        p.damage = 4.0f;
        p.attacker_survival = 1;
        mobs.net_apply_action( gs, p );
        check_bool( "action: owned mob takes damage", mobs.mobs[ 0 ].health < 10.0f - 0.01f );
    }

    return failures;
}

constexpr Test all_tests[] = {  //
    MK_TEST( ecs ),             //
    MK_TEST( vr_projection ),   //
    MK_TEST( redstone ),        //
    MK_TEST( torch_pick ),      //
    MK_TEST( player_placement_valid ), //
    MK_TEST( sneak ),           //
    MK_TEST( items ),          //
    MK_TEST( item_entities ),  //
    MK_TEST( save_migration ),  //
    MK_TEST( creative ),        //
    MK_TEST( fences ),          //
    MK_TEST( doors ),           //
    MK_TEST( block_world ),     //
    MK_TEST( mapgen ),          //
    MK_TEST( waterlog ),        //
    MK_TEST( lighting ),        //
    MK_TEST( tnt ),             //
    MK_TEST( falling_blocks ),  //
    MK_TEST( structure_border ), //
    MK_TEST( mobs ),             //
    MK_TEST( net_entities ),     //
    MK_TEST( net_item_crossing ), //
    MK_TEST( net_mobs ),          //
    MK_TEST( crafting ),         //
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
