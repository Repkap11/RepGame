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

    // --- oriented hull collision -------------------------------------------------
    // Exact oriented-rects vs terrain penetration for the assertions
    // below: a union of concentric yaw-aligned rectangles SAT-tested
    // against every overlapping cell's real collision box (fences,
    // slabs — not whole cells). Deepest minimum-translation overlap
    // over all (rect, box) pairs, 0 when clean. The skin-level Y
    // overlap a resting block has is ignored like the solver's rule.
    auto rects_pen = [ & ]( const Vehicles::Vehicle &v, const double *hls, const double *hws, const int nr ) -> double {
        const VehicleTypeDef &def = vehicle_type_def( v.type );
        const double ry = glm::radians( static_cast<double>( v.yaw ) );
        const glm::dvec2 fwd( sin( ry ), -cos( ry ) ), side( cos( ry ), sin( ry ) );
        const glm::dvec2 c( v.pos.x, v.pos.z );
        double lo_x = 1e9, hi_x = -1e9, lo_z = 1e9, hi_z = -1e9;
        for ( int r = 0; r < nr; r++ ) {
            lo_x = std::min( lo_x, c.x - hls[ r ] * fabs( fwd.x ) - hws[ r ] * fabs( side.x ) );
            hi_x = std::max( hi_x, c.x + hls[ r ] * fabs( fwd.x ) + hws[ r ] * fabs( side.x ) );
            lo_z = std::min( lo_z, c.y - hls[ r ] * fabs( fwd.y ) - hws[ r ] * fabs( side.y ) );
            hi_z = std::max( hi_z, c.y + hls[ r ] * fabs( fwd.y ) + hws[ r ] * fabs( side.y ) );
        }
        glm::dvec3 bmins[ 256 ], bmaxs[ 256 ];
        const int nb = Collision::terrain_boxes( world, glm::dvec3( lo_x, v.pos.y, lo_z ),
                                                 glm::dvec3( hi_x, v.pos.y + def.height, hi_z ), bmins, bmaxs, 256 );
        double pen = 0.0;
        for ( int i = 0; i < nb; i++ ) {
            const double oy = std::min( v.pos.y + def.height, bmaxs[ i ].y ) - std::max( v.pos.y, bmins[ i ].y );
            if ( oy <= 1e-3 ) {
                continue;
            }
            double bf_lo = 1e9, bf_hi = -1e9, bs_lo = 1e9, bs_hi = -1e9;
            for ( int dx = 0; dx <= 1; dx++ ) {
                for ( int dz = 0; dz <= 1; dz++ ) {
                    const glm::dvec2 b( dx ? bmaxs[ i ].x : bmins[ i ].x, dz ? bmaxs[ i ].z : bmins[ i ].z );
                    bf_lo = std::min( bf_lo, glm::dot( b, fwd ) ); bf_hi = std::max( bf_hi, glm::dot( b, fwd ) );
                    bs_lo = std::min( bs_lo, glm::dot( b, side ) ); bs_hi = std::max( bs_hi, glm::dot( b, side ) );
                }
            }
            for ( int r = 0; r < nr; r++ ) {
                const double rx_lo = c.x - hls[ r ] * fabs( fwd.x ) - hws[ r ] * fabs( side.x );
                const double rx_hi = c.x + hls[ r ] * fabs( fwd.x ) + hws[ r ] * fabs( side.x );
                const double rz_lo = c.y - hls[ r ] * fabs( fwd.y ) - hws[ r ] * fabs( side.y );
                const double rz_hi = c.y + hls[ r ] * fabs( fwd.y ) + hws[ r ] * fabs( side.y );
                const double rf_lo = glm::dot( c, fwd ) - hls[ r ], rf_hi = glm::dot( c, fwd ) + hls[ r ];
                const double rs_lo = glm::dot( c, side ) - hws[ r ], rs_hi = glm::dot( c, side ) + hws[ r ];
                const double ox = std::min( rx_hi, bmaxs[ i ].x ) - std::max( rx_lo, bmins[ i ].x );
                const double oz = std::min( rz_hi, bmaxs[ i ].z ) - std::max( rz_lo, bmins[ i ].z );
                const double of = std::min( rf_hi, bf_hi ) - std::max( rf_lo, bf_lo );
                const double os = std::min( rs_hi, bs_hi ) - std::max( rs_lo, bs_lo );
                const double p = std::min( oy, std::min( ox, std::min( oz, std::min( of, os ) ) ) );
                if ( p > 0.0 ) {
                    pen = std::max( pen, p );
                }
            }
        }
        return pen;
    };
    // The collision hull: the def's compound rects (or the single
    // width x length rect when a type has none).
    auto rect_pen = [ & ]( const Vehicles::Vehicle &v ) -> double {
        const VehicleTypeDef &def = vehicle_type_def( v.type );
        if ( def.hull_rect_count > 0 ) {
            double hls[ 8 ], hws[ 8 ];
            const int nr = std::min( def.hull_rect_count, 8 );
            for ( int r = 0; r < nr; r++ ) {
                hls[ r ] = def.hull_rects[ r ].half_length;
                hws[ r ] = def.hull_rects[ r ].half_width;
            }
            return rects_pen( v, hls, hws, nr );
        }
        const double hl = def.length * 0.5, hw = def.width * 0.5;
        return rects_pen( v, &hl, &hw, 1 );
    };
    // The rendered hull: always the full width x length rectangle —
    // used to measure how far the *visible* bow/stern enters terrain
    // where the narrower compound rects don't reach.
    auto render_pen = [ & ]( const Vehicles::Vehicle &v ) -> double {
        const VehicleTypeDef &def = vehicle_type_def( v.type );
        const double hl = def.length * 0.5, hw = def.width * 0.5;
        return rects_pen( v, &hl, &hw, 1 );
    };

    // Shore arenas: enclosed water ponds, one wall shape each.
    auto pond = [ & ]( const int x0, const int z0, const int w, const int wall_y0, const int wall_y1,
                       const bool diagonal ) -> glm::dvec3 {
        for ( int x = x0; x < x0 + w; x++ ) {
            for ( int z = z0; z < z0 + w; z++ ) {
                for ( int y = 11; y <= 13; y++ ) {
                    world.set_loaded_block( glm::ivec3( x, y, z ), water );
                }
            }
        }
        for ( int x = x0 - 1; x <= x0 + w; x++ ) {
            for ( int z = z0 - 1; z <= z0 + w; z++ ) {
                const bool rim = x == x0 - 1 || x == x0 + w || z == z0 - 1 || z == z0 + w;
                for ( int y = wall_y0; y <= wall_y1; y++ ) {
                    if ( rim ) {
                        world.set_loaded_block( glm::ivec3( x, y, z ), stone );
                    }
                }
                // Diagonal shore: a staircase wall filling the +x+z
                // corner of the pond (clear of the centre spawn point).
                if ( diagonal && ( x - x0 ) + ( z - z0 ) >= w + 4 ) {
                    for ( int y = wall_y0; y <= wall_y1; y++ ) {
                        world.set_loaded_block( glm::ivec3( x, y, z ), stone );
                    }
                }
            }
        }
        return glm::dvec3( x0 + w * 0.5, float_y, z0 + w * 0.5 );
    };
    const glm::dvec3 pond_low = pond( 16, 16, 10, 13, 13, false );   // 1-block wall at water level
    const glm::dvec3 pond_high = pond( 16, 30, 10, 13, 14, false );  // wall 2 high
    const glm::dvec3 pond_diag = pond( 30, 44, 10, 13, 14, true );   // diagonal wall
    // Beach rising a block above the water: thick solid shore at pond C.
    for ( int x = 15; x <= 42; x++ ) {
        for ( int z = 55; z <= 61; z++ ) {
            for ( int y = 11; y <= 14; y++ ) {
                world.set_loaded_block( glm::ivec3( x, y, z ), stone );
            }
        }
    }
    const glm::dvec3 pond_beach = pond( 16, 44, 10, 13, 14, false );

    // Drive forward AND backward into each shore at yaw 0/30/45/90: on every
    // tick the true hull rect must stay out of solid blocks and the boat
    // ends floating. Diagonal approaches keep sliding along the wall —
    // tangential speed stays above half the approach's along-wall part.
    const struct {
        const char *name;
        const glm::dvec3 &at;
    } shores[] = {
        { "1-high wall", pond_low }, { "2-high wall", pond_high }, { "beach", pond_beach }, { "diagonal", pond_diag },
    };
    const float shore_yaws[] = { 0.0f, 30.0f, 45.0f, 90.0f };
    const double shore_dirs[] = { 1.0, -1.0 };
    for ( const auto &shore : shores ) {
        double worst_pen = 0.0;
        for ( const float yaw : shore_yaws ) {
            for ( const double dir : shore_dirs ) {
                const uint32_t id = vehicles.spawn( VEHICLE_OAK_BOAT, shore.at, yaw );
                in.rider_vehicle = id;
                in.rider_forward = dir;
                char name[ 96 ];
                // Drive direction picks the hit wall; the tangential
                // axis is the other horizontal one.
                const double ry = glm::radians( static_cast<double>( yaw ) );
                const glm::dvec2 drive( dir * sin( ry ), dir * -cos( ry ) );
                const bool drive_x = fabs( drive.x ) >= fabs( drive.y );
                const bool diagonal = fabs( drive.x ) > 0.2 && fabs( drive.y ) > 0.2;
                double approach_tan = 0.0;
                int contact_tick = -1, slide_ticks = 0;
                for ( int i = 0; i < 8 * static_cast<int>( UPS_RATE ); i++ ) {
                    tick( 1 );
                    const double pen = rect_pen( only_boat( ) );
                    worst_pen = std::max( worst_pen, pen );
                    const double tan = ( drive_x ? fabs( only_boat( ).vel.z ) : fabs( only_boat( ).vel.x ) ) * UPS_RATE;
                    if ( contact_tick < 0 ) {
                        if ( pen > 0.0005 ) {
                            contact_tick = i;
                            approach_tan = tan;
                        }
                    } else if ( i - contact_tick < static_cast<int>( UPS_RATE ) && tan > 0.5 * approach_tan ) {
                        // Sliding is measured in the second after first
                        // contact — past that the boat may legitimately
                        // stop wedged in the far corner.
                        slide_ticks++;
                    }
                }
                snprintf( name, sizeof( name ), "boat shore: %s yaw=%.0f %s stays floating", shore.name, yaw,
                          dir > 0.0 ? "fwd" : "back" );
                check_val( name, fabs( only_boat( ).pos.y - float_y ) < 0.05, "y=%.3f", only_boat( ).pos.y );
                if ( diagonal && contact_tick >= 0 && approach_tan > 0.1 ) {
                    snprintf( name, sizeof( name ), "boat shore: %s yaw=%.0f %s keeps sliding", shore.name, yaw,
                              dir > 0.0 ? "fwd" : "back" );
                    check_val( name, slide_ticks > static_cast<int>( UPS_RATE ) / 4, "ticks=%d approach=%.2f",
                               slide_ticks, approach_tan );
                }
                in.rider_forward = 0.0;
                in.rider_vehicle = 0;
                vehicles.test_vehicles( ).clear( );
            }
        }
        char name[ 96 ];
        snprintf( name, sizeof( name ), "boat shore: %s hull never penetrates", shore.name );
        check_val( name, worst_pen <= 0.01, "pen=%.4f", worst_pen );
    }

    // Steering pivots a pressed hull off the wall — both directions, both
    // contact poses — and the hull never ends up inside terrain.
    auto turn_off_wall = [ & ]( const char *name, const glm::dvec3 &pos, const float yaw ) {
        double worst = 0.0;
        for ( const double t : { 1.0, -1.0 } ) {
            const uint32_t id = vehicles.spawn( VEHICLE_OAK_BOAT, pos, yaw );
            in.rider_vehicle = id;
            in.rider_turn = t;
            const float yaw0 = vehicles.test_vehicles( ).back( ).yaw;
            double turned = 0.0;
            for ( int i = 0; i < 2 * static_cast<int>( UPS_RATE ); i++ ) {
                tick( 1 );
                turned = fabs( vehicle_wrap_180( only_boat( ).yaw - yaw0 ) );
                worst = std::max( worst, rect_pen( only_boat( ) ) );
            }
            char label[ 96 ];
            snprintf( label, sizeof( label ), "boat turn: %s %s rotates off", name, t > 0.0 ? "A" : "D" );
            check_val( label, turned >= 45.0, "turned=%.1f", turned );
            in.rider_turn = 0.0;
            in.rider_vehicle = 0;
            vehicles.test_vehicles( ).clear( );
        }
        check_val( "boat turn: no penetration", worst <= 0.01, "pen=%.4f", worst );
    };
    // Hull side parallel to pond_low's north wall (face z=16): at yaw 90
    // the boat's -z side touches at pos.z = 16 + width/2 + skin.
    turn_off_wall( "side flush", glm::dvec3( 21.0, float_y, 16.0 + 0.626 ), 90.0f );
    // Bow-first into the same wall: at yaw 0 the front (-z) edge touches.
    turn_off_wall( "bow flush", glm::dvec3( 21.0, float_y, 16.0 + 1.001 ), 0.0f );

    // Turning uses a small depenetration budget — a wedged hull pivots
    // but never flings: while A is held (no paddling) the centre must
    // stay under ~3.5 b/s.
    {
        const uint32_t id = vehicles.spawn( VEHICLE_OAK_BOAT, glm::dvec3( 21.0, float_y, 16.0 + 0.626 ), 90.0f );
        in.rider_vehicle = id;
        in.rider_turn = 1.0;
        glm::dvec3 prev = only_boat( ).pos;
        double vmax = 0.0;
        for ( int i = 0; i < 3 * static_cast<int>( UPS_RATE ); i++ ) {
            tick( 1 );
            vmax = std::max( vmax, glm::length( only_boat( ).pos - prev ) * UPS_RATE );
            prev = only_boat( ).pos;
        }
        check_val( "boat turn: wedged pivot never flings", vmax <= 3.5, "v=%.2f", vmax );
        in.rider_turn = 0.0;
        in.rider_vehicle = 0;
        vehicles.test_vehicles( ).clear( );
    }

    // Wedge escapes: every escape needs either a >20 deg turn from A/D or
    // an S+A / S+D that both turns >20 deg and displaces >0.3.
    auto wedge_escape = [ & ]( const char *name, const glm::dvec3 &pos, const float yaw ) {
        bool ok_a = false, ok_d = false;
        for ( int side_i = 0; side_i < 2; side_i++ ) {
            const double t = side_i == 0 ? 1.0 : -1.0;
            for ( int attempt = 0; attempt < 2; attempt++ ) {
                const uint32_t id = vehicles.spawn( VEHICLE_OAK_BOAT, pos, yaw );
                in.rider_vehicle = id;
                const glm::dvec3 pos0 = only_boat( ).pos;
                const float yaw0 = only_boat( ).yaw;
                double turned = 0.0, disp = 0.0;
                in.rider_turn = t;
                in.rider_forward = attempt == 0 ? 0.0 : -1.0;
                const int secs = attempt == 0 ? 2 : 3;
                for ( int i = 0; i < secs * static_cast<int>( UPS_RATE ); i++ ) {
                    tick( 1 );
                    turned = fabs( vehicle_wrap_180( only_boat( ).yaw - yaw0 ) );
                    disp = glm::length( only_boat( ).pos - pos0 );
                }
                in.rider_turn = 0.0;
                in.rider_forward = 0.0;
                const bool escaped = attempt == 0 ? turned > 20.0 : turned > 20.0 && disp > 0.3;
                in.rider_vehicle = 0;
                vehicles.test_vehicles( ).clear( );
                if ( escaped ) {
                    if ( t > 0.0 ) {
                        ok_a = true;
                    } else {
                        ok_d = true;
                    }
                    break;
                }
            }
            vehicles.test_vehicles( ).clear( );
            in.rider_vehicle = 0;
        }
        char label[ 96 ];
        snprintf( label, sizeof( label ), "boat wedge: %s escapes", name );
        check_bool( label, ok_a && ok_d );
    };
    // (a) bow in a 90 deg concave corner at 45 deg: pond_low's
    // north+west wall faces meet at (16,16); the bow points at it at
    // yaw -45 (forward = (-.707,-.707)).
    wedge_escape( "concave corner", glm::dvec3( 16.0 + 0.72, float_y, 16.0 + 0.72 ), -45.0f );
    // (b) wedged sideways between two post pillars ~2.2 apart — fence
    // posts' narrow centred box leaves a gap the 2.0 hull can pivot out
    // of (a 2.0 hull in a 2.0 continuous slot can't turn at all).
    for ( int y = 13; y <= 14; y++ ) {
        world.set_loaded_block( glm::ivec3( 30, y, 48 ), BlockState{ OAK_FENCE, 0, 0, OAK_FENCE, 0 } );
        world.set_loaded_block( glm::ivec3( 33, y, 48 ), BlockState{ OAK_FENCE, 0, 0, OAK_FENCE, 0 } );
    }
    wedge_escape( "between pillars", glm::dvec3( 32.0, float_y, 48.2 ), 90.0f );
    // (c) bow against pond_low's north wall at 30 deg.
    wedge_escape( "bow at 30 deg", glm::dvec3( 21.0, float_y, 16.0 + 0.62 ), 30.0f );

    // Fuzz: 60 s of seeded random inputs in the pillar/diagonal pond;
    // never penetrate, and a periodic reverse+turn probe always moves.
    {
        // A couple of extra pillar columns in pond_diag for the fuzz.
        for ( int y = 11; y <= 14; y++ ) {
            world.set_loaded_block( glm::ivec3( 33, y, 48 ), stone );
            world.set_loaded_block( glm::ivec3( 36, y, 51 ), stone );
        }
        const uint32_t id = vehicles.spawn( VEHICLE_OAK_BOAT, glm::dvec3( 35.5, float_y, 46.2 ), 0.0f );
        in.rider_vehicle = id;
        unsigned rng = 0xC0FFEE;
        auto next_input = [ & ]( ) {
            rng = rng * 1664525u + 1013904223u;
            in.rider_forward = static_cast<double>( int( rng % 3 ) - 1 );
            rng = rng * 1664525u + 1013904223u;
            in.rider_turn = static_cast<double>( int( rng % 3 ) - 1 );
        };
        double worst = 0.0;
        bool probes_ok = true;
        next_input( );
        for ( int i = 0; i < 60 * static_cast<int>( UPS_RATE ); i++ ) {
            if ( i % ( 5 * static_cast<int>( UPS_RATE ) ) == 0 && i > 0 ) {
                // Every 5 s a "S + A for 3 s" probe must move or rotate.
                const glm::dvec3 p0 = only_boat( ).pos;
                const float y0 = only_boat( ).yaw;
                const double pf = in.rider_forward, pt = in.rider_turn;
                in.rider_forward = -1.0;
                in.rider_turn = 1.0;
                for ( int k = 0; k < 3 * static_cast<int>( UPS_RATE ); k++ ) {
                    tick( 1 );
                    worst = std::max( worst, rect_pen( only_boat( ) ) );
                }
                probes_ok &= glm::length( only_boat( ).pos - p0 ) > 0.05 ||
                             fabs( vehicle_wrap_180( only_boat( ).yaw - y0 ) ) > 3.0;
                in.rider_forward = pf;
                in.rider_turn = pt;
                next_input( );
                continue;
            }
            if ( i % 40 == 0 ) {
                next_input( );
            }
            tick( 1 );
            worst = std::max( worst, rect_pen( only_boat( ) ) );
        }
        check_val( "boat fuzz: random driving never penetrates", worst <= 0.01, "pen=%.4f", worst );
        check_bool( "boat fuzz: reverse+turn probe always frees", probes_ok );
        in.rider_forward = 0.0;
        in.rider_turn = 0.0;
        in.rider_vehicle = 0;
        vehicles.test_vehicles( ).clear( );
    }

    // A 2-wide canal is still navigable at yaw 0 (hull width 1.25 fits).
    {
        for ( int z = 30; z <= 50; z++ ) {
            for ( int y = 11; y <= 13; y++ ) {
                world.set_loaded_block( glm::ivec3( 52, y, z ), water );
                world.set_loaded_block( glm::ivec3( 53, y, z ), water );
            }
            for ( int y = 11; y <= 14; y++ ) {
                world.set_loaded_block( glm::ivec3( 51, y, z ), stone );
                world.set_loaded_block( glm::ivec3( 54, y, z ), stone );
            }
        }
        for ( int x = 51; x <= 54; x++ ) {
            for ( int y = 11; y <= 14; y++ ) {
                world.set_loaded_block( glm::ivec3( x, y, 29 ), stone );
                world.set_loaded_block( glm::ivec3( x, y, 51 ), stone );
            }
        }
        const uint32_t id = vehicles.spawn( VEHICLE_OAK_BOAT, glm::dvec3( 53.0, float_y, 49.0 ), 0.0f );
        in.rider_vehicle = id;
        in.rider_forward = 1.0;
        double worst = 0.0;
        for ( int i = 0; i < 6 * static_cast<int>( UPS_RATE ); i++ ) {
            tick( 1 );
            worst = std::max( worst, rect_pen( only_boat( ) ) );
        }
        check_val( "boat: 2-wide canal navigable end to end", only_boat( ).pos.z < 33.0 && worst <= 0.01, "z=%.2f pen=%.4f",
                   only_boat( ).pos.z, worst );
        in.rider_forward = 0.0;
        in.rider_vehicle = 0;
        vehicles.test_vehicles( ).clear( );
    }

    // 2-wide channel turnaround: every compound-hull corner is inside
    // radius ~0.985 of centre, so the union spins in place in the same
    // canal (open span x in [52,54]) with zero penetration.
    const glm::dvec3 chan_mid( 53.0, float_y, 40.0 );
    auto channel_spin = [ & ]( const char *tag, const double t ) {
        vehicles.spawn( VEHICLE_OAK_BOAT, chan_mid, 0.0f );
        in.rider_vehicle = only_boat( ).id;
        in.rider_turn = t;
        double turned = 0.0, worst = 0.0;
        float py = only_boat( ).yaw;
        int i = 0;
        for ( ; i < 4 * static_cast<int>( UPS_RATE ); i++ ) {
            tick( 1 );
            turned += vehicle_wrap_180( only_boat( ).yaw - py );
            py = only_boat( ).yaw;
            worst = std::max( worst, rect_pen( only_boat( ) ) );
            if ( fabs( turned ) >= 178.0 ) {
                break;
            }
        }
        char name[ 96 ];
        snprintf( name, sizeof( name ), "boat channel: %s turns around", tag );
        check_val( name, fabs( turned ) >= 178.0 && worst <= 0.01, "turned=%.1f pen=%.4f", turned, worst );
        in.rider_turn = 0.0;
        in.rider_vehicle = 0;
        vehicles.test_vehicles( ).clear( );
    };
    channel_spin( "D", 1.0 );
    channel_spin( "A", -1.0 );

    // Turnaround while paddling: W+D must turn fully around and move.
    {
        vehicles.spawn( VEHICLE_OAK_BOAT, chan_mid, 0.0f );
        in.rider_vehicle = only_boat( ).id;
        in.rider_turn = 1.0;
        in.rider_forward = 1.0;
        float py = only_boat( ).yaw;
        double turned = 0.0, path = 0.0, worst = 0.0;
        glm::dvec3 prev = only_boat( ).pos;
        for ( int i = 0; i < 6 * static_cast<int>( UPS_RATE ); i++ ) {
            tick( 1 );
            turned += vehicle_wrap_180( only_boat( ).yaw - py );
            py = only_boat( ).yaw;
            path += glm::length( only_boat( ).pos - prev );
            prev = only_boat( ).pos;
            worst = std::max( worst, rect_pen( only_boat( ) ) );
        }
        check_val( "boat channel: W+D turns around and moves", fabs( turned ) >= 178.0 && path > 0.5 && worst <= 0.01,
                   "turned=%.1f path=%.2f pen=%.4f", turned, path );
        in.rider_turn = 0.0;
        in.rider_forward = 0.0;
        in.rider_vehicle = 0;
        vehicles.test_vehicles( ).clear( );
    }

    // Never stuck: starting yaws every 15 deg — W, S or D must produce
    // motion along the channel or a turn within 2 s.
    for ( float yaw = 15.0f; yaw <= 165.0f; yaw += 15.0f ) {
        bool freed = false;
        char name[ 96 ];
        for ( int attempt = 0; attempt < 3 && !freed; attempt++ ) {
            vehicles.spawn( VEHICLE_OAK_BOAT, chan_mid, yaw );
            in.rider_vehicle = only_boat( ).id;
            tick( static_cast<int>( UPS_RATE ) / 2 ); // settle
            const glm::dvec3 pos0 = only_boat( ).pos;
            const float yaw0 = only_boat( ).yaw;
            if ( attempt == 0 ) {
                in.rider_forward = 1.0;
            } else if ( attempt == 1 ) {
                in.rider_forward = -1.0;
            } else {
                in.rider_turn = 1.0;
            }
            for ( int i = 0; i < 2 * static_cast<int>( UPS_RATE ); i++ ) {
                tick( 1 );
            }
            const double dz = fabs( only_boat( ).pos.z - pos0.z );
            const double turned = fabs( vehicle_wrap_180( only_boat( ).yaw - yaw0 ) );
            freed = dz >= 0.3 || turned >= 20.0;
            in.rider_forward = 0.0;
            in.rider_turn = 0.0;
            in.rider_vehicle = 0;
            vehicles.test_vehicles( ).clear( );
        }
        snprintf( name, sizeof( name ), "boat channel: yaw=%.0f escapes", yaw );
        check_bool( name, freed );
    }

    // Visual clipping sanity: the rendered 2.0x1.25 hull sticks out past
    // the compound collision rects at the bow/stern, so driving into a
    // wall shows a small overlap of the *visible* hull — measured and
    // bounded, not required to be zero. Diagonal contact is bounded
    // looser: the render corner's support toward the wall is 1.149 while
    // any spin-in-2-wide union (corner radius < 1) covers at most ~0.98,
    // so ~0.18 is the geometric floor.
    {
        double worst_bow = 0.0, worst_stern = 0.0, worst_diag = 0.0;
        // Bow-first into the z=29 end wall (yaw 0 drives -z).
        vehicles.spawn( VEHICLE_OAK_BOAT, chan_mid, 0.0f );
        in.rider_vehicle = only_boat( ).id;
        in.rider_forward = 1.0;
        for ( int i = 0; i < 3 * static_cast<int>( UPS_RATE ); i++ ) {
            tick( 1 );
            worst_bow = std::max( worst_bow, render_pen( only_boat( ) ) );
        }
        // Stern-first into the z=51 end wall.
        in.rider_forward = -1.0;
        for ( int i = 0; i < 4 * static_cast<int>( UPS_RATE ); i++ ) {
            tick( 1 );
            worst_stern = std::max( worst_stern, render_pen( only_boat( ) ) );
        }
        in.rider_forward = 0.0;
        in.rider_vehicle = 0;
        vehicles.test_vehicles( ).clear( );
        // 45 degrees into the side wall.
        vehicles.spawn( VEHICLE_OAK_BOAT, glm::dvec3( 53.0, float_y, 40.0 ), 135.0f );
        in.rider_vehicle = only_boat( ).id;
        in.rider_forward = 1.0;
        for ( int i = 0; i < 3 * static_cast<int>( UPS_RATE ); i++ ) {
            tick( 1 );
            worst_diag = std::max( worst_diag, render_pen( only_boat( ) ) );
        }
        in.rider_forward = 0.0;
        in.rider_vehicle = 0;
        vehicles.test_vehicles( ).clear( );
        check_val( "boat: render-hull clip bow / stern",
                   worst_bow < 0.15 && worst_stern < 0.15, "bow=%.3f stern=%.3f",
                   worst_bow, worst_stern );
        check_val( "boat: render-hull clip diag", worst_diag < 0.2, "diag=%.3f", worst_diag );
    }

    // A boat still places into a 2x3 pool.
    {
        for ( int x = 56; x <= 57; x++ ) {
            for ( int z = 20; z <= 22; z++ ) {
                for ( int y = 11; y <= 13; y++ ) {
                    world.set_loaded_block( glm::ivec3( x, y, z ), water );
                }
            }
        }
        for ( int x = 55; x <= 58; x++ ) {
            for ( int z = 19; z <= 23; z++ ) {
                const bool rim = x == 55 || x == 58 || z == 19 || z == 23;
                for ( int y = 11; y <= 14 && rim; y++ ) {
                    world.set_loaded_block( glm::ivec3( x, y, z ), stone );
                }
            }
        }
        test_setup_inventories( gs );
        gs.game_mode = GameMode_Survival;
        gs.hotbar.addItemWithQuantity( OAK_BOAT, 1 );
        gs.camera.angle_H = 0.0f;
        gs.block_selection.floatSelectionInBounds = 1;
        gs.block_selection.pos_destroy_water = glm::ivec3( 56, 13, 21 );
        gs.block_selection.selectionInBounds = 0;
        check_bool( "boat: places into a 2x3 pool",
                    repgame_vehicle_place( gs, vehicle_type_for_item( gs.hotbar.getSelectedItem( ) ) ) &&
                        vehicles.count( ) == 1 );
        tick( 2 * static_cast<int>( UPS_RATE ) );
        check_val( "boat: 2x3 pool boat does not sink deeper",
                   rect_pen( only_boat( ) ) <= 0.2 || vehicles.count( ) == 0, "pen=%.4f",
                   vehicles.count( ) ? rect_pen( only_boat( ) ) : 0.0 );
        vehicles.test_vehicles( ).clear( );
    }

    // Bug-2 regression: vehicle-vehicle separation must never embed a hull
    // in terrain (previously a shoved boat ended up inside the wall, after
    // which the face-crossing clamps let it sail through land).
    {
        for ( int x = 56; x <= 60; x++ ) {
            for ( int z = 40; z <= 44; z++ ) {
                for ( int y = 11; y <= 13; y++ ) {
                    world.set_loaded_block( glm::ivec3( x, y, z ), water );
                }
            }
        }
        for ( int x = 55; x <= 61; x++ ) {
            for ( int z = 39; z <= 46; z++ ) {
                const bool rim = x == 55 || x == 61 || z == 39 || z == 45 || z == 46;
                for ( int y = 11; y <= 14 && rim; y++ ) {
                    world.set_loaded_block( glm::ivec3( x, y, z ), stone );
                }
            }
        }
        vehicles.spawn( VEHICLE_OAK_BOAT, glm::dvec3( 58.5, float_y, 43.4 ), 0.0f );
        vehicles.spawn( VEHICLE_SPRUCE_BOAT, glm::dvec3( 58.5, float_y, 44.0 ), 0.0f );
        double worst = 0.0;
        for ( int i = 0; i < 3 * static_cast<int>( UPS_RATE ); i++ ) {
            tick( 1 );
            for ( const Vehicles::Vehicle &v : vehicles.test_vehicles( ) ) {
                worst = std::max( worst, rect_pen( v ) );
            }
        }
        check_val( "boat: pair separation never embeds a hull", worst <= 0.01, "pen=%.4f", worst );
        vehicles.test_vehicles( ).clear( );
    }

    return failures;
}
