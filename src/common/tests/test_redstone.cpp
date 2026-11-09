// Redstone + door mechanics tests — split out of rep_tests.cpp.
#include "test_util.hpp"
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
        for ( int i = 0; i < ticks; i++ ) {
            gs.tick_number++;
            gs.blockUpdateQueue.processAllBlockUpdates( gs, gs.tick_number );
        }
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

