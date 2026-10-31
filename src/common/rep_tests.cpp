
#include "common/rep_tests.hpp"

#include "common/RepGame.hpp"

#include <cmath>

#include "common/perlin_noise.hpp"
#include "common/map_gen.hpp"
#include "common/structure_gen.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/block_update_events/TntExplodeEvent.hpp"
#include "common/utils/collision.hpp"
#include "common/mobs.hpp"
#include "common/net/packet.hpp"
#include "common/mob_uv.hpp"
#include "common/creative_order_gen.hpp"
#include "common/utils/map_storage.hpp"

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
    int dupes = 0, bad = 0;
    for ( unsigned int i = 0; i < CREATIVE_BLOCK_COUNT; i++ ) {
        const BlockID bid = CREATIVE_BLOCKS[ i ];
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

    auto contains = [ & ]( BlockID bid ) { return seen[ bid ] != 0; };
    check_bool( "creative: ACACIA_PLANKS listed", contains( ACACIA_PLANKS ) );
    check_bool( "creative: ACACIA_FENCE hidden", !contains( ACACIA_FENCE ) );
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
        expected[ CREATIVE_BLOCKS[ i ] ] = 1;
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

    auto place = [ & ]( const glm::ivec3 &pos, const BlockState &block_state ) {
        gs.blockUpdateQueue.addBlockUpdate( std::make_shared<PlayerBlockPlacedEvent>( gs.tick_number, pos, block_state, false ) );
    };
    place( glm::ivec3( 10, 11, 10 ), { TNT, 0, 0, TNT, 0 } );
    place( glm::ivec3( 11, 11, 10 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );

    int failures = 0;
    // Ignition propagates within a few ticks — the powered TNT cell turns to
    // air long before the fuse event lands.
    for ( int i = 0; i < 10; i++ ) {
        gs.tick_number++;
        gs.blockUpdateQueue.processAllBlockUpdates( gs, gs.tick_number );
    }
    failures += check_id( world, "ignited TNT cell is air", glm::ivec3( 10, 11, 10 ), AIR );
    failures += check_id( world, "chain TNT not yet blasted", glm::ivec3( 13, 11, 10 ), TNT );
    failures += check_id( world, "platform intact before blast", glm::ivec3( 10, 10, 10 ), STONE );

    // Outlast the main fuse plus the longest possible chain fuse.
    const long wait = TNT_FUSE_TICKS + TNT_CHAIN_FUSE_MIN_TICKS + TNT_CHAIN_FUSE_SPREAD_TICKS + 40;
    for ( long i = 0; i < wait; i++ ) {
        gs.tick_number++;
        gs.blockUpdateQueue.processAllBlockUpdates( gs, gs.tick_number );
    }
    failures += check_id( world, "blast cell stays air", glm::ivec3( 10, 11, 10 ), AIR );
    failures += check_id( world, "stone under blast destroyed", glm::ivec3( 10, 10, 10 ), AIR );
    failures += check_id( world, "bedrock survives blast", glm::ivec3( 10, 9, 10 ), BEDROCK );
    failures += check_id( world, "chained TNT detonated", glm::ivec3( 13, 11, 10 ), AIR );
    failures += check_id( world, "far TNT untouched", glm::ivec3( 8, 11, 16 ), TNT );
    failures += check_id( world, "distant stone untouched", glm::ivec3( 25, 10, 25 ), STONE );
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
        MapGen::load_block_c( c );
        StructureGen::place( *c );
        return c;
    };
    auto free_chunk = []( Chunk *c ) {
        chunk_free_cells( c->cells );
        chunk_free_extras( c->extras );
        chunk_free_air_mask( c->light_air_mask );
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
            gs.hotbar.slots[ i ].block_id = LAST_BLOCK_ID;
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
        // Survival drop path: the killed pig hands drop_block to the player.
        RepGame::give_block_to_player( gs, RED_MUSHROOM, 1 );
        check_bool( "drop: RED_MUSHROOM in inventory", gs.hotbar.findSlotWithBlock( RED_MUSHROOM, false ) >= 0 );
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

    return failures;
}

constexpr Test all_tests[] = {  //
    MK_TEST( ecs ),             //
    MK_TEST( vr_projection ),   //
    MK_TEST( redstone ),        //
    MK_TEST( sneak ),           //
    MK_TEST( save_migration ),  //
    MK_TEST( creative ),        //
    MK_TEST( block_world ),     //
    MK_TEST( mapgen ),          //
    MK_TEST( waterlog ),        //
    MK_TEST( lighting ),        //
    MK_TEST( tnt ),             //
    MK_TEST( structure_border ), //
    MK_TEST( mobs ),             //
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
