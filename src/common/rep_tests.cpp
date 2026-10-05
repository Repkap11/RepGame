
#include "common/rep_tests.hpp"

#include "common/RepGame.hpp"

#include <cmath>

#include "common/perlin_noise.hpp"
#include "common/map_gen.hpp"
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
    place( glm::ivec3( 11, 12, 10 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS, 0 } ); // D: dust beside B
    place( glm::ivec3( 10, 13, 10 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS, 0 } ); // D2: dust on top of B
    place( glm::ivec3( 9, 12, 10 ), { REDSTONE_TORCH, 6, 0, REDSTONE_TORCH, 0 } );  // T2: attached to B
    place( glm::ivec3( 10, 12, 11 ), { PISTON, 0, 0, PISTON, 0 } );                // piston beside B, faces +z
    place( glm::ivec3( 10, 12, 9 ), { REDSTONE_LAMP, 0, 0, REDSTONE_LAMP, 0 } );   // lamp beside B
    place( glm::ivec3( 10, 12, 8 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS, 0 } ); // D3: dust beside lamp
    place( glm::ivec3( 13, 11, 10 ), { REDSTONE_TORCH, 0, 0, REDSTONE_TORCH, 0 } ); // T3: floor torch under glass
    place( glm::ivec3( 13, 12, 10 ), { GLASS, 0, 0, GLASS, 0 } );                  // glass above T3: can't be powered
    place( glm::ivec3( 13, 13, 10 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS, 0 } ); // G_d: dust on glass
    place( glm::ivec3( 10, 13, 9 ), { REDSTONE_LAMP, 0, 0, REDSTONE_LAMP, 0 } );   // lamp2 above lit lamp
    place( glm::ivec3( 9, 12, 11 ), { REDSTONE_LAMP, 0, 0, REDSTONE_LAMP, 0 } );   // lamp3 beside powered piston

    // Glass staircase: source at the bottom must climb it.
    place( glm::ivec3( 20, 11, 10 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS, 0 } ); // s1
    place( glm::ivec3( 21, 12, 10 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS, 0 } ); // s2
    place( glm::ivec3( 22, 13, 10 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS, 0 } ); // s3
    place( glm::ivec3( 20, 11, 11 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } ); // source beside s1

    // Stone staircase: source at the top must descend it.
    place( glm::ivec3( 25, 11, 10 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS, 0 } ); // s4
    place( glm::ivec3( 26, 12, 10 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS, 0 } ); // s5
    place( glm::ivec3( 26, 12, 11 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } ); // source beside s5

    // Blocked staircases.
    place( glm::ivec3( 28, 11, 10 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS, 0 } ); // b1
    place( glm::ivec3( 29, 12, 10 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS, 0 } ); // b2 (blocked)
    place( glm::ivec3( 28, 11, 11 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } ); // source beside b1
    place( glm::ivec3( 28, 11, 14 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS, 0 } ); // b3
    place( glm::ivec3( 29, 12, 14 ), { REDSTONE_CROSS, 0, 0, REDSTONE_CROSS, 0 } ); // b4 (glass corner)
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
                if ( surf == GRASS || surf == PODZEL || surf == MYCELIUM ) grass++;
                if ( surf == SAND || surf == ORANGE_SAND || surf == GRAVEL ) sand_like++;
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
#if defined( REPGAME_MAP_GEN_LEGACY )
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
    RepGameState gs;
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
    world.set_loaded_block( glm::ivec3( 10, 10, 10 ), { SEAWEED, 0, 0, SEAWEED, 0 } );
    const BlockState wet = world.get_loaded_block( glm::ivec3( 10, 10, 10 ) );
    check_bool( "plant placed in water keeps id", wet.id == SEAWEED );
    check_bool( "plant placed in water is waterlogged", wet.waterlogged == 1 );
    world.set_loaded_block( glm::ivec3( 20, 10, 20 ), { SEAWEED, 0, 0, SEAWEED, 0 } );
    const BlockState dry = world.get_loaded_block( glm::ivec3( 20, 10, 20 ) );
    check_bool( "plant placed on land is not waterlogged", dry.id == SEAWEED && dry.waterlogged == 0 );

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
    check_bool( "waterlogged coral survives water expansion", after.id == SEAWEED && after.waterlogged == 1 );
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
    RepGameState gs;
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

    // Regression: an overhang present BEFORE finalize must still get lateral
    // skylight — the interior band scan seeds lit cells facing deeper-dark
    // neighbor columns. Writing the block directly (not set_loaded_block)
    // keeps it out of the recheck path, which would seed around it and mask
    // the scan; light_finalize_chunk is the path terrain features take.
    chunk.blocks[ chunk.get_index_from_coords( 5, 30, 5 ) ] = stone;
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
    return failures;
}

constexpr Test all_tests[] = { //
    MK_TEST( ecs ),            //
    MK_TEST( redstone ),       //
    MK_TEST( sneak ),          //
    MK_TEST( mapgen ),         //
    MK_TEST( waterlog ),       //
    MK_TEST( lighting ),       //
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
