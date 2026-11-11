// Block behaviour tests — split out of rep_tests.cpp.
#include "test_util.hpp"
#include "common/block_update_events/LeafDecayEvent.hpp"
#include "common/block_update_events/BlockNextToChangeEvent.hpp"
#include "common/tree_grow.hpp"
#include "common/break_drops.hpp"
#include "common/ui_overlay_buffers.hpp"
#include "common/mesh_gen.hpp"
// Same CPU-side include as chunk.cpp: pulls mesh_face_seethrough et al. in
// as static inline helpers for the slab coverage checks.
#define MG_QUAL static inline
#include "common/mesh_gen_fns.hpp"
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

    // --- fence_pick_boxes -------------------------------------------------
    check_bool( "pick: no connections gives just the post",
                fence_pick_boxes( 0, boxes ) == 1 &&                                                            //
                    boxes[ 0 ].scale[ 0 ] == 4 && boxes[ 0 ].scale[ 1 ] == 16 && boxes[ 0 ].scale[ 2 ] == 4 && //
                    boxes[ 0 ].offset[ 0 ] == 6 && boxes[ 0 ].offset[ 1 ] == 0 && boxes[ 0 ].offset[ 2 ] == 6 );
    check_bool( "pick: one side gives post + 1 merged rail",
                fence_pick_boxes( FENCE_CONNECT_POS_X, boxes ) == 2 &&                                  //
                    boxes[ 1 ].offset[ 0 ] == 10 && boxes[ 1 ].scale[ 0 ] == 6 &&                       //
                    boxes[ 1 ].offset[ 1 ] == 6 && boxes[ 1 ].scale[ 1 ] == 9 && /* rails y 6..9 and    //
                        12..15 merge into y 6..15 */                                                    //
                    boxes[ 1 ].offset[ 2 ] == 7 && boxes[ 1 ].scale[ 2 ] == 2 );
    check_bool( "pick: opposite sides merge into one rail",
                fence_pick_boxes( FENCE_CONNECT_POS_X | FENCE_CONNECT_NEG_X, boxes ) == 2 && //
                    boxes[ 1 ].offset[ 0 ] == 0 && boxes[ 1 ].scale[ 0 ] == 16 );
    check_bool( "pick: all four sides give 3 boxes", fence_pick_boxes( 0xF, boxes ) == 3 );
    all_bounds = true;
    for ( unsigned conn = 0; conn < 16; conn++ ) {
        const int n = fence_pick_boxes( conn, boxes );
        all_bounds &= n == 1 + ( ( conn & ( FENCE_CONNECT_POS_X | FENCE_CONNECT_NEG_X ) ) != 0 ) + ( ( conn & ( FENCE_CONNECT_POS_Z | FENCE_CONNECT_NEG_Z ) ) != 0 );
        all_bounds &= boxes_in_bounds( boxes, n );
    }
    check_bool( "pick: every connection mask stays in bounds", all_bounds );

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
    check_bool( "gate: closed gives 7 boxes", fence_gate_boxes( false, boxes ) == 7 );
    check_bool( "gate: closed posts at x 0..2 and 14..16",
                boxes[ 0 ].offset[ 0 ] == 0 && boxes[ 0 ].scale[ 0 ] == 2 && boxes[ 1 ].offset[ 0 ] == 14 && //
                    boxes[ 0 ].offset[ 1 ] == 5 && boxes[ 0 ].scale[ 1 ] == 11 && boxes[ 0 ].offset[ 2 ] == 7 && boxes[ 0 ].scale[ 2 ] == 2 );
    // Rails split at the center upright — an unsplit rail's z faces would sit
    // coplanar with the upright's and z-fight.
    check_bool( "gate: closed rail halves x 2..6 and 10..14 y 6..9",
                boxes[ 2 ].offset[ 0 ] == 2 && boxes[ 2 ].scale[ 0 ] == 4 && boxes[ 2 ].offset[ 1 ] == 6 && boxes[ 2 ].scale[ 1 ] == 3 && //
                    boxes[ 3 ].offset[ 0 ] == 10 && boxes[ 3 ].scale[ 0 ] == 4 );
    check_bool( "gate: closed rail ends skip both x faces",
                boxes[ 2 ].skip_faces == ( ( 1u << FACE_LEFT ) | ( 1u << FACE_RIGHT ) ) );
    check_bool( "gate: closed upright x 6..10 y 6..15",
                boxes[ 6 ].offset[ 0 ] == 6 && boxes[ 6 ].scale[ 0 ] == 4 && boxes[ 6 ].offset[ 1 ] == 6 && boxes[ 6 ].scale[ 1 ] == 9 );
    check_bool( "gate: closed in bounds", boxes_in_bounds( boxes, 7 ) );
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

    // --- item icons: the real model boxes, emitted back-to-front ------------
    const Block *oak_gate = block_definition_get_definition( FENCE_GATE );
    check_bool( "icon: fence + gate are isometric", oak->icon_is_isometric && oak_gate->icon_is_isometric );
    const int n_fence_icon = fence_icon_boxes( boxes );
    check_bool( "icon: fence shows a connected segment", n_fence_icon == 5 );
    check_bool( "icon: fence rails split at the post",
                boxes[ 0 ].offset[ 2 ] == 0 && boxes[ 0 ].scale[ 2 ] == 6 && boxes[ 0 ].scale[ 0 ] == 2 && // back half
                    boxes[ 1 ].offset[ 2 ] == 0 && boxes[ 1 ].scale[ 2 ] == 6 &&                              // back half
                    boxes[ 2 ].scale[ 0 ] == 4 && boxes[ 2 ].scale[ 1 ] == 16 && boxes[ 2 ].offset[ 0 ] == 6 && // post
                    boxes[ 3 ].offset[ 2 ] == 10 && boxes[ 3 ].scale[ 2 ] == 6 &&                             // front half
                    boxes[ 4 ].offset[ 2 ] == 10 && boxes[ 4 ].scale[ 2 ] == 6 );                             // front half
    const int n_gate_icon = fence_gate_icon_boxes( boxes );
    check_bool( "icon: gate shows the closed leaf", n_gate_icon == 7 );
    check_bool( "icon: gate rails split at the upright",
                boxes[ 1 ].offset[ 2 ] == 2 && boxes[ 1 ].scale[ 2 ] == 4 && // back rail half z 2..6
                    boxes[ 3 ].offset[ 2 ] == 6 && boxes[ 3 ].scale[ 2 ] == 4 && // upright z 6..10
                    boxes[ 4 ].offset[ 2 ] == 10 && boxes[ 4 ].scale[ 2 ] == 4 ); // front rail half z 10..14
    check_bool( "icon: gate posts bracket the leaf",
                boxes[ 0 ].offset[ 2 ] == 0 && boxes[ 0 ].scale[ 1 ] == 11 && // far post first
                    boxes[ n_gate_icon - 1 ].offset[ 2 ] == 14 && boxes[ n_gate_icon - 1 ].scale[ 1 ] == 11 ); // near post last
    check_bool( "icon: box counts", block_icon_box_count( oak ) == 5 && block_icon_box_count( oak_gate ) == 7 );

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

        // Raycast: connected rails pick as one merged bar — a ray through
        // the +x rail (x 10..16px, z 7..9px, merged y 6..15px) hits the
        // fence even though the post box never reaches x>10px, and a ray
        // aimed at the rendered gap between the two rails (y 9..12px) hits
        // it too; only the unconnected -x side stays transparent. These are
        // the same boxes the MouseSelection outline draws.
        glm::ivec3 out( -1, -1, -1 );
        int which_face = -1;
        check_bool( "ray: +z ray hits the connected +x rail",
                    RayTraversal::find_block_from_to( world, glm::dvec3( 20.8, 11.5, 20.2 ), glm::dvec3( 20.8, 11.5, 20.8 ), out, &which_face ) &&
                        out == glm::ivec3( 20, 11, 20 ) && which_face == FACE_FRONT );
        check_bool( "ray: the merged rail covers the gap between rails",
                    RayTraversal::find_block_from_to( world, glm::dvec3( 20.8, 11.65, 20.2 ), glm::dvec3( 20.8, 11.65, 20.8 ), out, &which_face ) &&
                        out == glm::ivec3( 20, 11, 20 ) && which_face == FACE_FRONT );
        check_bool( "ray: the unconnected -x side stays open",
                    !RayTraversal::find_block_from_to( world, glm::dvec3( 20.2, 11.5, 20.2 ), glm::dvec3( 20.2, 11.5, 20.8 ), out, &which_face ) );

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

// Stairs: the MC getStairsShape corner derive, def-space box geometry, the
// world yaw remap, the top-half bit, generated defs, and world-level
// collision/raycast including a walk-up-the-staircase sim. stair_rots[]
// indexes the same direction space as rotation & 3: 0=-z(front), 1=+x
// (right of -z), 2=+z(back), 3=-x(left).
int test_stairs( ) {
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
    const int TOP = BLOCK_ROTATE_STAIRS_TOP;

    // --- stairs_shape_derive (facing 0 = -z: front idx 0, back idx 2, left idx 3, right idx 1)
    {
        int r[ 4 ] = { -1, -1, -1, -1 };
        check_bool( "derive: lone stair is straight", stairs_shape_derive( 0, r ) == STAIRS_STRAIGHT );
        check_bool( "derive: lone top-half stair is straight", stairs_shape_derive( TOP, r ) == STAIRS_STRAIGHT );

        r[ 0 ] = 0;
        check_bool( "derive: same-facing stair in front stays straight", stairs_shape_derive( 0, r ) == STAIRS_STRAIGHT );
        r[ 0 ] = 2;
        check_bool( "derive: back-facing stair in front stays straight", stairs_shape_derive( 0, r ) == STAIRS_STRAIGHT );
        r[ 0 ] = 1;
        check_bool( "derive: front stair facing right -> outer right", stairs_shape_derive( 0, r ) == STAIRS_OUTER_RIGHT );
        check_bool( "derive: bottom-half front stair ignored for top half", stairs_shape_derive( TOP, r ) == STAIRS_STRAIGHT );
        r[ 0 ] = 3;
        check_bool( "derive: front stair facing left -> outer left", stairs_shape_derive( 0, r ) == STAIRS_OUTER_LEFT );
        r[ 0 ] = 1 | TOP;
        check_bool( "derive: different-half front stair ignored", stairs_shape_derive( 0, r ) == STAIRS_STRAIGHT );
        check_bool( "derive: top half merges with top half", stairs_shape_derive( TOP, r ) == STAIRS_OUTER_RIGHT );

        // Third-side suppression: with the front stair facing +x, a stair
        // at -x (dir 3) facing our way keeps us straight.
        r[ 0 ] = 1;
        r[ 3 ] = 0;
        check_bool( "derive: outer corner suppressed by third-side stair", stairs_shape_derive( 0, r ) == STAIRS_STRAIGHT );
        r[ 3 ] = 1;
        check_bool( "derive: perpendicular third side doesn't suppress", stairs_shape_derive( 0, r ) == STAIRS_OUTER_RIGHT );
        r[ 3 ] = -1;
        r[ 0 ] = -1;

        r[ 2 ] = 3;
        check_bool( "derive: back stair facing left -> inner left", stairs_shape_derive( 0, r ) == STAIRS_INNER_LEFT );
        // MC probes the merge-side cell (pos + back stair facing) for a
        // same-facing stair to suppress the corner.
        r[ 3 ] = 0;
        check_bool( "derive: inner left suppressed by -x stair", stairs_shape_derive( 0, r ) == STAIRS_STRAIGHT );
        r[ 3 ] = 1;
        check_bool( "derive: perpendicular -x stair doesn't suppress", stairs_shape_derive( 0, r ) == STAIRS_INNER_LEFT );
        r[ 3 ] = -1;
        r[ 2 ] = 1;
        check_bool( "derive: back stair facing right -> inner right", stairs_shape_derive( 0, r ) == STAIRS_INNER_RIGHT );
        r[ 1 ] = 0;
        check_bool( "derive: inner right suppressed by +x stair", stairs_shape_derive( 0, r ) == STAIRS_STRAIGHT );
        r[ 1 ] = -1;
        r[ 0 ] = 1;
        check_bool( "derive: outer beats inner when both match", stairs_shape_derive( 0, r ) == STAIRS_OUTER_RIGHT );
        r[ 0 ] = -1;
        r[ 2 ] = -1;

        // Facing 1 = +x: front idx 1, back idx 3, left = -z (idx 0), right = +z (idx 2).
        r[ 1 ] = 0;
        check_bool( "derive: +x facing, front stair faces left(-z) -> outer left", stairs_shape_derive( 1, r ) == STAIRS_OUTER_LEFT );
        r[ 1 ] = 2;
        check_bool( "derive: +x facing, front stair faces right(+z) -> outer right", stairs_shape_derive( 1, r ) == STAIRS_OUTER_RIGHT );
        r[ 1 ] = -1;
        r[ 3 ] = 0;
        check_bool( "derive: +x facing, back stair faces left -> inner left", stairs_shape_derive( 1, r ) == STAIRS_INNER_LEFT );
    }

    // --- stairs_boxes (def space: facing -z, full-height step at z 0..8) -----
    {
        BlockBox boxes[ STAIRS_MAX_BOXES ];
        check_bool( "boxes: straight bottom = slab + step",
                    stairs_boxes( STAIRS_STRAIGHT, false, boxes ) == 2 &&                       //
                        boxes[ 0 ].scale[ 1 ] == 8 && boxes[ 0 ].offset[ 1 ] == 0 &&            //
                        boxes[ 1 ].scale[ 0 ] == 16 && boxes[ 1 ].scale[ 2 ] == 8 &&            //
                        boxes[ 1 ].offset[ 1 ] == 8 && boxes[ 1 ].offset[ 2 ] == 0 );
        check_bool( "boxes: straight top flips the halves",
                    stairs_boxes( STAIRS_STRAIGHT, true, boxes ) == 2 &&                        //
                        boxes[ 0 ].offset[ 1 ] == 8 && boxes[ 1 ].offset[ 1 ] == 0 );
        check_bool( "boxes: step skips the coplanar slab face",
                    boxes[ 1 ].skip_faces == ( 1u << FACE_TOP ) );
        check_bool( "boxes: bottom step skips FACE_BOTTOM",
                    stairs_boxes( STAIRS_STRAIGHT, false, boxes ) == 2 && boxes[ 1 ].skip_faces == ( 1u << FACE_BOTTOM ) );
        check_bool( "boxes: outer left = slab + corner octet at (-x,-z)",
                    stairs_boxes( STAIRS_OUTER_LEFT, false, boxes ) == 2 &&                     //
                        boxes[ 1 ].scale[ 0 ] == 8 && boxes[ 1 ].scale[ 2 ] == 8 &&             //
                        boxes[ 1 ].offset[ 0 ] == 0 && boxes[ 1 ].offset[ 2 ] == 0 );
        check_bool( "boxes: outer right octet at (+x,-z)",
                    stairs_boxes( STAIRS_OUTER_RIGHT, false, boxes ) == 2 && boxes[ 1 ].offset[ 0 ] == 8 && boxes[ 1 ].offset[ 2 ] == 0 );
        check_bool( "boxes: inner left = slab + strip + octet at (-x,+z)",
                    stairs_boxes( STAIRS_INNER_LEFT, false, boxes ) == 3 &&                     //
                        boxes[ 1 ].scale[ 0 ] == 16 &&                                          //
                        boxes[ 2 ].scale[ 0 ] == 8 && boxes[ 2 ].offset[ 0 ] == 0 && boxes[ 2 ].offset[ 2 ] == 8 );
        check_bool( "boxes: inner right octet at (+x,+z)",
                    stairs_boxes( STAIRS_INNER_RIGHT, false, boxes ) == 3 && boxes[ 2 ].offset[ 0 ] == 8 && boxes[ 2 ].offset[ 2 ] == 8 );
        check_bool( "boxes: inner octet skips the -z seam",
                    ( boxes[ 2 ].skip_faces & ( 1u << FACE_BACK ) ) != 0 && ( boxes[ 2 ].skip_faces & ( 1u << FACE_BOTTOM ) ) != 0 );
        bool all_bounds = true;
        for ( int s = STAIRS_STRAIGHT; s <= STAIRS_OUTER_RIGHT; s++ ) {
            all_bounds &= boxes_in_bounds( boxes, stairs_boxes( static_cast<StairShape>( s ), false, boxes ) );
            all_bounds &= boxes_in_bounds( boxes, stairs_boxes( static_cast<StairShape>( s ), true, boxes ) );
        }
        check_bool( "boxes: every shape x half stays in bounds", all_bounds );
    }

    // --- stairs_collision_boxes: def-space boxes yawed to world --------------
    {
        BlockBox boxes[ STAIRS_MAX_BOXES ];
        int r[ 4 ] = { -1, -1, -1, -1 };
        check_bool( "collide: rot 0 step at -z",
                    stairs_collision_boxes( 0, r, boxes ) == 2 &&                               //
                        boxes[ 1 ].offset[ 2 ] == 0 && boxes[ 1 ].scale[ 2 ] == 8 );
        check_bool( "collide: rot 1 step at +x",
                    stairs_collision_boxes( 1, r, boxes ) == 2 &&                               //
                        boxes[ 1 ].offset[ 0 ] == 8 && boxes[ 1 ].scale[ 0 ] == 8 && boxes[ 1 ].scale[ 2 ] == 16 );
        check_bool( "collide: rot 2 step at +z",
                    stairs_collision_boxes( 2, r, boxes ) == 2 &&                               //
                        boxes[ 1 ].offset[ 2 ] == 8 && boxes[ 1 ].scale[ 2 ] == 8 );
        check_bool( "collide: rot 3 step at -x",
                    stairs_collision_boxes( 3, r, boxes ) == 2 &&                               //
                        boxes[ 1 ].offset[ 0 ] == 0 && boxes[ 1 ].scale[ 0 ] == 8 && boxes[ 1 ].scale[ 2 ] == 16 );
        check_bool( "collide: top-half slab on top",
                    stairs_collision_boxes( TOP, r, boxes ) == 2 && boxes[ 0 ].offset[ 1 ] == 8 );
        // Same neighbor merge the mesher sees: front stair facing +x gives
        // the outer-right octet in world space.
        r[ 0 ] = 1;
        check_bool( "collide: corner merge shrinks the step",
                    stairs_collision_boxes( 0, r, boxes ) == 2 &&                               //
                        boxes[ 1 ].offset[ 0 ] == 8 && boxes[ 1 ].scale[ 0 ] == 8 && boxes[ 1 ].scale[ 2 ] == 8 );
    }

    // --- generated definitions ------------------------------------------------
    const Block *oak_stairs = block_definition_get_definition( OAK_STAIRS );
    check_bool( "def: OAK_STAIRS is_stairs + rotates",
                oak_stairs->is_stairs && oak_stairs->rotate_on_placement &&                    //
                    oak_stairs->non_full_size && oak_stairs->collides_with_player &&           //
                    !oak_stairs->is_solid && oak_stairs->is_seethrough );
    check_bool( "def: stair yaw masks the top-half bit",
                block_yaw( oak_stairs, 0 | TOP ) == 0 && block_yaw( oak_stairs, 3 | TOP ) == 3 );
    check_bool( "def: plain side faces (no mask tile)",
                strstr( texture_name( oak_stairs->textures[ FACE_LEFT ] ), "__stairs" ) == nullptr );
    check_bool( "def: isometric stair icon", oak_stairs->icon_is_isometric );

    // Icon shape: slab + step yawed 270 so the tread faces +z (iso FRONT).
    BlockBox icon_boxes[ STAIRS_MAX_BOXES ];
    const int n_icon = stairs_icon_boxes( icon_boxes );
    check_bool( "icon: slab + step", n_icon == 2 );
    check_bool( "icon: slab is the bottom half", icon_boxes[ 0 ].scale[ 1 ] == 8 && icon_boxes[ 0 ].offset[ 1 ] == 0 );
    check_bool( "icon: step atop the far -x half",
                icon_boxes[ 1 ].scale[ 1 ] == 8 && icon_boxes[ 1 ].offset[ 1 ] == 8 && //
                    icon_boxes[ 1 ].scale[ 0 ] == 8 && icon_boxes[ 1 ].offset[ 0 ] == 0 );
    check_bool( "icon: two icon boxes", block_icon_box_count( oak_stairs ) == 2 );
    check_bool( "icon: stone is one", item_icon_box_count( STONE ) == 1 );

    check_bool( "showcase: stair run along z", MapGen::block_world_id( -2, 1, -14 ) == OAK_STAIRS );
    check_bool( "showcase: mixed row", MapGen::block_world_id( 6, 1, -14 ) == STONE_STAIRS );

    // --- world-level collision + raycast --------------------------------------
    {
        RepGameState &gs2 = *new RepGameState( );
        gs2.tick_number = 0;
        gs2.camera.pos = glm::dvec3( 0, 0, 0 );
        test_setup_world( gs2.world );
        World &world = gs2.world;
        const BlockState stone = { STONE, 0, 0, STONE, 0 };
        for ( int x = 18; x <= 23; x++ ) {
            for ( int z = 17; z <= 24; z++ ) {
                world.set_loaded_block( glm::ivec3( x, 10, z ), stone );
            }
        }
        const BlockState stair = { OAK_STAIRS, 0, 0, OAK_STAIRS, 0 };
        world.set_loaded_block( glm::ivec3( 20, 11, 20 ), stair );

        // rot 0 faces -z: step at z 20..20.5 (top y=12), slab top at y=11.5.
        // Probes sit on the front half, >half-width (0.3) clear of the step.
        const double eye_y = 11.0 + PLAYER_EYE_HEIGHT;
        check_bool( "world: feet on front slab collide at +0.4",
                    Collision::collides_at( world, glm::dvec3( 20.5, eye_y + 0.4, 20.95 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
        check_bool( "world: open air above front slab at +0.6",
                    !Collision::collides_at( world, glm::dvec3( 20.5, eye_y + 0.6, 20.95 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
        check_bool( "world: step top collides at +0.9",
                    Collision::collides_at( world, glm::dvec3( 20.5, eye_y + 0.9, 20.25 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
        check_bool( "world: clear above the step at +1.1",
                    !Collision::collides_at( world, glm::dvec3( 20.5, eye_y + 1.1, 20.25 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );

        // Top-half stair: the upper slab collides where the bottom stair
        // leaves air (feet +0.6), and the void under its step is open space.
        world.set_loaded_block( glm::ivec3( 22, 11, 20 ), { OAK_STAIRS, 0 | TOP, 0, OAK_STAIRS, 0 } );
        check_bool( "world: top stair's upper slab collides at +0.6",
                    Collision::collides_at( world, glm::dvec3( 22.5, eye_y + 0.6, 20.75 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
        check_bool( "world: top stair is clear above at +1.1",
                    !Collision::collides_at( world, glm::dvec3( 22.5, eye_y + 1.1, 20.75 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );

        // Neighbor merge in collision: a front stair facing +x shrinks our
        // step to the +x octet; a same-facing stair at -x un-shrinks it.
        world.set_loaded_block( glm::ivec3( 20, 11, 19 ), { OAK_STAIRS, 1, 0, OAK_STAIRS, 0 } );
        check_bool( "world: outer corner leaves -x open at step height",
                    !Collision::collides_at( world, glm::dvec3( 20.15, eye_y + 0.9, 20.25 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
        check_bool( "world: outer corner still steps at +x",
                    Collision::collides_at( world, glm::dvec3( 20.75, eye_y + 0.9, 20.25 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
        world.set_loaded_block( glm::ivec3( 19, 11, 20 ), { OAK_STAIRS, 0, 0, OAK_STAIRS, 0 } );
        check_bool( "world: third-side stair restores the straight step",
                    Collision::collides_at( world, glm::dvec3( 20.15, eye_y + 0.9, 20.25 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
        world.set_loaded_block( glm::ivec3( 19, 11, 20 ), BLOCK_STATE_AIR );
        world.set_loaded_block( glm::ivec3( 20, 11, 19 ), BLOCK_STATE_AIR );

        // Walking up a staircase: stair run ascending -z (+0.5 per cell
        // step) via the same STEP_HEIGHT step-up path slabs use.
        world.set_loaded_block( glm::ivec3( 20, 12, 19 ), { OAK_STAIRS, 0, 0, OAK_STAIRS, 0 } );
        glm::dvec3 pos( 20.5, eye_y + 0.5, 20.9 );
        int standing = 0;
        for ( int i = 0; i < 40 && pos.z > 19.2; i++ ) {
            glm::dvec3 vec( 0.0, -0.05, -0.05 );
            Collision::check_move( world, vec, pos, &standing, PLAYER_HEIGHT, PLAYER_EYE_HEIGHT, false );
            pos += vec;
        }
        const double feet = pos.y - PLAYER_EYE_HEIGHT;
        check_bool( "world: walked up the staircase to +13", pos.z < 19.6 && fabs( feet - 13.0 ) < 0.05 );
        world.set_loaded_block( glm::ivec3( 20, 12, 19 ), BLOCK_STATE_AIR );

        // Raycast: the upper-front void misses, the step and slab hit with
        // correct faces, and the hit t gives the in-cell y fraction.
        glm::ivec3 out( -1, -1, -1 );
        int which_face = -1;
        double hit_t = -1.0;
        check_bool( "ray: upper-front void misses",
                    !RayTraversal::find_block_from_to( world, glm::dvec3( 20.5, 11.75, 22.0 ), glm::dvec3( 20.5, 11.75, 20.75 ), out, &which_face ) );
        check_bool( "ray: step's +z face hits the stair",
                    RayTraversal::find_block_from_to( world, glm::dvec3( 20.5, 11.75, 22.0 ), glm::dvec3( 20.5, 11.75, 18.5 ), out, &which_face, 0, &hit_t ) &&
                        out == glm::ivec3( 20, 11, 20 ) && which_face == FACE_BACK );
        // Crossing the void horizontally, the ray ignores the stair's cell
        // and hits the top-half stair's upper slab on the far side.
        check_bool( "ray: through the void hits the top stair behind",
                    RayTraversal::find_block_from_to( world, glm::dvec3( 18.5, 11.75, 20.75 ), glm::dvec3( 22.5, 11.75, 20.75 ), out, &which_face ) &&
                        out == glm::ivec3( 22, 11, 20 ) && which_face == FACE_LEFT );
        hit_t = -1.0;
        check_bool( "ray: slab top hit reports t at +0.5",
                    RayTraversal::find_block_from_to( world, glm::dvec3( 20.5, 12.5, 20.75 ), glm::dvec3( 20.5, 10.5, 20.75 ), out, &which_face, 0, &hit_t ) &&
                        out == glm::ivec3( 20, 11, 20 ) && which_face == FACE_TOP && fabs( hit_t - 1.0 ) < 0.01 );
    }
    return failures;
}

// Half slabs: the derived is_slab flag, rotation bit 2 as the top-half
// marker (BLOCK_ROTATE_SLAB_TOP — shared with stairs/gates/doors), the
// mirrored collision/raycast box, the block_adjust geometry flip, and the
// mesh coverage flip. The placement rule itself (bottom face or upper side
// hit => top half) is the same branch stairs ride in getPlacedRotation.
int test_slabs( ) {
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
    const int TOP = BLOCK_ROTATE_SLAB_TOP;

    // --- generated definitions ---------------------------------------------
    const Block *oak_slab = block_definition_get_definition( OAK_SLAB );
    check_bool( "def: OAK_SLAB is_slab + bottom half box",
                oak_slab->is_slab && oak_slab->non_full_size && oak_slab->collides_with_player && //
                    oak_slab->scale.x == 16 && oak_slab->scale.y == 8 && oak_slab->scale.z == 16 && //
                    oak_slab->offset.x == 0 && oak_slab->offset.y == 0 && oak_slab->offset.z == 0 );
    check_bool( "def: doubles and lookalikes aren't slabs",
                !block_definition_get_definition( DOUBLE_STONE_SLAB )->is_slab && // full cube double
                    !block_definition_get_definition( STONE_SLAB2 )->is_slab &&   // SLAB-named full cube
                    !block_definition_get_definition( BEETROOT_2 )->is_slab &&    // slab box, no collision
                    !block_definition_get_definition( STONE )->is_slab );
    check_bool( "def: slab yaw masks the top-half bit", block_yaw( oak_slab, TOP ) == 0 && block_yaw( oak_slab, 0 ) == 0 );

    // --- block_adjust_coord_based_on_state ----------------------------------
    {
        BlockState top_state = { OAK_SLAB, TOP, 0, OAK_SLAB, 0 };
        BlockCoords coords = {};
        coords.scale_y = oak_slab->scale.y;
        coords.offset_y = oak_slab->offset.y;
        block_adjust_coord_based_on_state( oak_slab, &top_state, &coords );
        check_bool( "adjust: top slab box mirrors to y 8..16", coords.offset_y == 8 && coords.scale_y == 8 );
        BlockState bottom_state = { OAK_SLAB, 0, 0, OAK_SLAB, 0 };
        coords.offset_y = oak_slab->offset.y;
        block_adjust_coord_based_on_state( oak_slab, &bottom_state, &coords );
        check_bool( "adjust: bottom slab stays at y 0..8", coords.offset_y == 0 && coords.scale_y == 8 );
    }

    // --- mesh coverage: the mirrored box flips the y coverage lookups --------
    // A top slab covers its cell top like a bottom slab covers its bottom —
    // so the neighbor above's underside culls, and the slab's own top face
    // draws on the boundary instead of mid-cell.
    const GPUBlockDef *defs = mesh_def_table( );
    check_bool( "mesh: bottom slab covers its bottom, top open",
                !mesh_face_seethrough( defs[ OAK_SLAB ], 0, 0, FACE_BOTTOM ) && //
                    mesh_face_seethrough( defs[ OAK_SLAB ], 0, 0, FACE_TOP ) != 0 );
    check_bool( "mesh: top slab covers its top, bottom open",
                !mesh_face_seethrough( defs[ OAK_SLAB ], 0, TOP, FACE_TOP ) && //
                    mesh_face_seethrough( defs[ OAK_SLAB ], 0, TOP, FACE_BOTTOM ) != 0 );
    check_bool( "mesh: slab sides stay half-open on both halves",
                mesh_face_seethrough( defs[ OAK_SLAB ], 0, TOP, FACE_RIGHT ) != 0 && //
                    mesh_face_seethrough( defs[ OAK_SLAB ], 0, 0, FACE_RIGHT ) != 0 && //
                    mesh_slab_face( defs[ OAK_SLAB ], TOP, FACE_RIGHT ) == FACE_RIGHT );
    check_bool( "mesh: coverage face is identity for non-slabs",
                mesh_slab_face( defs[ STONE ], TOP, FACE_TOP ) == FACE_TOP && //
                    mesh_slab_face( defs[ OAK_SLAB ], 0, FACE_TOP ) == FACE_TOP );

    // --- piston head coverage -------------------------------------------------
    // A head plate only ever covers its head-end boundary — every other
    // side stays open (the arm doesn't cover a face), so a solid neighbor
    // beside a vertical head must still draw its face instead of culling
    // into a see-through hole.
    check_bool( "mesh: piston head covers only the head-end face",
                !mesh_face_seethrough( defs[ PISTON_ARM_COLLISION ], 0, 0, FACE_FRONT ) && //
                    mesh_face_seethrough( defs[ PISTON_ARM_COLLISION ], 0, 0, FACE_BACK ) != 0 && //
                    mesh_face_seethrough( defs[ PISTON_ARM_COLLISION ], 0, 0, FACE_TOP ) != 0 && //
                    mesh_face_seethrough( defs[ PISTON_ARM_COLLISION ], 0, 0, FACE_RIGHT ) != 0 );
    check_bool( "mesh: up/down piston heads cover only the plate plane",
                !mesh_face_seethrough( defs[ PISTON_ARM_COLLISION ], 0, PISTON_ROTATE_UP, FACE_TOP ) &&   //
                    mesh_face_seethrough( defs[ PISTON_ARM_COLLISION ], 0, PISTON_ROTATE_UP, FACE_BOTTOM ) != 0 && //
                    mesh_face_seethrough( defs[ PISTON_ARM_COLLISION ], 0, PISTON_ROTATE_UP, FACE_FRONT ) != 0 && //
                    !mesh_face_seethrough( defs[ PISTON_ARM_COLLISION ], 0, PISTON_ROTATE_DOWN, FACE_BOTTOM ) && //
                    mesh_face_seethrough( defs[ PISTON_ARM_COLLISION ], 0, PISTON_ROTATE_DOWN, FACE_TOP ) != 0 && //
                    mesh_face_seethrough( defs[ PISTON_ARM_COLLISION ], 0, PISTON_ROTATE_DOWN, FACE_BACK ) != 0 );

    // --- world-level collision + raycast -------------------------------------
    {
        RepGameState &gs2 = *new RepGameState( );
        gs2.tick_number = 0;
        gs2.camera.pos = glm::dvec3( 0, 0, 0 );
        test_setup_world( gs2.world );
        World &world = gs2.world;
        const BlockState stone = { STONE, 0, 0, STONE, 0 };
        for ( int x = 19; x <= 23; x++ ) {
            for ( int z = 19; z <= 22; z++ ) {
                world.set_loaded_block( glm::ivec3( x, 10, z ), stone );
            }
        }
        const double eye_y = 11.0 + PLAYER_EYE_HEIGHT;
        const BlockState bottom_slab = { OAK_SLAB, 0, 0, OAK_SLAB, 0 };
        const BlockState top_slab = { OAK_SLAB, TOP, 0, OAK_SLAB, 0 };
        world.set_loaded_block( glm::ivec3( 20, 11, 20 ), bottom_slab );
        world.set_loaded_block( glm::ivec3( 22, 11, 20 ), top_slab );
        world.set_loaded_block( glm::ivec3( 23, 11, 20 ), stone );
        // Air under the top slab's column so the up-ray can reach its
        // underside without crossing the floor's top face.
        world.set_loaded_block( glm::ivec3( 22, 10, 20 ), BLOCK_STATE_AIR );

        // The single collision box mirrors into the upper half.
        check_bool( "world: bottom slab collides at +0.4",
                    Collision::collides_at( world, glm::dvec3( 20.5, eye_y + 0.4, 20.75 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
        check_bool( "world: bottom slab open at +0.6",
                    !Collision::collides_at( world, glm::dvec3( 20.5, eye_y + 0.6, 20.75 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
        check_bool( "world: top slab collides at +0.6",
                    Collision::collides_at( world, glm::dvec3( 22.5, eye_y + 0.6, 20.75 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
        check_bool( "world: top slab clear above the cell at +1.1",
                    !Collision::collides_at( world, glm::dvec3( 22.5, eye_y + 1.1, 20.75 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
        const double bottom_top = Collision::block_collision_top( world, glm::ivec3( 20, 11, 20 ) );
        const double top_top = Collision::block_collision_top( world, glm::ivec3( 22, 11, 20 ) );
        check_bool( "world: collision tops at 11.5 / 12.0", fabs( bottom_top - 11.5 ) < 0.001 && fabs( top_top - 12.0 ) < 0.001 );

        // A dropped player lands on the top slab's surface, flush with the
        // cell top — same as standing on the full block that would be there.
        glm::dvec3 pos( 22.5, 11.0 + 1.6 + PLAYER_EYE_HEIGHT, 20.75 );
        int standing = 0;
        for ( int i = 0; i < 40 && standing == 0; i++ ) {
            glm::dvec3 vec( 0.0, -0.05, 0.0 );
            Collision::check_move( world, vec, pos, &standing, PLAYER_HEIGHT, PLAYER_EYE_HEIGHT, false );
            pos += vec;
        }
        const double feet = pos.y - PLAYER_EYE_HEIGHT;
        check_bool( "world: dropped player lands on top slab at 12.0", standing == 1 && fabs( feet - 12.0 ) < 0.05 );

        // Raycast: the pick box mirrors — rays hit the moved surfaces and
        // pass straight through the empty lower half to what's behind.
        glm::ivec3 out( -1, -1, -1 );
        int which_face = -1;
        double hit_t = -1.0;
        check_bool( "ray: down at top slab hits the cell-top face",
                    RayTraversal::find_block_from_to( world, glm::dvec3( 22.5, 12.5, 20.75 ), glm::dvec3( 22.5, 10.5, 20.75 ), out, &which_face, 0, &hit_t ) &&
                        out == glm::ivec3( 22, 11, 20 ) && which_face == FACE_TOP && fabs( hit_t - 0.5 ) < 0.01 );
        hit_t = -1.0;
        check_bool( "ray: up at top slab hits the mid-cell underside",
                    RayTraversal::find_block_from_to( world, glm::dvec3( 22.5, 10.5, 20.75 ), glm::dvec3( 22.5, 12.5, 20.75 ), out, &which_face, 0, &hit_t ) &&
                        out == glm::ivec3( 22, 11, 20 ) && which_face == FACE_BOTTOM && fabs( hit_t - 1.0 ) < 0.01 );
        check_bool( "ray: through the open lower half hits the stone behind",
                    RayTraversal::find_block_from_to( world, glm::dvec3( 21.5, 11.25, 20.75 ), glm::dvec3( 23.5, 11.25, 20.75 ), out, &which_face ) &&
                        out == glm::ivec3( 23, 11, 20 ) && which_face == FACE_LEFT );
        check_bool( "ray: upper half picks the slab's own side",
                    RayTraversal::find_block_from_to( world, glm::dvec3( 21.5, 11.75, 20.75 ), glm::dvec3( 22.5, 11.75, 20.75 ), out, &which_face ) &&
                        out == glm::ivec3( 22, 11, 20 ) && which_face == FACE_LEFT );
        check_bool( "ray: down at bottom slab hits the half-height top",
                    RayTraversal::find_block_from_to( world, glm::dvec3( 20.5, 12.5, 20.75 ), glm::dvec3( 20.5, 10.5, 20.75 ), out, &which_face, 0, &hit_t ) &&
                        out == glm::ivec3( 20, 11, 20 ) && which_face == FACE_TOP && fabs( hit_t - 1.0 ) < 0.01 );
    }
    return failures;
}

// Cake eating: the is_cake flag, bites stored in rotation (never a yaw —
// block_yaw masks it to 0 so the wall-torch branch can't see it), the +x
// edge shrinking 2px per bite across block_adjust/collision/raycast, the
// interior face texture, and the support check keeping the floor face.
int test_cake( ) {
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

    // --- generated definitions ---------------------------------------------
    const Block *cake = block_definition_get_definition( CAKE );
    check_bool( "def: CAKE is_cake partial box",
                cake->is_cake && cake->non_full_size &&                                          //
                    cake->scale.x == 14 && cake->scale.y == 8 && cake->scale.z == 14 &&          //
                    cake->offset.x == 1 && cake->offset.y == 0 && cake->offset.z == 1 );
    check_bool( "def: CAKE does not rotate on placement", !cake->rotate_on_placement );
    // The cake cell is mostly air — it must not be light-opaque or a bitten
    // sliver still casts a whole-cell shadow on the floor.
    check_bool( "def: CAKE casts no shadow", !cake->casts_shadow );
    check_bool( "def: candles are not edible", !block_definition_get_definition( BLACK_CANDLE )->is_cake );
    check_bool( "def: bites fit the rotation field", CAKE_MAX_BITES <= 7 );
    check_bool( "def: bite count is never a yaw", block_yaw( cake, CAKE_MAX_BITES ) == 0 && block_yaw( cake, 4 ) == 0 );

    // --- block_adjust_coord_based_on_state ----------------------------------
    {
        auto cake_coords = [ & ]( int bites ) {
            BlockCoords coords = {};
            coords.scale_x = static_cast<signed char>( cake->scale.x );
            for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
                coords.face[ i ] = cake->textures[ i ];
            }
            BlockState state = { CAKE, static_cast<unsigned char>( bites ), 0, CAKE, 0 };
            block_adjust_coord_based_on_state( cake, &state, &coords );
            return coords;
        };
        const BlockCoords whole = cake_coords( 0 );
        check_bool( "adjust: whole cake keeps the box + frosting", whole.scale_x == 14 && whole.face[ FACE_RIGHT ] == TEX_CAKE_SIDE );
        const BlockCoords half = cake_coords( 3 );
        check_bool( "adjust: 3 bites shrink to 8px", half.scale_x == 8 && half.face[ FACE_RIGHT ] == TEX_CAKE_INNER );
        check_bool( "adjust: bite keeps the other faces", half.face[ FACE_LEFT ] == TEX_CAKE_SIDE && half.face[ FACE_FRONT ] == TEX_CAKE_SIDE );
        check_bool( "adjust: last bite leaves 2px", cake_coords( CAKE_MAX_BITES ).scale_x == 2 );
    }

    // --- world: support rule, collision, raycast -----------------------------
    {
        RepGameState &gs = *new RepGameState( );
        gs.tick_number = 0;
        gs.camera.pos = glm::dvec3( 0, 0, 0 );
        test_setup_world( gs.world );
        World &world = gs.world;
        const glm::ivec3 pos( 10, 11, 10 );
        const BlockState stone = { STONE, 0, 0, STONE, 0 };
        world.set_loaded_block( pos + glm::ivec3( 0, -1, 0 ), stone );
        world.set_loaded_block( pos, { CAKE, 4, 0, CAKE, 0 } );
        // Bites aren't a yaw: 4 slices gone, the support check still reads
        // the floor face (needs_place_on_any_solid top), not a side face.
        check_bool( "survive: bitten cake keeps the floor check", block_can_survive_at( world, pos, world.get_loaded_block( pos ) ) );
        world.set_loaded_block( pos + glm::ivec3( 0, -1, 0 ), BLOCK_STATE_AIR );
        check_bool( "survive: bitten cake still needs the floor", !block_can_survive_at( world, pos, world.get_loaded_block( pos ) ) );
        world.set_loaded_block( pos + glm::ivec3( 0, -1, 0 ), stone );

        // Collision: 4 bites leave x in [1,7]px — a body inside the eaten
        // band doesn't collide, one inside the remaining cake does.
        const double mid_cake = 11.0 + PLAYER_EYE_HEIGHT + 4.0 / 16.0; // feet at y+0.25, inside the 8px-tall box
        check_bool( "world: eaten band doesn't collide",
                    !Collision::collides_at( world, glm::dvec3( 10.9, mid_cake, 10.5 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
        check_bool( "world: remaining cake collides",
                    Collision::collides_at( world, glm::dvec3( 10.1, mid_cake, 10.5 ), PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );

        // Raycast: the interior face picks at the shrunken +x edge, and a
        // ray crossing only the eaten band passes to the block behind.
        world.set_loaded_block( pos + glm::ivec3( 1, 0, 0 ), stone );
        glm::ivec3 out( -1, -1, -1 );
        int which_face = -1;
        double hit_t = -1.0;
        check_bool( "ray: -x hits the cut face at x=7/16",
                    RayTraversal::find_block_from_to( world, glm::dvec3( 10.9, 11.25, 10.5 ), glm::dvec3( 9.5, 11.25, 10.5 ), out, &which_face, 0, &hit_t ) &&
                        out == pos && which_face == FACE_RIGHT && fabs( hit_t - 0.4625 ) < 0.01 );
        check_bool( "ray: through the eaten band hits the block behind",
                    RayTraversal::find_block_from_to( world, glm::dvec3( 10.9, 11.25, 10.5 ), glm::dvec3( 11.5, 11.25, 10.5 ), out, &which_face ) &&
                        out == pos + glm::ivec3( 1, 0, 0 ) && which_face == FACE_LEFT );

        // --- the eat click: the state-update write lands the bite, the
        // final slice's AIR write removes the block (BreakDrops::None) -----
        const glm::ivec3 pos2( 14, 11, 10 );
        world.set_loaded_block( pos2 + glm::ivec3( 0, -1, 0 ), stone );
        world.set_loaded_block( pos2, { CAKE, 0, 0, CAKE, 0 } );
        gs.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( gs.tick_number, pos2, BlockState{ CAKE, 1, 0, CAKE, 0 }, true );
        gs.tick_number++;
        gs.blockUpdateQueue.processAllBlockUpdates( gs, gs.tick_number );
        check_bool( "eat: bite write lands the rotation", world.get_loaded_block( pos2 ).rotation == 1 );
        world.set_loaded_block( pos2, { CAKE, CAKE_MAX_BITES, 0, CAKE, 0 } );
        gs.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( gs.tick_number, pos2, BLOCK_STATE_AIR, false, BreakDrops::None, BreakDebris::None );
        gs.tick_number++;
        gs.blockUpdateQueue.processAllBlockUpdates( gs, gs.tick_number );
        failures += check_id( world, "eat: last slice removes the block", pos2, AIR );
        // The last slice is eaten, not mined — no break debris. A plain
        // removal still spawns it, so the flag isn't a global no-op.
        check_bool( "eat: last slice spawns no debris", world.blockDebris.count( ) == 0 );
        const glm::ivec3 pos3( 18, 11, 10 );
        world.set_loaded_block( pos3 + glm::ivec3( 0, -1, 0 ), stone );
        world.set_loaded_block( pos3, { CAKE, 0, 0, CAKE, 0 } );
        gs.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( gs.tick_number, pos3, BLOCK_STATE_AIR, false );
        gs.tick_number++;
        gs.blockUpdateQueue.processAllBlockUpdates( gs, gs.tick_number );
        check_bool( "break: normal removal still spawns debris", world.blockDebris.count( ) > 0 );
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

    // Sign showcase (z<0): standing signs on the floor, wall signs on the
    // +z face of the stones at z=-21, hanging signs under the y=2 beam.
    check_bool( "bw: standing sign on floor", MapGen::block_world_id( -9, 1, -19 ) == STANDING_SIGN );
    check_bool( "bw: wall sign in front of its stone", MapGen::block_world_id( -9, 1, -20 ) == WALL_SIGN && MapGen::block_world_id( -9, 1, -21 ) == STONE );
    check_bool( "bw: hanging sign below beam", MapGen::block_world_id( -9, 1, -24 ) == OAK_HANGING_SIGN && MapGen::block_world_id( -9, 2, -24 ) == STONE );
    check_bool( "bw: beam ends at x=-3", MapGen::block_world_id( -2, 2, -24 ) == AIR );

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
    // Floor under the whole play area: the single loaded chunk refuses
    // writes to its boundary cells (no neighbour chunk to fix up), which
    // would leave falling water retrying a write that can never land.
    for ( int x = 2; x <= 19; x++ ) {
        for ( int z = 2; z <= 19; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 9, z ), stone );
        }
    }
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
        gs.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( gs.tick_number, pos, block_state, false );
    };
    place( glm::ivec3( 10, 10, 10 ), { WATER, 0, 0, WATER, 0 } ); // water "onto" the wet coral
    place( glm::ivec3( 12, 10, 10 ), { STONE, 0, 0, STONE, 0 } ); // a change beside the water
    for ( int i = 0; i < 1500; i++ ) {
        gs.tick_number++;
        gs.blockUpdateQueue.processAllBlockUpdates( gs, gs.tick_number );
    }
    // With the loop, ~14 neighbor updates per expansion re-arm every 20
    // ticks and the queue explodes past the 100k-event guard (exit). The
    // state must also hold: coral stays waterlogged, neighbor stays water.
    const BlockState after = world.get_loaded_block( glm::ivec3( 10, 10, 10 ) );
    check_bool( "waterlogged coral survives water expansion", after.id == KELP && after.waterlogged == 1 );
    failures += check_id( world, "water cell beside coral stays water", glm::ivec3( 11, 10, 10 ), WATER );
    check_bool( "fluid updates drain after the expansion", gs.pending_fluid_updates.empty( ) );

    // Breaking a waterlogged block leaves its water behind.
    world.set_loaded_block( glm::ivec3( 10, 10, 10 ), BLOCK_STATE_AIR );
    failures += check_id( world, "breaking waterlogged plant leaves water", glm::ivec3( 10, 10, 10 ), WATER );

    // Face-adjacent flow + falls: a boxed-in source with only its +x face
    // open pours over the edge — water reaches (31,10,30) by falling from
    // (31,11,30). A fully sealed cell stays dry no matter how the flow
    // wraps around.
    for ( int x = 22; x <= 42; x++ ) {
        for ( int z = 21; z <= 39; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 9, z ), stone );
        }
    }
    world.set_loaded_block( glm::ivec3( 30, 10, 30 ), stone ); // floor under the water
    world.set_loaded_block( glm::ivec3( 29, 11, 30 ), stone ); // -x face sealed
    world.set_loaded_block( glm::ivec3( 30, 11, 29 ), stone ); // -z face sealed
    world.set_loaded_block( glm::ivec3( 30, 11, 31 ), stone ); // +z face sealed
    world.set_loaded_block( glm::ivec3( 30, 12, 30 ), stone ); // top sealed
    world.set_loaded_block( glm::ivec3( 31, 9, 30 ), stone );  // plug under the landing cell
    // A sealed stone box at (33,11,30): all 6 faces are stone.
    world.set_loaded_block( glm::ivec3( 33, 10, 30 ), stone );
    world.set_loaded_block( glm::ivec3( 33, 12, 30 ), stone );
    world.set_loaded_block( glm::ivec3( 33, 11, 29 ), stone );
    world.set_loaded_block( glm::ivec3( 33, 11, 31 ), stone );
    world.set_loaded_block( glm::ivec3( 32, 11, 30 ), stone );
    world.set_loaded_block( glm::ivec3( 34, 11, 30 ), stone );
    place( glm::ivec3( 30, 11, 30 ), { WATER, 0, 0, WATER, 0 } ); // wakes the source
    for ( int i = 0; i < 1500; i++ ) {
        gs.tick_number++;
        gs.blockUpdateQueue.processAllBlockUpdates( gs, gs.tick_number );
    }
    failures += check_id( world, "water falls into the open cell", glm::ivec3( 31, 10, 30 ), WATER );
    failures += check_id( world, "sealed box interior stays air", glm::ivec3( 33, 11, 30 ), AIR );
    failures += check_id( world, "source water remains", glm::ivec3( 30, 11, 30 ), WATER );
    check_bool( "fluid updates drain after the fall", gs.pending_fluid_updates.empty( ) );
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
        gs.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( gs.tick_number, pos, block_state, false );
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

    // --- Inverse TNT: the restore blast chain-ignites TNT caught in its
    // sphere instead of silently overwriting it with regenerated terrain. ---
    world.set_loaded_block( glm::ivec3( 40, 10, 40 ), stone ); // floor for the primed body
    world.set_loaded_block( glm::ivec3( 43, 10, 40 ), stone );
    world.set_loaded_block( glm::ivec3( 37, 10, 40 ), stone );
    world.set_loaded_block( glm::ivec3( 43, 11, 40 ), { TNT, 0, 0, TNT, 0 } );
    world.set_loaded_block( glm::ivec3( 37, 11, 40 ), { INVERSE_TNT, 0, 0, INVERSE_TNT, 0 } );
    place( glm::ivec3( 40, 11, 40 ), { INVERSE_TNT, 0, 0, INVERSE_TNT, 0 } );
    place( glm::ivec3( 40, 11, 41 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );

    for ( int i = 0; i < 10; i++ ) {
        tick( );
    }
    failures += check_id( world, "inverse: ignited cell is air", glm::ivec3( 40, 11, 40 ), AIR );
    check_bool( "inverse: one primed body", world.bodies.count_tnt( ) == 1 );

    // Once the main fuse has elapsed, both in-sphere TNT blocks must be
    // airborne primed bodies — not deleted by the terrain restore. The
    // shortest chain fuse is far longer than the few ticks since the blast.
    for ( long i = 0; i < TNT_FUSE_TICKS; i++ ) {
        tick( );
    }
    check_bool( "inverse: two chained bodies launched", world.bodies.count_tnt( ) == 2 );

    for ( long i = 0; i < wait; i++ ) {
        tick( );
    }
    check_bool( "inverse: no primed TNT bodies remain", world.bodies.count_tnt( ) == 0 );
    check_bool( "inverse: chained TNT cell restored", world.get_loaded_block( glm::ivec3( 43, 11, 40 ) ).id != TNT && world.get_loaded_block( glm::ivec3( 43, 11, 40 ) ).id != INVERSE_TNT );
    check_bool( "inverse: chained inverse TNT cell restored", world.get_loaded_block( glm::ivec3( 37, 11, 40 ) ).id != TNT && world.get_loaded_block( glm::ivec3( 37, 11, 40 ) ).id != INVERSE_TNT );
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
        gs.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( gs.tick_number, pos, block_state, false );
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
    gs.blockUpdateQueue.addBlockUpdate<TntExplodeEvent>( gs.tick_number, blast, 0, false );
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


// Count live item entities carrying `item` (sum of stack counts).
static int test_count_items( World &world, const ItemID item ) {
    int total = 0;
    for ( const auto &it : world.items.debug_items( ) ) {
        if ( it.item == item ) {
            total += it.count;
        }
    }
    return total;
}

// Leaf decay: a severed trunk's natural leaves decay through LeafDecayEvent;
// persistent (player-placed) leaves never do; leaves within LEAF_DECAY_RANGE
// of a second trunk stay; decays drop their rolled items.
int test_leaf_decay( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    RepGameState &gs = *new RepGameState( );
    gs.tick_number = 0;
    gs.game_mode = GameMode_Survival;
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
        gs.blockUpdateQueue.processAllBlockUpdates( gs, gs.tick_number );
    };
    auto run = [ & ]( const int ticks ) {
        for ( int i = 0; i < ticks; i++ ) {
            tick( );
        }
    };
    auto place = [ & ]( const glm::ivec3 &pos, const BlockState &block_state ) {
        gs.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( gs.tick_number, pos, block_state, false );
    };
    const BlockState log = { OAK_LOG, 0, 0, OAK_LOG, 0 };
    const BlockState leaf = { OAK_LEAVES, 0, 0, OAK_LEAVES, 0 };
    const BlockState pleaf = { OAK_LEAVES_PERSISTENT, 0, 0, OAK_LEAVES_PERSISTENT, 0 };

    // Tree 1: trunk at x=10 (y 10-13), a leaf cell above it and a horizontal
    // leaf chain out to x=18 at y=14. Tree 2's trunk at x=20 with its own
    // leaves at x=19..20 touches the chain end — after tree 1 falls, chain
    // leaves close enough to trunk 2 (x=14..18 within 6 of its canopy) keep
    // their support while the near end (x=10..13) decays.
    for ( int y = 10; y <= 13; y++ ) {
        world.set_loaded_block( glm::ivec3( 10, y, 10 ), log );
        world.set_loaded_block( glm::ivec3( 20, y, 10 ), log );
    }
    for ( int x = 10; x <= 18; x++ ) {
        world.set_loaded_block( glm::ivec3( x, 14, 10 ), leaf );
    }
    // Extra leaf mass around the trunk so at least one drop roll lands (leaf
    // drop chances are 2-7.5%; ~40 cells make a dry run essentially
    // impossible).
    for ( int x = 9; x <= 11; x++ ) {
        for ( int z = 9; z <= 11; z++ ) {
            for ( int y = 14; y <= 16; y++ ) {
                const glm::ivec3 p( x, y, z );
                if ( world.get_loaded_block( p ).id == AIR ) {
                    world.set_loaded_block( p, leaf );
                }
            }
        }
    }
    // A player-placed leaf beyond the canopy edge: unsupported, but
    // persistent variants never decay.
    world.set_loaded_block( glm::ivec3( 10, 17, 10 ), pleaf );
    world.set_loaded_block( glm::ivec3( 19, 14, 10 ), leaf );
    world.set_loaded_block( glm::ivec3( 20, 14, 10 ), leaf );

    check_bool( "decay: supported leaf before break", leaf_has_log_support( world, glm::ivec3( 11, 14, 10 ) ) );
    check_bool( "decay: unloaded edge counts as supported", leaf_has_log_support( world, glm::ivec3( 61, 30, 10 ) ) );

    // Fell the whole trunk — four separate AIR writes, logs don't collapse.
    for ( int y = 10; y <= 13; y++ ) {
        place( glm::ivec3( 10, y, 10 ), BLOCK_STATE_AIR );
    }
    // ~20 s: each cascade hop lands 0.5-2.5 s after the leaf before it.
    run( 20 * UPS_RATE );

    failures += check_id( world, "decay: leaf at dead trunk gone", glm::ivec3( 10, 14, 10 ), AIR );
    failures += check_id( world, "decay: near-end chain leaf gone", glm::ivec3( 12, 14, 10 ), AIR );
    failures += check_id( world, "decay: canopy mass leaf gone", glm::ivec3( 10, 16, 10 ), AIR );
    failures += check_id( world, "decay: persistent leaf stays", glm::ivec3( 10, 17, 10 ), OAK_LEAVES_PERSISTENT );
    failures += check_id( world, "decay: leaf near second trunk stays", glm::ivec3( 18, 14, 10 ), OAK_LEAVES );
    failures += check_id( world, "decay: leaf in range of second stays", glm::ivec3( 16, 14, 10 ), OAK_LEAVES );
    failures += check_id( world, "decay: second trunk leaf stays", glm::ivec3( 20, 14, 10 ), OAK_LEAVES );
    check_bool( "decay: at least one item dropped", world.items.count( ) >= 1 );
    check_bool( "decay: no checks left pending", gs.pending_leaf_decay.empty( ) );
    return failures;
}

// Support-loss drops (BreakDrops::Survival on the queued AIR write): torches
// and both halves of a two-cell door each yield exactly one item per break,
// nothing in creative.
int test_support_drops( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    RepGameState &gs = *new RepGameState( );
    gs.tick_number = 0;
    gs.game_mode = GameMode_Survival;
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
        gs.blockUpdateQueue.processAllBlockUpdates( gs, gs.tick_number );
    };
    auto run = [ & ]( const int ticks ) {
        for ( int i = 0; i < ticks; i++ ) {
            tick( );
        }
    };
    auto place = [ & ]( const glm::ivec3 &pos, const BlockState &block_state ) {
        gs.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( gs.tick_number, pos, block_state, false );
    };
    const BlockState stone = { STONE, 0, 0, STONE, 0 };

    // --- Torch on stone: breaking the support drops exactly one torch. ---
    world.set_loaded_block( glm::ivec3( 10, 10, 10 ), stone );
    world.set_loaded_block( glm::ivec3( 10, 11, 10 ), { TORCH, 0, 0, TORCH, 0 } );
    int before = test_count_items( world, TORCH );
    place( glm::ivec3( 10, 10, 10 ), BLOCK_STATE_AIR );
    run( 20 );
    check_bool( "support: torch dropped exactly once", test_count_items( world, TORCH ) - before == 1 );
    failures += check_id( world, "support: torch cell cleared", glm::ivec3( 10, 11, 10 ), AIR );

    // --- Door, three ways. Each scenario stands a fresh lower+upper pair on
    // a stone floor, removes one piece, and expects exactly one door item.
    const BlockState door_lower = { WOODEN_DOOR_LOWER, 0, 0, WOODEN_DOOR_LOWER, 0 };
    const BlockState door_upper = { WOODEN_DOOR_UPPER, 0, 0, WOODEN_DOOR_UPPER, 0 };
    auto door_case = [ & ]( const char *name, const glm::ivec3 &base, const glm::ivec3 &kill_pos, const bool kill_is_door ) {
        world.set_loaded_block( base + glm::ivec3( 0, -1, 0 ), stone );
        world.set_loaded_block( base, door_lower );
        world.set_loaded_block( base + glm::ivec3( 0, 1, 0 ), door_upper );
        const int had = test_count_items( world, WOODEN_DOOR_LOWER );
        // The AIR write carries BreakDrops::Survival like the player/support
        // paths; a floor break goes through the support-loss kill instead.
        if ( kill_is_door ) {
            gs.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( gs.tick_number, kill_pos, BLOCK_STATE_AIR, false, BreakDrops::Survival );
        } else {
            place( kill_pos, BLOCK_STATE_AIR );
        }
        run( 20 );
        char label[ 96 ];
        snprintf( label, sizeof( label ), "support: %s drops exactly one door item", name );
        check_bool( label, test_count_items( world, WOODEN_DOOR_LOWER ) - had == 1 );
        snprintf( label, sizeof( label ), "support: %s lower cleared", name );
        failures += check_id( world, label, base, AIR );
        snprintf( label, sizeof( label ), "support: %s upper cleared", name );
        failures += check_id( world, label, base + glm::ivec3( 0, 1, 0 ), AIR );
    };
    // Remove the lower half directly: the upper pops partnerless.
    door_case( "lower break", glm::ivec3( 10, 21, 20 ), glm::ivec3( 10, 21, 20 ), true );
    // Remove the upper half directly: the lower pops partnerless.
    door_case( "upper break", glm::ivec3( 14, 21, 20 ), glm::ivec3( 14, 22, 20 ), true );
    // Break the floor: the lower fails its ground check (drops), the upper
    // then finds no partner (no drop).
    door_case( "floor break", glm::ivec3( 18, 21, 20 ), glm::ivec3( 18, 20, 20 ), false );

    // --- Signs: each variant rides one support face through
    // needs_place_on_any_solid and pops when that support is removed. ---
    const Block *standing_sign_def = block_definition_get_definition( STANDING_SIGN );
    check_bool( "sign def: standing sign is a floor column",
                standing_sign_def->needs_place_on_any_solid[ FACE_TOP ] && !standing_sign_def->collides_with_player &&
                    standing_sign_def->scale.x == 8 && standing_sign_def->scale.y == 16 && standing_sign_def->offset.x == 4 );
    const Block *wall_sign_def = block_definition_get_definition( WALL_SIGN );
    check_bool( "sign def: wall sign mounts like a ladder",
                wall_sign_def->rotate_on_placement && wall_sign_def->needs_place_on_any_solid[ FACE_FRONT ] &&
                    wall_sign_def->scale.z == 2 && !wall_sign_def->collides_with_player );
    const Block *hanging_sign_def = block_definition_get_definition( OAK_HANGING_SIGN );
    check_bool( "sign def: hanging sign hangs from above",
                hanging_sign_def->needs_place_on_any_solid[ FACE_BOTTOM ] && !hanging_sign_def->collides_with_player );
    check_bool( "sign def: signs waterlog instead of breaking",
                standing_sign_def->waterloggable && wall_sign_def->waterloggable && hanging_sign_def->waterloggable &&
                    standing_sign_def->breaks_in_liquid && wall_sign_def->breaks_in_liquid && hanging_sign_def->breaks_in_liquid );
    // The two forms share one item: standing names its wall variant, the
    // wall block points back, and block_item_id hands drops/pick-block the
    // standing item — wall signs stay out of the creative list.
    check_bool( "sign def: standing/wall variants paired",
                standing_sign_def->wall_variant == WALL_SIGN && wall_sign_def->standing_variant == STANDING_SIGN &&
                    block_item_id( WALL_SIGN ) == STANDING_SIGN );
    {
        bool standing_listed = false, wall_listed = false;
        for ( unsigned int i = 0; i < CREATIVE_BLOCK_COUNT; i++ ) {
            standing_listed |= CREATIVE_BLOCKS[ i ] == STANDING_SIGN;
            wall_listed |= CREATIVE_BLOCKS[ i ] == WALL_SIGN;
        }
        check_bool( "sign def: standing listed, wall hidden in creative", standing_listed && !wall_listed );
    }

    // Wall sign (rotation 0: plate hugs -z, support at z-1 like a ladder);
    // the shared item means the drop is the standing sign's item.
    world.set_loaded_block( glm::ivec3( 34, 10, 30 ), stone );
    world.set_loaded_block( glm::ivec3( 34, 10, 31 ), { WALL_SIGN, 0, 0, WALL_SIGN, 0 } );
    before = test_count_items( world, STANDING_SIGN );
    place( glm::ivec3( 34, 10, 30 ), BLOCK_STATE_AIR );
    run( 20 );
    check_bool( "support: wall sign dropped once", test_count_items( world, STANDING_SIGN ) - before == 1 );
    failures += check_id( world, "support: wall sign cell cleared", glm::ivec3( 34, 10, 31 ), AIR );

    // Standing sign sits on the floor cell below.
    world.set_loaded_block( glm::ivec3( 38, 10, 30 ), stone );
    world.set_loaded_block( glm::ivec3( 38, 11, 30 ), { STANDING_SIGN, 0, 0, STANDING_SIGN, 0 } );
    before = test_count_items( world, STANDING_SIGN );
    place( glm::ivec3( 38, 10, 30 ), BLOCK_STATE_AIR );
    run( 20 );
    check_bool( "support: standing sign dropped once", test_count_items( world, STANDING_SIGN ) - before == 1 );
    failures += check_id( world, "support: standing sign cell cleared", glm::ivec3( 38, 11, 30 ), AIR );

    // Hanging sign needs the block above (needs bottom => support at +y).
    world.set_loaded_block( glm::ivec3( 42, 10, 30 ), stone );
    world.set_loaded_block( glm::ivec3( 42, 9, 30 ), { OAK_HANGING_SIGN, 0, 0, OAK_HANGING_SIGN, 0 } );
    before = test_count_items( world, OAK_HANGING_SIGN );
    place( glm::ivec3( 42, 10, 30 ), BLOCK_STATE_AIR );
    run( 20 );
    check_bool( "support: hanging sign dropped once", test_count_items( world, OAK_HANGING_SIGN ) - before == 1 );
    failures += check_id( world, "support: hanging sign cell cleared", glm::ivec3( 42, 9, 30 ), AIR );

    // --- Creative: support loss spawns nothing. ---
    gs.game_mode = GameMode_Creative;
    world.set_loaded_block( glm::ivec3( 30, 10, 30 ), stone );
    world.set_loaded_block( glm::ivec3( 30, 11, 30 ), { TORCH, 0, 0, TORCH, 0 } );
    before = test_count_items( world, TORCH );
    place( glm::ivec3( 30, 10, 30 ), BLOCK_STATE_AIR );
    run( 20 );
    check_bool( "support: creative torch drops nothing", test_count_items( world, TORCH ) - before == 0 );
    return failures;
}

// Sapling growth: tree_grow_try writes the shared tree shapes; the shape is
// a pure function of position, natural leaves are used, and bad placements
// fail cleanly.
int test_sapling_grow( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    RepGameState &gs = *new RepGameState( );
    gs.tick_number = 0;
    gs.game_mode = GameMode_Survival;
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
    const BlockState grass = { GRASS, 0, 0, GRASS, 0 };
    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    const BlockState sapling = { OAK_SAPLING, 0, 0, OAK_SAPLING, 0 };

    // --- Oak on grass under open sky. ---
    const glm::ivec3 pos( 10, 11, 10 );
    world.set_loaded_block( pos + glm::ivec3( 0, -1, 0 ), grass );
    world.set_loaded_block( pos, sapling );
    check_bool( "grow: oak sapling grows", tree_grow_try( gs, pos ) );
    check_bool( "grow: sapling cell is now a log", world.get_loaded_block( pos ).id == OAK_LOG );
    // Trunk column: logs up through the spec's height.
    TreeSpec spec;
    check_bool( "grow: oak spec resolved", tree_spec_for_sapling( OAK_SAPLING, pos, &spec ) );
    bool trunk_ok = world.get_loaded_block( pos + glm::ivec3( 0, spec.trunk_h - 1, 0 ) ).id == OAK_LOG;
    check_bool( "grow: trunk column filled with logs", trunk_ok );
    bool natural_leaf_found = false;
    for ( int dy = spec.trunk_h - 3; dy <= spec.trunk_h + 2 && !natural_leaf_found; dy++ ) {
        for ( int dx = -spec.canopy_r; dx <= spec.canopy_r && !natural_leaf_found; dx++ ) {
            for ( int dz = -spec.canopy_r; dz <= spec.canopy_r && !natural_leaf_found; dz++ ) {
                if ( world.get_loaded_block( pos + glm::ivec3( dx, dy, dz ) ).id == OAK_LEAVES ) {
                    natural_leaf_found = true;
                }
            }
        }
    }
    check_bool( "grow: canopy uses natural (not persistent) leaves", natural_leaf_found );

    // --- Determinism: snapshot, replant at the same position, regrow. ---
    std::vector<BlockID> first_ids;
    const glm::ivec3 lo( -5, 0, -5 ), hi( 5, spec.trunk_h + 5, 5 );
    for ( int x = lo.x; x <= hi.x; x++ ) {
        for ( int y = lo.y; y <= hi.y; y++ ) {
            for ( int z = lo.z; z <= hi.z; z++ ) {
                first_ids.push_back( world.get_loaded_block( pos + glm::ivec3( x, y, z ) ).id );
            }
        }
    }
    for ( int x = lo.x; x <= hi.x; x++ ) {
        for ( int y = lo.y; y <= hi.y; y++ ) {
            for ( int z = lo.z; z <= hi.z; z++ ) {
                world.set_loaded_block( pos + glm::ivec3( x, y, z ), BLOCK_STATE_AIR );
            }
        }
    }
    world.set_loaded_block( pos + glm::ivec3( 0, -1, 0 ), grass );
    world.set_loaded_block( pos, sapling );
    check_bool( "grow: replanted oak grows again", tree_grow_try( gs, pos ) );
    bool same = true;
    int idx = 0;
    for ( int x = lo.x; x <= hi.x; x++ ) {
        for ( int y = lo.y; y <= hi.y; y++ ) {
            for ( int z = lo.z; z <= hi.z; z++ ) {
                if ( world.get_loaded_block( pos + glm::ivec3( x, y, z ) ).id != first_ids[ idx++ ] ) {
                    same = false;
                }
            }
        }
    }
    check_bool( "grow: same position writes the same cells", same );

    // --- Rejections. ---
    // Stone under the sapling: no soil.
    const glm::ivec3 bad_soil( 30, 11, 30 );
    world.set_loaded_block( bad_soil + glm::ivec3( 0, -1, 0 ), stone );
    world.set_loaded_block( bad_soil, sapling );
    check_bool( "grow: stone underneath fails", !tree_grow_try( gs, bad_soil ) );
    // A stone cell inside the future trunk.
    const glm::ivec3 blocked( 40, 11, 40 );
    world.set_loaded_block( blocked + glm::ivec3( 0, -1, 0 ), grass );
    world.set_loaded_block( blocked, sapling );
    world.set_loaded_block( blocked + glm::ivec3( 0, 2, 0 ), stone );
    check_bool( "grow: blocked trunk fails", !tree_grow_try( gs, blocked ) );
    // Bamboo never grows into a tree.
    check_bool( "grow: bamboo returns false", !tree_spec_for_sapling( BAMBOO_SAPLING, pos, &spec ) );
    const glm::ivec3 bamboo( 50, 11, 50 );
    world.set_loaded_block( bamboo + glm::ivec3( 0, -1, 0 ), grass );
    world.set_loaded_block( bamboo, { BAMBOO_SAPLING, 0, 0, BAMBOO_SAPLING, 0 } );
    check_bool( "grow: bamboo sapling fails", !tree_grow_try( gs, bamboo ) );
    return failures;
}

// Placing natural leaves from the hotbar writes the persistent variant;
// block_item_id maps persistent blocks back to the natural leaf item.
int test_leaf_placement( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
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
    check_bool( "place: natural leaf maps to persistent", block_placed_leaf_id( OAK_LEAVES ) == OAK_LEAVES_PERSISTENT );
    check_bool( "place: persistent leaf maps to natural item", block_item_id( OAK_LEAVES_PERSISTENT ) == OAK_LEAVES );
    check_bool( "place: natural leaf is a leaf", block_is_leaf( OAK_LEAVES ) );
    check_bool( "place: persistent leaf is a leaf", block_is_leaf( OAK_LEAVES_PERSISTENT ) );
    check_bool( "place: persistent is not natural", !block_is_natural_leaf( block_definition_get_definition( OAK_LEAVES_PERSISTENT ) ) );

    // The hotbar path runs the item through block_placed_leaf_id before the
    // queued write — replicate that and land it through the normal event.
    const BlockID placed_id = block_placed_leaf_id( OAK_LEAVES );
    const glm::ivec3 pos( 10, 11, 10 );
    world.set_loaded_block( pos + glm::ivec3( 0, -1, 0 ), { STONE, 0, 0, STONE, 0 } );
    gs.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( gs.tick_number, pos, BlockState{ placed_id, 0, 0, placed_id, 0 }, false );
    const glm::ivec3 pos2( 12, 11, 10 );
    gs.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( gs.tick_number, pos2, BlockState{ OAK_LEAVES, 0, 0, OAK_LEAVES, 0 }, false );
    gs.tick_number++;
    gs.blockUpdateQueue.processAllBlockUpdates( gs, gs.tick_number );
    failures += check_id( world, "place: cell holds persistent leaves", pos, OAK_LEAVES_PERSISTENT );
    failures += check_id( world, "place: natural id lands as-is", pos2, OAK_LEAVES );
    return failures;
}

// Shears harvest the leaf item itself; bone meal grows a sapling instantly
// and consumes one in survival.
int test_shears_bonemeal( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    RepGameState &gs = *new RepGameState( );
    gs.tick_number = 0;
    gs.game_mode = GameMode_Survival;
    gs.camera.pos = glm::dvec3( 0, 0, 0 );
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

    // Shears: the player path calls break_drops with force_item = the leaf
    // item (block_item_id maps both variants to the natural one).
    world.set_loaded_block( glm::ivec3( 10, 11, 10 ), { OAK_LEAVES_PERSISTENT, 0, 0, OAK_LEAVES_PERSISTENT, 0 } );
    const int had_leaves = test_count_items( world, OAK_LEAVES );
    break_drops( gs, glm::ivec3( 10, 11, 10 ), OAK_LEAVES_PERSISTENT, block_item_id( OAK_LEAVES_PERSISTENT ) );
    check_bool( "shears: leaf item dropped", test_count_items( world, OAK_LEAVES ) - had_leaves == 1 );

    // Bone meal: grows the sapling immediately and consumes one.
    gs.hotbar.addItemWithQuantity( BONE_MEAL, 3, true );
    const glm::ivec3 pos( 20, 11, 20 );
    world.set_loaded_block( pos + glm::ivec3( 0, -1, 0 ), { GRASS, 0, 0, GRASS, 0 } );
    world.set_loaded_block( pos, { BIRCH_SAPLING, 0, 0, BIRCH_SAPLING, 0 } );
    check_bool( "bonemeal: sapling grows", tree_grow_try( gs, pos ) );
    check_bool( "bonemeal: trunk wrote logs", world.get_loaded_block( pos ).id == BIRCH_LOG );
    check_bool( "bonemeal: one consumed", gs.hotbar.consumeSelected( 1 ) && gs.hotbar.getSelectedQuantity( ) == 2 );
    return failures;
}
// Grass lifecycle (World::grass_random_tick — the sampler calls it per
// sampled grass-family cell; the test drives it directly since looping the
// full process_random_ticks over MAX_LOADED_CHUNKS is too slow): covered
// grass dies to dirt, open grass spreads to uncovered dirt in reach
// (±1 xz, -3..+1 y), snowy grass spreads plain grass, mycelium spreads
// mycelium.
int test_grass( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
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
    const BlockState grass = { GRASS, 0, 0, GRASS, 0 };
    const BlockState dirt = { DIRT, 0, 0, DIRT, 0 };
    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    const BlockState mycelium = { MYCELIUM, 0, 0, MYCELIUM, 0 };
    const BlockState snowy = { SNOWY_GRASS, 0, 0, SNOWY_GRASS, 0 };

    // Covered grass dies deterministically — the death path has no roll.
    const glm::ivec3 covered_src( 10, 10, 10 );
    world.set_loaded_block( covered_src, grass );
    world.set_loaded_block( covered_src + glm::ivec3( 0, 1, 0 ), stone );
    world.grass_random_tick( gs, covered_src, grass );
    failures += check_id( world, "grass: covered grass dies to dirt", covered_src, DIRT );

    // Open grass survives its tick (spread picks land on air here).
    const glm::ivec3 open_src( 20, 10, 20 );
    world.set_loaded_block( open_src, grass );
    world.grass_random_tick( gs, open_src, grass );
    failures += check_id( world, "grass: open grass survives", open_src, GRASS );

    // Cover means light-opaque (casts_shadow), not just a block above: a
    // torch or a leaf canopy shades but never kills the grass under it.
    const BlockState torch = { TORCH, 0, 0, TORCH, 0 };
    const glm::ivec3 torch_src( 20, 14, 20 );
    world.set_loaded_block( torch_src, grass );
    world.set_loaded_block( torch_src + glm::ivec3( 0, 1, 0 ), torch );
    world.grass_random_tick( gs, torch_src, grass );
    failures += check_id( world, "grass: torch overhead doesn't kill", torch_src, GRASS );

    const BlockState leaves = { OAK_LEAVES, 0, 0, OAK_LEAVES, 0 };
    const glm::ivec3 leaves_src( 20, 18, 20 );
    world.set_loaded_block( leaves_src, grass );
    world.set_loaded_block( leaves_src + glm::ivec3( 0, 1, 0 ), leaves );
    world.grass_random_tick( gs, leaves_src, grass );
    failures += check_id( world, "grass: leaf canopy doesn't kill", leaves_src, GRASS );

    // A fluid cell above is cover too (MC's opacity-3 water): source and
    // flowing cells both kill, and so does a waterlogged cell.
    const glm::ivec3 water_src( 20, 22, 20 );
    world.set_loaded_block( water_src, grass );
    world.set_loaded_block( water_src + glm::ivec3( 0, 1, 0 ), { WATER, 0, 0, WATER, 0 } );
    world.grass_random_tick( gs, water_src, grass );
    failures += check_id( world, "grass: underwater grass dies", water_src, DIRT );

    const glm::ivec3 flow_src( 20, 26, 20 );
    world.set_loaded_block( flow_src, grass );
    world.set_loaded_block( flow_src + glm::ivec3( 0, 1, 0 ), { WATER, 4, 0, WATER, 0 } );
    world.grass_random_tick( gs, flow_src, grass );
    failures += check_id( world, "grass: grass under a flow dies", flow_src, DIRT );

    const glm::ivec3 wlog_src( 20, 30, 20 );
    world.set_loaded_block( wlog_src, grass );
    world.set_loaded_block( wlog_src + glm::ivec3( 0, 1, 0 ), { KELP, 0, 0, KELP, 0 } );
    world.set_loaded_block( wlog_src + glm::ivec3( 0, 1, 0 ), { WATER, 0, 0, WATER, 0 } ); // waterlogs the kelp
    world.grass_random_tick( gs, wlog_src, grass );
    failures += check_id( world, "grass: waterlogged above kills too", wlog_src, DIRT );

    // Spread garden: dirt floor at dy=-3 plus a same-level dirt ring, all
    // open-topped — 17 of the 45 picks are eligible, so a passing roll
    // (~1-in-3 per call, deterministic stream) converts something fast.
    auto garden = [ & ]( const glm::ivec3 &src, const BlockState &src_state, const BlockID expected ) {
        for ( int dx = -1; dx <= 1; dx++ ) {
            for ( int dz = -1; dz <= 1; dz++ ) {
                world.set_loaded_block( src + glm::ivec3( dx, -3, dz ), dirt );
                if ( dx != 0 || dz != 0 ) {
                    world.set_loaded_block( src + glm::ivec3( dx, 0, dz ), dirt );
                }
            }
        }
        world.set_loaded_block( src, src_state );
        for ( int i = 0; i < 40; i++ ) {
            world.grass_random_tick( gs, src, src_state );
        }
        int converted = 0;
        for ( int dx = -1; dx <= 1; dx++ ) {
            for ( int dy = -3; dy <= 1; dy++ ) {
                for ( int dz = -1; dz <= 1; dz++ ) {
                    if ( world.get_loaded_block( src + glm::ivec3( dx, dy, dz ) ).id == expected ) {
                        converted++;
                    }
                }
            }
        }
        return converted;
    };
    check_bool( "grass: grass spreads to dirt", garden( glm::ivec3( 30, 10, 30 ), grass, GRASS ) > 0 );
    check_bool( "grass: mycelium spreads mycelium", garden( glm::ivec3( 41, 10, 41 ), mycelium, MYCELIUM ) > 0 );
    check_bool( "grass: snowy grass spreads grass", garden( glm::ivec3( 52, 10, 52 ), snowy, GRASS ) > 0 );

    // A covered dirt cell is not a valid target: the open floor cells around
    // it convert, the covered one stays dirt.
    const glm::ivec3 gated_src( 10, 10, 45 );
    world.set_loaded_block( gated_src + glm::ivec3( 1, 0, 1 ), dirt );
    world.set_loaded_block( gated_src + glm::ivec3( 1, 1, 1 ), stone );
    world.set_loaded_block( gated_src + glm::ivec3( -1, 0, -1 ), dirt );
    world.set_loaded_block( gated_src + glm::ivec3( -1, 1, -1 ), { WATER, 0, 0, WATER, 0 } );
    for ( int dx = -1; dx <= 1; dx++ ) {
        for ( int dz = -1; dz <= 1; dz++ ) {
            world.set_loaded_block( gated_src + glm::ivec3( dx, -3, dz ), dirt );
        }
    }
    world.set_loaded_block( gated_src, grass );
    for ( int i = 0; i < 40; i++ ) {
        world.grass_random_tick( gs, gated_src, grass );
    }
    failures += check_id( world, "grass: covered dirt untouched", gated_src + glm::ivec3( 1, 0, 1 ), DIRT );
    failures += check_id( world, "grass: water-covered dirt untouched", gated_src + glm::ivec3( -1, 0, -1 ), DIRT );
    int opened = 0;
    for ( int dx = -1; dx <= 1; dx++ ) {
        for ( int dz = -1; dz <= 1; dz++ ) {
            if ( world.get_loaded_block( gated_src + glm::ivec3( dx, -3, dz ) ).id == GRASS ) {
                opened++;
            }
        }
    }
    check_bool( "grass: open dirt still converts nearby", opened > 0 );

    // Nothing writes grass spontaneously — a lone dirt with no source in
    // reach stays dirt.
    world.set_loaded_block( glm::ivec3( 58, 20, 58 ), dirt );
    failures += check_id( world, "grass: lone dirt stays dirt", glm::ivec3( 58, 20, 58 ), DIRT );
    return failures;
}

// Empirical: how many freshly generated chunks do natural random ticks
// dirty (and therefore persist on departure)? Mapgens a real 7x3x7 grid —
// the ±2/±1 tick window covers the inner 5x3x5; the outer ring only
// receives boundary halo writes — and counts dirty/player_edited flags at
// checkpoints. sidecars.player_edited must stay 0 everywhere: natural writes keep
// the pen rule out, so every dirty chunk here is a purely natural change
// that would still be written to disk.
int test_grass_persist( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    setenv( "REPGAME_MOB_TEST", "1", 1 ); // skip the mob roll — irrelevant to block ticks
    RepGameState &gs = *new RepGameState( );
    gs.tick_number = 0;
    test_setup_world( gs.world );
    World &world = gs.world;
    ChunkLoader &cl = world.chunkLoader;

    MapStorage ms;
    ms.init_readonly( "/nonexistent/grass_persist" ); // no save files -> pure mapgen

    const glm::ivec3 center( 32, 0, 32 );
    const float surf = MapGen::calculateTerrainHeight( center.x * CHUNK_SIZE_X + 31, center.z * CHUNK_SIZE_Z + 31 );
    const int cy = static_cast<int>( floorf( surf / static_cast<float>( CHUNK_SIZE_Y ) ) );
    const int ring = 2 * CHUNK_RADIUS_XZ + 1, yring = 2 * CHUNK_RADIUS_Y + 1;
    std::vector<Chunk *> inner, outer;
    for ( int dx = -3; dx <= 3; dx++ ) {
        for ( int dy = -1; dy <= 1; dy++ ) {
            for ( int dz = -3; dz <= 3; dz++ ) {
                const glm::ivec3 cpos( center.x + dx, cy + dy, center.z + dz );
                Chunk &chunk = cl.chunkArray[ chunk_slot_from_pos( cpos ) ];
                chunk.chunk_pos = cpos;
                chunk.chunk_mod = glm::ivec3( ( ( cpos.x % ring ) + ring ) % ring, ( ( cpos.y % yring ) + yring ) % yring, ( ( cpos.z % ring ) + ring ) % ring );
                chunk.load_state = CHUNK_LIVE;
                chunk.load_terrain( ms );
                ( abs( dx ) <= 2 && abs( dz ) <= 2 ? inner : outer ).push_back( &chunk );
            }
        }
    }
    // Pin the camera to the middle of the center chunk so the tick window
    // is exactly the inner 5x3x5.
    gs.camera.pos = glm::dvec3( center.x * CHUNK_SIZE_X + 31.0, cy * CHUNK_SIZE_Y + 31.0, center.z * CHUNK_SIZE_Z + 31.0 );

    auto count_grass = [ & ]( Chunk *chunk ) {
        for ( int x = 0; x < CHUNK_SIZE_X; x++ ) {
            for ( int y = 0; y < CHUNK_SIZE_Y; y++ ) {
                for ( int z = 0; z < CHUNK_SIZE_Z; z++ ) {
                    const BlockID id = chunk->get_block( glm::ivec3( x, y, z ) ).id;
                    if ( id == GRASS || id == SNOWY_GRASS || id == MYCELIUM ) {
                        return 1;
                    }
                }
            }
        }
        return 0;
    };
    int grass_chunks = 0;
    for ( Chunk *chunk : inner ) {
        grass_chunks += count_grass( chunk );
    }
    pr_test( "  persist probe: %zu inner chunks (%d with grass sources), %zu outer ring", inner.size( ), grass_chunks, outer.size( ) );

    auto count_flags = [ & ]( const std::vector<Chunk *> &set, int *dirty, int *edited ) {
        *dirty = 0;
        *edited = 0;
        for ( Chunk *chunk : set ) {
            *dirty += chunk->dirty ? 1 : 0;
            *edited += chunk->sidecars.player_edited ? 1 : 0;
        }
    };
    const int checkpoints[] = { 0, 100, 1000, 5000, 20000 };
    int ran = 0;
    int failures = 0;
    int final_inner_dirty = 0, final_edited = 0;
    for ( const int checkpoint : checkpoints ) {
        for ( ; ran < checkpoint; ran++ ) {
            world.process_random_ticks( gs );
        }
        int in_dirty, in_edited, out_dirty, out_edited;
        count_flags( inner, &in_dirty, &in_edited );
        count_flags( outer, &out_dirty, &out_edited );
        pr_test( "  persist probe: after %5d ticks (~%4ds game time): inner dirty %d/%zu, outer dirty %d/%zu, player_edited %d",
                 checkpoint, checkpoint / 144, in_dirty, inner.size( ), out_dirty, outer.size( ), in_edited + out_edited );
        final_inner_dirty = in_dirty;
        final_edited = in_edited + out_edited;
    }
    // Attribute the dirties: regenerate each dirty chunk pristine and diff —
    // structures are pure functions of position too, so every remaining
    // difference is a genuine tick write. DIRT->grass-family = spread,
    // grass-family->DIRT = covered-grass death.
    int spread_writes = 0, death_writes = 0, other_writes = 0;
    for ( Chunk *chunk : inner ) {
        if ( !chunk->dirty ) {
            continue;
        }
        Chunk *pristine = new Chunk( );
        std::atomic<uint64_t> stale_bits{ 0 };
        pristine->light.stale_word = &stale_bits;
        pristine->light.stale_bit = 1;
        pristine->chunk_pos = chunk->chunk_pos;
        pristine->chunk_mod = chunk->chunk_mod;
        pristine->load_terrain( ms );
        for ( int x = 0; x < CHUNK_SIZE_X; x++ ) {
            for ( int y = 0; y < CHUNK_SIZE_Y; y++ ) {
                for ( int z = 0; z < CHUNK_SIZE_Z; z++ ) {
                    const glm::ivec3 local( x, y, z );
                    const BlockID actual = chunk->get_block( local ).id;
                    const BlockID before = pristine->get_block( local ).id;
                    const bool was_grass = before == GRASS || before == SNOWY_GRASS || before == MYCELIUM;
                    const bool is_grass = actual == GRASS || actual == SNOWY_GRASS || actual == MYCELIUM;
                    if ( before == DIRT && is_grass ) {
                        spread_writes++;
                    } else if ( was_grass && actual == DIRT ) {
                        death_writes++;
                    } else if ( actual != before ) {
                        other_writes++;
                    }
                }
            }
        }
        delete pristine;
    }
    pr_test( "  persist probe: dirty-chunk tick writes — %d spread (dirt->grass), %d death (grass->dirt), %d other", spread_writes, death_writes, other_writes );

    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };
    check_bool( "persist: natural ticks dirty real chunks", final_inner_dirty > 0 );
    check_bool( "persist: natural writes never set player_edited", final_edited == 0 );
    return failures;
}
