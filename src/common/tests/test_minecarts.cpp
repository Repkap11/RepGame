// Minecart tests: rail following (flat, slopes, curves), powered/unpowered
// rails, friction, derailment, blocked track, cart-cart and player
// collisions, damage/drop rules, persistence and the MP/capture gates.

#include "test_util.hpp"

#include "common/harness/run_policy.hpp"
#include "common/rail.hpp"
#include "common/utils/ray_traversal.hpp"

int test_minecarts( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    RepGameState &gs = *new RepGameState( ); // leaked on purpose — see test_redstone
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 0.0 );
    test_setup_world( gs.world );
    World &world = gs.world;
    // All positions stay inside [0,62)^3 — the one live chunk.

    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };

    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    const BlockState rail = { RAIL, 0, 0, RAIL, 0 };
    const BlockState powered_rail = { POWERED_RAIL, 0, 0, POWERED_RAIL, 0 };
    // Floor at y=10 across the test area; rails sit at y=11.
    for ( int x = 0; x <= 61; x++ ) {
        for ( int z = 0; z <= 61; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 10, z ), stone );
        }
    }
    auto place = [ & ]( const glm::ivec3 &pos, const BlockState &block_state ) {
        gs.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( gs.tick_number, pos, block_state, false );
    };
    auto run = [ & ]( const int ticks ) {
        for ( int i = 0; i < ticks; i++ ) {
            gs.tick_number++;
            repgame_drain_block_updates( gs );
        }
    };
    Vehicles::VehicleTickInput in;
    in.player_feet = glm::dvec3( 0.0, -100.0, 0.0 ); // far away: no pushing
    auto tick = [ & ]( const int n ) {
        for ( int i = 0; i < n; i++ ) {
            gs.world.vehicles.tick( gs, in );
        }
    };
    Vehicles &vehicles = world.vehicles;
    auto only_cart = [ & ]( ) -> Vehicles::Vehicle & { return vehicles.test_vehicles( ).at( 0 ); };

    // --- flat straight: empty cart slows and (nearly) stops --------------------
    for ( int x = 4; x <= 20; x++ ) {
        place( glm::ivec3( x, 11, 4 ), rail );
    }
    run( 10 );
    uint32_t id = vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 6.5, 11.0 + 1.0 / 16.0, 4.5 ), 0.0f );
    vehicles.test_vehicles( )[ 0 ].vel = glm::dvec3( 0.04, 0.0, 0.0 );
    tick( 10 );
    check_bool( "cart: rides the flat rail", only_cart( ).on_rail && only_cart( ).pos.x > 6.5 );
    const double flat_speed = glm::length( only_cart( ).vel );
    tick( 3000 );
    check_bool( "cart: empty cart slows", glm::length( only_cart( ).vel ) < flat_speed * 0.05 );
    check_bool( "cart: empty cart stays on track", only_cart( ).on_rail && only_cart( ).pos.x <= 20.5 );
    vehicles.test_vehicles( ).clear( );

    // --- rider flag uses lower friction ---------------------------------------
    // Two vehicles at the same speed on parallel tracks; the flagged one
    // coasts farther.
    for ( int x = 4; x <= 20; x++ ) {
        place( glm::ivec3( x, 11, 6 ), rail );
    }
    run( 10 );
    const uint32_t rider_id = vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 6.5, 11.0 + 1.0 / 16.0, 6.5 ), 0.0f );
    vehicles.test_vehicles( )[ 0 ].vel = glm::dvec3( 0.02, 0.0, 0.0 );
    vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 6.5, 11.0 + 1.0 / 16.0, 4.5 ), 0.0f );
    vehicles.test_vehicles( )[ 1 ].vel = glm::dvec3( 0.02, 0.0, 0.0 );
    in.rider_vehicle = rider_id;
    tick( 400 );
    in.rider_vehicle = 0;
    check_bool( "cart: rider coasts farther than empty",
                vehicles.test_vehicles( )[ 0 ].pos.x > vehicles.test_vehicles( )[ 1 ].pos.x + 0.5 );
    vehicles.test_vehicles( ).clear( );

    // --- powered rail boosts toward max speed ---------------------------
    // Two redstone sources: chain power reaches 8 hops from each directly
    // powered rail, so the whole x=4..20 run stays lit.
    for ( int x = 4; x <= 20; x++ ) {
        place( glm::ivec3( x, 11, 9 ), powered_rail );
    }
    place( glm::ivec3( 4, 11, 10 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );
    place( glm::ivec3( 13, 11, 10 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );
    run( 20 );
    vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 6.5, 11.0 + 1.0 / 16.0, 9.5 ), 0.0f );
    vehicles.test_vehicles( )[ 0 ].vel = glm::dvec3( 0.005, 0.0, 0.0 );
    tick( 80 );
    {
        const double bs = glm::length( only_cart( ).vel ) * UPS_RATE;
        check_bool( "cart: powered rail boosts toward max", bs > 8.0 && bs <= 12.0 + 1e-9 );
        check_bool( "cart: boosted cart stays on rail", only_cart( ).on_rail );
    }
    vehicles.test_vehicles( ).clear( );

    // --- unpowered rail brakes to a stop --------------------------------
    for ( int x = 24; x <= 40; x++ ) {
        place( glm::ivec3( x, 11, 9 ), powered_rail );
    }
    run( 20 );
    vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 26.5, 11.0 + 1.0 / 16.0, 9.5 ), 0.0f );
    vehicles.test_vehicles( )[ 0 ].vel = glm::dvec3( 0.04, 0.0, 0.0 );
    tick( 200 );
    check_bool( "cart: unpowered rail brakes to stop", glm::length( only_cart( ).vel ) == 0.0 );
    check_bool( "cart: braked cart stays on rail", only_cart( ).on_rail && only_cart( ).pos.x <= 40.5 );
    vehicles.test_vehicles( ).clear( );

    // --- powered rail launch kick off a blocked end ---------------------------
    // A rest cart on a powered rail whose +x end is walled gets kicked away.
    for ( int x = 24; x <= 27; x++ ) {
        place( glm::ivec3( x, 11, 12 ), powered_rail );
    }
    place( glm::ivec3( 24, 11, 13 ), { REDSTONE_BLOCK, 0, 0, REDSTONE_BLOCK, 0 } );
    place( glm::ivec3( 28, 11, 12 ), stone );
    run( 20 );
    vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 27.4, 11.0 + 1.0 / 16.0, 12.5 ), 0.0f );
    tick( 60 );
    check_bool( "cart: launch kick away from blocked end", only_cart( ).vel.x < -0.001 || only_cart( ).pos.x < 27.0 );
    vehicles.test_vehicles( ).clear( );

    // --- curve turns the travel direction 90 degrees ---------------------------
    // EW run into an SW corner at (14,16), then the track continues south.
    for ( int x = 10; x <= 14; x++ ) {
        place( glm::ivec3( x, 11, 16 ), rail );
    }
    for ( int z = 17; z <= 20; z++ ) {
        place( glm::ivec3( 14, 11, z ), rail );
    }
    run( 10 );
    check_bool( "cart: corner resolved as a curve", rail_shape_of( world.get_loaded_block( glm::ivec3( 14, 11, 16 ) ) ) == RAIL_SW );
    vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 10.5, 11.0 + 1.0 / 16.0, 16.5 ), 0.0f );
    vehicles.test_vehicles( )[ 0 ].vel = glm::dvec3( 0.05, 0.0, 0.0 );
    tick( 300 );
    check_bool( "cart: curve turns travel 90 degrees",
                only_cart( ).pos.z > 16.5 && fabs( only_cart( ).vel.z ) > fabs( only_cart( ).vel.x ) );
    vehicles.test_vehicles( ).clear( );

    // --- ascending onto a raised rail -------------------------------------------
    // Flat rail at (15,20), a step at (17,20), rail on top at y=12 — the
    // rail at (16,20) resolves to ASC_E and carries the cart up.
    for ( int x = 17; x <= 22; x++ ) {
        world.set_loaded_block( glm::ivec3( x, 11, 20 ), stone );
    }
    for ( int x = 14; x <= 16; x++ ) {
        place( glm::ivec3( x, 11, 20 ), rail );
    }
    for ( int x = 17; x <= 22; x++ ) {
        place( glm::ivec3( x, 12, 20 ), rail );
    }
    run( 10 );
    check_bool( "cart: rail below step ascends E", rail_shape_of( world.get_loaded_block( glm::ivec3( 16, 11, 20 ) ) ) == RAIL_ASC_E );
    // Spawn on the flat cell (15,20): (14,20) is a curve — the corner run
    // above left rails z=17..20 there, so a cart spawned on it would sweep
    // around the bend and reach the slope facing backwards.
    vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 15.5, 11.0 + 1.0 / 16.0, 20.5 ), 0.0f );
    vehicles.test_vehicles( )[ 0 ].vel = glm::dvec3( 0.05, 0.0, 0.0 );
    {
        float max_pitch = 0.0f;
        for ( int i = 0; i < 80; i++ ) {
            tick( 1 );
            if ( only_cart( ).pos.x > 16.0 && only_cart( ).pos.x < 17.0 && only_cart( ).pitch > max_pitch ) {
                max_pitch = only_cart( ).pitch;
            }
        }
        check_bool( "cart: climbs onto the raised rail",
                    only_cart( ).on_rail && only_cart( ).pos.x > 16.5 && only_cart( ).pos.y > 11.5 );
        check_bool( "cart: nose up while ascending", max_pitch > 10.0f );
    }
    // Back down the same slope: yaw continuity keeps the cart facing +x
    // (uphill) while it rolls down backwards, so pitch stays positive —
    // the body lies parallel to the rail in either facing.
    vehicles.test_vehicles( )[ 0 ].vel = glm::dvec3( -0.1, 0.0, 0.0 );
    {
        float min_pitch = 90.0f;
        for ( int i = 0; i < 120; i++ ) {
            tick( 1 );
            if ( only_cart( ).pos.x > 16.0 && only_cart( ).pos.x < 17.0 && only_cart( ).pitch < min_pitch ) {
                min_pitch = only_cart( ).pitch;
            }
        }
        check_bool( "cart: backwards descent hugs the slope", min_pitch > 10.0f );
    }
    vehicles.test_vehicles( ).clear( );
    // A parked cart still lies flat against the rail: facing uphill (+x)
    // its pitch is positive, facing downhill (-x) negative — the sign
    // follows the cart's facing, not a travel direction it doesn't have.
    vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 16.5, 11.5, 20.5 ), 90.0f );
    tick( 3 );
    check_bool( "cart: parked on slope facing uphill", only_cart( ).pitch > 10.0f );
    vehicles.test_vehicles( ).clear( );
    vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 16.5, 11.5, 20.5 ), -90.0f );
    tick( 3 );
    check_bool( "cart: parked on slope facing downhill", only_cart( ).pitch < -10.0f );
    vehicles.test_vehicles( ).clear( );

    // --- end of track: derail and fall to the ground -----------------------------
    for ( int x = 20; x <= 24; x++ ) {
        place( glm::ivec3( x, 11, 24 ), rail );
    }
    run( 10 );
    vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 21.5, 11.0 + 1.0 / 16.0, 24.5 ), 0.0f );
    vehicles.test_vehicles( )[ 0 ].vel = glm::dvec3( 0.03, 0.0, 0.0 );
    tick( 600 );
    check_bool( "cart: derails at track end", !only_cart( ).on_rail && only_cart( ).pos.x > 24.0 );
    check_bool( "cart: falls and lands on the floor", only_cart( ).pos.y <= 11.001 && only_cart( ).pos.y >= 10.9 );
    vehicles.test_vehicles( ).clear( );

    // --- blocked next cell stops the cart ---------------------------------------
    for ( int x = 30; x <= 34; x++ ) {
        place( glm::ivec3( x, 11, 27 ), rail );
    }
    place( glm::ivec3( 35, 11, 27 ), stone );
    run( 10 );
    vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 31.5, 11.0 + 1.0 / 16.0, 27.5 ), 0.0f );
    vehicles.test_vehicles( )[ 0 ].vel = glm::dvec3( 0.03, 0.0, 0.0 );
    tick( 300 );
    check_bool( "cart: blocked next cell stops at the end",
                only_cart( ).pos.x <= 35.0 && glm::length( only_cart( ).vel ) < 1e-9 );
    check_bool( "cart: blocked cart stays on rail", only_cart( ).on_rail );
    vehicles.test_vehicles( ).clear( );

    // --- cart-cart collision exchanges speed --------------------------------------
    for ( int x = 40; x <= 50; x++ ) {
        place( glm::ivec3( x, 11, 30 ), rail );
    }
    run( 10 );
    vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 42.5, 11.0 + 1.0 / 16.0, 30.5 ), 0.0f );
    vehicles.test_vehicles( )[ 0 ].vel = glm::dvec3( 0.03, 0.0, 0.0 );
    vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 43.2, 11.0 + 1.0 / 16.0, 30.5 ), 0.0f );
    tick( 30 );
    {
        const double va = vehicles.test_vehicles( )[ 0 ].vel.x;
        const double vb = vehicles.test_vehicles( )[ 1 ].vel.x;
        check_bool( "cart: collision passes speed to the parked cart", vb > 0.001 );
        check_bool( "cart: collision slows the moving cart", va < vb );
    }
    vehicles.test_vehicles( ).clear( );

    // --- player overlap pushes an empty cart ---------------------------------------
    vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 45.5, 11.0 + 1.0 / 16.0, 30.5 ), 0.0f );
    in.player_feet = glm::dvec3( 44.9, 11.0, 30.5 ); // inside the cart's box edge
    tick( 60 );
    in.player_feet = glm::dvec3( 0.0, -100.0, 0.0 );
    check_bool( "cart: player overlap pushes empty cart", only_cart( ).pos.x > 45.5 );
    vehicles.test_vehicles( ).clear( );

    // --- player overlap pushes a grounded (off-rail) cart --------------------------
    // Same box rule in world space — no rail under the cart.
    vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 45.5, 11.0, 34.5 ), 0.0f );
    in.player_feet = glm::dvec3( 44.9, 11.0, 34.5 );
    tick( 60 );
    in.player_feet = glm::dvec3( 0.0, -100.0, 0.0 );
    check_bool( "cart: player overlap pushes grounded cart",
                !only_cart( ).on_rail && only_cart( ).pos.x > 45.5 );
    vehicles.test_vehicles( ).clear( );

    // --- pushed back onto the track --------------------------------------------------
    // A grounded cart one cell east of a rail gets shoved west until its
    // bottom-centre enters the rail cell and re-snaps. The feet keep
    // trailing the cart — a player walking into it.
    place( glm::ivec3( 55, 11, 34 ), rail );
    run( 10 );
    vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 56.5, 11.0, 34.5 ), 0.0f );
    for ( int i = 0; i < 400 && !only_cart( ).on_rail; i++ ) {
        in.player_feet = only_cart( ).pos + glm::dvec3( 0.6, 0.0, 0.0 );
        tick( 1 );
    }
    in.player_feet = glm::dvec3( 0.0, -100.0, 0.0 );
    check_bool( "cart: pushed back onto a rail",
                only_cart( ).on_rail && only_cart( ).pos.x > 55.0 && only_cart( ).pos.x < 56.0 &&
                    fabs( only_cart( ).pos.y - ( 11.0 + 1.0 / 16.0 ) ) < 1e-6 );
    vehicles.test_vehicles( ).clear( );

    // --- damage / drop rules ---------------------------------------------------------
    vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 46.5, 11.0 + 1.0 / 16.0, 30.5 ), 0.0f );
    {
        bool drop = false;
        check_bool( "cart: survival hit 1 survives", !vehicles.hurt( 0, true, &drop ) && !drop );
        check_bool( "cart: survival hit 2 survives", !vehicles.hurt( 0, true, &drop ) && !drop );
        check_bool( "cart: survival hit 3 removes + drops", vehicles.hurt( 0, true, &drop ) && drop );
        check_bool( "cart: removed after 3 hits", vehicles.count( ) == 0 );
    }
    vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 46.5, 11.0 + 1.0 / 16.0, 30.5 ), 0.0f );
    {
        bool drop = true;
        check_bool( "cart: creative hit removes without drop", vehicles.hurt( 0, false, &drop ) && !drop );
    }
    // Damage decays: hits spread far apart never accumulate to a break.
    vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 46.5, 11.0 + 1.0 / 16.0, 30.5 ), 0.0f );
    {
        bool drop = false;
        check_bool( "cart: slow hit 1", !vehicles.hurt( 0, true, &drop ) );
        tick( 400 ); // ~2.8s: the accumulated damage fully decays
        check_bool( "cart: damage decayed", only_cart( ).damage < 0.01f );
        check_bool( "cart: slow hit 2", !vehicles.hurt( 0, true, &drop ) );
        tick( 400 );
        check_bool( "cart: slow hits never break", !vehicles.hurt( 0, true, &drop ) && !drop && vehicles.count( ) == 1 );
    }
    vehicles.test_vehicles( ).clear( );

    // --- persistence: adopt restores a record, persist writes it back -----------------
    {
        ScopedTempHome temp_home( "/tmp/repgame_test_carts_home" );
        MapStorage storage;
        storage.init( "TestCarts" );
        Vehicles store_carts;
        store_carts.init_storage( &storage );
        VehicleFileRecord rec = { 5.5, 11.2, 30.5, 0.1f, -0.2f, 0.3f, 45.0f, VEHICLE_MINECART };
        Chunk chunk;
        chunk.chunk_pos = glm::ivec3( 0, 0, 0 );
        chunk.sidecars.vehicles.push_back( rec );
        chunk.sidecars.vehicles_file_present = 1;
        store_carts.adopt_chunk( chunk );
        check_bool( "cart persist: adopt spawns record", store_carts.count( ) == 1 && chunk.sidecars.vehicles.empty( ) );
        if ( store_carts.count( ) == 1 ) {
            const Vehicles::Vehicle c = store_carts.debug_vehicles( )[ 0 ];
            check_bool( "cart persist: adopt restores pos/vel/yaw",
                        fabs( c.pos.x - 5.5 ) < 1e-9 && fabs( c.pos.y - 11.2 ) < 1e-9 && fabs( c.vel.y + 0.2 ) < 1e-6 && fabs( c.yaw - 45.0f ) < 1e-6 );
        }
        store_carts.persist_all( );
        std::vector<VehicleFileRecord> out;
        check_bool( "cart persist: persist_all writes record",
                    storage.load_vehicles( glm::ivec3( 0, 0, 0 ), out ) == 1 && out.size( ) == 1 &&
                        fabs( out[ 0 ].x - 5.5 ) < 1e-9 && fabs( out[ 0 ].yaw - 45.0f ) < 1e-6 );
        // Invalid records (non-finite pos, out-of-range type) are skipped
        // by the loader.
        VehicleFileRecord bad = rec;
        bad.x = std::numeric_limits<double>::quiet_NaN( );
        VehicleFileRecord bad_type = rec;
        bad_type.type = 0xFFFFFFFFu;
        storage.persist_vehicles( glm::ivec3( 0, 0, 0 ), { bad, bad_type, rec } );
        check_bool( "cart persist: non-finite record skipped", storage.load_vehicles( glm::ivec3( 0, 0, 0 ), out ) == 1 && out.size( ) == 1 );
        check_bool( "cart persist: out-of-range type skipped", out.size( ) == 1 && out[ 0 ].type == VEHICLE_MINECART );

        // A pre-`type` carts file (40-byte record_size) loads as
        // VEHICLE_MINECART: the loader zero-fills the tail and copies only
        // the shared prefix.
        {
            const uint32_t old_record_size = 40;
            char raw[ 40 ];
            memset( raw, 0, sizeof( raw ) );
            memcpy( raw, &rec, sizeof( raw ) ); // old record = prefix without `type`
            char file_name[ CHUNK_NAME_MAX_LENGTH ];
            snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_CARTS, storage.get_map_name( ), 1, 0, 0 );
            FILE *w = fopen( file_name, "wb" );
            if ( w != nullptr ) {
                fwrite( &old_record_size, sizeof( uint32_t ), 1, w );
                const uint32_t one = 1;
                fwrite( &one, sizeof( uint32_t ), 1, w );
                fwrite( raw, sizeof( raw ), 1, w );
                fclose( w );
            }
            check_bool( "cart persist: legacy 40-byte record loads as minecart",
                        storage.load_vehicles( glm::ivec3( 1, 0, 0 ), out ) == 1 && out.size( ) == 1 && out[ 0 ].type == VEHICLE_MINECART &&
                            fabs( out[ 0 ].x - 5.5 ) < 1e-9 && fabs( out[ 0 ].yaw - 45.0f ) < 1e-6 );
        }

        // New-size round trip preserves the type field.
        VehicleFileRecord typed = rec;
        typed.type = VEHICLE_TYPE_COUNT - 1; // last valid type
        storage.persist_vehicles( glm::ivec3( 0, 0, 0 ), { typed } );
        check_bool( "cart persist: type round-trips",
                    storage.load_vehicles( glm::ivec3( 0, 0, 0 ), out ) == 1 && out.size( ) == 1 && out[ 0 ].type == typed.type );

        // MP mode: spawn refuses and nothing persists.
        Vehicles mp_carts;
        mp_carts.init_storage( &storage );
        mp_carts.set_multiplayer_mode( true, nullptr );
        // MP with no live sync hub (never connected / dropped) still refuses.
        check_bool( "cart: mp unconnected refuses spawn", mp_carts.spawn( VEHICLE_MINECART, glm::dvec3( 5.5, 11.0, 30.5 ), 0.0f ) == 0 && mp_carts.count( ) == 0 );
        // In MP the adopt path drops the sidecar records instead of spawning.
        Chunk mp_chunk;
        mp_chunk.chunk_pos = glm::ivec3( 0, 0, 0 );
        mp_chunk.sidecars.vehicles.push_back( rec );
        mp_carts.adopt_chunk( mp_chunk );
        check_bool( "cart: mp adopt drops records", mp_carts.count( ) == 0 && mp_chunk.sidecars.vehicles.empty( ) );
    }

    // --- riding -------------------------------------------------------------------------
    // Mount sets riding_vehicle, the follow seats the camera on the cart, and
    // the rider flag flips the cart to ridden friction on the next tick.
    const uint32_t ride_id = vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 45.5, 11.0 + 1.0 / 16.0, 30.5 ), 0.0f );
    check_bool( "ride: mount sets riding", repgame_vehicle_mount( gs, ride_id ) && gs.camera.riding_vehicle == ride_id );
    in.rider_vehicle = gs.camera.riding_vehicle;
    tick( 5 );
    check_bool( "ride: rider flag set on tick", only_cart( ).rider );
    repgame_vehicle_ride_follow( gs );
    check_bool( "ride: follow seats the camera",
                fabs( gs.camera.pos.x - only_cart( ).pos.x ) < 1e-9 && fabs( gs.camera.pos.z - only_cart( ).pos.z ) < 1e-9 &&
                    fabs( gs.camera.pos.y - ( only_cart( ).pos.y + 1.3 ) ) < 1e-9 );
    // Rider push accelerates a rest cart along the rail (rider push caps
    // at 4 b/s — slower than the powered boost but enough to move).
    in.rider_push = glm::dvec2( 1.0, 0.0 );
    tick( 200 );
    in.rider_push = glm::dvec2( 0.0 );
    check_bool( "ride: rider push accelerates from rest", glm::length( only_cart( ).vel ) > 0.005 );
    tick( 600 );
    check_bool( "ride: fall_dist stays 0 while riding", gs.vitals.fall_dist == 0.0 );
    // Dismount: one perpendicular side walled -> the other side wins.
    // Park the cart back on the straight first — it coasted during the
    // push test.
    vehicles.test_vehicles( )[ 0 ].pos = glm::dvec3( 45.5, 11.0 + 1.0 / 16.0, 30.5 );
    vehicles.test_vehicles( )[ 0 ].yaw = -90.0f;
    vehicles.test_vehicles( )[ 0 ].vel = glm::dvec3( 0.0 );
    world.set_loaded_block( glm::ivec3( 45, 11, 29 ), stone );
    world.set_loaded_block( glm::ivec3( 45, 12, 29 ), stone );
    repgame_vehicle_dismount( gs );
    check_bool( "ride: dismount picks the free side",
                gs.camera.riding_vehicle == 0 && fabs( gs.camera.pos.z - 31.5 ) < 1e-6 && fabs( gs.camera.pos.y - ( 11.0 + PLAYER_EYE_HEIGHT ) ) < 1e-6 );
    // Dismount while the sneak pose is active uses the sneak heights, not
    // the standing constants.
    check_bool( "ride: remount for sneak dismount", repgame_vehicle_mount( gs, ride_id ) );
    gs.camera.player_height = PLAYER_SNEAK_HEIGHT;
    gs.camera.eye_height = PLAYER_SNEAK_EYE_HEIGHT;
    repgame_vehicle_dismount( gs );
    check_bool( "ride: sneak dismount uses sneak eye height",
                gs.camera.riding_vehicle == 0 && fabs( gs.camera.pos.y - ( 11.0 + PLAYER_SNEAK_EYE_HEIGHT ) ) < 1e-6 );
    gs.camera.player_height = PLAYER_HEIGHT;
    gs.camera.eye_height = PLAYER_EYE_HEIGHT;
    // Both perpendicular sides walled -> falls back along the track.
    check_bool( "ride: remount works", repgame_vehicle_mount( gs, ride_id ) );
    world.set_loaded_block( glm::ivec3( 45, 11, 31 ), stone );
    world.set_loaded_block( glm::ivec3( 45, 12, 31 ), stone );
    repgame_vehicle_dismount( gs );
    check_bool( "ride: walled sides fall back along the rail",
                gs.camera.riding_vehicle == 0 && fabs( gs.camera.pos.z - 30.5 ) < 1e-6 && fabs( gs.camera.pos.x - 44.5 ) < 1e-6 );
    // Breaking the ridden cart drops the riding state on the next follow.
    check_bool( "ride: remount for break test", repgame_vehicle_mount( gs, ride_id ) );
    {
        bool drop = false;
        vehicles.hurt( 0, false, &drop );
        repgame_vehicle_ride_follow( gs );
        check_bool( "ride: broken cart clears riding", gs.camera.riding_vehicle == 0 && vehicles.count( ) == 0 );
    }
    // The ridden cart is excluded from the targeting resolve.
    const uint32_t ride_id2 = vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 45.5, 11.0 + 1.0 / 16.0, 30.5 ), 0.0f );
    check_bool( "ride: mount for target test", repgame_vehicle_mount( gs, ride_id2 ) );
    gs.block_selection.ray_origin = gs.camera.pos;
    gs.block_selection.ray_dir = glm::dvec3( 0.0, -1.0, 0.0 );
    gs.block_selection.selectionInBounds = 0;
    gs.block_selection.block_hit_dist = REACH_DISTANCE;
    RepGame::resolve_target_mob( gs, gs.camera.pos );
    check_bool( "ride: ridden cart not targetable", gs.block_selection.target_vehicle < 0 );
    repgame_vehicle_dismount( gs );
    vehicles.test_vehicles( ).clear( );

    // --- targeting: a cart wins over the rail it sits on --------------------
    // Regression: the block-vs-entity compare used the selected cell's whole
    // AABB, and the rail's cell contains the cart — so the rail always won
    // and right-click placed a second cart instead of mounting. The compare
    // now uses the pick-shape hit distance (block_hit_dist).
    vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 45.5, 11.0 + 1.0 / 16.0, 30.5 ), 0.0f );
    {
        // Straight down onto the cart: the pick ray passes through the cart
        // and hits the rail plate at the bottom of its cell — hit_t is
        // behind the cart's own hitbox entry, so the cart must win.
        const glm::dvec3 origin( 45.5, 13.0, 30.5 );
        const glm::dvec3 dir( 0.0, -1.0, 0.0 );
        glm::ivec3 hit_cell;
        int face = -1;
        double hit_t = 0.0;
        check_bool( "target: ray through cart reaches its rail",
                    RayTraversal::find_block_from_to( world, origin, origin + dir * static_cast<double>( REACH_DISTANCE ), hit_cell, &face, 0, &hit_t ) &&
                        hit_cell == glm::ivec3( 45, 11, 30 ) && hit_t > 1.8 );
        gs.block_selection.ray_origin = origin;
        gs.block_selection.ray_dir = dir;
        gs.block_selection.selectionInBounds = 1;
        gs.block_selection.pos_destroy = hit_cell;
        gs.block_selection.block_hit_dist = hit_t;
        RepGame::resolve_target_mob( gs, origin );
        check_bool( "target: cart beats the rail under it", gs.block_selection.target_vehicle == 0 );
        // A nearer block still wins: a stone above the cart shadows it.
        world.set_loaded_block( glm::ivec3( 45, 12, 30 ), stone );
        gs.block_selection.selectionInBounds = 1;
        gs.block_selection.pos_destroy = glm::ivec3( 45, 12, 30 );
        gs.block_selection.block_hit_dist = 0.0;
        RepGame::resolve_target_mob( gs, origin );
        check_bool( "target: occluding block beats the cart", gs.block_selection.target_vehicle < 0 );
        world.set_loaded_block( glm::ivec3( 45, 12, 30 ), BLOCK_STATE_AIR );
        gs.block_selection.selectionInBounds = 0;
        gs.block_selection.block_hit_dist = REACH_DISTANCE;
    }
    vehicles.test_vehicles( ).clear( );

    // --- capture mode freezes the sim --------------------------------------------------
    vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 46.5, 11.0 + 1.0 / 16.0, 30.5 ), 0.0f );
    vehicles.test_vehicles( )[ 0 ].vel = glm::dvec3( 0.03, 0.0, 0.0 );
    {
        run_policy( ).capture.enabled = true;
        const glm::dvec3 pos0 = only_cart( ).pos;
        tick( 10 );
        run_policy( ).capture.enabled = false;
        check_bool( "cart: capture mode freezes", only_cart( ).pos.x == pos0.x );
    }
    vehicles.test_vehicles( ).clear( );

    // --- remote players shove an owned empty cart --------------------------------------
    // Same overlap rule as the local player, from VehicleTickInput::extra_pushers.
    vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 6.5, 11.0 + 1.0 / 16.0, 4.5 ), 0.0f );
    {
        Vehicles::VehicleTickInput push_in = in;
        push_in.extra_pushers.push_back( glm::dvec3( 6.0, 11.0, 4.5 ) );
        vehicles.tick( gs, push_in );
        check_bool( "cart: remote player pushes", only_cart( ).vel.x > 1e-6 );
    }
    vehicles.test_vehicles( ).clear( );

    // --- multiplayer: wire codec + mirror/owner split ------------------------------------
    {
        // VEHICLE record codec round trip (pos = bottom-centre, yaw + flags).
        NetEntityRecord rec = { };
        rec.id = 0x123456789ull;
        rec.kind = NetEntityKind::VEHICLE;
        rec.x = 16.5;
        rec.y = 11.0625;
        rec.z = 20.5;
        rec.vx = 0.02f;
        rec.yaw = 90.0f;
        rec.flags = NET_VEHICLE_FLAG_HURT;
        PacketWriter w;
        net_write_entity_record( w, rec );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetEntityRecord got = { };
        check_bool( "net cart: record round trip",
                    net_read_entity_record( r, got ) && got.kind == NetEntityKind::VEHICLE && got.x == 16.5 &&
                        got.yaw == 90.0f && got.flags == NET_VEHICLE_FLAG_HURT );

        Multiplayer mp;
        NetEntities net( &mp );
        net.test_welcome( 9 );
        net.test_set_role( glm::ivec3( 0, 0, 0 ), NET_ROLE_OWNER );
        Vehicles mc;
        mc.set_multiplayer_mode( true, &net );
        // A welcomed session mints + owns its spawn (until the echo/role says
        // otherwise); a pre-WELCOME session still refuses (covered above).
        check_bool( "net cart: welcomed spawn mints id",
                    mc.spawn( VEHICLE_MINECART, glm::dvec3( 6.5, 11.0 + 1.0 / 16.0, 4.5 ), 0.0f ) != 0 && mc.test_vehicles( )[ 0 ].net.id != 0 );
        mc.test_vehicles( ).clear( );

        // Mirror: no simulation — make chunk 0 a VIEWER role (the cart must
        // stay inside the one live chunk for easing to run), parked on the
        // ascending rail at (16,11,20): never slides, and only applied
        // state moves it.
        net.test_set_role( glm::ivec3( 0, 0, 0 ), NET_ROLE_VIEWER );
        NetEntityRecord srec = { };
        srec.id = ( static_cast<NetEntityId>( 9 ) << 32 ) | 7;
        srec.kind = NetEntityKind::VEHICLE;
        srec.x = 16.5;
        srec.y = 11.0 + 1.0 / 16.0;
        srec.z = 20.5;
        mc.net_apply_spawn( srec, /*mirror*/ true );
        check_bool( "net cart: mirror spawned", mc.count( ) == 1 && mc.test_vehicles( )[ 0 ].net.mirror );
        const glm::dvec3 mpos = mc.test_vehicles( )[ 0 ].pos;
        for ( int i = 0; i < 200; i++ ) {
            mc.tick( gs, in );
        }
        check_bool( "net cart: mirror never simulates", mc.test_vehicles( )[ 0 ].pos == mpos );

        NetEntityStateEntry se = { };
        se.id = srec.id;
        se.kind = static_cast<uint8_t>( NetEntityKind::VEHICLE );
        se.present = NET_SE_POS_ABS | NET_SE_VEL | NET_SE_YAW;
        se.x = 17.2;
        se.y = 11.0 + 1.0 / 16.0;
        se.z = 20.5;
        se.yaw = 90.0f;
        mc.net_apply_state( se );
        for ( int i = 0; i < 50; i++ ) {
            mc.tick( gs, in );
        }
        check_bool( "net cart: mirror eases to streamed pos", mc.test_vehicles( )[ 0 ].pos.x > mpos.x + 0.1 );
        check_bool( "net cart: mirror yaw from stream", fabs( mc.test_vehicles( )[ 0 ].yaw - 90.0f ) < 0.01f );

        // HURT flag rising edge starts the wobble on a mirror.
        NetEntityStateEntry fe = { };
        fe.id = srec.id;
        fe.kind = static_cast<uint8_t>( NetEntityKind::VEHICLE );
        fe.present = NET_SE_FLAGS;
        fe.flags = NET_VEHICLE_FLAG_HURT;
        mc.net_apply_state( fe );
        check_bool( "net cart: HURT edge starts wobble", mc.test_vehicles( )[ 0 ].hurt_ticks > 0 );

        // A left-click on a mirror sends NET_ACTION_HIT — no local damage.
        const int hres = mc.hit_interact( gs, 0, /*survival*/ true, glm::dvec3( 0.0 ) );
        check_bool( "net cart: mirror hit sends action", hres == 2 );
        check_bool( "net cart: mirror hit does no local damage", mc.count( ) == 1 && mc.test_vehicles( )[ 0 ].damage == 0.0f );

        // Owned: emits a state entry (first collect always does), drifts
        // emit again, and VEHICLE entries never carry NET_SE_COUNT.
        net.test_set_role( glm::ivec3( 0, 0, 0 ), NET_ROLE_OWNER );
        NetEntityRecord orec = srec;
        orec.id = ( static_cast<NetEntityId>( 9 ) << 32 ) | 8;
        orec.x = 10.5;
        orec.z = 10.5;
        mc.net_apply_spawn( orec, /*mirror*/ false );
        Vehicles::Vehicle &own = mc.test_vehicles( )[ 1 ];
        check_bool( "net cart: owned spawn", !own.net.mirror && own.net.chunk == glm::ivec3( 0, 0, 0 ) );
        std::vector<NetEntityStateEntry> batch;
        mc.collect_net_state( net, batch, 1 );
        check_bool( "net cart: owned emits state",
                    batch.size( ) == 1 && batch[ 0 ].kind == static_cast<uint8_t>( NetEntityKind::VEHICLE ) );
        bool any_count = false;
        for ( const NetEntityStateEntry &e : batch ) {
            any_count |= ( e.present & NET_SE_COUNT ) != 0;
        }
        check_bool( "net cart: owned never streams COUNT", !any_count );
        batch.clear( );
        own.pos.x += 0.5; // simulated drift beyond the dead-reckoning eps
        mc.collect_net_state( net, batch, 20 );
        check_bool( "net cart: drift emits again", batch.size( ) == 1 );

        // A forwarded ENTITY_ACTION HIT lands on the owner: three survival
        // hits break the cart and drop the MINECART item exactly once.
        const size_t items_before = gs.world.items.count( );
        NetEntityActionPayload pact = { };
        pact.id = orec.id;
        pact.action = NET_ACTION_HIT;
        pact.attacker_survival = 1;
        mc.net_apply_action( gs, pact );
        check_bool( "net cart: owner applies forwarded hit", mc.test_vehicles( )[ 1 ].hurt_ticks > 0 );
        mc.net_apply_action( gs, pact );
        mc.net_apply_action( gs, pact );
        check_bool( "net cart: third hit breaks + drops once",
                    mc.count( ) == 1 && gs.world.items.count( ) == items_before + 1 &&
                        gs.world.items.debug_items( ).back( ).item == MINECART );
        mc.test_vehicles( ).clear( );
    }

    // --- multiplayer phase 2: the rider session owns the sim ------------------
    {
        Multiplayer mp;
        NetEntities net( &mp );
        net.test_welcome( 9 );                                              // our session id
        net.test_set_role( glm::ivec3( 0, 0, 0 ), NET_ROLE_OWNER );         // chunk 0 owned, chunk 1 foreign
        vehicles.set_multiplayer_mode( true, &net );

        // Empty cart in a foreign chunk: mirror by chunk ownership.
        NetEntityRecord frec = { };
        frec.id = ( static_cast<NetEntityId>( 77 ) << 32 ) | 5;
        frec.kind = NetEntityKind::VEHICLE;
        frec.x = 70.5;
        frec.y = 11.0 + 1.0 / 16.0;
        frec.z = 10.5;
        vehicles.net_apply_spawn( frec );
        Vehicles::Vehicle *fc = vehicles.find_by_net_id( frec.id );
        check_bool( "ride net: foreign empty cart mirrors", fc != nullptr && fc->net.mirror );

        // The server's COUNT naming our session hands the sim to us — same
        // cart, same foreign chunk.
        NetEntityStateEntry ce = { };
        ce.id = frec.id;
        ce.kind = static_cast<uint8_t>( NetEntityKind::VEHICLE );
        ce.present = NET_SE_COUNT;
        ce.count = 9;
        vehicles.net_apply_state( ce );
        check_bool( "ride net: ridden-by-me sims in foreign chunk", fc != nullptr && !fc->net.mirror );

        // A server-originated COUNT-only state must not flip an owned cart
        // to mirror nor clobber its simulated position.
        fc->pos = glm::dvec3( 71.25, 12.0, 10.5 );
        vehicles.net_apply_state( ce );
        check_bool( "ride net: COUNT-only keeps owned pos",
                    !fc->net.mirror && fc->pos.x == 71.25 && fc->pos.y == 12.0 );

        // Ridden by someone else in our owned chunk: mirror — both via a
        // COUNT state and via a chunk-role grant record.
        NetEntityRecord orec = frec;
        orec.id = ( static_cast<NetEntityId>( 77 ) << 32 ) | 6;
        orec.x = 10.5;
        vehicles.net_apply_spawn( orec, /*mirror*/ false );
        Vehicles::Vehicle *oc = vehicles.find_by_net_id( orec.id );
        check_bool( "ride net: empty cart in owned chunk is ours", oc != nullptr && !oc->net.mirror );
        NetEntityStateEntry oe = ce;
        oe.id = orec.id;
        oe.count = 77;
        vehicles.net_apply_state( oe );
        check_bool( "ride net: ridden-by-other mirrors in owned chunk", oc != nullptr && oc->net.mirror );

        NetEntityRecord grec = frec;
        grec.id = ( static_cast<NetEntityId>( 77 ) << 32 ) | 7;
        grec.count = 77;
        grec.x = 20.5;
        const std::vector<NetEntityRecord> recs{ grec };
        vehicles.net_apply_chunk_role( glm::ivec3( 0, 0, 0 ), NET_ROLE_OWNER, recs, { } );
        const Vehicles::Vehicle *gc = vehicles.find_by_net_id( grec.id );
        check_bool( "ride net: grant record ridden-by-other mirrors", gc != nullptr && gc->net.mirror );

        // Pending mount: request completes when a COUNT state names us.
        vehicles.test_vehicles( ).clear( );
        const uint32_t mid = vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 6.5, 11.0 + 1.0 / 16.0, 4.5 ), 0.0f );
        check_bool( "ride net: mp spawn mints net id", mid != 0 && vehicles.test_vehicles( )[ 0 ].net.id != 0 );
        const NetEntityId mid_net = vehicles.test_vehicles( )[ 0 ].net.id;
        check_bool( "ride net: mount request sent", vehicles.request_mount( gs, 0 ) );
        NetEntityStateEntry grant = ce;
        grant.id = mid_net;
        grant.count = 9;
        vehicles.net_apply_state( grant );
        tick( 1 ); // tick_pending_mount runs inside vehicles.tick
        check_bool( "ride net: pending mount completes on COUNT", gs.camera.riding_vehicle == mid );
        repgame_vehicle_dismount( gs );
        tick( 1 );

        // A pending mount that never gets a COUNT times out after 3 s — a
        // late grant must not mount then.
        vehicles.test_vehicles( ).clear( );
        const uint32_t mid2 = vehicles.spawn( VEHICLE_MINECART, glm::dvec3( 6.5, 11.0 + 1.0 / 16.0, 4.5 ), 0.0f );
        check_bool( "ride net: second mount request sent", mid2 != 0 && vehicles.request_mount( gs, 0 ) );
        const NetEntityId mid2_net = vehicles.test_vehicles( )[ 0 ].net.id;
        gs.tick_number += 3 * static_cast<long>( UPS_RATE ) + 1;
        tick( 1 );
        NetEntityStateEntry late = grant;
        late.id = mid2_net;
        vehicles.net_apply_state( late );
        tick( 1 );
        check_bool( "ride net: timed-out mount does not complete", gs.camera.riding_vehicle == 0 );

        repgame_vehicle_dismount( gs );
        vehicles.set_multiplayer_mode( false, nullptr );
        vehicles.test_vehicles( ).clear( );
    }

    return failures;
}
