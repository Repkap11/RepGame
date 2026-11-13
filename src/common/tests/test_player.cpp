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
        RayTraversal::find_block_from_to( world, from, to, out, &which_face );
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

// pos_create convention: the cell on the other side of the hit face must be
// the cell the ray came from. The z mapping is the subtle one — FRONT is the
// -z side of the cell in RayTraversal's convention (opposite sign from
// FACE_DIR_Z_OFFSETS in mesh_gen_shared.hpp), so this locks it in.
int test_ray_face_neighbor( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    RepGameState &gs = *new RepGameState( );
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 0, 0, 0 );
    test_setup_world( gs.world );
    World &world = gs.world;

    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    world.set_loaded_block( glm::ivec3( 10, 10, 10 ), stone );

    int failures = 0;
    auto ray = [ & ]( const char *name, const glm::dvec3 &from, const glm::ivec3 &expect_create ) {
        glm::ivec3 out( -1, -1, -1 );
        int which_face = -1;
        RayTraversal::find_block_from_to( world, from, glm::dvec3( 10.5, 10.5, 10.5 ), out, &which_face );
        const glm::ivec3 create = RayTraversal::placement_cell( out, which_face );
        const bool ok = out == glm::ivec3( 10, 10, 10 ) && create == expect_create;
        pr_test( "  %-45s hit=(%d,%d,%d) face=%d create=(%d,%d,%d)  %s", name, out.x, out.y, out.z, which_face, create.x, create.y, create.z,
                 ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };
    ray( "-x approach enters FACE_RIGHT", glm::dvec3( 12.5, 10.5, 10.5 ), glm::ivec3( 11, 10, 10 ) );
    ray( "+x approach enters FACE_LEFT", glm::dvec3( 8.5, 10.5, 10.5 ), glm::ivec3( 9, 10, 10 ) );
    ray( "from above enters FACE_TOP", glm::dvec3( 10.5, 12.5, 10.5 ), glm::ivec3( 10, 11, 10 ) );
    ray( "from below enters FACE_BOTTOM", glm::dvec3( 10.5, 8.5, 10.5 ), glm::ivec3( 10, 9, 10 ) );
    ray( "+z approach enters FACE_FRONT", glm::dvec3( 10.5, 10.5, 8.5 ), glm::ivec3( 10, 10, 9 ) );
    ray( "-z approach enters FACE_BACK", glm::dvec3( 10.5, 10.5, 12.5 ), glm::ivec3( 10, 10, 11 ) );

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

    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };

    // getPlacedRotation's wall-mount branch (block_mount_rotation): the
    // clicked face pins the attachment instead of the yaw bucket, so an
    // oblique click still lands the ladder on the face the user aimed at.
    // Each side click must yield the rotation the support check then
    // accepts against the clicked block.
    const glm::ivec3 wall( 30, 10, 30 );
    world.set_loaded_block( wall, stone );
    struct MountCase {
        glm::ivec3 dir;
        int rot;
    };
    const MountCase mount_cases[] = {
        { glm::ivec3( 0, 0, 1 ), 0 },  // clicked +z face
        { glm::ivec3( 0, 0, -1 ), 2 }, // clicked -z face
        { glm::ivec3( 1, 0, 0 ), 3 },  // clicked +x face
        { glm::ivec3( -1, 0, 0 ), 1 }, // clicked -x face
    };
    {
        const Block *ladder_def = block_definition_get_definition( LADDER );
        for ( const MountCase &c : mount_cases ) {
            char name[ 96 ];
            snprintf( name, sizeof( name ), "mount click (%d,%d,%d) -> rot %d", c.dir.x, c.dir.y, c.dir.z, c.rot );
            check_bool( name, block_mount_rotation( ladder_def, c.dir ) == c.rot );
            const glm::ivec3 create = wall + c.dir;
            snprintf( name, sizeof( name ), "mount click (%d,%d,%d) survives on clicked block", c.dir.x, c.dir.y, c.dir.z );
            check_bool( name, player_placement_valid( world, create, { LADDER, (unsigned char)c.rot, 0, LADDER, 0 } ) );
        }
        check_bool( "top click falls back to yaw", block_mount_rotation( ladder_def, glm::ivec3( 0, 1, 0 ) ) < 0 );
        check_bool( "bottom click falls back to yaw", block_mount_rotation( ladder_def, glm::ivec3( 0, -1, 0 ) ) < 0 );
        check_bool( "null mount falls back to yaw", block_mount_rotation( ladder_def, glm::ivec3( 0, 0, 0 ) ) < 0 );
        check_bool( "non-mounted block falls back to yaw",
                    block_mount_rotation( block_definition_get_definition( STONE ), glm::ivec3( 0, 0, 1 ) ) < 0 );
    }

    // Wall signs share the wall-mount machinery (rotate_on_placement +
    // needs front): the same side clicks must mount them onto the clicked
    // face of a solid block.
    {
        const Block *wall_sign_def = block_definition_get_definition( WALL_SIGN );
        for ( const MountCase &c : mount_cases ) {
            const glm::ivec3 create = wall + c.dir;
            char name[ 96 ];
            snprintf( name, sizeof( name ), "wall sign click (%d,%d,%d) -> rot %d", c.dir.x, c.dir.y, c.dir.z, c.rot );
            check_bool( name, block_mount_rotation( wall_sign_def, c.dir ) == c.rot );
            snprintf( name, sizeof( name ), "wall sign click (%d,%d,%d) survives", c.dir.x, c.dir.y, c.dir.z );
            check_bool( name, player_placement_valid( world, create, { WALL_SIGN, (unsigned char)c.rot, 0, WALL_SIGN, 0 } ) );
        }
        check_bool( "wall sign top click falls back to yaw", block_mount_rotation( wall_sign_def, glm::ivec3( 0, 1, 0 ) ) < 0 );
        // A clicked face with nothing solid behind it rejects placement.
        check_bool( "wall sign without support rejected",
                    !player_placement_valid( world, glm::ivec3( 35, 11, 35 ), { WALL_SIGN, 0, 0, WALL_SIGN, 0 } ) );
    }

    // One sign item covers both forms (wall_variant on the standing def):
    // a side click picks the wall mount's yaw, a floor/ceiling click keeps
    // the standing block.
    {
        const Block *standing_sign_def = block_definition_get_definition( STANDING_SIGN );
        for ( const MountCase &c : mount_cases ) {
            char name[ 96 ];
            snprintf( name, sizeof( name ), "sign item click (%d,%d,%d) -> wall rot %d", c.dir.x, c.dir.y, c.dir.z, c.rot );
            check_bool( name, block_wall_variant_rotation( standing_sign_def, c.dir ) == c.rot );
        }
        check_bool( "sign item floor click stays standing", block_wall_variant_rotation( standing_sign_def, glm::ivec3( 0, 1, 0 ) ) < 0 );
        check_bool( "sign item ceiling click stays standing", block_wall_variant_rotation( standing_sign_def, glm::ivec3( 0, -1, 0 ) ) < 0 );
        check_bool( "non-sign block has no wall variant",
                    block_wall_variant_rotation( block_definition_get_definition( LADDER ), glm::ivec3( 0, 0, 1 ) ) < 0 );
    }

    return failures;
}

// Ladder detection: Collision::on_climbable is true while the player AABB
// overlaps a cell holding a "climbable" block (ladders don't collide, so
// the overlap scan is the only way movement senses them) and false once no
// part of the box touches the ladder's cell.
//
//   wall at x=10, y=10..14   ladder column hung on its +x face (cell x=11)
int test_climb( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    RepGameState &gs = *new RepGameState( );
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 0, 0, 0 );
    test_setup_world( gs.world );
    World &world = gs.world;

    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    const BlockState ladder = { LADDER, 0, 0, LADDER, 0 };
    for ( int y = 10; y <= 14; y++ ) {
        world.set_loaded_block( glm::ivec3( 10, y, 10 ), stone );
        world.set_loaded_block( glm::ivec3( 11, y, 10 ), ladder );
    }

    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };
    auto check = [ & ]( const char *name, const glm::dvec3 &eye, bool expected ) {
        const bool got = Collision::on_climbable( world, eye, PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) != 0;
        const bool ok = got == expected;
        pr_test( "  %-55s on=%d expected=%d  %s", name, got, expected, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };

    check_bool( "def: LADDER climbable, STONE not",
                block_definition_get_definition( LADDER )->climbable && !block_definition_get_definition( STONE )->climbable );

    const double eye_y = 10.0 + PLAYER_EYE_HEIGHT;
    check( "centered in the ladder cell", glm::dvec3( 11.5, eye_y, 10.5 ), true );
    check( "body edge still reaches the cell (x=12.25)", glm::dvec3( 12.25, eye_y, 10.5 ), true );
    check( "fully clear of the cell (x=12.4)", glm::dvec3( 12.4, eye_y, 10.5 ), false );
    check( "one cell over in z", glm::dvec3( 11.5, eye_y, 11.5 ), false );
    check( "top of the column (feet still in it)", glm::dvec3( 11.5, 14.8 + PLAYER_EYE_HEIGHT, 10.5 ), true );
    check( "above the ladder column", glm::dvec3( 11.5, 16.0 + PLAYER_EYE_HEIGHT, 10.5 ), false );
    check( "unloaded area reads false", glm::dvec3( 100.5, eye_y, 100.5 ), false );

    // Stone floor under the column area (tops at y=10) so the movement loop
    // below has ground to land and stand on.
    for ( int x = 9; x <= 16; x++ ) {
        for ( int z = 9; z <= 12; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 9, z ), stone );
        }
    }

    // Feet exactly on a floor top (flush contact, no overlap) must land on
    // that face, not slip through it: the same boundary case as a player
    // dropped by `goto` onto flat ground.
    {
        glm::dvec3 eye( 14.5, 10.0 + PLAYER_EYE_HEIGHT, 10.5 );
        int standing = 0;
        glm::dvec3 vec( 0, -0.05, 0 );
        Collision::check_move( world, vec, eye, &standing, PLAYER_HEIGHT, PLAYER_EYE_HEIGHT, false );
        eye += vec;
        check_bool( "feet flush with floor top: lands standing", standing == 1 );
        check_bool( "feet flush with floor top: rests on face", fabs( eye.y - ( 10.0 + PLAYER_EYE_HEIGHT + 1e-4 ) ) < 0.01 );
    }

    // Drive the same loop process_movement runs (accel toward target vel,
    // climbing branch, gravity, check_move, velocity writeback) while holding
    // forward into the wall: the player should grab the ladder and ascend.
    // `forward_input` stands in for the real branch's cos(angleH) > 0.5 gate
    // — only forward-ish input climbs; strafe/back pushes do not.
    auto simulate = [ & ]( bool forward_input ) {
        glm::dvec3 eye( 12.5, 10.0 + PLAYER_EYE_HEIGHT, 10.5 );
        glm::dvec2 horizontal_vel( 0.0 );
        double y_speed = 0.0;
        int standing = 0;
        const glm::dvec2 target( -MOVEMENT_SENSITIVITY_WALKING, 0.0 ); // pushing -x into the wall
        for ( int tick = 0; tick < 200; tick++ ) {
            const bool on_ladder = Collision::on_climbable( world, eye, PLAYER_HEIGHT, PLAYER_EYE_HEIGHT );
            // The climbing branch of process_movement: forward input held
            // while the previous tick's applied horizontal displacement ~0.
            const bool pushing = forward_input && glm::length( horizontal_vel ) < 0.01;
            if ( on_ladder ) {
                if ( pushing ) {
                    y_speed = CLIMB_VERTICAL_SPEED;
                } else if ( y_speed < -CLIMB_SLIDE_SPEED ) {
                    y_speed = -CLIMB_SLIDE_SPEED;
                }
            } else if ( y_speed > -0.5 ) {
                y_speed -= GRAVITY_STRENGTH;
            }
            // horizontal_vel approaches target at the ground/air accel rate.
            const glm::dvec2 dvel = target - horizontal_vel;
            const double dvel_len = glm::length( dvel );
            const double rate = standing ? PLAYER_GROUND_ACCEL : PLAYER_AIR_ACCEL;
            horizontal_vel = dvel_len <= rate ? target : horizontal_vel + dvel * ( rate / dvel_len );
            glm::dvec3 vec( horizontal_vel.x, y_speed - GRAVITY_STRENGTH, horizontal_vel.y );
            Collision::check_move( world, vec, eye, &standing, PLAYER_HEIGHT, PLAYER_EYE_HEIGHT, false );
            eye += vec;
            horizontal_vel.x = vec.x;
            horizontal_vel.y = vec.z;
        }
        return eye;
    };

    {
        const glm::dvec3 eye = simulate( true );
        const double feet = eye.y - PLAYER_EYE_HEIGHT;
        check_bool( "walk into wall grabs the ladder and climbs", feet > 11.0 );
        check_bool( "climb stops against the wall face (x≈11.3)", eye.x > 11.2 && eye.x < 11.4 );
        check_bool( "still overlapping the ladder cell", Collision::on_climbable( world, eye, PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) );
    }
    {
        // Same push into the wall but not forward-held (strafe/back): the
        // body stays pressed into the wall in the ladder cell and slides
        // down to the floor instead of climbing.
        const glm::dvec3 eye = simulate( false );
        const double feet = eye.y - PLAYER_EYE_HEIGHT;
        check_bool( "strafe/back push into the wall does not climb", feet < 10.5 );
    }

    return failures;
}

// Held-item swing phase (HeldSwing::tick): parks at 0 while idle, advances
// once per tick while the button is held, wraps mid-chop rather than
// stopping at 1, and after release runs the in-flight swing out to the
// rest pose instead of snapping.
int test_held_swing( ) {
    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };
    const float step = 1.0f / HeldSwing::PERIOD_TICKS;

    HeldSwing swing;
    swing.tick( false );
    check_bool( "idle stays parked", swing.phase == 0.0f );

    swing.tick( true );
    check_bool( "first held tick advances", fabsf( swing.phase - step ) < 1e-6f && swing.prev_phase == 0.0f );

    // Hold well past one period — the phase must loop, not clamp at 1.
    for ( int i = 0; i < 2 * ( int )HeldSwing::PERIOD_TICKS; i++ ) {
        swing.tick( true );
    }
    check_bool( "held loops past 1", swing.phase > 0.0f && swing.phase < 1.0f );

    // Release mid-swing: the chop runs out to the rest pose then parks.
    swing.phase = 0.5f;
    swing.prev_phase = 0.5f - step;
    swing.tick( false );
    check_bool( "released mid-swing keeps going", swing.phase > 0.5f );
    while ( swing.phase > 0.0f ) {
        swing.tick( false );
    }
    check_bool( "finished swing parks at rest", swing.phase == 0.0f && swing.prev_phase > 0.0f );
    swing.tick( false );
    check_bool( "stays parked after finishing", swing.phase == 0.0f && swing.prev_phase == 0.0f );

    // Re-press starts a fresh chop from rest.
    swing.tick( true );
    check_bool( "re-press restarts from rest", fabsf( swing.phase - step ) < 1e-6f );

    return failures;
}


// Player vitals: player_damage gates on game mode / dead / the shared
// i-frame window and raises the death screen at 0; player_heal clamps at
// max; repgame_eat_food heals the item's "food" value and consumes one in
// survival; repgame_tick_vitals drains breath underwater then drowns,
// burns on lava/fire, regenerates in peaceful; the explosion helper does
// distance-falloff damage plus a shove; respawn restores vitals and pins
// the player back on the spawn column.
//
//   eye at (8.5, 10.5, 8.5), eye_height 1.62:
//     eye cell (8,10,8)  body cell (8,9,8)  feet cell (8,8,8)
int test_vitals( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    RepGameState &gs = *new RepGameState( );
    gs.tick_number = 0;
    // Interior of the single test chunk — set_loaded_block refuses border
    // writes (a missing neighbor can't be fixed up).
    gs.camera.pos = glm::dvec3( 8.5, 10.5, 8.5 );
    test_setup_world( gs.world );
    test_setup_inventories( gs );

    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };
    auto check_near = [ & ]( const char *name, double actual, double expected ) {
        const bool ok = fabs( actual - expected ) <= 1e-4;
        pr_test( "  %-55s value=%-9.4f expected=%-9.4f  %s", name, actual, expected, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };

    gs.game_mode = GameMode_Survival;

    // Food metadata lands in the generated item table.
    check_bool( "food heal values (apple/bread/beef)",
                item_food_heal( APPLE ) == 4 && item_food_heal( BREAD ) == 5 && item_food_heal( COOKED_BEEF ) == 8 );
    check_bool( "non-food heals 0", item_food_heal( STONE ) == 0 && item_food_heal( ARROW ) == 0 && item_food_heal( LAST_BLOCK_ID ) == 0 );

    // player_damage: survival takes it, i-frames gate the follow-up.
    check_bool( "survival takes damage", RepGame::player_damage( gs, 5.0f ) && gs.vitals.health == 15.0f );
    check_bool( "i-frames gate the next hit", !RepGame::player_damage( gs, 5.0f ) && gs.vitals.health == 15.0f );
    gs.vitals.hurt_cooldown = 0;
    check_bool( "cooldown expiry lets damage through", RepGame::player_damage( gs, 3.0f ) && gs.vitals.health == 12.0f );
    check_bool( "non-positive damage refused", !RepGame::player_damage( gs, 0.0f ) );
    gs.game_mode = GameMode_Creative;
    gs.vitals.hurt_cooldown = 0;
    check_bool( "creative is invulnerable", !RepGame::player_damage( gs, 5.0f ) && gs.vitals.health == 12.0f );
    gs.game_mode = GameMode_Survival;

    // Heal clamps at max.
    gs.vitals.health = 19.0f;
    RepGame::player_heal( gs, 5.0f );
    check_bool( "heal clamps at max", gs.vitals.health == PLAYER_MAX_HEALTH );

    // Lethal hit: health pins at 0, dead raises the (locked) death screen.
    gs.vitals.hurt_cooldown = 0;
    gs.vitals.health = 4.0f;
    check_bool( "lethal hit kills", RepGame::player_damage( gs, 10.0f ) && gs.vitals.dead && gs.vitals.health == 0.0f && gs.input.menu_open );
    check_bool( "dead refuses more damage", !RepGame::player_damage( gs, 1.0f ) );
    gs.vitals = PlayerVitals( );
    gs.ui.pause.set_dead( gs, false );
    gs.input.menu_open = false;

    // Eating: survival consumes one, heals the food value; refused at full
    // health or on a non-food item; creative heals without consuming.
    gs.hotbar.setSlotContents( 0, { APPLE, 4 } );
    gs.vitals.health = 10.0f;
    check_bool( "eat heals + consumes one",
                repgame_eat_food( gs ) && gs.vitals.health == 14.0f && gs.hotbar.getSelectedItem( ) == APPLE && gs.hotbar.getSelectedQuantity( ) == 3 );
    gs.vitals.health = PLAYER_MAX_HEALTH;
    check_bool( "eat refused at full health", !repgame_eat_food( gs ) && gs.hotbar.getSelectedQuantity( ) == 3 );
    gs.hotbar.setSlotContents( 0, { STONE, 4 } );
    gs.vitals.health = 10.0f;
    check_bool( "eat refused on non-food", !repgame_eat_food( gs ) && gs.vitals.health == 10.0f && gs.hotbar.getSelectedQuantity( ) == 4 );
    gs.game_mode = GameMode_Creative;
    gs.hotbar.setSlotContents( 0, { BREAD, 2 } );
    check_bool( "creative eat heals, keeps stack", repgame_eat_food( gs ) && gs.vitals.health == 15.0f && gs.hotbar.getSelectedQuantity( ) == 2 );
    gs.game_mode = GameMode_Survival;

    // tick_vitals: breath drains with the head in water, then drowning hits
    // through the shared damage funnel (paced by i-frames).
    gs.vitals = PlayerVitals( );
    gs.vitals.health = 12.0f;
    gs.world.set_loaded_block( glm::ivec3( 8, 10, 8 ), { WATER, 0, 0, WATER, 0 } );
    repgame_tick_vitals( gs );
    check_bool( "submerged: breath drains", gs.vitals.air_ticks == PLAYER_AIR_MAX_TICKS - 1 );
    gs.vitals.air_ticks = 0;
    repgame_tick_vitals( gs );
    check_near( "breath out: drowns", gs.vitals.health, 12.0f - PLAYER_DROWN_DAMAGE );
    check_bool( "drowning sets i-frames", gs.vitals.hurt_cooldown == PLAYER_HURT_COOLDOWN_TICKS );
    repgame_tick_vitals( gs );
    check_near( "drowning paced by i-frames", gs.vitals.health, 12.0f - PLAYER_DROWN_DAMAGE );
    gs.world.set_loaded_block( glm::ivec3( 8, 10, 8 ), BLOCK_STATE_AIR );
    gs.vitals.air_ticks = 5;
    repgame_tick_vitals( gs );
    check_bool( "surfaced: breath refills", gs.vitals.air_ticks == PLAYER_AIR_MAX_TICKS );

    // Lava burns fast at the feet cell; open fire burns slower.
    gs.vitals.hurt_cooldown = 0;
    gs.vitals.health = 12.0f;
    gs.world.set_loaded_block( glm::ivec3( 8, 8, 8 ), { LAVA, 0, 0, LAVA, 0 } );
    repgame_tick_vitals( gs );
    check_near( "lava at feet burns", gs.vitals.health, 12.0f - PLAYER_LAVA_DAMAGE );
    gs.world.set_loaded_block( glm::ivec3( 8, 8, 8 ), { FIRE, 0, 0, FIRE, 0 } );
    gs.vitals.hurt_cooldown = 0;
    repgame_tick_vitals( gs );
    check_near( "fire at feet burns slower", gs.vitals.health, 12.0f - PLAYER_LAVA_DAMAGE - PLAYER_FIRE_DAMAGE );
    gs.world.set_loaded_block( glm::ivec3( 8, 8, 8 ), BLOCK_STATE_AIR );

    // Peaceful regen: one half-heart per PLAYER_PEACEFUL_REGEN_TICKS while
    // hurt in survival; off without the toggle.
    gs.vitals = PlayerVitals( );
    gs.vitals.health = 10.0f;
    gs.peaceful = true;
    for ( int i = 0; i < PLAYER_PEACEFUL_REGEN_TICKS; i++ ) {
        repgame_tick_vitals( gs );
    }
    check_near( "peaceful regen ticks to +1", gs.vitals.health, 11.0f );
    gs.peaceful = false;
    gs.vitals.regen_accum = 0;
    for ( int i = 0; i < PLAYER_PEACEFUL_REGEN_TICKS * 2; i++ ) {
        repgame_tick_vitals( gs );
    }
    check_near( "no regen without peaceful", gs.vitals.health, 11.0f );

    // Dead: vitals tick holds the menu open and keeps breath full.
    gs.vitals.dead = true;
    gs.input.menu_open = false;
    gs.vitals.air_ticks = 0;
    repgame_tick_vitals( gs );
    check_bool( "dead: menu held open, breath refilled", gs.input.menu_open && gs.vitals.air_ticks == PLAYER_AIR_MAX_TICKS );
    gs.vitals = PlayerVitals( );
    gs.input.menu_open = false;

    // Explosion: linear falloff inside the radius, a shove away; nothing
    // past the edge.
    gs.vitals.health = PLAYER_MAX_HEALTH;
    gs.camera.horizontal_vel = glm::dvec2( 0.0 );
    gs.camera.y_speed = 0.0;
    repgame_explosion_affect_player( gs, glm::dvec3( 10.5, 10.5, 8.5 ), 8.0f );
    check_near( "blast in radius damages (75% falloff)", gs.vitals.health, PLAYER_MAX_HEALTH - 0.75 * PLAYER_EXPLOSION_MAX_DAMAGE );
    check_bool( "blast pushes away from center", gs.camera.horizontal_vel.x < 0.0 && gs.camera.y_speed > 0.0 );
    gs.vitals = PlayerVitals( );
    gs.camera.horizontal_vel = glm::dvec2( 0.0 );
    gs.camera.y_speed = 0.0;
    repgame_explosion_affect_player( gs, glm::dvec3( 28.5, 10.5, 8.5 ), 8.0f );
    check_bool( "outside radius: untouched", gs.vitals.health == PLAYER_MAX_HEALTH && gs.camera.horizontal_vel.x == 0.0 );

    // respawn restores vitals, clears the death screen and re-pins the
    // spawn column (spawn_pending does the ground snap once it loads).
    gs.vitals.health = 0.0f;
    gs.vitals.dead = true;
    gs.vitals.air_ticks = 0;
    gs.vitals.fall_dist = 12.0;
    gs.ui.pause.set_dead( gs, true );
    gs.input.menu_open = true;
    repgame_respawn( gs );
    check_bool( "respawn restores vitals", gs.vitals.health == PLAYER_MAX_HEALTH && !gs.vitals.dead &&
                                              gs.vitals.air_ticks == PLAYER_AIR_MAX_TICKS && gs.vitals.fall_dist == 0.0 );
    check_bool( "respawn closes menu + re-pins spawn", !gs.input.menu_open && gs.spawn_pending );

    // Avatar hurt flash: a health drop between remote updates flashes the
    // player's mob red; the first packet (-1 -> known) and a heal don't.
    {
        MobManager mobs;
        MultiplayerAvatars avatars;
        avatars.init( &mobs );
        avatars.add( 2 );
        const glm::dvec3 listener( 0.0 );
        const glm::mat4 rot( 1.0f );
        avatars.update_position( 2, 1.0f, 10.0f, 1.0f, rot, PLAYER_EYE_HEIGHT, 0, 20.0f, LAST_BLOCK_ID, listener );
        check_bool( "first update: no flash", mobs.mobs[ 0 ].hurt_ticks == 0 );
        avatars.update_position( 2, 1.0f, 10.0f, 1.0f, rot, PLAYER_EYE_HEIGHT, 0, 10.0f, LAST_BLOCK_ID, listener );
        check_bool( "health drop: avatar flashes", mobs.mobs[ 0 ].hurt_ticks > 0 );
        mobs.mobs[ 0 ].hurt_ticks = 0;
        avatars.update_position( 2, 1.0f, 10.0f, 1.0f, rot, PLAYER_EYE_HEIGHT, 0, 15.0f, LAST_BLOCK_ID, listener );
        check_bool( "heal: no flash", mobs.mobs[ 0 ].hurt_ticks == 0 );
        avatars.update_position( 2, 1.0f, 10.0f, 1.0f, rot, PLAYER_EYE_HEIGHT, 0, 15.0f, LAST_BLOCK_ID, listener );
        check_bool( "same health: no flash", mobs.mobs[ 0 ].hurt_ticks == 0 );
        // The streamed held item lands on the avatar's mob.
        avatars.update_position( 2, 1.0f, 10.0f, 1.0f, rot, PLAYER_EYE_HEIGHT, 0, 15.0f, STICK, listener );
        check_bool( "held item reaches the mob", mobs.mobs[ 0 ].held_item == STICK );
    }

    return failures;
}

// Bed sleeping: repgame_try_sleep records the bed as the respawn point on
// any click but only advances world_time in the night half of the day, and
// refuses while a hostile mob is near (spawn point still set).
// repgame_resolve_pending_spawn stands the player on the recorded bed —
// retrying while its column is ungenerated, falling back to the
// world-spawn column (and clearing the point) when the bed is gone.
//
//   bed at (8,10,8), 9/16 top at y=10.5625, player lands at eye 12.1825
int test_sleep( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    RepGameState &gs = *new RepGameState( );
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 8.5, 12.5, 8.5 );
    test_setup_world( gs.world );
    World &world = gs.world;
    const glm::ivec3 bed( 8, 10, 8 );
    world.set_loaded_block( bed, { BED, 0, 0, BED, 0 } );

    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };
    auto check_near = [ & ]( const char *name, double actual, double expected ) {
        const bool ok = fabs( actual - expected ) <= 1e-4;
        pr_test( "  %-55s value=%-9.4f expected=%-9.4f  %s", name, actual, expected, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };

    // Day: refused, but the spawn point is still recorded (MC semantics).
    gs.world_time = DAY_LENGTH_TICKS / 4;
    check_bool( "day: sleep refused", !repgame_try_sleep( gs, bed ) );
    check_bool( "day: spawn point still set", gs.spawn_point_set && gs.spawn_point == bed );
    check_bool( "day: clock unchanged", gs.world_time == DAY_LENGTH_TICKS / 4 );

    // Night: the clock lands just past the next dawn boundary.
    gs.world_time = 5 * DAY_LENGTH_TICKS + DAY_LENGTH_TICKS * 3 / 4;
    check_bool( "night: sleeps to next dawn",
                repgame_try_sleep( gs, bed ) && gs.world_time == 6 * DAY_LENGTH_TICKS + SLEEP_WAKE_OFFSET );

    // Non-bed target (the harness `sleep x y z` command can aim at any
    // cell): refused outright at night, existing point untouched.
    gs.world_time = 6 * DAY_LENGTH_TICKS + DAY_LENGTH_TICKS * 3 / 4;
    check_bool( "non-bed: refused, point kept",
                !repgame_try_sleep( gs, glm::ivec3( 8, 11, 8 ) ) && gs.spawn_point == bed );

    // Monsters nearby: refused even at night (the point stays set).
    gs.world_time = DAY_LENGTH_TICKS * 3 / 4;
    world.mobs.spawn( MOB_ZOMBIE, glm::dvec3( 10.5, 12.0, 8.5 ), 0.0f, 1, false );
    check_bool( "monsters nearby: refused",
                !repgame_try_sleep( gs, bed ) && gs.world_time == DAY_LENGTH_TICKS * 3 / 4 );

    // Bed column ungenerated (outside the single test chunk): pending
    // holds and the camera is pulled over the column so it loads.
    gs.spawn_point = glm::ivec3( 64, 10, 64 );
    gs.spawn_point_set = true;
    gs.spawn_pending = true;
    repgame_resolve_pending_spawn( gs );
    check_bool( "unloaded bed: stays pending, camera pulled",
                gs.spawn_pending && gs.spawn_point_set && gs.camera.pos.x == 64.5 && gs.camera.pos.z == 64.5 );

    // Respawn at the recorded bed: stands on the bed top, point kept for
    // the next death.
    gs.spawn_point = bed;
    repgame_respawn( gs );
    repgame_resolve_pending_spawn( gs );
    check_bool( "bed respawn resolves", !gs.spawn_pending && gs.spawn_point_set );
    check_near( "bed respawn stands on top", gs.camera.pos.y, Collision::block_collision_top( world, bed ) + PLAYER_EYE_HEIGHT );
    check_near( "bed respawn centered on cell", gs.camera.pos.x, 8.5 );

    // Bed gone: the point clears and resolution falls back to the
    // world-spawn column (still pending in the test world — column (0,0)
    // is all air there).
    gs.spawn_point = glm::ivec3( 4, 10, 4 );
    gs.spawn_point_set = true;
    gs.spawn_pending = true;
    repgame_resolve_pending_spawn( gs );
    check_bool( "missing bed: point cleared, world spawn pending", !gs.spawn_point_set && gs.spawn_pending );

    return failures;
}

// The bed is a two-cell block like a door turned on its side: placing the
// foot item writes the head one cell in the placer's facing direction
// (yaw 0 = -z), either half sleeps, and breaking one half pops the other.
//
//   foot (10,10,10..13) with stone floors at y=9
int test_bed_pair( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    RepGameState &gs = *new RepGameState( );
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 8.5, 12.5, 8.5 );
    test_setup_world( gs.world );
    World &world = gs.world;

    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };
    auto run = [ & ]( const int ticks ) {
        for ( int i = 0; i < ticks; i++ ) {
            gs.tick_number++;
            gs.blockUpdateQueue.processAllBlockUpdates( gs, gs.tick_number );
        }
    };
    auto place = [ & ]( const glm::ivec3 &pos, const BlockState &block_state ) {
        gs.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( gs.tick_number, pos, block_state, false );
    };
    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    const BlockState bed_yaw[ 4 ] = {
        { BED, 0, 0, BED, 0 }, { BED, BLOCK_ROTATE_90, 0, BED, 0 }, { BED, BLOCK_ROTATE_180, 0, BED, 0 }, { BED, BLOCK_ROTATE_270, 0, BED, 0 },
    };
    const glm::ivec3 head_dir[ 4 ] = { glm::ivec3( 0, 0, -1 ), glm::ivec3( 1, 0, 0 ), glm::ivec3( 0, 0, 1 ), glm::ivec3( -1, 0, 0 ) };

    // A foot writes itself plus its head one cell in the yaw direction.
    const glm::ivec3 feet[ 4 ] = { glm::ivec3( 4, 10, 12 ), glm::ivec3( 8, 10, 12 ), glm::ivec3( 12, 10, 12 ), glm::ivec3( 4, 10, 4 ) };
    for ( int yaw = 0; yaw < 4; yaw++ ) {
        const glm::ivec3 foot = feet[ yaw ];
        const glm::ivec3 head = foot + head_dir[ yaw ];
        world.set_loaded_block( foot + glm::ivec3( 0, -1, 0 ), stone );
        world.set_loaded_block( head + glm::ivec3( 0, -1, 0 ), stone );
        place( foot, bed_yaw[ yaw ] );
    }
    run( 2 );
    char label[ 96 ];
    for ( int yaw = 0; yaw < 4; yaw++ ) {
        snprintf( label, sizeof( label ), "place: yaw %d foot written", yaw );
        failures += check_id( world, label, feet[ yaw ], BED );
        snprintf( label, sizeof( label ), "place: yaw %d head written", yaw );
        failures += check_id( world, label, feet[ yaw ] + head_dir[ yaw ], BED_HEAD );
    }

    // Sleeping works off the head half too — same is_bed interaction.
    gs.world_time = DAY_LENGTH_TICKS * 3 / 4;
    check_bool( "night: sleeps via the head half", repgame_try_sleep( gs, feet[ 0 ] + head_dir[ 0 ] ) );

    // No floor under the head cell, a blocked head cell, and no floor under
    // the foot each leave both cells empty.
    const glm::ivec3 no_head_floor( 8, 10, 6 );
    world.set_loaded_block( no_head_floor + glm::ivec3( 0, -1, 0 ), stone );
    place( no_head_floor, bed_yaw[ 0 ] );
    const glm::ivec3 blocked_head( 12, 10, 6 );
    world.set_loaded_block( blocked_head + glm::ivec3( 0, -1, 0 ), stone );
    world.set_loaded_block( blocked_head + head_dir[ 0 ] + glm::ivec3( 0, -1, 0 ), stone );
    world.set_loaded_block( blocked_head + head_dir[ 0 ], stone );
    place( blocked_head, bed_yaw[ 0 ] );
    place( glm::ivec3( 10, 10, 8 ), bed_yaw[ 0 ] );
    run( 2 );
    failures += check_id( world, "no head floor: foot not placed", no_head_floor, AIR );
    failures += check_id( world, "no head floor: head not placed", no_head_floor + head_dir[ 0 ], AIR );
    failures += check_id( world, "blocked head: foot not placed", blocked_head, AIR );
    failures += check_id( world, "no foot floor: nothing placed", glm::ivec3( 10, 10, 8 ), AIR );

    // A lone head can't be placed by itself (no partner).
    world.set_loaded_block( glm::ivec3( 6, 9, 6 ), stone );
    place( glm::ivec3( 6, 10, 6 ), { BED_HEAD, 0, 0, BED_HEAD, 0 } );
    run( 2 );
    failures += check_id( world, "lone head: not placed", glm::ivec3( 6, 10, 6 ), AIR );

    // Breaking the foot pops the head through the survive check.
    place( feet[ 0 ], BLOCK_STATE_AIR );
    run( 2 );
    failures += check_id( world, "foot break: foot cleared", feet[ 0 ], AIR );
    failures += check_id( world, "foot break: head popped", feet[ 0 ] + head_dir[ 0 ], AIR );
    // Breaking the head pops the foot.
    place( feet[ 1 ] + head_dir[ 1 ], BLOCK_STATE_AIR );
    run( 2 );
    failures += check_id( world, "head break: head cleared", feet[ 1 ] + head_dir[ 1 ], AIR );
    failures += check_id( world, "head break: foot popped", feet[ 1 ], AIR );

    // The head half is is_pickable=false but still ray-targetable — the
    // is_bed whitelist spot in RayTraversal, same as the door upper.
    const glm::ivec3 head2 = feet[ 2 ] + head_dir[ 2 ];
    glm::ivec3 out( -1, -1, -1 );
    int which_face = -1;
    check_bool( "pick: ray hits the head half",
                RayTraversal::find_block_from_to( world,
                                                  glm::dvec3( head2.x + 0.5, head2.y + 0.3, head2.z + 2.5 ),
                                                  glm::dvec3( head2.x + 0.5, head2.y + 0.3, head2.z - 1.5 ),
                                                  out, &which_face ) &&
                    out == head2 );

    return failures;
}
