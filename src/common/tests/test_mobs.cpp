// Mob simulation tests — split out of rep_tests.cpp.
#include "test_util.hpp"

#include <filesystem>
#include <string>
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

    // --- 2b: fences/closed gates read un-jumpable (collision top 1.5) ------
    {
        // The auto-jump must key off the wall's real collision top, not cell
        // occupancy: a one-cell fence collides 1.5 high, past the ~1.2 a hop
        // clears, so a mob pressing against it must never leave the ground.
        const BlockState fence = { FENCE, 0, 0, FENCE, 0 };
        const BlockState gate = { FENCE_GATE, 0, 0, FENCE_GATE, 0 }; // rot 0 = closed
        for ( int part = 0; part < 2; part++ ) {
            const BlockState wall = part == 0 ? fence : gate;
            const char *tag = part == 0 ? "fence" : "closed gate";
            for ( int z = 9; z <= 11; z++ ) {
                world.set_loaded_block( glm::ivec3( 14, 11, z ), wall );
            }
            MobManager mobs;
            mobs.spawn( MOB_PIG, glm::dvec3( 10.5, 11.0, 10.5 ), 90.0f, 1 );
            MobManager::Mob &mob = mobs.mobs[ 0 ];
            mob.ai_state = 1;
            mob.ai_ticks = 100000;
            mob.target = glm::dvec3( 20.0, 11.0, 10.5 );
            int airborne = 0;
            for ( int i = 0; i < 600; i++ ) {
                mobs.tick( world, player_pos );
                airborne += mob.standing ? 0 : 1;
            }
            char name[ 64 ];
            snprintf( name, sizeof( name ), "%s: no doomed hopping", tag );
            check_bool( name, airborne == 0 );
            snprintf( name, sizeof( name ), "%s: held behind", tag );
            check_bool( name, mob.pos.x < 14.0 && mob.pos.y < 12.0 );
            for ( int z = 9; z <= 11; z++ ) {
                world.set_loaded_block( glm::ivec3( 14, 11, z ), BLOCK_STATE_AIR );
            }
        }

        // The check is a height check, not a fence blacklist: from a half
        // slab the wall top is only ~1.0 above the feet, inside hop reach.
        const BlockState slab = { STONE_BRICK_SLAB, 0, 0, STONE_BRICK_SLAB, 0 };
        for ( int x = 8; x <= 13; x++ ) {
            for ( int z = 8; z <= 12; z++ ) {
                world.set_loaded_block( glm::ivec3( x, 11, z ), slab );
            }
        }
        for ( int z = 9; z <= 11; z++ ) {
            world.set_loaded_block( glm::ivec3( 14, 11, z ), fence );
        }
        MobManager mobs;
        mobs.spawn( MOB_PIG, glm::dvec3( 10.5, 11.5, 10.5 ), 90.0f, 1 );
        MobManager::Mob &mob = mobs.mobs[ 0 ];
        mob.ai_state = 1;
        mob.ai_ticks = 100000;
        mob.target = glm::dvec3( 20.0, 11.0, 10.5 );
        for ( int i = 0; i < 1200 && mob.pos.x < 14.6; i++ ) {
            mobs.tick( world, player_pos );
        }
        check_bool( "fence: slab boost hops it", mob.pos.x > 14.6 );
        for ( int x = 8; x <= 13; x++ ) {
            for ( int z = 8; z <= 12; z++ ) {
                world.set_loaded_block( glm::ivec3( x, 11, z ), BLOCK_STATE_AIR );
            }
        }
        for ( int z = 9; z <= 11; z++ ) {
            world.set_loaded_block( glm::ivec3( 14, 11, z ), BLOCK_STATE_AIR );
        }
    }

    // --- 2c: a baby grown against a wall/fence is pushed back, not through --
    {
        // Babies collide at half width: pressed flush against a wall, the
        // adult hitbox overlaps the block the instant age_ticks hits 0, and
        // check_move's face-crossing clamps can't see that overlap — the mob
        // would tunnel straight through. The grow-up unstick must push it
        // back to its own side.
        const BlockState fence = { FENCE, 0, 0, FENCE, 0 };
        for ( int part = 0; part < 2; part++ ) {
            const BlockState wall = part == 0 ? stone : fence;
            const char *tag = part == 0 ? "wall" : "fence";
            for ( int y = 11; y <= 12; y++ ) {
                for ( int z = 9; z <= 11; z++ ) {
                    world.set_loaded_block( glm::ivec3( 14, y, z ), wall );
                }
            }
            // Baby pig face flush on the wall: baby half-width is 0.225; the
            // fence's side face sits at 14.375 inside the cell.
            const double face = part == 0 ? 14.0 : 14.375;
            MobManager mobs;
            mobs.spawn( MOB_PIG, glm::dvec3( face - 0.225 - 1e-4, 11.0, 10.5 ), 0.0f, 1 );
            MobManager::Mob &mob = mobs.mobs[ 0 ];
            mob.ai_state = 0;
            mob.ai_ticks = 100000;
            mob.age_ticks = -1;
            mobs.tick( world, player_pos );
            char name[ 64 ];
            snprintf( name, sizeof( name ), "%s: grow-up pushed back to own side", tag );
            check_bool( name, mob.age_ticks == 0 && mob.pos.x + 0.45 <= face + 0.01 );
            // Keep walking into it: the mob must never reach the far side.
            mob.ai_state = 1;
            mob.ai_ticks = 100000;
            mob.target = glm::dvec3( 20.0, 11.0, 10.5 );
            for ( int i = 0; i < 600; i++ ) {
                mobs.tick( world, player_pos );
            }
            snprintf( name, sizeof( name ), "%s: grown mob never tunnels through", tag );
            check_bool( name, mob.pos.x < 14.0 );
            for ( int y = 11; y <= 12; y++ ) {
                for ( int z = 9; z <= 11; z++ ) {
                    world.set_loaded_block( glm::ivec3( 14, y, z ), BLOCK_STATE_AIR );
                }
            }
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

    // --- 6: mobs file round trip, net_id + removed, empty removes file -----
    {
        // Point the map root at a throwaway dir under /tmp (getRepGamePath
        // reads $HOME): no writes land in a real world.
        std::filesystem::remove_all( "/tmp/repgame_test_mobs_home" );
        ScopedTempHome temp_home( "/tmp/repgame_test_mobs_home" );
        MapStorage storage;
        storage.init( "TestMobs" );
        const glm::ivec3 chunk_pos( 0, 0, 0 );
        std::vector<MobFileRecord> in;
        MobFileRecord r = { 0, 0, 0.0, 0.0, 0.0, 0.0f, 0.0f, 0u, 0u, 0 };
        r.type = MOB_PIG;
        r.variant = 1;
        r.x = 10.5;
        r.y = 11.0;
        r.z = 10.25;
        r.yaw = 45.0f;
        r.health = 7.5f;
        r.net_id = net_natural_mob_id( chunk_pos, 3 );
        in.push_back( r );
        storage.persist_mobs( chunk_pos, in, { 3, 7 } );
        std::vector<MobFileRecord> out;
        std::vector<uint8_t> removed;
        check_bool( "mob file: present", storage.load_mobs( chunk_pos, out, &removed ) == 1 );
        check_bool( "mob file: one record", out.size( ) == 1 );
        if ( out.size( ) == 1 ) {
            const MobFileRecord &o = out[ 0 ];
            check_bool( "mob file: fields + net_id round trip",
                        o.type == MOB_PIG && o.variant == 1 && o.x == 10.5 && o.y == 11.0 && o.z == 10.25 && o.yaw == 45.0f &&
                            o.health == 7.5f && o.net_id == r.net_id );
        }
        check_bool( "mob file: removed round trip", removed.size( ) == 2 && removed[ 0 ] == 3 && removed[ 1 ] == 7 );
        // Empty records + empty removed deletes the file (no populated
        // marker semantics anymore).
        storage.persist_mobs( chunk_pos, { }, { } );
        out.clear( );
        check_bool( "mob file: empty+empty removes", storage.load_mobs( chunk_pos, out ) == 0 );
        // A pre-net_id file (record_size without net_id, no removed
        // section) still loads with net_id == 0.
        {
            char *dir = getRepGamePath( );
            char map_root[ CHUNK_NAME_MAX_LENGTH ];
            snprintf( map_root, sizeof( map_root ), "%s/TestMobs", dir );
            free( dir );
            char file_name[ CHUNK_NAME_MAX_LENGTH ];
            snprintf( file_name, sizeof( file_name ), FILE_ROOT_MOBS, map_root, 0, 0, 3 );
            FILE *f = fopen( file_name, "wb" );
            const uint32_t old_size = static_cast<uint32_t>( offsetof( MobFileRecord, net_id ) );
            const uint32_t one = 1;
            fwrite( &old_size, sizeof( uint32_t ), 1, f );
            fwrite( &one, sizeof( uint32_t ), 1, f );
            fwrite( &r, old_size, 1, f );
            fclose( f );
            out.clear( );
            removed.clear( );
            check_bool( "mob file: old record loads", storage.load_mobs( glm::ivec3( 0, 0, 3 ), out, &removed ) == 1 && out.size( ) == 1 );
            check_bool( "mob file: old record has net_id 0", out.size( ) == 1 && out[ 0 ].net_id == 0 && removed.empty( ) );
        }
        // Unknown mob types are skipped on load.
        MobFileRecord bad = r;
        bad.type = MOB_TYPE_COUNT + 3;
        storage.persist_mobs( glm::ivec3( 0, 0, 2 ), std::vector<MobFileRecord>( 1, bad ) );
        out.clear( );
        check_bool( "mob file: unknown type skipped", storage.load_mobs( glm::ivec3( 0, 0, 2 ), out ) == 1 && out.empty( ) );
    }

    // --- 6a: natural mob ids pack/unpack -----------------------------------
    {
        const glm::ivec3 homes[] = { glm::ivec3( 0, 0, 0 ), glm::ivec3( -12, 1, 40 ), glm::ivec3( 1048575, -128, -1048576 ), glm::ivec3( -7, 3, -9 ) };
        bool ok = true;
        for ( const glm::ivec3 &h : homes ) {
            for ( const int idx : { 0, 1, 17, 63 } ) {
                const NetEntityId id = net_natural_mob_id( h, idx );
                ok &= net_is_natural_mob_id( id ) && net_natural_mob_home( id ) == h && net_natural_mob_index( id ) == idx;
            }
        }
        check_bool( "natural id: pack/unpack round trip", ok );
        check_bool( "natural id: client session id is not natural", !net_is_natural_mob_id( ( static_cast<NetEntityId>( 7 ) << 32 ) | 5 ) );
        check_bool( "natural id: session-0 id is not natural", !net_is_natural_mob_id( 42 ) );
    }

    // --- 6b: pristine naturals leave no file; hurt -> record + tombstone ---
    {
        std::filesystem::remove_all( "/tmp/repgame_test_mobs_home" );
        ScopedTempHome temp_home( "/tmp/repgame_test_mobs_home" );
        MapStorage storage;
        storage.init( "TestMobs" );
        // A chunk outside every section's reach (x 310..371).
        const glm::ivec3 home( 5, 0, 0 );
        MobManager mobs;
        mobs.init_storage( &storage );
        mobs.owned_chunks.push_back( { home, 0, {} } );
        mobs.spawn( MOB_PIG, glm::dvec3( 320.5, 11.0, 10.5 ), 0.0f, 1 );
        mobs.mobs[ 0 ].natural_id = net_natural_mob_id( home, 0 );
        mobs.mobs[ 0 ].net_natural = true;
        mobs.spawn( MOB_COW, glm::dvec3( 322.5, 11.0, 10.5 ), 0.0f, 1 );
        mobs.mobs[ 1 ].natural_id = net_natural_mob_id( home, 1 );
        mobs.mobs[ 1 ].net_natural = true;
        mobs.persist_all( );
        std::vector<MobFileRecord> out;
        std::vector<uint8_t> removed;
        check_bool( "pristine naturals: no file written", storage.load_mobs( home, out, &removed ) == 0 );
        // A player hit makes the mob touched: its record persists (net_id +
        // provenance flags) and its roll index is tombstoned.
        mobs.hurt( 0, 1.0f, glm::dvec3( 318.5, 11.5, 10.5 ) );
        mobs.persist_all( );
        check_bool( "touched natural: file written", storage.load_mobs( home, out, &removed ) == 1 && out.size( ) == 1 );
        if ( out.size( ) == 1 ) {
            check_bool( "touched natural: net_id + flags persist",
                        out[ 0 ].net_id == net_natural_mob_id( home, 0 ) &&
                            ( out[ 0 ].flags & NET_MOB_FLAG_NATURAL ) != 0 && ( out[ 0 ].flags & NET_MOB_FLAG_TOUCHED ) != 0 );
        }
        check_bool( "touched natural: index tombstoned", removed.size( ) == 1 && removed[ 0 ] == 0 );
    }

    // --- 6c: roll -> tombstone -> reload skips the index --------------------
    {
        // Find a chunk pos whose deterministic roll emits a herd: a fresh
        // chunk full of grass floor rolls the same way every time.
        Chunk roll_chunk;
        glm::ivec3 found_pos( -1 );
        const int floor_y = 10;
        for ( int x = 0; x < 64 && found_pos.x < 0; x++ ) {
            roll_chunk.chunk_pos = glm::ivec3( x, 0, 0 );
            roll_chunk.cells = chunk_alloc_cells( );
            for ( int cx = 0; cx < CHUNK_SIZE_X; cx++ ) {
                for ( int cz = 0; cz < CHUNK_SIZE_Z; cz++ ) {
                    roll_chunk.cells[ Chunk::get_index_from_coords( glm::ivec3( cx, floor_y, cz ) ) ] = block_cell_pack( GRASS, 0, 0, 0 );
                }
            }
            roll_chunk.pending_mobs.clear( );
            if ( roll_chunk.spawn_mobs( ) && roll_chunk.pending_mobs.size( ) >= 2 ) {
                found_pos = glm::ivec3( x, 0, 0 );
            } else {
                chunk_free_cells( roll_chunk.cells );
                roll_chunk.cells = nullptr;
            }
        }
        check_bool( "natural roll: found a chunk with a herd", found_pos.x >= 0 );
        if ( found_pos.x >= 0 ) {
            const std::vector<MobFileRecord> rolled = roll_chunk.pending_mobs;
            const size_t n = rolled.size( );
            const int dead_idx = net_natural_mob_index( rolled[ 0 ].net_id );
            check_bool( "roll: records carry natural ids",
                        net_is_natural_mob_id( rolled[ 0 ].net_id ) && net_natural_mob_home( rolled[ 0 ].net_id ) == found_pos &&
                            ( rolled[ 0 ].flags & NET_MOB_FLAG_NATURAL ) != 0 );
            std::filesystem::remove_all( "/tmp/repgame_test_mobs_home" );
            ScopedTempHome temp_home( "/tmp/repgame_test_mobs_home" );
            MapStorage storage;
            storage.init( "TestMobs" );
            // Record 0 is the (touched) survivor; its index is tombstoned.
            MobFileRecord survivor = rolled[ 0 ];
            survivor.flags |= NET_MOB_FLAG_TOUCHED;
            storage.persist_mobs( found_pos, std::vector<MobFileRecord>( 1, survivor ), { static_cast<uint8_t>( dead_idx ) } );
            // Reload like the worker: records first, then re-roll skipping
            // tombstoned indices and ids already among the records.
            std::vector<MobFileRecord> file_recs;
            std::vector<uint8_t> file_removed;
            check_bool( "reload: file present", storage.load_mobs( found_pos, file_recs, &file_removed ) == 1 );
            std::set<uint8_t> skip( file_removed.begin( ), file_removed.end( ) );
            for ( const MobFileRecord &rec : file_recs ) {
                if ( net_is_natural_mob_id( rec.net_id ) && net_natural_mob_home( rec.net_id ) == found_pos ) {
                    skip.insert( static_cast<uint8_t>( net_natural_mob_index( rec.net_id ) ) );
                }
            }
            roll_chunk.pending_mobs.clear( );
            roll_chunk.spawn_mobs( &skip );
            int dup = 0;
            for ( const MobFileRecord &rec : roll_chunk.pending_mobs ) {
                if ( net_natural_mob_index( rec.net_id ) == dead_idx ) {
                    dup++;
                }
            }
            check_bool( "reload: roll skips tombstoned index", roll_chunk.pending_mobs.size( ) + 1 == n && dup == 0 );
            check_bool( "reload: restored + rolled == original count", file_recs.size( ) + roll_chunk.pending_mobs.size( ) == n );
            chunk_free_cells( roll_chunk.cells );
            roll_chunk.cells = nullptr;
        }
    }

    // --- 6d: pen rule: a natural mob in a player-edited chunk is touched ----
    {
        Chunk *c0 = world.chunkLoader.get_chunk( glm::ivec3( 0, 0, 0 ) );
        c0->player_edited = 1;
        MobManager mobs;
        mobs.owned_chunks.push_back( { glm::ivec3( 0, 0, 0 ), 0, {} } );
        mobs.spawn( MOB_PIG, glm::dvec3( 10.5, 11.0, 10.5 ), 0.0f, 1 );
        mobs.mobs[ 0 ].natural_id = net_natural_mob_id( glm::ivec3( 0, 0, 0 ), 2 );
        mobs.mobs[ 0 ].net_natural = true;
        mobs.mobs[ 0 ].ai_state = 0;
        mobs.mobs[ 0 ].ai_ticks = 100000;
        mobs.tick( world, player_pos );
        check_bool( "pen: edited chunk touches the natural", mobs.mobs[ 0 ].touched );
        check_bool( "pen: index tombstoned in owned chunk", mobs.owned_chunks[ 0 ].removed.count( 2 ) == 1 );

        c0->player_edited = 0;
        MobManager mobs2;
        mobs2.owned_chunks.push_back( { glm::ivec3( 0, 0, 0 ), 0, {} } );
        mobs2.spawn( MOB_PIG, glm::dvec3( 10.5, 11.0, 10.5 ), 0.0f, 1 );
        mobs2.mobs[ 0 ].natural_id = net_natural_mob_id( glm::ivec3( 0, 0, 0 ), 2 );
        mobs2.mobs[ 0 ].ai_state = 0;
        mobs2.mobs[ 0 ].ai_ticks = 100000;
        mobs2.tick( world, player_pos );
        check_bool( "pen: unedited chunk leaves it pristine", !mobs2.mobs[ 0 ].touched );
        c0->player_edited = 1;
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
        {
            ScopedTempHome temp_home( "/tmp/repgame_test_mobs_home" );
            MapStorage storage;
            storage.init( "TestMobs" );
            mobs.init_storage( &storage );
            mobs.owned_chunks.push_back( { glm::ivec3( 0, 0, 0 ), 0, {} } );
            mobs.spawn( MOB_PIG, glm::dvec3( 10.5, 11.0, 10.5 ), 0.0f, 1 );
            mobs.persist_all( );
            std::vector<MobFileRecord> out;
            check_bool( "remote: no mobs record",
                        storage.load_mobs( glm::ivec3( 0, 0, 0 ), out ) == 1 && out.size( ) == 1 && out[ 0 ].type == MOB_PIG );
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
            if ( head_i >= 0 ) {
                glm::mat4 w0[ 32 ], w1[ 32 ];
                MobManager::bone_worlds( stand_mob, pdef, 1.0f, w0 );
                MobManager::bone_worlds( sneak_mob, pdef, 1.0f, w1 );
                float rot_diff = 0.0f;
                for ( int c = 0; c < 3; c++ ) {
                    for ( int row = 0; row < 3; row++ ) {
                        rot_diff = fmaxf( rot_diff, fabsf( w1[ head_i ][ c ][ row ] - w0[ head_i ][ c ][ row ] ) );
                    }
                }
                check_near( "remote sneak: head orientation unchanged", rot_diff, 0.0f, 1e-5f );
                const glm::vec3 dt = glm::vec3( w1[ head_i ][ 3 ] - w0[ head_i ][ 3 ] );
                check_bool( "remote sneak: head drops 2.5px", fabsf( dt.x ) < 1e-4f && fabsf( dt.z ) < 1e-4f && fabsf( dt.y + 2.5f ) < 1e-4f );
            }
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

    // --- 13: every new non-remote type spawns and settles at floor+1 -------
    // (hoverers settle at floor+1+hover_h and never report standing) --------
    {
        const MobType new_types[] = { MOB_COW, MOB_CHICKEN, MOB_SHEEP, MOB_ZOMBIE, MOB_CREEPER, MOB_BEE };
        for ( MobType t : new_types ) {
            const MobTypeDef &tdef = mob_types[ t ];
            MobManager mobs;
            mobs.spawner_enabled = false;
            char name[ 64 ];
            snprintf( name, sizeof( name ), "%s: settles standing at floor+1", tdef.name );
            check_bool( name, mobs.spawn( t, glm::dvec3( 10.5, 14.0, 10.5 ), 0.0f, 1 ) );
            for ( int i = 0; i < 300 && !mobs.mobs[ 0 ].standing; i++ ) {
                mobs.tick( world, player_pos );
            }
            if ( tdef.hover_h > 0.0f ) {
                snprintf( name, sizeof( name ), "%s: hovering at floor+1+%.1f", tdef.name, tdef.hover_h );
                check_bool( name, mobs.mobs[ 0 ].standing == 0 && fabs( mobs.mobs[ 0 ].pos.y - 11.0 - tdef.hover_h ) < 0.05 );
            } else {
                snprintf( name, sizeof( name ), "%s: standing", tdef.name );
                check_bool( name, mobs.mobs[ 0 ].standing == 1 && fabs( mobs.mobs[ 0 ].pos.y - 11.0 ) < 0.01 );
            }
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
        ScopedTempHome temp_home( "/tmp/repgame_test_mobs_home" );
        MapStorage storage;
        storage.init( "TestMobs" );
        MobManager mobs;
        mobs.init_storage( &storage );
        // A fresh chunk (x 186..247) so earlier sections' files can't leak in:
        // the despawnable zombie writes nothing, the persistent pig does.
        mobs.owned_chunks.push_back( { glm::ivec3( 3, 0, 0 ), 0, {} } );
        mobs.spawn( MOB_ZOMBIE, glm::dvec3( 190.5, 11.0, 10.5 ), 0.0f, 1 );
        mobs.mobs[ 0 ].despawnable = 1;
        mobs.spawn( MOB_PIG, glm::dvec3( 190.5, 11.0, 10.5 ), 0.0f, 1 );
        mobs.persist_all( );
        std::vector<MobFileRecord> out;
        check_bool( "despawnable: only the persistent mob is written",
                    storage.load_mobs( glm::ivec3( 3, 0, 0 ), out ) == 1 && out.size( ) == 1 && out[ 0 ].type == MOB_PIG );
    }

    // --- 19: MAX_MOBS caps locally created mobs, not storage/rendering -----
    // spawn()/spawner/roll products obey the cap; avatars and network mobs
    // (mirrors, owned transfers) must always fit — dropping them would lose
    // an entity the server thinks we hold.
    {
        MobManager mobs;
        for ( int i = 0; i < MAX_MOBS; i++ ) {
            mobs.spawn( MOB_PIG, glm::dvec3( 10.5, 11.0, 10.5 ), 0.0f, 1 );
        }
        check_bool( "cap: MAX_MOBS local spawns all accepted", mobs.mobs.size( ) == MAX_MOBS );
        check_bool( "cap: local spawn refused at MAX_MOBS", !mobs.spawn( MOB_PIG, glm::dvec3( 10.5, 11.0, 10.5 ), 0.0f, 1 ) );

        NetEntityRecord rec;
        rec.id = 0x10001;
        rec.kind = NetEntityKind::MOB;
        rec.x = 10.5;
        rec.y = 11.0;
        rec.z = 10.5;
        rec.type = MOB_PIG;
        check_bool( "cap: avatar spawn_remote works at cap", mobs.spawn_remote( MOB_PLAYER ) != 0 );
        check_bool( "cap: mirror record accepted at cap", mobs.spawn_record( rec, true ) );
        rec.id = 0x10002;
        check_bool( "cap: owned transfer record accepted at cap", mobs.spawn_record( rec, false ) );
        check_bool( "cap: storage grew past MAX_MOBS", mobs.mobs.size( ) > MAX_MOBS );

        // Mirrors don't count against the cap: a full house of mirrors still
        // takes local spawns.
        MobManager mobs2;
        for ( int i = 0; i < MAX_MOBS; i++ ) {
            rec.id = 0x20000 + i;
            mobs2.spawn_record( rec, true );
        }
        check_bool( "cap: MAX_MOBS mirrors don't block local spawn", mobs2.spawn( MOB_PIG, glm::dvec3( 10.5, 11.0, 10.5 ), 0.0f, 1 ) );

        check_bool( "cap: capped=false bypasses", mobs.spawn( MOB_PIG, glm::dvec3( 10.5, 11.0, 10.5 ), 0.0f, 1, false ) );
    }

    // --- 20: every non-remote type has a working spawn egg ---------------
    {
        for ( int t = 0; t < MOB_TYPE_COUNT; t++ ) {
            const MobTypeDef &def = mob_types[ t ];
            const std::string label = std::string( "egg: " ) + def.name;
            if ( def.ai == MOB_AI_REMOTE ) {
                check_bool( ( label + " has no egg" ).c_str( ), def.spawn_egg == LAST_BLOCK_ID );
            } else {
                check_bool( ( label + " egg is a valid item" ).c_str( ), item_id_valid( def.spawn_egg ) && !item_is_block( def.spawn_egg ) );
                check_bool( ( label + " egg maps back" ).c_str( ), mob_type_for_spawn_egg( def.spawn_egg ) == t );
            }
        }
        check_bool( "egg: non-egg item maps to none", mob_type_for_spawn_egg( STICK ) == MOB_TYPE_COUNT );
        check_bool( "egg: block item maps to none", mob_type_for_spawn_egg( STONE ) == MOB_TYPE_COUNT );
        check_bool( "egg: empty hand maps to none", mob_type_for_spawn_egg( LAST_BLOCK_ID ) == MOB_TYPE_COUNT );
    }

    // --- 21: breeding: feed -> love -> mate -> baby -------------------------
    {
        // Breed-item tables: pigs eat roots, cow/sheep wheat, chicken seeds,
        // bees any flower (the is_flower flag, off the "flowers" item tag).
        check_bool( "breed: pig takes carrot", mob_def_is_breed_item( mob_types[ MOB_PIG ], CARROT ) );
        check_bool( "breed: pig takes potato + beetroot",
                    mob_def_is_breed_item( mob_types[ MOB_PIG ], POTATO ) && mob_def_is_breed_item( mob_types[ MOB_PIG ], BEETROOT ) );
        check_bool( "breed: pig refuses wheat", !mob_def_is_breed_item( mob_types[ MOB_PIG ], WHEAT ) );
        check_bool( "breed: cow + sheep take wheat",
                    mob_def_is_breed_item( mob_types[ MOB_COW ], WHEAT ) && mob_def_is_breed_item( mob_types[ MOB_SHEEP ], WHEAT ) );
        check_bool( "breed: chicken takes seeds",
                    mob_def_is_breed_item( mob_types[ MOB_CHICKEN ], WHEAT_SEEDS ) && mob_def_is_breed_item( mob_types[ MOB_CHICKEN ], BEETROOT_SEEDS ) );
        check_bool( "breed: bee takes flowers",
                    mob_def_is_breed_item( mob_types[ MOB_BEE ], DANDELION ) && mob_def_is_breed_item( mob_types[ MOB_BEE ], POPPY ) &&
                        mob_def_is_breed_item( mob_types[ MOB_BEE ], OXEYE_DAISY ) );
        check_bool( "breed: bee refuses wheat + stone",
                    !mob_def_is_breed_item( mob_types[ MOB_BEE ], WHEAT ) && !mob_def_is_breed_item( mob_types[ MOB_BEE ], STONE ) );
        check_bool( "breed: zombie has no foods", !mob_def_is_breed_item( mob_types[ MOB_ZOMBIE ], CARROT ) );

        MobManager mobs;
        mobs.spawner_enabled = false;
        mobs.spawn( MOB_PIG, glm::dvec3( 10.5, 11.0, 10.5 ), 0.0f, 1 );
        mobs.spawn( MOB_PIG, glm::dvec3( 12.5, 11.0, 10.5 ), 0.0f, 1 );
        check_bool( "feed: pig accepts carrot", mobs.feedable( 0, CARROT ) );
        check_bool( "feed: pig refuses wheat", !mobs.feedable( 0, WHEAT ) );
        check_bool( "feed: carrot feeds", mobs.feed( 0, CARROT ) );
        check_bool( "feed: enters love mode", mobs.mobs[ 0 ].love_ticks > 0 );
        check_bool( "feed: in-love pig stops eating", !mobs.feedable( 0, CARROT ) );
        mobs.feed( 1, CARROT );
        // Both in love: they approach and mate within the love window.
        int ticks_to_baby = -1;
        for ( int i = 0; i < 10 * UPS_RATE; i++ ) {
            mobs.tick( world, player_pos );
            if ( mobs.mobs.size( ) == 3 ) {
                ticks_to_baby = i;
                break;
            }
        }
        check_bool( "mate: pair produced a baby", ticks_to_baby >= 0 && mobs.mobs.size( ) == 3 );
        if ( mobs.mobs.size( ) == 3 ) {
            check_bool( "mate: baby is_baby", mobs.is_baby_at( 2 ) );
            check_bool( "mate: baby flagged for the wire", ( MobManager::mob_net_flags( mobs.mobs[ 2 ] ) & NET_MOB_FLAG_BABY ) != 0 );
            check_bool( "mate: parents on cooldown", mobs.mobs[ 0 ].breed_cooldown > 0 && mobs.mobs[ 1 ].breed_cooldown > 0 );
            check_bool( "mate: love consumed", mobs.mobs[ 0 ].love_ticks == 0 && mobs.mobs[ 1 ].love_ticks == 0 );
            check_bool( "mate: cooldown blocks feeding", !mobs.feedable( 0, CARROT ) );
            // Feeding a baby accelerates its growth (each feed ~10%).
            const int age0 = mobs.mobs[ 2 ].age_ticks;
            check_bool( "baby: feedable for growth", mobs.feedable( 2, CARROT ) && mobs.feed( 2, CARROT ) );
            check_bool( "baby: feed shortens growth", mobs.mobs[ 2 ].age_ticks > age0 && mobs.mobs[ 2 ].age_ticks < 0 );
            // Reaching 0 grows up; the BABY flag clears with it.
            mobs.mobs[ 2 ].age_ticks = -1;
            mobs.tick( world, player_pos );
            check_bool( "baby: grows to adult at 0", mobs.mobs[ 2 ].age_ticks == 0 && !mobs.is_baby_at( 2 ) );
            check_bool( "baby: grown mob drops the flag", ( MobManager::mob_net_flags( mobs.mobs[ 2 ] ) & NET_MOB_FLAG_BABY ) == 0 );
            // Babies drop nothing: the caller-side guard is is_baby_at.
            mobs.mobs[ 2 ].age_ticks = -100;
            check_bool( "baby: killable but flagged (no drops by guard)", mobs.is_baby_at( 2 ) );
        }
    }

    // --- 22: temptation: held breed food pulls the mob toward the player -----
    {
        MobManager mobs;
        mobs.spawner_enabled = false;
        mobs.spawn( MOB_PIG, glm::dvec3( 16.5, 11.0, 10.5 ), 0.0f, 1 );
        mobs.mobs[ 0 ].ai_state = 0;
        mobs.mobs[ 0 ].ai_ticks = 100000;
        const glm::dvec3 pfeet( 11.5, 11.0, 10.5 );
        const glm::dvec3 ppos = pfeet + glm::dvec3( 0.0, PLAYER_EYE_HEIGHT, 0.0 );
        MobTickInput in = { true, DAY_LENGTH_TICKS / 4, PLAYER_EYE_HEIGHT };
        in.held_item = CARROT;
        for ( int i = 0; i < 4 * UPS_RATE; i++ ) {
            mobs.tick( world, ppos, &in );
        }
        check_bool( "tempt: pig walks to the carrot holder", mobs.mobs[ 0 ].pos.x < 16.0 );
        // The hostile-mobs toggle doesn't gate tempting — a peaceful
        // player's livestock still follows food.
        mobs.mobs[ 0 ].pos.x = 16.5;
        in.player_targetable = false;
        for ( int i = 0; i < 4 * UPS_RATE; i++ ) {
            mobs.tick( world, ppos, &in );
        }
        check_bool( "tempt: peaceful still tempts", mobs.mobs[ 0 ].pos.x < 16.0 );
        // No-clip does: a spectator-style camera attracts nothing.
        mobs.mobs[ 0 ].pos.x = 16.5;
        mobs.mobs[ 0 ].ai_ticks = 100000;
        in.player_no_clip = true;
        const double x0 = mobs.mobs[ 0 ].pos.x;
        for ( int i = 0; i < 2 * UPS_RATE; i++ ) {
            mobs.tick( world, ppos, &in );
        }
        check_bool( "tempt: no-clip doesn't tempt", fabs( mobs.mobs[ 0 ].pos.x - x0 ) < 1.0 );
        // A non-food item leaves it idling where it stands.
        mobs.mobs[ 0 ].pos.x = 16.5;
        mobs.mobs[ 0 ].ai_ticks = 100000;
        in.player_no_clip = false;
        in.held_item = STONE;
        const double x1 = mobs.mobs[ 0 ].pos.x;
        for ( int i = 0; i < 2 * UPS_RATE; i++ ) {
            mobs.tick( world, ppos, &in );
        }
        check_bool( "tempt: wrong item doesn't tempt", fabs( mobs.mobs[ 0 ].pos.x - x1 ) < 1.0 );
    }

    // --- 23: trample: a landing mob reports the farmland cell ----------------
    {
        MobManager mobs;
        mobs.spawner_enabled = false;
        world.set_loaded_block( glm::ivec3( 25, 10, 10 ), { FARMLAND, 0, 0, FARMLAND, 0 } );
        mobs.spawn( MOB_PIG, glm::dvec3( 25.5, 13.5, 10.5 ), 0.0f, 1 );
        mobs.mobs[ 0 ].ai_state = 0;
        mobs.mobs[ 0 ].ai_ticks = 100000;
        MobTickOutput out;
        bool trampled = false;
        for ( int i = 0; i < 400 && !trampled; i++ ) {
            mobs.tick( world, player_pos, nullptr, &out );
            for ( const glm::ivec3 &p : out.tramples ) {
                trampled |= p == glm::ivec3( 25, 10, 10 );
            }
        }
        check_bool( "trample: landing reports the farmland cell", trampled );
        // A pig just standing/walking on farmland never reports it.
        out.tramples.clear( );
        for ( int i = 0; i < 60; i++ ) {
            mobs.tick( world, player_pos, nullptr, &out );
        }
        check_bool( "trample: standing on farmland is safe", out.tramples.empty( ) );
        world.set_loaded_block( glm::ivec3( 25, 10, 10 ), { STONE, 0, 0, STONE, 0 } );
    }

    // --- 24: grow_ticks persists the baby/cooldown state ---------------------
    {
        std::filesystem::remove_all( "/tmp/repgame_test_mobs_home" );
        ScopedTempHome temp_home( "/tmp/repgame_test_mobs_home" );
        MapStorage storage;
        storage.init( "TestMobs" );
        // Chunk 4 covers x 248..309 (CHUNK_SIZE_X stride).
        MobManager mobs;
        mobs.init_storage( &storage );
        mobs.owned_chunks.push_back( { glm::ivec3( 4, 0, 0 ), 0, {} } );
        mobs.spawn( MOB_PIG, glm::dvec3( 260.5, 11.0, 10.5 ), 0.0f, 1 );
        mobs.mobs[ 0 ].age_ticks = -12345; // a baby mid-growth
        mobs.spawn( MOB_COW, glm::dvec3( 262.5, 11.0, 10.5 ), 0.0f, 1 );
        mobs.mobs[ 1 ].breed_cooldown = 777; // a parent on cooldown
        mobs.persist_all( );
        std::vector<MobFileRecord> out;
        check_bool( "breed persist: both records written",
                    storage.load_mobs( glm::ivec3( 4, 0, 0 ), out ) == 1 && out.size( ) == 2 );
        if ( out.size( ) == 2 ) {
            check_bool( "breed persist: baby grow_ticks negative", out[ 0 ].grow_ticks == -12345 );
            check_bool( "breed persist: cooldown grow_ticks positive", out[ 1 ].grow_ticks == 777 );
        }
        // A pre-grow_ticks file (shorter record) loads with grow_ticks 0.
        {
            char *dir = getRepGamePath( );
            char map_root[ CHUNK_NAME_MAX_LENGTH ];
            snprintf( map_root, sizeof( map_root ), "%s/TestMobs", dir );
            free( dir );
            char file_name[ CHUNK_NAME_MAX_LENGTH ];
            snprintf( file_name, sizeof( file_name ), FILE_ROOT_MOBS, map_root, 0, 0, 4 );
            FILE *f = fopen( file_name, "wb" );
            const uint32_t old_size = static_cast<uint32_t>( offsetof( MobFileRecord, grow_ticks ) );
            const uint32_t one = 1;
            MobFileRecord r = { 0, 0, 0.0, 0.0, 0.0, 0.0f, 0.0f, 0u, 0u, 0 };
            r.type = MOB_PIG;
            fwrite( &old_size, sizeof( uint32_t ), 1, f );
            fwrite( &one, sizeof( uint32_t ), 1, f );
            fwrite( &r, old_size, 1, f );
            fclose( f );
            out.clear( );
            check_bool( "breed persist: old record loads with grow_ticks 0",
                        storage.load_mobs( glm::ivec3( 0, 0, 4 ), out ) == 1 && out.size( ) == 1 && out[ 0 ].grow_ticks == 0 );
        }
    }

    return failures;
}

