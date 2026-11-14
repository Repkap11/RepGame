// Boat tests: float/settle states, underwater rise, land rest, rider
// paddle/steer input, land crawl, wall stops, player shoves, damage/drop
// items, persistence, riding (seat, camera yaw, dismount), placement and
// the MP record/mirror path.

#include "test_util.hpp"

#include "common/fluid.hpp"
#include "common/harness/run_policy.hpp"
#include "common/vehicle_physics.hpp" // VEHICLE_BOAT_DRAFT, vehicle_wrap_180

int test_boats( ) {
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
    auto check_val = [ & ]( const char *name, bool cond, const char *fmt, double v, double v2 = 0.0 ) {
        char buf[ 128 ];
        snprintf( buf, sizeof( buf ), fmt, v, v2 );
        pr_test( "  %-55s %s  (%s)", name, cond ? "PASS" : "FAIL", buf );
        if ( !cond ) {
            failures++;
        }
    };

    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    const BlockState water = { WATER, 0, 0, WATER, 0 };
    // Floor at y=10 across the test area.
    for ( int x = 0; x <= 61; x++ ) {
        for ( int z = 0; z <= 61; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 10, z ), stone );
        }
    }
    Vehicles::VehicleTickInput in;
    in.player_feet = glm::dvec3( 0.0, -100.0, 0.0 ); // far away: no pushing
    auto tick = [ & ]( const int n ) {
        for ( int i = 0; i < n; i++ ) {
            gs.world.vehicles.tick( gs, in );
        }
    };
    Vehicles &vehicles = world.vehicles;
    auto only_boat = [ & ]( ) -> Vehicles::Vehicle & { return vehicles.test_vehicles( ).at( 0 ); };

    // A walled 5x5x3 pool: water cells at y=11..13 (3 deep), stone rim at
    // y=11..14. Surface of the top water cell is cell.y + 0.875.
    auto pool = [ & ]( const int x0, const int z0 ) {
        for ( int x = x0; x < x0 + 5; x++ ) {
            for ( int z = z0; z < z0 + 5; z++ ) {
                for ( int y = 11; y <= 13; y++ ) {
                    world.set_loaded_block( glm::ivec3( x, y, z ), water );
                }
            }
        }
        for ( int x = x0 - 1; x <= x0 + 5; x++ ) {
            for ( int z = z0 - 1; z <= z0 + 5; z++ ) {
                const bool rim = x == x0 - 1 || x == x0 + 5 || z == z0 - 1 || z == z0 + 5;
                if ( !rim ) {
                    continue;
                }
                for ( int y = 11; y <= 14; y++ ) {
                    world.set_loaded_block( glm::ivec3( x, y, z ), stone );
                }
            }
        }
        return glm::dvec3( x0 + 2.5, 0.0, z0 + 2.5 );
    };
    const glm::dvec3 mid = pool( 8, 8 );
    const double surface = 13.0 + 0.875;
    const double float_y = surface - VEHICLE_BOAT_DRAFT;

    // --- floats to the surface and settles ------------------------------------
    vehicles.spawn( VEHICLE_OAK_BOAT, glm::dvec3( mid.x, 15.5, mid.z ), 0.0f );
    {
        double min_y = 1e9, max_y = -1e9;
        for ( int i = 0; i < 3 * static_cast<int>( UPS_RATE ); i++ ) {
            tick( 1 );
            if ( i >= 2 * static_cast<int>( UPS_RATE ) ) {
                min_y = std::min( min_y, only_boat( ).pos.y );
                max_y = std::max( max_y, only_boat( ).pos.y );
            }
        }
        check_bool( "boat: settles on the surface draft line",
                    fabs( only_boat( ).pos.y - float_y ) < 0.02 && fabs( only_boat( ).vel.y ) < 1e-4 );
        check_bool( "boat: no visible bob once settled", max_y - min_y < 0.01 );
    }
    vehicles.test_vehicles( ).clear( );

    // --- spawned under water, rises and settles -------------------------------
    vehicles.spawn( VEHICLE_OAK_BOAT, glm::dvec3( mid.x, 12.0, mid.z ), 0.0f );
    tick( 3 * static_cast<int>( UPS_RATE ) );
    check_bool( "boat: submerged boat rises to the surface",
                fabs( only_boat( ).pos.y - float_y ) < 0.02 && fabs( only_boat( ).vel.y ) < 1e-4 );
    vehicles.test_vehicles( ).clear( );

    // --- dropped on land, rests standing --------------------------------------
    vehicles.spawn( VEHICLE_OAK_BOAT, glm::dvec3( 32.5, 14.5, 32.5 ), 0.0f );
    tick( 3 * static_cast<int>( UPS_RATE ) );
    check_val( "boat: rests standing on land",
               fabs( only_boat( ).pos.y - 11.0 ) < 0.01 && glm::length( only_boat( ).vel ) < 0.01, "y=%.6f |v|=%.6f",
               only_boat( ).pos.y, glm::length( only_boat( ).vel ) );
    vehicles.test_vehicles( ).clear( );

    // A 3-wide canal with a long runway for the cruising-speed test.
    for ( int x = 44; x <= 46; x++ ) {
        for ( int z = 8; z <= 56; z++ ) {
            for ( int y = 11; y <= 13; y++ ) {
                world.set_loaded_block( glm::ivec3( x, y, z ), water );
            }
        }
    }
    for ( int z = 8; z <= 56; z++ ) {
        for ( int y = 11; y <= 14; y++ ) {
            world.set_loaded_block( glm::ivec3( 43, y, z ), stone );
            world.set_loaded_block( glm::ivec3( 47, y, z ), stone );
        }
    }
    for ( int x = 43; x <= 47; x++ ) {
        for ( int y = 11; y <= 14; y++ ) {
            world.set_loaded_block( glm::ivec3( x, y, 7 ), stone );
            world.set_loaded_block( glm::ivec3( x, y, 57 ), stone );
        }
    }
    const glm::dvec3 canal( 45.5, float_y, 54.0 );

    // --- rider forward on water: MC cruising speed, then damps ----------------
    const uint32_t fwd_id = vehicles.spawn( VEHICLE_OAK_BOAT, canal, 0.0f );
    in.rider_vehicle = fwd_id;
    in.rider_forward = 1.0;
    // Sample the peak before the boat reaches the far pool wall.
    double water_speed = 0.0;
    for ( int i = 0; i < 3 * static_cast<int>( UPS_RATE ); i++ ) {
        tick( 1 );
        const glm::dvec3 fwd_dir( sin( glm::radians( static_cast<double>( only_boat( ).yaw ) ) ), 0.0,
                                  -cos( glm::radians( static_cast<double>( only_boat( ).yaw ) ) ) );
        water_speed = std::max( water_speed, glm::dot( only_boat( ).vel, fwd_dir ) * UPS_RATE );
    }
    check_val( "boat: paddles to cruising speed", water_speed >= 6.5 && water_speed <= 8.0, "speed=%.3f", water_speed );
    in.rider_forward = 0.0;
    tick( 3 * static_cast<int>( UPS_RATE ) );
    check_bool( "boat: released paddle damps to a stop", glm::length( only_boat( ).vel ) * UPS_RATE < 0.5 );
    in.rider_vehicle = 0;
    vehicles.test_vehicles( ).clear( );

    // --- steering turns the hull at MC rate ------------------------------------
    const uint32_t turn_id = vehicles.spawn( VEHICLE_OAK_BOAT, glm::dvec3( mid.x, float_y, mid.z ), 0.0f );
    in.rider_vehicle = turn_id;
    in.rider_turn = 1.0;
    {
        double yaw_turned = 0.0;
        for ( int i = 0; i < static_cast<int>( UPS_RATE ); i++ ) {
            const float y0 = only_boat( ).yaw;
            tick( 1 );
            yaw_turned += vehicle_wrap_180( only_boat( ).yaw - y0 );
        }
        const float yaw1 = only_boat( ).yaw;
        tick( static_cast<int>( UPS_RATE ) );
        const double rate = vehicle_wrap_180( only_boat( ).yaw - yaw1 );
        check_val( "boat: positive turn increases yaw", yaw_turned > 0.0, "turned=%.1f", yaw_turned );
        check_val( "boat: steady turn rate", rate >= 120.0 && rate <= 220.0, "rate=%.1f", rate );
    }
    in.rider_turn = -1.0;
    {
        tick( 2 * static_cast<int>( UPS_RATE ) );
        const float yaw_before = only_boat( ).yaw;
        tick( static_cast<int>( UPS_RATE ) );
        check_bool( "boat: negative turn decreases yaw", vehicle_wrap_180( only_boat( ).yaw - yaw_before ) < 0.0 );
    }
    in.rider_turn = 0.0;
    in.rider_vehicle = 0;
    vehicles.test_vehicles( ).clear( );

    // --- land crawl is a fraction of water speed --------------------------------
    const uint32_t land_id = vehicles.spawn( VEHICLE_OAK_BOAT, glm::dvec3( 32.5, 11.0, 32.5 ), 0.0f );
    in.rider_vehicle = land_id;
    in.rider_forward = 1.0;
    tick( 3 * static_cast<int>( UPS_RATE ) );
    const double land_speed = glm::length( only_boat( ).vel ) * UPS_RATE;
    check_bool( "boat: land crawl under quarter speed", land_speed < 0.25 * water_speed );
    in.rider_forward = 0.0;
    in.rider_vehicle = 0;
    vehicles.test_vehicles( ).clear( );

    // --- driven into the pool wall: stops, no tunnelling, stays floating -------
    {
        const uint32_t wall_id = vehicles.spawn( VEHICLE_OAK_BOAT, glm::dvec3( mid.x, float_y, mid.z ), 0.0f );
        in.rider_vehicle = wall_id;
        in.rider_forward = 1.0;
        tick( 5 * static_cast<int>( UPS_RATE ) );
        check_bool( "boat: wall stops without tunnelling",
                    only_boat( ).pos.z > 7.0 && glm::length( only_boat( ).vel ) * UPS_RATE < 0.5 );
        check_bool( "boat: still floating against the wall", fabs( only_boat( ).pos.y - float_y ) < 0.02 );
        in.rider_forward = 0.0;
        in.rider_vehicle = 0;
        vehicles.test_vehicles( ).clear( );
    }

    // --- player overlap shoves an empty boat, never a ridden one ----------------
    {
        vehicles.spawn( VEHICLE_OAK_BOAT, glm::dvec3( 32.5, 11.0, 32.5 ), 0.0f );
        in.player_feet = glm::dvec3( 31.9, 11.0, 32.5 ); // inside the hull edge
        tick( 60 );
        in.player_feet = glm::dvec3( 0.0, -100.0, 0.0 );
        check_bool( "boat: player overlap shoves empty boat", only_boat( ).pos.x > 32.5 );
        vehicles.test_vehicles( ).clear( );

        const uint32_t shoved_id = vehicles.spawn( VEHICLE_OAK_BOAT, glm::dvec3( 32.5, 11.0, 32.5 ), 0.0f );
        in.rider_vehicle = shoved_id;
        in.player_feet = glm::dvec3( 31.9, 11.0, 32.5 );
        tick( 60 );
        in.player_feet = glm::dvec3( 0.0, -100.0, 0.0 );
        in.rider_vehicle = 0;
        check_bool( "boat: ridden boat is not shoved", fabs( only_boat( ).pos.x - 32.5 ) < 1e-6 );
        vehicles.test_vehicles( ).clear( );
    }

    // --- damage / drop rules ------------------------------------------------------
    {
        const size_t items_before = gs.world.items.count( );
        vehicles.spawn( VEHICLE_SPRUCE_BOAT, glm::dvec3( 32.5, 11.0, 32.5 ), 0.0f );
        check_bool( "boat: survival hit 1 applied", vehicles.hit_interact( gs, 0, true, glm::dvec3( 0.0 ) ) == 1 );
        check_bool( "boat: survival hit 2 applied", vehicles.hit_interact( gs, 0, true, glm::dvec3( 0.0 ) ) == 1 );
        check_bool( "boat: survival hit 3 applied", vehicles.hit_interact( gs, 0, true, glm::dvec3( 0.0 ) ) == 1 );
        check_bool( "boat: third hit breaks + drops spruce boat",
                    vehicles.count( ) == 0 && gs.world.items.count( ) == items_before + 1 &&
                        gs.world.items.debug_items( ).back( ).item == SPRUCE_BOAT );
        vehicles.test_vehicles( ).clear( );

        vehicles.spawn( VEHICLE_SPRUCE_BOAT, glm::dvec3( 32.5, 11.0, 32.5 ), 0.0f );
        check_bool( "boat: creative hit removes without drop",
                    vehicles.hit_interact( gs, 0, false, glm::dvec3( 0.0 ) ) == 1 && vehicles.count( ) == 0 &&
                        gs.world.items.count( ) == items_before + 1 );
    }

    // --- persistence round trip keeps the type -------------------------------------
    {
        ScopedTempHome temp_home( "/tmp/repgame_test_boats_home" );
        MapStorage storage;
        storage.init( "TestBoats" );
        Vehicles store_vehicles;
        store_vehicles.init_storage( &storage );
        VehicleFileRecord rec = { 5.5, 11.2, 30.5, 0.1f, -0.2f, 0.3f, 45.0f, VEHICLE_CHERRY_BOAT };
        Chunk chunk;
        chunk.chunk_pos = glm::ivec3( 0, 0, 0 );
        chunk.sidecars.vehicles.push_back( rec );
        chunk.sidecars.vehicles_file_present = 1;
        store_vehicles.adopt_chunk( chunk );
        check_bool( "boat persist: adopt spawns cherry boat",
                    store_vehicles.count( ) == 1 && store_vehicles.debug_vehicles( )[ 0 ].type == VEHICLE_CHERRY_BOAT );
        store_vehicles.persist_all( );
        std::vector<VehicleFileRecord> out;
        check_bool( "boat persist: type round-trips",
                    storage.load_vehicles( glm::ivec3( 0, 0, 0 ), out ) == 1 && out.size( ) == 1 && out[ 0 ].type == VEHICLE_CHERRY_BOAT );
    }

    // --- riding: seat, camera yaw coupling, dismount ---------------------------------
    {
        // Shore beside the pool: a stone column at the rim for the land dismount.
        world.set_loaded_block( glm::ivec3( 7, 11, 10 ), stone );
        world.set_loaded_block( glm::ivec3( 7, 12, 10 ), stone );
        const uint32_t ride_id = vehicles.spawn( VEHICLE_OAK_BOAT, glm::dvec3( 9.2, float_y, 10.5 ), 180.0f );
        check_bool( "boat ride: mount sets riding", repgame_vehicle_mount( gs, ride_id ) && gs.camera.riding_vehicle == ride_id );
        in.rider_vehicle = ride_id;
        tick( 5 );
        repgame_vehicle_ride_follow( gs );
        const VehicleTypeDef &def = vehicle_type_def( VEHICLE_OAK_BOAT );
        check_bool( "boat ride: eye at rider_eye_offset",
                    fabs( gs.camera.pos.y - ( only_boat( ).pos.y + def.rider_eye_offset ) ) < 1e-9 );
        // Turning the hull turns the camera by the same delta.
        gs.camera.angle_H = 0.0f;
        in.rider_turn = 1.0;
        double cam_delta = 0.0, hull_delta = 0.0;
        for ( int i = 0; i < static_cast<int>( UPS_RATE ); i++ ) {
            const float y0 = only_boat( ).yaw;
            tick( 1 );
            hull_delta += vehicle_wrap_180( only_boat( ).yaw - y0 );
            repgame_vehicle_ride_follow( gs );
        }
        cam_delta = gs.camera.angle_H;
        check_bool( "boat ride: hull turns camera equally",
                    fabs( cam_delta - hull_delta ) < 0.5 && fabs( hull_delta ) > 30.0 );
        in.rider_turn = 0.0;
        // Sneak dismount onto the shore column beside the boat.
        repgame_vehicle_dismount( gs );
        check_bool( "boat ride: dismount lands on adjacent shore",
                    gs.camera.riding_vehicle == 0 && fabs( gs.camera.pos.y - ( 13.0 + PLAYER_EYE_HEIGHT ) ) < 1e-6 );
        // Away from land the dismount lands in the water beside the boat.
        const uint32_t swim_id = vehicles.spawn( VEHICLE_OAK_BOAT, glm::dvec3( mid.x, float_y, mid.z ), 0.0f );
        check_bool( "boat ride: remount over water", repgame_vehicle_mount( gs, swim_id ) );
        repgame_vehicle_dismount( gs );
        check_bool( "boat ride: dismount into water beside",
                    gs.camera.riding_vehicle == 0 && gs.camera.pos.y - PLAYER_EYE_HEIGHT < surface );
        in.rider_vehicle = 0;
        vehicles.test_vehicles( ).clear( );
        world.set_loaded_block( glm::ivec3( 7, 11, 10 ), BLOCK_STATE_AIR );
        world.set_loaded_block( glm::ivec3( 7, 12, 10 ), BLOCK_STATE_AIR );
    }

    // --- placement: water pick cell and land-top --------------------------------------
    {
        test_setup_inventories( gs );
        gs.game_mode = GameMode_Survival;
        gs.hotbar.addItemWithQuantity( OAK_BOAT, 1 );
        gs.camera.angle_H = 42.0f;
        gs.block_selection.floatSelectionInBounds = 1;
        gs.block_selection.pos_destroy_water = glm::ivec3( 9, 13, 9 );
        gs.block_selection.selectionInBounds = 0;
        check_bool( "boat place: water cell spawns afloat",
                    repgame_vehicle_place( gs, vehicle_type_for_item( gs.hotbar.getSelectedItem( ) ) ) );
        check_bool( "boat place: spawned oak boat on the surface",
                    vehicles.count( ) == 1 && only_boat( ).type == VEHICLE_OAK_BOAT &&
                        fabs( only_boat( ).pos.y - float_y ) < 0.02 && fabs( only_boat( ).yaw - 42.0f ) < 1e-6 );
        check_bool( "boat place: survival consumes the item", gs.hotbar.getSelectedQuantity( ) == 0 );
        vehicles.test_vehicles( ).clear( );

        gs.hotbar.addItemWithQuantity( OAK_BOAT, 1 );
        gs.block_selection.floatSelectionInBounds = 0;
        gs.block_selection.selectionInBounds = 1;
        gs.block_selection.pos_destroy = glm::ivec3( 33, 10, 33 ); // solid floor cell
        check_bool( "boat place: land top spawns",
                    repgame_vehicle_place( gs, vehicle_type_for_item( gs.hotbar.getSelectedItem( ) ) ) );
        check_bool( "boat place: rests on the block top",
                    vehicles.count( ) == 1 && fabs( only_boat( ).pos.y - 11.0 ) < 1e-9 );
        vehicles.test_vehicles( ).clear( );
    }

    // --- multiplayer: wire codec + mirror easing ----------------------------------------
    {
        NetEntityRecord rec = { };
        rec.id = 0x123456789ull;
        rec.kind = NetEntityKind::VEHICLE;
        rec.type = VEHICLE_CHERRY_BOAT;
        rec.x = 10.5;
        rec.y = 13.775;
        rec.z = 10.5;
        rec.yaw = 90.0f;
        PacketWriter w;
        net_write_entity_record( w, rec );
        PacketReader r( w.buf( ).data( ), w.buf( ).size( ) );
        NetEntityRecord got = { };
        check_bool( "net boat: record type round trip",
                    net_read_entity_record( r, got ) && got.kind == NetEntityKind::VEHICLE && got.type == VEHICLE_CHERRY_BOAT );

        Multiplayer mp;
        NetEntities net( &mp );
        net.test_welcome( 9 );
        Vehicles mv;
        mv.set_multiplayer_mode( true, &net );
        net.test_set_role( glm::ivec3( 0, 0, 0 ), NET_ROLE_VIEWER );
        NetEntityRecord srec = { };
        srec.id = ( static_cast<NetEntityId>( 9 ) << 32 ) | 7;
        srec.kind = NetEntityKind::VEHICLE;
        srec.type = VEHICLE_OAK_BOAT;
        srec.x = mid.x;
        srec.y = float_y;
        srec.z = mid.z;
        mv.net_apply_spawn( srec, /*mirror*/ true );
        check_bool( "net boat: mirror spawned", mv.count( ) == 1 && mv.test_vehicles( )[ 0 ].net.mirror &&
                                                  mv.test_vehicles( )[ 0 ].type == VEHICLE_OAK_BOAT );
        NetEntityStateEntry se = { };
        se.id = srec.id;
        se.kind = static_cast<uint8_t>( NetEntityKind::VEHICLE );
        se.present = NET_SE_POS_ABS | NET_SE_VEL | NET_SE_YAW;
        se.x = mid.x + 0.7;
        se.y = float_y;
        se.z = mid.z;
        se.yaw = 30.0f;
        mv.net_apply_state( se );
        for ( int i = 0; i < 50; i++ ) {
            mv.tick( gs, in );
        }
        check_bool( "net boat: mirror eases to streamed pos", mv.test_vehicles( )[ 0 ].pos.x > mid.x + 0.1 );
        check_bool( "net boat: mirror runs no physics", fabs( mv.test_vehicles( )[ 0 ].pos.y - float_y ) < 0.2 );
        check_bool( "net boat: mirror pitch stays 0", mv.test_vehicles( )[ 0 ].pitch == 0.0f );
        mv.test_vehicles( ).clear( );
    }

    return failures;
}
