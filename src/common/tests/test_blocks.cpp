// Block behaviour tests — split out of rep_tests.cpp.
#include "test_util.hpp"
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

    // Flat world (world_type `flat`): sandstone floor at y=0, air above —
    // same ground as block world without the grid.
    check_bool( "flat: floor is sandstone at y=0", MapGen::flat_world_id( 0, 0, 0 ) == SANDSTONE );
    check_bool( "flat: sandstone below floor", MapGen::flat_world_id( 5, -3, 9 ) == SANDSTONE );
    check_bool( "flat: air above floor", MapGen::flat_world_id( 0, 1, 0 ) == AIR );
    check_bool( "flat: air far above", MapGen::flat_world_id( 100, 50, -7 ) == AIR );
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

    // Diagonal flow regression: breaking the block at (31,10,30) queues a
    // diagonal dust wake whose updating cell is the water at (30,11,30) —
    // down-and-over, not a face neighbor — and the expansion used to run on
    // it anyway, putting water where no face path reaches. The +x face of
    // the water stays open: that corner cell (31,11,30) is what gates the
    // up-and-over wake in queue_neighbor_block_updates, so sealing it would
    // never queue the diagonal event the regression needs.
    world.set_loaded_block( glm::ivec3( 30, 10, 30 ), stone );                  // floor under the water
    world.set_loaded_block( glm::ivec3( 30, 11, 30 ), { WATER, 0, 0, WATER, 0 } );
    world.set_loaded_block( glm::ivec3( 29, 11, 30 ), stone );                  // -x face sealed
    world.set_loaded_block( glm::ivec3( 30, 11, 29 ), stone );                  // -z face sealed
    world.set_loaded_block( glm::ivec3( 30, 11, 31 ), stone );                  // +z face sealed
    world.set_loaded_block( glm::ivec3( 31, 10, 30 ), stone );                  // diagonal cell to break
    place( glm::ivec3( 31, 10, 30 ), BLOCK_STATE_AIR );
    for ( int i = 0; i < 60; i++ ) {
        gs.tick_number++;
        gs.blockUpdateQueue.processAllBlockUpdates( gs, gs.tick_number );
    }
    failures += check_id( world, "water does not flow diagonally", glm::ivec3( 31, 10, 30 ), AIR );
    failures += check_id( world, "diagonal source water remains", glm::ivec3( 30, 11, 30 ), WATER );
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

