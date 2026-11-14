// Redstone + door mechanics tests — split out of rep_tests.cpp.
#include "test_util.hpp"
#include "common/block_update_events/BlockNextToChangeEvent.hpp"
#include "common/block_update_events/PressurePlateEvent.hpp"
#include "common/block_update_events/PistonEvent.hpp"
#include "common/repeater.hpp"
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
        gs.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( gs.tick_number, pos, block_state, false );
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

    test_run_redstone_ticks( gs, 20 );

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

    // The extended head at (10,12,12) is raycast-targetable on its plate box
    // even though is_pickable stays false (the door upper precedent): a ray
    // at the plate hits the head cell, a ray beside it passes through, and
    // mining retargets the base via piston_head_base.
    {
        auto check_bool = [ & ]( const char *name, bool cond ) {
            pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
            if ( !cond ) {
                failures++;
            }
        };
        glm::ivec3 out( -1, -1, -1 );
        int face = -1;
        double t = -1.0;
        check_bool( "piston head: -z ray hits the plate face",
                    RayTraversal::find_block_from_to( world, glm::dvec3( 10.5, 12.5, 14.5 ), glm::dvec3( 10.5, 12.5, 11.5 ), out, &face, 0, &t ) &&
                        out == glm::ivec3( 10, 12, 12 ) && face == FACE_BACK && fabs( t - 1.5 ) < 0.01 );
        check_bool( "piston head: side ray at plate depth hits",
                    RayTraversal::find_block_from_to( world, glm::dvec3( 8.5, 12.5, 12.9 ), glm::dvec3( 12.5, 12.5, 12.9 ), out, &face ) &&
                        out == glm::ivec3( 10, 12, 12 ) && face == FACE_LEFT );
        check_bool( "piston head: side ray through the arm shaft hits",
                    RayTraversal::find_block_from_to( world, glm::dvec3( 8.5, 12.5, 12.5 ), glm::dvec3( 12.5, 12.5, 12.5 ), out, &face, 0, &t ) &&
                        out == glm::ivec3( 10, 12, 12 ) && face == FACE_LEFT && fabs( t - 1.875 ) < 0.01 );
        out = glm::ivec3( -1, -1, -1 );
        RayTraversal::find_block_from_to( world, glm::dvec3( 8.5, 12.7, 12.5 ), glm::dvec3( 12.5, 12.7, 12.5 ), out, &face );
        check_bool( "piston head: side ray above the shaft misses it", out != glm::ivec3( 10, 12, 12 ) );
        // The extended base's open throat: a ray threading the recess meets
        // the 4x4x4 arm stub, not the void behind it.
        t = -1.0;
        check_bool( "piston base: ray through the throat hits the arm stub",
                    RayTraversal::find_block_from_to( world, glm::dvec3( 12.5, 12.5, 11.9 ), glm::dvec3( 8.5, 12.5, 11.9 ), out, &face, 0, &t ) &&
                        out == glm::ivec3( 10, 12, 11 ) && face == FACE_RIGHT && fabs( t - 1.875 ) < 0.01 );
        // Collision matches the same boxes: a player can't clip through the
        // shaft region, while a narrow body still threads the arm gap.
        check_bool( "piston head: player can't clip through the arm",
                    Collision::collides_at( world, glm::dvec3( 10.5, 14.0, 12.4 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
        check_bool( "piston head: thin body still slips beside the arm",
                    !Collision::collides_at( world, glm::dvec3( 10.2, 14.0, 12.4 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT, 0.1 ) );
        check_bool( "piston head: placement cell beyond the plate",
                    RayTraversal::placement_cell( glm::ivec3( 10, 12, 12 ), FACE_BACK ) == glm::ivec3( 10, 12, 13 ) );
        const BlockState head_state = world.get_loaded_block( glm::ivec3( 10, 12, 12 ) );
        check_bool( "piston head: base pairing finds (10,12,11)",
                    piston_head_base( world, glm::ivec3( 10, 12, 12 ), head_state ) == glm::ivec3( 10, 12, 11 ) );
        check_bool( "piston head: orphaned head pairs to itself",
                    piston_head_base( world, glm::ivec3( 10, 12, 13 ), head_state ) == glm::ivec3( 10, 12, 13 ) );
    }

    // Teardown phase: breaking the source must unpower the whole glass
    // staircase — a missed diagonal update would leave it stuck on. Blocking
    // the corner above s4 must cut the stone staircase's downward feed.
    place( glm::ivec3( 20, 11, 11 ), BLOCK_STATE_AIR );
    place( glm::ivec3( 25, 12, 10 ), stone );
    place( glm::ivec3( 28, 11, 19 ), BLOCK_STATE_AIR );
    test_run_redstone_ticks( gs, 20 );
    failures += check_power( world, "s1 after source removed", glm::ivec3( 20, 11, 10 ), 0 );
    failures += check_power( world, "s2 after source removed", glm::ivec3( 21, 12, 10 ), 0 );
    failures += check_power( world, "s3 after source removed", glm::ivec3( 22, 13, 10 ), 0 );
    failures += check_power( world, "s4 after corner blocked", glm::ivec3( 25, 11, 10 ), 0 );
    failures += check_power( world, "M1 after source removed (no latch)", glm::ivec3( 28, 11, 18 ), 0 );
    failures += check_power( world, "M2 after source removed (no latch)", glm::ivec3( 29, 11, 18 ), 0 );

    // Mining a head lands its removal on the base (change_block retargets
    // pos_destroy through piston_head_base): the base write then pops the
    // head through the same survive check an orphaned head uses.
    place( glm::ivec3( 10, 12, 11 ), BLOCK_STATE_AIR );
    test_run_redstone_ticks( gs, 5 );
    failures += check_id( world, "piston base removed", glm::ivec3( 10, 12, 11 ), AIR );
    failures += check_id( world, "piston head popped with base", glm::ivec3( 10, 12, 12 ), AIR );
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
    check_bool( "def: block_item_id maps upper to lower", block_item_id( WOODEN_DOOR_UPPER ) == WOODEN_DOOR_LOWER );

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
        gs.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( gs.tick_number, pos, block_state, false );
    };
    auto run = [ & ]( const int ticks ) {
        test_run_redstone_ticks( gs, ticks );
    };
    auto toggle = [ & ]( const glm::ivec3 &pos ) {
        gs.blockUpdateQueue.addBlockUpdate<DoorEvent>( gs.tick_number, pos, DoorEvent::TOGGLE );
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
    RayTraversal::find_block_from_to( world, glm::dvec3( 10.5, 12.5, 9.5 ), glm::dvec3( 10.5, 12.5, 11.5 ), pick_out, &pick_face );
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

    // --- tall plants (TALL_GRASS / TALL_GRASS_UPPER) ----------------------
    const Block *tg = block_definition_get_definition( TALL_GRASS );
    const Block *tgu = block_definition_get_definition( TALL_GRASS_UPPER );
    check_bool( "tall plant: partners point at each other",
                tg->tall_plant_partner == TALL_GRASS_UPPER && tgu->tall_plant_partner == TALL_GRASS && block_is_tall_plant( tg ) && block_is_tall_plant( tgu ) );
    check_bool( "tall plant: only the upper is flagged", !tg->is_tall_plant_upper && tgu->is_tall_plant_upper );
    check_bool( "tall plant: upper hidden (not pickable)", !tgu->is_pickable && tg->is_pickable );
    check_bool( "tall plant: upper maps to TALL_GRASS item", block_item_id( TALL_GRASS_UPPER ) == TALL_GRASS );

    // Grass floor at y=10 (collides -> satisfies needs_place_on_any_solid
    // top), a blocked upper cell above (33,12,30), and a spot with no floor.
    const BlockState grass = { GRASS, 0, 0, GRASS, 0 };
    for ( int x = 30; x <= 36; x++ ) {
        for ( int z = 30; z <= 36; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 10, z ), grass );
        }
    }
    world.set_loaded_block( glm::ivec3( 33, 12, 30 ), stone );
    const BlockState tall_grass = { TALL_GRASS, 0, 0, TALL_GRASS, 0 };
    place( glm::ivec3( 30, 11, 30 ), tall_grass );
    place( glm::ivec3( 33, 11, 30 ), tall_grass ); // blocked upper cell
    place( glm::ivec3( 38, 11, 38 ), tall_grass ); // no floor
    run( 2 );
    failures += check_id( world, "tall plant: lower written", glm::ivec3( 30, 11, 30 ), TALL_GRASS );
    failures += check_id( world, "tall plant: upper written", glm::ivec3( 30, 12, 30 ), TALL_GRASS_UPPER );
    failures += check_id( world, "tall plant: blocked upper places nothing", glm::ivec3( 33, 11, 30 ), AIR );
    failures += check_id( world, "tall plant: no floor places nothing", glm::ivec3( 38, 11, 38 ), AIR );

    // The upper is !is_pickable (creative exclusion) but must still be
    // targetable: a pick ray through the upper cross-quads hits it.
    pick_out = glm::ivec3( -1, -1, -1 );
    pick_face = -1;
    RayTraversal::find_block_from_to( world, glm::dvec3( 29.5, 12.5, 29.5 ), glm::dvec3( 31.5, 12.5, 31.5 ), pick_out, &pick_face );
    check_bool( "tall plant: pick ray hits the upper half", pick_out == glm::ivec3( 30, 12, 30 ) );

    // Breaking either half pops the other through the survive check.
    place( glm::ivec3( 30, 12, 30 ), BLOCK_STATE_AIR );
    run( 2 );
    failures += check_id( world, "tall plant: breaking upper removes lower", glm::ivec3( 30, 11, 30 ), AIR );
    place( glm::ivec3( 30, 11, 30 ), tall_grass );
    run( 2 );
    failures += check_id( world, "tall plant: replanted", glm::ivec3( 30, 12, 30 ), TALL_GRASS_UPPER );
    place( glm::ivec3( 30, 11, 30 ), BLOCK_STATE_AIR );
    run( 2 );
    failures += check_id( world, "tall plant: breaking lower removes upper", glm::ivec3( 30, 12, 30 ), AIR );
    place( glm::ivec3( 30, 11, 30 ), tall_grass );
    run( 2 );
    place( glm::ivec3( 30, 10, 30 ), BLOCK_STATE_AIR ); // knock the ground out
    run( 2 );
    failures += check_id( world, "tall plant: ground gone pops lower", glm::ivec3( 30, 11, 30 ), AIR );
    failures += check_id( world, "tall plant: ground gone pops upper", glm::ivec3( 30, 12, 30 ), AIR );

    // Legacy gen: every TALL_GRASS in a generated chunk pairs with its
    // upper and vice versa.
#if REPGAME_MAP_GEN_LEGACY
    {
        bool pairs_ok = true;
        int lowers = 0, uppers = 0;
        for ( const glm::ivec3 cp : { glm::ivec3( 0, 0, 0 ), glm::ivec3( -1, 0, -1 ), glm::ivec3( 0, 0, -1 ) } ) {
            Chunk chunk;
            chunk.chunk_pos = cp;
            chunk.cells = chunk_alloc_cells( );
            chunk.extras = nullptr;
            chunk.light.air_mask = nullptr;
            chunk.light.shade_mask = nullptr;
            MapGen::load_block_c( &chunk );
            StructureGen::place( chunk );
            for ( int x = 0; x < CHUNK_SIZE_X; x++ ) {
                for ( int y = 0; y < CHUNK_SIZE_Y; y++ ) {
                    for ( int z = 0; z < CHUNK_SIZE_Z; z++ ) {
                        const BlockID id = chunk.get_block( glm::ivec3( x, y, z ) ).id;
                        if ( id == TALL_GRASS ) {
                            lowers++;
                            
                            if ( chunk.get_block( glm::ivec3( x, y + 1, z ) ).id != TALL_GRASS_UPPER ) {
                                pairs_ok = false;
                            }
                        }
                        if ( id == TALL_GRASS_UPPER ) {
                            uppers++;
                            if ( y == 0 || chunk.get_block( glm::ivec3( x, y - 1, z ) ).id != TALL_GRASS ) {
                                pairs_ok = false;
                            }
                        }
                    }
                }
            }
            chunk_free_cells( chunk.cells );
        }
        check_bool( "mapgen: tall-grass halves always paired", pairs_ok );
        check_bool( "mapgen: tall grass actually generates", lowers > 0 && lowers == uppers );
    }
#endif
    return failures;
}


// Repeaters: the facing/delay encoding, the one-way power pass through
// RepeaterEvent, per-delay timing and MC's pulse extension, hard power into
// the front block (mechanism cap included), repeater-to-repeater chains,
// dust connections along the facing axis only, and the floor-support kill.
int test_repeaters( ) {
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

    // --- encoding helpers ----------------------------------------------------
    check_bool( "repeater: rot 0 outputs -z", repeater_output_dir( 0 ) == glm::ivec3( 0, 0, -1 ) );
    check_bool( "repeater: rot 1 outputs +x", repeater_output_dir( 1 ) == glm::ivec3( 1, 0, 0 ) );
    check_bool( "repeater: rot 2 outputs +z", repeater_output_dir( 2 ) == glm::ivec3( 0, 0, 1 ) );
    check_bool( "repeater: rot 3 outputs -x", repeater_output_dir( 3 ) == glm::ivec3( -1, 0, 0 ) );
    bool delay_ok = true;
    const BlockID delay_ids[ 4 ] = { REPEATER, REPEATER_DELAY_2, REPEATER_DELAY_3, REPEATER_DELAY_4 };
    for ( int d = 1; d <= 4; d++ ) {
        const BlockState s = repeater_with_delay( { REPEATER, 0, 0, REPEATER, 0 }, d );
        delay_ok &= repeater_delay( s ) == d && s.display_id == delay_ids[ d - 1 ];
    }
    check_bool( "repeater: delay/display_id round-trips 1-4", delay_ok );
    check_bool( "repeater: cycle wraps 4 back to 1",
                repeater_delay( repeater_cycle_delay( repeater_with_delay( { REPEATER, 0, 0, REPEATER, 0 }, 4 ) ) ) == 1 );
    check_bool( "repeater: delay scales by REDSTONE_REPEATER_STEP_GT",
                repeater_delay_gt( repeater_with_delay( { REPEATER, 0, 0, REPEATER, 0 }, 3 ) ) == 3 * REDSTONE_REPEATER_STEP_GT );

    RepGameState &gs = *new RepGameState( ); // leaked on purpose — see test_redstone
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 0, 0, 0 );
    test_setup_world( gs.world );
    World &world = gs.world;
    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    // Repeaters need a solid support: a stone floor under the test area.
    for ( int x = 0; x < 32; x++ ) {
        for ( int z = 0; z < 32; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 10, z ), stone );
        }
    }
    auto place = [ & ]( const glm::ivec3 &pos, const BlockState &block_state ) {
        gs.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( gs.tick_number, pos, block_state, false );
    };
    auto run = [ & ]( const int ticks ) {
        test_run_redstone_ticks( gs, ticks );
    };
    const BlockState rep_x = { REPEATER, 1, 0, REPEATER, 0 }; // facing +x

    // --- power pass ----------------------------------------------------------
    // block -- dust -- repeater(+x) -- dust, plus dust beside the repeater's
    // sides that must stay dark.
    place( glm::ivec3( 4, 11, 4 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );
    place( glm::ivec3( 5, 11, 4 ), { REDSTONE_WIRE_CROSS, 0, 0, REDSTONE_WIRE_CROSS, 0 } );
    place( glm::ivec3( 6, 11, 4 ), rep_x );
    place( glm::ivec3( 7, 11, 4 ), { REDSTONE_WIRE_CROSS, 0, 0, REDSTONE_WIRE_CROSS, 0 } );
    place( glm::ivec3( 6, 11, 3 ), { REDSTONE_WIRE_CROSS, 0, 0, REDSTONE_WIRE_CROSS, 0 } );
    place( glm::ivec3( 6, 11, 5 ), { REDSTONE_WIRE_CROSS, 0, 0, REDSTONE_WIRE_CROSS, 0 } );

    // Direction: a source at the repeater's front drives neither the output
    // nor the dust behind it.
    place( glm::ivec3( 9, 11, 4 ), { REDSTONE_WIRE_CROSS, 0, 0, REDSTONE_WIRE_CROSS, 0 } );
    place( glm::ivec3( 10, 11, 4 ), rep_x );
    place( glm::ivec3( 11, 11, 4 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );

    run( 10 );
    failures += check_power( world, "repeater powered through dust", glm::ivec3( 6, 11, 4 ), 15 );
    failures += check_power( world, "dust in front of powered repeater", glm::ivec3( 7, 11, 4 ), 14 );
    failures += check_power( world, "dust beside repeater (-z) stays dark", glm::ivec3( 6, 11, 3 ), 0 );
    failures += check_power( world, "dust beside repeater (+z) stays dark", glm::ivec3( 6, 11, 5 ), 0 );
    failures += check_power( world, "repeater ignores a source at its front", glm::ivec3( 10, 11, 4 ), 0 );
    failures += check_power( world, "dust behind front-fed repeater stays dark", glm::ivec3( 9, 11, 4 ), 0 );

    // --- delay timing --------------------------------------------------------
    // One repeater per delay: the gap between the game tick the input first
    // reads on and the game tick the output reaches 15 is 2 * delay gt.
    long input_on[ 5 ] = { -1, -1, -1, -1, -1 };
    long power_on[ 5 ] = { -1, -1, -1, -1, -1 };
    for ( int d = 1; d <= 4; d++ ) {
        place( glm::ivec3( 19, 11, 8 + d ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );
        place( glm::ivec3( 20, 11, 8 + d ), repeater_with_delay( rep_x, d ) );
    }
    for ( int i = 0; i < 100; i++ ) {
        gs.tick_number++;
        repgame_drain_block_updates( gs );
        const long now = redstone_tick_now( gs );
        for ( int d = 1; d <= 4; d++ ) {
            const glm::ivec3 p( 20, 11, 8 + d );
            const BlockState s = world.get_loaded_block( p );
            if ( input_on[ d ] < 0 && repeater_input_powered( world, p, s ) ) {
                input_on[ d ] = now;
            }
            if ( power_on[ d ] < 0 && s.current_redstone_power == 15 ) {
                power_on[ d ] = now;
            }
        }
    }
    for ( int d = 1; d <= 4; d++ ) {
        const long gap = ( input_on[ d ] >= 0 && power_on[ d ] >= 0 ) ? power_on[ d ] - input_on[ d ] : -1;
        check_bool( d == 1   ? "repeater delay 1: on within [2,3] gt" :
                    d == 2   ? "repeater delay 2: on within [4,5] gt" :
                    d == 3   ? "repeater delay 3: on within [6,7] gt" :
                               "repeater delay 4: on within [8,9] gt",
                    gap >= 2 * d && gap <= 2 * d + 1 );
    }

    // --- pulse extension -----------------------------------------------------
    // A 1-gt input pulse on a delay-4 repeater still produces a full-delay
    // output pulse (the event re-arms when the input is already gone).
    const glm::ivec3 pulse_rep( 24, 11, 16 );
    place( glm::ivec3( 23, 11, 16 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );
    place( pulse_rep, repeater_with_delay( rep_x, 4 ) );
    test_run_game_ticks( gs, 8 ); // input on and the tick scheduled, but the delay hasn't elapsed
    place( glm::ivec3( 23, 11, 16 ), BLOCK_STATE_AIR );
    long pulse_on = -1;
    long pulse_off = -1;
    for ( int i = 0; i < 200; i++ ) {
        gs.tick_number++;
        repgame_drain_block_updates( gs );
        const long now = redstone_tick_now( gs );
        const int p = world.get_loaded_block( pulse_rep ).current_redstone_power;
        if ( pulse_on < 0 && p == 15 ) {
            pulse_on = now;
        }
        if ( pulse_on >= 0 && p == 0 ) {
            pulse_off = now;
            break;
        }
    }
    check_bool( "pulse: delay-4 repeater still turns on", pulse_on >= 0 );
    check_bool( "pulse: stays on about the full delay",
                pulse_on >= 0 && pulse_off > pulse_on && pulse_off - pulse_on >= 4 * REDSTONE_REPEATER_STEP_GT - 1 &&
                    pulse_off - pulse_on <= 4 * REDSTONE_REPEATER_STEP_GT + 2 );
    check_bool( "pulse: no repeater tick left pending", gs.pending_repeater_ticks.empty( ) );

    // --- hard power out the front ---------------------------------------------
    // repeater -> stone: the stone is hard 15, dust on its far side reads 14,
    // and a torch on the stone turns off. A lamp in front of a second
    // repeater is capped to "activated" (1).
    place( glm::ivec3( 3, 11, 20 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );
    place( glm::ivec3( 4, 11, 20 ), rep_x );
    place( glm::ivec3( 5, 11, 20 ), stone );
    place( glm::ivec3( 6, 11, 20 ), { REDSTONE_WIRE_CROSS, 0, 0, REDSTONE_WIRE_CROSS, 0 } );
    place( glm::ivec3( 5, 12, 20 ), { REDSTONE_TORCH, 0, 0, REDSTONE_TORCH, 0 } );
    place( glm::ivec3( 8, 11, 20 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );
    place( glm::ivec3( 9, 11, 20 ), rep_x );
    place( glm::ivec3( 10, 11, 20 ), { REDSTONE_LAMP, 0, 0, REDSTONE_LAMP, 0 } );

    // --- chain ---------------------------------------------------------------
    // rep1 -> rep2: the second fires only after both delays.
    place( glm::ivec3( 14, 11, 24 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );
    place( glm::ivec3( 15, 11, 24 ), rep_x );
    place( glm::ivec3( 16, 11, 24 ), repeater_with_delay( rep_x, 2 ) );

    // --- dust shape ----------------------------------------------------------
    // Lone dust behind the repeater becomes a LINE along its axis; lone dust
    // at its side stays a CROSS (no connection off-axis).
    place( glm::ivec3( 20, 11, 28 ), rep_x );
    place( glm::ivec3( 19, 11, 28 ), { REDSTONE_WIRE_CROSS, 0, 0, REDSTONE_WIRE_CROSS, 0 } );
    place( glm::ivec3( 20, 11, 29 ), { REDSTONE_WIRE_CROSS, 0, 0, REDSTONE_WIRE_CROSS, 0 } );

    run( 5 );
    failures += check_power( world, "repeater hard-powers the front block", glm::ivec3( 5, 11, 20 ), 15 );
    failures += check_power( world, "dust past hard-powered block", glm::ivec3( 6, 11, 20 ), 14 );
    failures += check_power( world, "torch on powered stone turns off", glm::ivec3( 5, 12, 20 ), 0 );
    failures += check_power( world, "lamp in front of repeater caps at 1", glm::ivec3( 10, 11, 20 ), 1 );
    failures += check_power( world, "chain: rep1 on", glm::ivec3( 15, 11, 24 ), 15 );
    failures += check_power( world, "chain: rep2 still off inside delay 2", glm::ivec3( 16, 11, 24 ), 0 );
    check_bool( "dust behind repeater is a LINE", world.get_loaded_block( glm::ivec3( 19, 11, 28 ) ).display_id == REDSTONE_WIRE_LINE_1 );
    check_bool( "dust beside repeater stays a CROSS", world.get_loaded_block( glm::ivec3( 20, 11, 29 ) ).display_id == REDSTONE_WIRE_CROSS );

    run( 10 );
    failures += check_power( world, "chain: rep2 on after both delays", glm::ivec3( 16, 11, 24 ), 15 );

    // --- support -------------------------------------------------------------
    // Removing the block under a repeater pops it like a torch.
    place( glm::ivec3( 26, 11, 28 ), rep_x );
    run( 4 );
    place( glm::ivec3( 26, 10, 28 ), BLOCK_STATE_AIR );
    run( 10 );
    failures += check_id( world, "repeater pops without its support", glm::ivec3( 26, 11, 28 ), AIR );

    check_bool( "no repeater ticks left pending", gs.pending_repeater_ticks.empty( ) );
    return failures;
}

// A redstone-queue probe for the batch-semantics check: counts its fires
// and, when `chain` is set, queues another copy of itself at delay 0 —
// which must wait for the next drain, not run in the same batch.
static int clock_probe_hits = 0;
class ClockProbeEvent : public BlockUpdateEvent {
  public:
    ClockProbeEvent( long tick_number, bool chain ) : BlockUpdateEvent( tick_number ), chain( chain ) {
    }
    void performAction( RepGameState &state ) override {
        clock_probe_hits++;
        if ( this->chain ) {
            schedule_redstone_update<ClockProbeEvent>( state, 0, false );
        }
    }

  private:
    bool chain;
};

// The redstone clock itself: the tick_number -> game-tick mapping, delay-0
// and delay-n firing semantics, dust staying fast, and the MC delays of
// torches, pistons, buttons and pressure plates.
int test_redstone_clock( ) {
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
    RepGameState &gs = *new RepGameState( ); // leaked on purpose — see test_redstone
    gs.camera.pos = glm::dvec3( 0, 0, 0 );
    test_setup_world( gs.world );
    World &world = gs.world;
    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    for ( int x = 0; x < 32; x++ ) {
        for ( int z = 0; z < 32; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 10, z ), stone );
        }
    }
    auto place = [ & ]( const glm::ivec3 &pos, const BlockState &block_state ) {
        gs.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( gs.tick_number, pos, block_state, false );
    };

    // --- clock mapping --------------------------------------------------------
    {
        bool monotonic = true, gaps78 = true;
        long prev = -1, last_change = 0;
        for ( long t = 0; t <= 200; t++ ) {
            gs.tick_number = t;
            const long now = redstone_tick_now( gs );
            if ( now < prev ) {
                monotonic = false;
            }
            if ( now != prev ) {
                if ( prev >= 0 && t - last_change != 7 && t - last_change != 8 ) {
                    gaps78 = false;
                }
                last_change = t;
            }
            prev = now;
        }
        gs.tick_number = 0;
        check_bool( "clock: 0 at tick 0", redstone_tick_now( gs ) == 0 );
        gs.tick_number = 144;
        check_bool( "clock: 20 at tick 144", redstone_tick_now( gs ) == 20 );
        check_bool( "clock: monotonic, advances every 7-8 ticks", monotonic && gaps78 );
        gs.tick_number = 0;
    }

    // --- delay 0 --------------------------------------------------------------
    // Fires in the next drain — no redstone-tick boundary needed — and a
    // delay-0 event it queues does not run in the same drain.
    clock_probe_hits = 0;
    schedule_redstone_update<ClockProbeEvent>( gs, 0, true );
    gs.tick_number++;
    repgame_drain_block_updates( gs );
    check_bool( "clock: delay-0 event fired on the next drain", clock_probe_hits == 1 );
    gs.tick_number++;
    repgame_drain_block_updates( gs );
    check_bool( "clock: delay-0 child waited for the next drain", clock_probe_hits == 2 );

    // --- delay n --------------------------------------------------------------
    // A STONE write at delay 3 stays unapplied while the clock is behind and
    // lands on the first drain reaching start + 3.
    const glm::ivec3 p3( 5, 11, 5 );
    const long start = redstone_tick_now( gs );
    schedule_redstone_update<PlayerBlockPlacedEvent>( gs, 3, p3, BlockState{ STONE, 0, 0, STONE, 0 }, false );
    bool early = false;
    while ( redstone_tick_now( gs ) < start + 3 ) {
        early |= world.get_loaded_block( p3 ).id == STONE;
        gs.tick_number++;
        repgame_drain_block_updates( gs );
    }
    check_bool( "clock: delay-3 write waits for the clock", !early );
    if ( world.get_loaded_block( p3 ).id != STONE ) {
        gs.tick_number++;
        repgame_drain_block_updates( gs );
    }
    check_bool( "clock: delay-3 write applied at the boundary", world.get_loaded_block( p3 ).id == STONE );

    // --- dust stays fast ------------------------------------------------------
    // A 10-dust line from a redstone block: ~1 hop per drain, so the far end
    // is powered well inside 4 redstone ticks. The source goes down last so
    // its neighbor wake recomputes the already-placed wire; the line avoids
    // x=0, a chunk border where test-world writes are dropped.
    for ( int x = 11; x <= 19; x++ ) {
        place( glm::ivec3( x, 11, 20 ), { REDSTONE_WIRE_CROSS, 0, 0, REDSTONE_WIRE_CROSS, 0 } );
    }
    place( glm::ivec3( 10, 11, 20 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );
    test_run_redstone_ticks( gs, 4 );
    check_bool( "clock: 10-dust line lit within 4 gt", world.get_loaded_block( glm::ivec3( 19, 11, 20 ) ).current_redstone_power > 0 );

    // --- torch: 2 gt ----------------------------------------------------------
    // Powering the stone under a torch turns it off 2 gt later. The rig sits
    // on the y=11 layer so the power source lands on an empty cell.
    place( glm::ivec3( 14, 11, 8 ), stone );
    place( glm::ivec3( 14, 12, 8 ), { REDSTONE_TORCH, 0, 0, REDSTONE_TORCH, 0 } );
    test_run_redstone_ticks( gs, 3 );
    failures += check_power( world, "clock: torch on before the edge", glm::ivec3( 14, 12, 8 ), 15 );
    place( glm::ivec3( 15, 11, 8 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );
    long attach_on = -1, torch_off = -1;
    for ( int i = 0; i < 80; i++ ) {
        gs.tick_number++;
        repgame_drain_block_updates( gs );
        const long now = redstone_tick_now( gs );
        if ( attach_on < 0 && world.get_loaded_block( glm::ivec3( 14, 11, 8 ) ).current_redstone_power > 0 ) {
            attach_on = now;
        }
        if ( torch_off < 0 && world.get_loaded_block( glm::ivec3( 14, 12, 8 ) ).current_redstone_power == 0 ) {
            torch_off = now;
        }
    }
    check_bool( "clock: torch reads 15 until 2 gt", attach_on >= 0 && torch_off - attach_on >= 2 );
    check_bool( "clock: torch off by 3 gt", attach_on >= 0 && torch_off - attach_on <= 3 );

    // --- piston: move starts on the edge, lands 2 gt later ----------------------
    // The delay-0 PistonEvent writes MOVING_PISTON cells on its drain; the
    // landed head appears PISTON_MOVE_GT after that.
    place( glm::ivec3( 18, 11, 12 ), { PISTON, 0, 0, PISTON, 0 } ); // faces +z
    test_run_redstone_ticks( gs, 2 );
    place( glm::ivec3( 18, 11, 11 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );
    long edge = -1, head_on = -1;
    for ( int i = 0; i < 40; i++ ) {
        gs.tick_number++;
        repgame_drain_block_updates( gs );
        const long now = redstone_tick_now( gs );
        if ( edge < 0 && world.get_loaded_block( glm::ivec3( 18, 11, 12 ) ).current_redstone_power > 0 ) {
            edge = now;
        }
        if ( head_on < 0 && world.get_loaded_block( glm::ivec3( 18, 11, 13 ) ).id == PISTON_ARM_COLLISION ) {
            head_on = now;
        }
    }
    check_bool( "clock: piston lands 2+ gt after its edge", edge >= 0 && head_on - edge >= 2 );
    check_bool( "clock: piston extended", head_on >= 0 && head_on - edge <= 4 );

    // --- buttons: 20/30 gt -----------------------------------------------------
    // rot-0 buttons mount on the -z side, so each gets a stone behind it.
    auto button_release_gt = [ & ]( const glm::ivec3 &pos, const BlockState &button ) {
        place( pos + glm::ivec3( 0, 0, -1 ), stone );
        place( pos, button );
        test_run_redstone_ticks( gs, 2 );
        repgame_press_button( gs, pos );
        test_run_game_ticks( gs, 2 ); // the instant press write applies
        const long t0 = redstone_tick_now( gs );
        long rel = -1;
        for ( int i = 0; i < 300; i++ ) {
            gs.tick_number++;
            repgame_drain_block_updates( gs );
            if ( world.get_loaded_block( pos ).current_redstone_power == 0 ) {
                rel = redstone_tick_now( gs ) - t0;
                break;
            }
        }
        return rel;
    };
    const long stone_rel = button_release_gt( glm::ivec3( 22, 11, 16 ), { STONE_BUTTON, 0, 0, STONE_BUTTON, 0 } );
    check_bool( "clock: stone button released in [19,21] gt", stone_rel >= 19 && stone_rel <= 21 );
    const long wood_rel = button_release_gt( glm::ivec3( 24, 11, 16 ), { WOODEN_BUTTON, 0, 0, WOODEN_BUTTON, 0 } );
    check_bool( "clock: wood button released in [29,31] gt", wood_rel >= 29 && wood_rel <= 31 );

    // --- pressure plate: 20 gt recheck -----------------------------------------
    // A pressed plate with nobody on it releases at its next recheck.
    const glm::ivec3 plate( 26, 11, 24 );
    place( plate, { STONE_PRESSURE_PLATE, 0, 0, STONE_PRESSURE_PLATE, 0 } );
    test_run_redstone_ticks( gs, 2 );
    gs.camera.pos = glm::dvec3( 26.5, 11.0 + PLAYER_EYE_HEIGHT, 24.5 );
    gs.watched_pressure_plates.insert( plate );
    schedule_redstone_update<PressurePlateEvent>( gs, 0, plate );
    test_run_redstone_ticks( gs, 2 );
    check_bool( "clock: plate pressed while stood on", world.get_loaded_block( plate ).current_redstone_power == 15 );
    gs.camera.pos.y += 10.0;
    const long t0 = redstone_tick_now( gs );
    long plate_rel = -1;
    for ( int i = 0; i < 300; i++ ) {
        gs.tick_number++;
        repgame_drain_block_updates( gs );
        if ( world.get_loaded_block( plate ).current_redstone_power == 0 ) {
            plate_rel = redstone_tick_now( gs ) - t0;
            break;
        }
    }
    check_bool( "clock: plate releases within 21 gt", plate_rel >= 1 && plate_rel <= 21 );

    return failures;
}

// Piston slide animation: a power edge writes MOVING_PISTON cells locally and
// spawns one kinematic BODY_PISTON per mover; per-cell PistonLandEvents land
// the carried states PISTON_MOVE_GT (2 gt) later, relay then.
int test_piston( ) {
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
    RepGameState &gs = *new RepGameState( ); // leaked on purpose — see test_redstone
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 0, 0, 0 );
    test_setup_world( gs.world );
    World &world = gs.world;
    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    for ( int x = 0; x < 32; x++ ) {
        for ( int z = 0; z < 32; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 10, z ), stone );
        }
    }
    auto place = [ & ]( const glm::ivec3 &pos, const BlockState &block_state ) {
        gs.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( gs.tick_number, pos, block_state, false );
    };
    auto piston_body_count = [ & ]( ) {
        int n = 0;
        for ( const auto &b : world.bodies.debug_bodies( ) ) {
            n += b.kind == BlockBodies::BODY_PISTON;
        }
        return n;
    };
    auto moving_count = [ & ]( ) {
        int n = 0;
        for ( int x = 0; x < 32; x++ ) {
            for ( int y = 0; y < 32; y++ ) {
                for ( int z = 0; z < 32; z++ ) {
                    n += world.get_loaded_block( glm::ivec3( x, y, z ) ).id == MOVING_PISTON;
                }
            }
        }
        return n;
    };

    // --- extension: MOVING_PISTON mid-state, lands after 2 gt -----------------
    // Piston at (5,11,5) faces +z, pushes the stone at (5,11,6) into (5,11,7).
    place( glm::ivec3( 5, 11, 5 ), { PISTON, 0, 0, PISTON, 0 } );
    place( glm::ivec3( 5, 11, 6 ), stone );
    test_run_redstone_ticks( gs, 2 );
    place( glm::ivec3( 5, 11, 4 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );
    test_run_game_ticks( gs, 3 ); // the edge + the delay-0 PistonEvent both ran
    const BlockState mid_head = world.get_loaded_block( glm::ivec3( 5, 11, 6 ) );
    const BlockState mid_dest = world.get_loaded_block( glm::ivec3( 5, 11, 7 ) );
    check_bool( "piston: head cell is MOVING_PISTON mid-slide", mid_head.id == MOVING_PISTON );
    check_bool( "piston: head cell carries the head id", mid_head.display_id == PISTON_ARM_COLLISION );
    check_bool( "piston: pushed cell is MOVING_PISTON mid-slide", mid_dest.id == MOVING_PISTON );
    check_bool( "piston: pushed cell carries the stone", mid_dest.display_id == STONE );
    check_bool( "piston: side table holds both movers", gs.piston_moves.size( ) == 2 );
    check_bool( "piston: one BODY_PISTON per mover", piston_body_count( ) == 2 );
    test_run_redstone_ticks( gs, 3 );
    failures += check_id( world, "piston: head landed", glm::ivec3( 5, 11, 6 ), PISTON_ARM_COLLISION );
    failures += check_id( world, "piston: pushed stone landed", glm::ivec3( 5, 11, 7 ), STONE );
    check_bool( "piston: no movers after landing", piston_body_count( ) == 0 );
    check_bool( "piston: side table drained", gs.piston_moves.empty( ) );
    check_bool( "piston: no orphan MOVING_PISTON cells", moving_count( ) == 0 );

    // --- plain retract: base stays MOVING_PISTON for the slide ----------------
    // Cutting the power on the extended piston at (5,11,5) makes the base a
    // moving cell too, so it keeps drawing the extended model until landing.
    place( glm::ivec3( 5, 11, 4 ), BLOCK_STATE_AIR );
    // The power-0 write and the MOVING_PISTON swap happen in the same drain:
    // drain-by-drain the base must never appear as an unpowered piston.
    bool saw_moving = false;
    bool saw_unpowered_base = false;
    for ( int i = 0; i < 16 && !saw_moving; i++ ) {
        gs.tick_number++;
        repgame_drain_block_updates( gs );
        const BlockState s = world.get_loaded_block( glm::ivec3( 5, 11, 5 ) );
        saw_moving = s.id == MOVING_PISTON;
        saw_unpowered_base = saw_unpowered_base || ( s.id == PISTON && s.current_redstone_power == 0 );
    }
    check_bool( "piston: base never flashes the retracted model", saw_moving && !saw_unpowered_base );
    const BlockState mid_base = world.get_loaded_block( glm::ivec3( 5, 11, 5 ) );
    check_bool( "piston: base is MOVING_PISTON mid-retract", mid_base.id == MOVING_PISTON && mid_base.display_id == PISTON );
    check_bool( "piston: base mover + head mover mid-retract", piston_body_count( ) == 2 );
    test_run_redstone_ticks( gs, 3 );
    const BlockState landed_base = world.get_loaded_block( glm::ivec3( 5, 11, 5 ) );
    check_bool( "piston: base landed as unpowered piston", landed_base.id == PISTON && landed_base.current_redstone_power == 0 );
    failures += check_id( world, "piston: retracted head cell is AIR", glm::ivec3( 5, 11, 6 ), AIR );
    check_bool( "piston: plain retract left no orphans", moving_count( ) == 0 && gs.piston_moves.empty( ) );
    // Head-slide movers have no table entry and only expire in bodies.tick —
    // drain them so later body counts aren't polluted.
    world.bodies.tick( gs );
    check_bool( "piston: retract movers expired", piston_body_count( ) == 0 );

    // --- sticky retract: pull cell AIRs now, pulled block lands at 2 gt ------
    // Sticky piston at (8,11,5) faces +z; its head takes (8,11,6) and pulls
    // the stone at (8,11,7) back to (8,11,6) on release.
    place( glm::ivec3( 8, 11, 5 ), { STICKY_PISTON, 0, 0, STICKY_PISTON, 0 } );
    place( glm::ivec3( 8, 11, 7 ), stone );
    test_run_redstone_ticks( gs, 2 );
    place( glm::ivec3( 8, 11, 4 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );
    test_run_redstone_ticks( gs, 4 ); // extended: head at (8,11,6)
    failures += check_id( world, "piston: sticky head extended", glm::ivec3( 8, 11, 6 ), STICKY_PISTON_ARM_COLLISION );
    place( glm::ivec3( 8, 11, 4 ), BLOCK_STATE_AIR );
    test_run_game_ticks( gs, 3 );
    failures += check_id( world, "piston: pull cell vacated at once", glm::ivec3( 8, 11, 7 ), AIR );
    const BlockState mid_pull = world.get_loaded_block( glm::ivec3( 8, 11, 6 ) );
    check_bool( "piston: pulled block slides as MOVING_PISTON", mid_pull.id == MOVING_PISTON && mid_pull.display_id == STONE );
    const BlockState mid_sbase = world.get_loaded_block( glm::ivec3( 8, 11, 5 ) );
    check_bool( "piston: sticky base is MOVING_PISTON mid-retract", mid_sbase.id == MOVING_PISTON && mid_sbase.display_id == STICKY_PISTON );
    check_bool( "piston: base + head + pull movers mid-retract", piston_body_count( ) == 3 );
    test_run_redstone_ticks( gs, 3 );
    const BlockState landed_sbase = world.get_loaded_block( glm::ivec3( 8, 11, 5 ) );
    check_bool( "piston: sticky base landed unpowered", landed_sbase.id == STICKY_PISTON && landed_sbase.current_redstone_power == 0 );
    failures += check_id( world, "piston: pulled stone landed at head cell", glm::ivec3( 8, 11, 6 ), STONE );
    check_bool( "piston: retract left no orphans", moving_count( ) == 0 && gs.piston_moves.empty( ) );

    // --- power drop mid-extension: finish-on-reverse ---------------------------
    // Piston at (11,11,5) starts pushing the stone to (11,11,7); cutting the
    // power before 2 gt lands the in-flight move, then retracts the head.
    place( glm::ivec3( 11, 11, 5 ), { PISTON, 0, 0, PISTON, 0 } );
    place( glm::ivec3( 11, 11, 6 ), stone );
    test_run_redstone_ticks( gs, 2 );
    place( glm::ivec3( 11, 11, 4 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );
    test_run_game_ticks( gs, 3 ); // mid-slide: MOVING_PISTON at (11,11,6)+(11,11,7)
    check_bool( "piston: reverse setup is mid-slide", world.get_loaded_block( glm::ivec3( 11, 11, 7 ) ).id == MOVING_PISTON );
    place( glm::ivec3( 11, 11, 4 ), BLOCK_STATE_AIR );
    test_run_redstone_ticks( gs, 4 );
    failures += check_id( world, "piston: reverse finished the push", glm::ivec3( 11, 11, 7 ), STONE );
    failures += check_id( world, "piston: reverse removed the head", glm::ivec3( 11, 11, 6 ), AIR );
    check_bool( "piston: reverse left no orphans", moving_count( ) == 0 && gs.piston_moves.empty( ) );

    // --- re-power mid-retraction: base lands powered, extends again -----------
    // Piston at (20,11,5) faces +z. The retracting base cell is MOVING_PISTON
    // and ignores redstone, so the recompute on landing picks the power up.
    place( glm::ivec3( 20, 11, 5 ), { PISTON, 0, 0, PISTON, 0 } );
    test_run_redstone_ticks( gs, 2 );
    place( glm::ivec3( 20, 11, 4 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );
    test_run_redstone_ticks( gs, 4 ); // extended: head at (20,11,6)
    failures += check_id( world, "piston: repower head extended", glm::ivec3( 20, 11, 6 ), PISTON_ARM_COLLISION );
    place( glm::ivec3( 20, 11, 4 ), BLOCK_STATE_AIR );
    test_run_game_ticks( gs, 3 ); // mid-retract: base is MOVING_PISTON
    check_bool( "piston: repower setup mid-retract", world.get_loaded_block( glm::ivec3( 20, 11, 5 ) ).id == MOVING_PISTON );
    place( glm::ivec3( 20, 11, 4 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );
    test_run_redstone_ticks( gs, 6 ); // base lands powered, re-extends, lands
    failures += check_id( world, "piston: repowered base landed", glm::ivec3( 20, 11, 5 ), PISTON );
    failures += check_id( world, "piston: repowered head back out", glm::ivec3( 20, 11, 6 ), PISTON_ARM_COLLISION );
    check_bool( "piston: repower left no orphans", moving_count( ) == 0 && gs.piston_moves.empty( ) );
    place( glm::ivec3( 20, 11, 4 ), BLOCK_STATE_AIR ); // settle: retract finishes
    test_run_redstone_ticks( gs, 4 );

    // --- attachment: a button mounted on the base survives a retraction -------
    // A rot-3 button's required solid face maps to its -x neighbour
    // (FACE_ROTATE_90^3[RIGHT] = FRONT), so the button at (26,11,5) mounts on
    // the +x face of the base at (25,11,5).
    place( glm::ivec3( 25, 11, 5 ), { PISTON, 0, 0, PISTON, 0 } );
    place( glm::ivec3( 26, 11, 5 ), { STONE_BUTTON, 3, 0, STONE_BUTTON, 0 } );
    test_run_redstone_ticks( gs, 2 );
    place( glm::ivec3( 25, 11, 4 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );
    test_run_redstone_ticks( gs, 4 ); // extended
    place( glm::ivec3( 25, 11, 4 ), BLOCK_STATE_AIR );
    test_run_game_ticks( gs, 3 ); // mid-retract
    failures += check_id( world, "piston: button survives mid-retract", glm::ivec3( 26, 11, 5 ), STONE_BUTTON );
    test_run_redstone_ticks( gs, 3 );
    failures += check_id( world, "piston: button survives the landing", glm::ivec3( 26, 11, 5 ), STONE_BUTTON );
    check_bool( "piston: button case left no orphans", moving_count( ) == 0 && gs.piston_moves.empty( ) );

    // --- repeater delay survives the push -------------------------------------
    // A delay-3 repeater (delay lives in display_id) keeps its full state
    // through the side table, not just the fallback encoding.
    place( glm::ivec3( 14, 11, 5 ), { PISTON, 0, 0, PISTON, 0 } );
    place( glm::ivec3( 14, 11, 6 ), { REPEATER, 0, 0, REPEATER_DELAY_3, 0 } );
    test_run_redstone_ticks( gs, 2 );
    place( glm::ivec3( 14, 11, 4 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );
    test_run_redstone_ticks( gs, 4 );
    const BlockState landed_rep = world.get_loaded_block( glm::ivec3( 14, 11, 7 ) );
    check_bool( "piston: repeater landed at destination", landed_rep.id == REPEATER );
    check_bool( "piston: repeater kept delay 3", landed_rep.display_id == REPEATER_DELAY_3 );

    // --- rescue: a MOVING_PISTON without an entry lands on its encoding -------
    world.set_loaded_block( glm::ivec3( 17, 11, 5 ), { MOVING_PISTON, 0, 0, GOLD_BLOCK, 0 } );
    piston_land_cell( gs, glm::ivec3( 17, 11, 5 ) );
    failures += check_id( world, "piston: orphan MOVING_PISTON landed via fallback", glm::ivec3( 17, 11, 5 ), GOLD_BLOCK );
    return failures;
}
