// Arrow projectiles: ballistic flight + drag, block sticking, stuck-arrow
// pickup/despawn/anchor-break, mob hits + kill drops, water slowdown,
// capture/chunk freeze, cap eviction, survival ammo draw vs creative fire,
// and the causer-simulated multiplayer path (spawn/state/remove/transfer).

#include "test_util.hpp"

#include "common/harness/run_policy.hpp"

int test_arrows( ) {
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
    // Arrow sim runs only in the live chunk — all positions stay inside
    // [0,32)^3 (the one chunk test_setup_world fills).

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

    // --- ballistic flight: gravity + drag -----------------------------------
    {
        ArrowEntities arrows;
        arrows.spawn( glm::dvec3( 10.5, 20.0, 10.5 ), glm::dvec3( 0.3, 0.0, 0.0 ), ArrowEntities::ARROW_DAMAGE, 1 );
        ArrowEntities::ArrowTickOutput out;
        gs.tick_number++;
        arrows.tick( gs, &out );
        const ArrowEntities::Arrow a = arrows.debug_arrows( )[ 0 ];
        check_bool( "flight: advances along +x", a.pos.x > 10.5 );
        check_bool( "flight: gravity pulls down", a.vel.y < 0.0 );
        check_bool( "flight: drag slows", a.vel.x < 0.3 );
        check_bool( "flight: no hits reported", out.mob_hits.empty( ) );
    }

    // --- stick into a wall -----------------------------------------------------
    {
        for ( int y = 15; y <= 25; y++ ) {
            for ( int z = 5; z <= 15; z++ ) {
                world.set_loaded_block( glm::ivec3( 15, y, z ), stone );
            }
        }
        ArrowEntities arrows;
        arrows.spawn( glm::dvec3( 10.5, 20.0, 10.5 ), glm::dvec3( ArrowEntities::ARROW_SPEED, 0.0, 0.0 ), ArrowEntities::ARROW_DAMAGE, 1 );
        ArrowEntities::ArrowTickOutput out;
        for ( int i = 0; i < 100 && ( arrows.count( ) == 0 || !arrows.debug_arrows( )[ 0 ].stuck ); i++ ) {
            gs.tick_number++;
            arrows.tick( gs, &out );
        }
        check_bool( "stick: parked in the wall", arrows.count( ) == 1 && arrows.debug_arrows( )[ 0 ].stuck == 1 );
        const ArrowEntities::Arrow a = arrows.debug_arrows( )[ 0 ];
        check_bool( "stick: center just off the face", a.pos.x > 14.0 && a.pos.x < 15.0 );
        check_bool( "stick: tip cell inside the wall", a.stuck_cell.x == 15 );
    }

    // --- stuck arrow falls free when the anchor block breaks -------------------
    {
        ArrowEntities arrows;
        arrows.spawn( glm::dvec3( 10.5, 20.0, 10.5 ), glm::dvec3( ArrowEntities::ARROW_SPEED, 0.0, 0.0 ), ArrowEntities::ARROW_DAMAGE, 1 );
        ArrowEntities::ArrowTickOutput out;
        for ( int i = 0; i < 100 && !arrows.debug_arrows( )[ 0 ].stuck; i++ ) {
            gs.tick_number++;
            arrows.tick( gs, &out );
        }
        check_bool( "anchor: arrow stuck", arrows.debug_arrows( )[ 0 ].stuck == 1 );
        const glm::ivec3 anchor = arrows.debug_arrows( )[ 0 ].stuck_cell;
        world.set_loaded_block( anchor, BLOCK_STATE_AIR );
        gs.tick_number++;
        arrows.tick( gs, &out );
        check_bool( "anchor: released when the block broke", arrows.debug_arrows( )[ 0 ].stuck == 0 );
        for ( int i = 0; i < 30; i++ ) {
            gs.tick_number++;
            arrows.tick( gs, &out );
        }
        check_bool( "anchor: falls after release", arrows.debug_arrows( )[ 0 ].vel.y < 0.0 );
        // Restore the wall cell for the remaining cases.
        world.set_loaded_block( anchor, stone );
    }

    // --- stuck pickup ---------------------------------------------------------
    {
        test_setup_inventories( gs );
        ArrowEntities arrows;
        arrows.spawn( glm::dvec3( 10.5, 20.0, 10.5 ), glm::dvec3( ArrowEntities::ARROW_SPEED, 0.0, 0.0 ), ArrowEntities::ARROW_DAMAGE, 1 );
        ArrowEntities::ArrowTickOutput out;
        for ( int i = 0; i < 100 && !arrows.debug_arrows( )[ 0 ].stuck; i++ ) {
            gs.tick_number++;
            arrows.tick( gs, &out );
        }
        // Walk over the shaft: feet just below the arrow center.
        const glm::dvec3 apos = arrows.debug_arrows( )[ 0 ].pos;
        gs.camera.pos = glm::dvec3( apos.x, apos.y + PLAYER_EYE_HEIGHT - 0.5, apos.z );
        gs.tick_number++;
        arrows.tick( gs, &out );
        check_bool( "pickup: stuck arrow collected", arrows.count( ) == 0 );
        check_bool( "pickup: ARROW lands on the hotbar", gs.hotbar.findSlotWithItem( ARROW, false ) >= 0 );
        gs.camera.pos = glm::dvec3( 1.5, 60.0, 1.5 ); // back out of range
    }

    // --- despawn at 5 minutes --------------------------------------------------
    {
        ArrowEntities arrows;
        arrows.spawn( glm::dvec3( 10.5, 20.0, 10.5 ), glm::dvec3( ArrowEntities::ARROW_SPEED, 0.0, 0.0 ), ArrowEntities::ARROW_DAMAGE, 1 );
        ArrowEntities::ArrowTickOutput out;
        for ( int i = 0; i < 100 && !arrows.debug_arrows( )[ 0 ].stuck; i++ ) {
            gs.tick_number++;
            arrows.tick( gs, &out );
        }
        check_bool( "despawn: arrow stuck", arrows.count( ) == 1 );
        for ( int i = 0; i < ArrowEntities::ARROW_DESPAWN_TICKS + 5; i++ ) {
            gs.tick_number++;
            arrows.tick( gs, &out );
        }
        check_bool( "despawn: gone at 5 minutes", arrows.count( ) == 0 );
    }

    // --- mob hit: damage, then kill + drops ------------------------------------
    {
        MobManager &mobs = world.mobs;
        check_bool( "mob: spawn pig", mobs.spawn( MOB_PIG, glm::dvec3( 14.5, 18.0, 10.5 ), 0.0f, 1 ) );
        ArrowEntities arrows;
        ArrowEntities::ArrowTickOutput out;
        // Two hits at 9 damage against the 10hp pig: first hurts, second kills.
        for ( int shot = 0; shot < 2; shot++ ) {
            out.mob_hits.clear( );
            arrows.spawn( glm::dvec3( 10.5, 18.4, 10.5 ), glm::dvec3( ArrowEntities::ARROW_SPEED, 0.0, 0.0 ), ArrowEntities::ARROW_DAMAGE, 1 );
            for ( int i = 0; i < 100 && out.mob_hits.empty( ); i++ ) {
                gs.tick_number++;
                arrows.tick( gs, &out );
            }
            check_bool( shot == 0 ? "mob: first arrow connects" : "mob: second arrow connects", out.mob_hits.size( ) == 1 );
            check_bool( "mob: arrow consumed by the hit", arrows.count( ) == 0 );
            if ( out.mob_hits.empty( ) ) {
                break;
            }
            const ArrowEntities::ArrowMobHit &hit = out.mob_hits[ 0 ];
            const bool killed = mobs.hurt( hit.mob_index, hit.damage, hit.pos );
            check_bool( shot == 0 ? "mob: 9 damage leaves 10hp pig" : "mob: second arrow kills", killed == ( shot == 1 ) );
            if ( killed ) {
                RepGame::spawn_mob_drops( gs, mobs.def_at( hit.mob_index ), mobs.pos_at( hit.mob_index ) );
                // Drops land as item entities at the corpse — picked up
                // later like any drop.
                bool pork = false;
                for ( const ItemEntities::ItemEntity &it : world.items.debug_items( ) ) {
                    pork = pork || it.item == PORKCHOP;
                }
                check_bool( "mob: kill spawns drops", pork );
            }
        }
    }

    // --- water slows the arrow -------------------------------------------------
    {
        world.set_loaded_block( glm::ivec3( 10, 20, 10 ), water );
        ArrowEntities arrows;
        arrows.spawn( glm::dvec3( 10.5, 20.0, 10.5 ), glm::dvec3( 0.4, 0.0, 0.0 ), ArrowEntities::ARROW_DAMAGE, 1 );
        ArrowEntities::ArrowTickOutput out;
        gs.tick_number++;
        arrows.tick( gs, &out );
        // Submerged drag (0.93) vs air (0.9986).
        check_near( "water: heavy drag applies", arrows.debug_arrows( )[ 0 ].vel.x, 0.4 * 0.93, 1e-3 );
        world.set_loaded_block( glm::ivec3( 10, 20, 10 ), BLOCK_STATE_AIR );
    }

    // --- capture mode freezes the sim ------------------------------------------
    {
        ArrowEntities arrows;
        arrows.spawn( glm::dvec3( 10.5, 20.0, 10.5 ), glm::dvec3( 0.4, 0.0, 0.0 ), ArrowEntities::ARROW_DAMAGE, 1 );
        ArrowEntities::ArrowTickOutput out;
        run_policy( ).capture.enabled = true;
        for ( int i = 0; i < 20; i++ ) {
            gs.tick_number++;
            arrows.tick( gs, &out );
        }
        run_policy( ).capture.enabled = false;
        const ArrowEntities::Arrow a = arrows.debug_arrows( )[ 0 ];
        check_bool( "capture: arrow frozen", a.pos == glm::dvec3( 10.5, 20.0, 10.5 ) && a.age_ticks == 0 );
    }

    // --- frozen in a non-live chunk ---------------------------------------------
    {
        ArrowEntities arrows;
        arrows.spawn( glm::dvec3( 100.5, 60.0, 100.5 ), glm::dvec3( 0.4, 0.0, 0.0 ), ArrowEntities::ARROW_DAMAGE, 1 );
        ArrowEntities::ArrowTickOutput out;
        for ( int i = 0; i < 20; i++ ) {
            gs.tick_number++;
            arrows.tick( gs, &out );
        }
        const ArrowEntities::Arrow a = arrows.debug_arrows( )[ 0 ];
        check_bool( "chunk: frozen outside the live chunk", a.pos == glm::dvec3( 100.5, 60.0, 100.5 ) && a.age_ticks == 0 );
    }

    // --- cap eviction -------------------------------------------------------------
    {
        ArrowEntities arrows;
        for ( size_t i = 0; i < ArrowEntities::MAX_ARROWS + 2; i++ ) {
            arrows.spawn( glm::dvec3( 10.5, 20.0, 10.5 ), glm::dvec3( 0.0, 0.01, 0.0 ), ArrowEntities::ARROW_DAMAGE, 1 );
        }
        check_bool( "cap: oldest evicted at MAX_ARROWS", arrows.count( ) == ArrowEntities::MAX_ARROWS );
    }

    // --- survival ammo draw vs creative free fire ----------------------------------
    {
        ArrowEntities &arrows = world.arrows;
        arrows.cleanup( );
        gs.camera.pos = glm::dvec3( 10.5, 20.0, 10.5 );
        gs.camera.look = glm::vec3( 1.0f, 0.0f, 0.0f );
        gs.game_mode = GameMode_Survival;

        test_setup_inventories( gs );
        gs.hotbar.addItemWithQuantity( ARROW, 3 );
        check_bool( "fire: survival fires from the hotbar", repgame_fire_arrow( gs ) && arrows.count( ) == 1 );
        const int bar_slot = gs.hotbar.findSlotWithItem( ARROW, false );
        check_bool( "fire: one arrow consumed", bar_slot >= 0 && gs.hotbar.getSlotContents( bar_slot ).quantity == 2 );

        // Empty hotbar: falls back to the survival inventory.
        test_setup_inventories( gs );
        check_bool( "fire: no ammo -> no shot", !repgame_fire_arrow( gs ) && arrows.count( ) == 1 );
        gs.survival_inventory.addItem( ARROW, 1 );
        check_bool( "fire: draws from the inventory", repgame_fire_arrow( gs ) && arrows.count( ) == 2 );
        int arrows_left = 0;
        const InventorySlot *inv_slots = gs.survival_inventory.getSlots( );
        for ( int i = 0; i < SURVIVAL_INVENTORY_WIDTH * SURVIVAL_INVENTORY_HEIGHT; i++ ) {
            if ( inv_slots[ i ].item_id == ARROW ) {
                arrows_left += inv_slots[ i ].quantity;
            }
        }
        check_bool( "fire: inventory stack emptied", arrows_left == 0 );

        // Creative fires without ammo or consumption.
        gs.game_mode = GameMode_Creative;
        check_bool( "fire: creative shoots free", repgame_fire_arrow( gs ) && arrows.count( ) == 3 );
        arrows.cleanup( );
        gs.game_mode = GameMode_Survival;
    }

    // --- multiplayer: mirror spawn/state/remove, transfer, disconnect -------------
    {
        Multiplayer mp;
        NetEntities net( &mp );
        ArrowEntities arrows;
        arrows.set_multiplayer_mode( true, &net );

        // Mirror arrow: eases toward streamed poses; no local physics.
        NetEntityRecord rec{};
        rec.id = ( static_cast<NetEntityId>( 8 ) << 32 ) | 1;
        rec.kind = NetEntityKind::ARROW;
        rec.x = 10.5;
        rec.y = 25.0;
        rec.z = 10.5;
        rec.vx = 0.4f;
        rec.health = 9.0f;
        rec.flags = NET_ARROW_FLAG_ATTACKER_SURVIVAL;
        arrows.net_apply_spawn( rec, /*mirror*/ true );
        check_bool( "net mirror: spawned", arrows.debug_arrows( ).size( ) == 1 );
        check_bool( "net mirror: flagged mirror", arrows.debug_arrows( )[ 0 ].net.mirror );
        for ( int i = 0; i < 50; i++ ) {
            gs.tick_number++;
            arrows.tick( gs, nullptr );
        }
        check_bool( "net mirror: no physics (floats)", fabs( arrows.debug_arrows( )[ 0 ].pos.y - 25.0 ) < 0.05 );

        // A state stream re-aims the pose and carries the stuck flag.
        NetEntityStateEntry e{};
        e.id = rec.id;
        e.kind = static_cast<uint8_t>( NetEntityKind::ARROW );
        e.x = 12.0;
        e.y = 24.0;
        e.z = 10.5;
        e.vx = 0.0f;
        e.vy = -0.1f;
        e.flags = NET_ARROW_FLAG_STUCK;
        arrows.net_apply_state( e );
        for ( int i = 0; i < 60; i++ ) {
            gs.tick_number++;
            arrows.tick( gs, nullptr );
        }
        check_bool( "net mirror: eased toward stream", fabs( arrows.debug_arrows( )[ 0 ].pos.x - 12.0 ) < 0.1 );
        check_bool( "net mirror: stuck flag applied", arrows.debug_arrows( )[ 0 ].stuck == 1 );
        arrows.net_apply_remove( rec.id );
        check_bool( "net mirror: ENTITY_REMOVE removes", arrows.count( ) == 0 );

        // Owned arrow streams ENTITY_STATE (no chunk role needed —
        // causer-simulated kinds ignore roles).
        NetEntityRecord own{};
        own.id = ( static_cast<NetEntityId>( 8 ) << 32 ) | 2;
        own.kind = NetEntityKind::ARROW;
        own.x = 10.5;
        own.y = 25.0;
        own.z = 10.5;
        own.health = 9.0f;
        arrows.net_apply_spawn( own, /*mirror*/ false );
        std::vector<NetEntityStateEntry> sent;
        arrows.collect_net_state( sent, 0 );
        check_bool( "net owned: emits state entry", sent.size( ) == 1 && sent[ 0 ].id == own.id );
        arrows.net_apply_remove( own.id );

        // ENTITY_TRANSFER adopt=1: becomes locally simulated.
        NetEntityRecord rec3{};
        rec3.id = ( static_cast<NetEntityId>( 8 ) << 32 ) | 3;
        rec3.kind = NetEntityKind::ARROW;
        rec3.x = 10.5;
        rec3.y = 25.0;
        rec3.z = 10.5;
        rec3.health = 9.0f;
        arrows.net_apply_transfer( rec3 );
        check_bool( "net transfer: spawned owned", arrows.debug_arrows( ).size( ) == 1 && !arrows.debug_arrows( )[ 0 ].net.mirror );

        // Adopted stuck arrow: the anchor cell derives from pos + entry
        // direction (vel carries it while stuck), so handover keeps the
        // arrow parked on the wall instead of releasing to air.
        NetEntityRecord rec4{};
        rec4.id = ( static_cast<NetEntityId>( 8 ) << 32 ) | 4;
        rec4.kind = NetEntityKind::ARROW;
        rec4.x = 12.5;
        rec4.y = 20.0;
        rec4.z = 10.5;
        rec4.vx = 0.4f;
        arrows.net_apply_spawn( rec4, /*mirror*/ true );
        NetEntityStateEntry es{};
        es.id = rec4.id;
        es.kind = static_cast<uint8_t>( NetEntityKind::ARROW );
        es.x = 14.9;
        es.y = 20.0;
        es.z = 10.5;
        es.vx = 0.4f; // stuck arrows stream the entry direction as vel
        es.flags = NET_ARROW_FLAG_STUCK;
        arrows.net_apply_state( es );
        arrows.net_apply_transfer( rec4 );
        const auto by_id = [ &arrows ]( const NetEntityId id ) {
            for ( const ArrowEntities::Arrow &aa : arrows.debug_arrows( ) ) {
                if ( aa.net.id == id ) {
                    return aa;
                }
            }
            return ArrowEntities::Arrow{};
        };
        check_bool( "net adopt: owned after transfer", !by_id( rec4.id ).net.mirror );
        check_bool( "net adopt: anchor cell derived", by_id( rec4.id ).stuck_cell == glm::ivec3( 15, 20, 10 ) );
        for ( int i = 0; i < 10; i++ ) {
            gs.tick_number++;
            arrows.tick( gs, nullptr );
        }
        check_bool( "net adopt: stays parked on the wall", by_id( rec4.id ).stuck == 1 );
        arrows.net_apply_remove( rec4.id );

        // Disconnect: mirrors drop, owned arrows keep flying locally.
        arrows.net_apply_spawn( rec, /*mirror*/ true );
        check_bool( "net disconnect: mirror + owned live", arrows.count( ) == 2 );
        arrows.net_disconnect( );
        check_bool( "net disconnect: mirrors dropped, owned stay", arrows.count( ) == 1 && !arrows.debug_arrows( )[ 0 ].net.mirror );
    }

    // Tidy the wall used across the cases.
    for ( int y = 15; y <= 25; y++ ) {
        for ( int z = 5; z <= 15; z++ ) {
            world.set_loaded_block( glm::ivec3( 15, y, z ), BLOCK_STATE_AIR );
        }
    }
    return failures;
}
