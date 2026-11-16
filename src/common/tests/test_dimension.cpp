// Dimension band encoding (dimension.hpp), nether generator invariants, and
// the portal frame detection/travel helpers. The band layout lives in
// dimension.hpp's header comment.
#include "test_util.hpp"
#include "common/dimension.hpp"
#include "common/dimension_profile.hpp"
#include "common/map_gen_nether.hpp"
#include "common/portal.hpp"

int test_dimension( ) {
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

    // Marks a torus slot live at chunk_pos. cells == nullptr reads as
    // uniform air; alloc_cells=1 gives it a writable cell array.
    auto make_chunk_live = [ & ]( World &world, const glm::ivec3 &cp, int alloc_cells ) {
        ChunkLoader &cl = world.chunkLoader;
        Chunk &chunk = cl.chunkArray[ chunk_slot_from_pos( cp ) ];
        chunk.chunk_pos = cp;
        chunk.chunk_mod = cp;
        chunk.load_state = CHUNK_LIVE;
        if ( alloc_cells && chunk.cells == nullptr ) {
            chunk.cells = chunk_alloc_cells( );
        }
    };
    // Marks CHUNK_LIVE every chunk overlapping the pending-travel search
    // box (src_center +- PORTAL_SEARCH_R on x/z, the dim band's full y
    // span) inside the active window — portal_destination_ready's whole
    // requirement. `skip` leaves one chunk non-live; returns the count.
    auto live_search_box = [ & ]( World &world, const glm::ivec3 &src_center, DimensionId dim, const glm::ivec3 *skip ) {
        ChunkLoader &cl = world.chunkLoader;
        const int y_min = dim == DIM_NETHER ? DIM_BY_MIN_N : DIM_BY_MIN_O;
        const int y_max = dim == DIM_NETHER ? DIM_BY_MAX_N : DIM_BY_MAX_O;
        const glm::ivec3 cs( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
        const glm::ivec3 cmin = glm::floor( glm::vec3( src_center.x - PORTAL_SEARCH_R, y_min, src_center.z - PORTAL_SEARCH_R ) / glm::vec3( cs ) );
        const glm::ivec3 cmax = glm::floor( glm::vec3( src_center.x + PORTAL_SEARCH_R, y_max, src_center.z + PORTAL_SEARCH_R ) / glm::vec3( cs ) );
        int n = 0;
        for ( int cy = cmin.y; cy <= cmax.y; cy++ ) {
            for ( int cx = cmin.x; cx <= cmax.x; cx++ ) {
                for ( int cz = cmin.z; cz <= cmax.z; cz++ ) {
                    const glm::ivec3 cp( cx, cy, cz );
                    if ( !cl.in_active_window( cp ) || ( skip != nullptr && *skip == cp ) ) {
                        continue;
                    }
                    make_chunk_live( world, cp, 0 );
                    n++;
                }
            }
        }
        return n;
    };
    // A pending travel aimed at `dim`, held at hold_feet with the search
    // centred on src_center — the state portal_travel leaves behind.
    auto stage_pending = [ & ]( RepGameState &gs, DimensionId dim, const glm::dvec3 &hold_feet, const glm::ivec3 &src_center ) {
        PortalState &p = gs.portal;
        p.must_exit = 1;
        p.pending_build = 1;
        p.pending_axis = 0;
        p.pending_w = 2;
        p.pending_h = 3;
        p.pending_dim = dim;
        p.hold_feet = hold_feet;
        p.src_center = src_center;
        p.pending_start_tick = gs.tick_number;
        p.pending_diff_snap = 0;
        p.pending_wait_logged = 0;
    };
    auto feet_of = [ & ]( const RepGameState &gs ) {
        return glm::dvec3( gs.camera.pos.x, gs.camera.pos.y - gs.camera.eye_height, gs.camera.pos.z );
    };

    // ---- Band edges: chunk y ----
    check_bool( "cy 0 is overworld", dimension_of_chunk( 0 ) == DIM_OVERWORLD );
    check_bool( "cy -32 is overworld", dimension_of_chunk( -32 ) == DIM_OVERWORLD );
    check_bool( "cy 31 is overworld", dimension_of_chunk( 31 ) == DIM_OVERWORLD );
    check_bool( "cy -33 is nether", dimension_of_chunk( -33 ) == DIM_NETHER );
    check_bool( "cy -96 is nether", dimension_of_chunk( -96 ) == DIM_NETHER );
    check_bool( "cy 32 is spare 2", dimension_of_chunk( 32 ) == DIM_SPARE_2 );
    check_bool( "cy 95 is spare 2", dimension_of_chunk( 95 ) == DIM_SPARE_2 );
    check_bool( "cy 96 is spare 3", dimension_of_chunk( 96 ) == DIM_SPARE_3 );
    check_bool( "cy 127 is spare 3", dimension_of_chunk( 127 ) == DIM_SPARE_3 );
    check_bool( "cy -97 is spare 3 (wrap)", dimension_of_chunk( -97 ) == DIM_SPARE_3 );
    check_bool( "cy -128 is spare 3 (wrap)", dimension_of_chunk( -128 ) == DIM_SPARE_3 );

    // ---- Band edges: block y ----
    check_bool( "by -1984 is overworld", dimension_of_block_y( DIM_BY_MIN_O ) == DIM_OVERWORLD );
    check_bool( "by 1983 is overworld", dimension_of_block_y( DIM_BY_MAX_O ) == DIM_OVERWORLD );
    check_bool( "by -1985 is nether", dimension_of_block_y( DIM_BY_MAX_N ) == DIM_NETHER );
    check_bool( "by -5952 is nether", dimension_of_block_y( DIM_BY_MIN_N ) == DIM_NETHER );
    check_bool( "by 1984 is spare 2", dimension_of_block_y( DIM_BY_MIN_2 ) == DIM_SPARE_2 );
    check_bool( "by -1984/-1985 bands touch", DIM_BY_MAX_N + 1 == DIM_BY_MIN_O );

    // ---- local/world round-trips at the edges ----
    bool rt_ok = true;
    for ( int dim = 0; dim < DIM_COUNT; dim++ ) {
        for ( const int ly : { 0, 1, DIM_BLOCK_SPAN / 2, DIM_BLOCK_SPAN - 1 } ) {
            const int by = dim_world_block_y( static_cast<DimensionId>( dim ), ly );
            if ( dimension_of_block_y( by ) != dim || dim_local_block_y( by ) != ly ) {
                rt_ok = false;
            }
        }
        for ( const int lcy : { 0, 1, DIM_CHUNK_SPAN - 1 } ) {
            const int cy = dim_world_chunk_y( static_cast<DimensionId>( dim ), lcy );
            if ( dimension_of_chunk( cy ) != dim || dim_local_chunk_y( cy ) != lcy ) {
                rt_ok = false;
            }
        }
    }
    check_bool( "local<->world y round-trips all bands", rt_ok );
    check_bool( "nether local 0 is the band floor", dim_world_block_y( DIM_NETHER, 0 ) == DIM_BY_MIN_N );
    check_bool( "overworld local of y=0 is half span", dim_local_block_y( 0 ) == 1984 );

    // ---- Natural-mob ids stay unique + invertible across bands ----
    {
        const glm::ivec3 over_chunk( 100, -1, -77 ), nether_chunk( 100, -90, -77 );
        const NetEntityId a = net_natural_mob_id( over_chunk, 5 );
        const NetEntityId b = net_natural_mob_id( nether_chunk, 5 );
        check_bool( "same x/z different band -> different ids", a != b );
        check_bool( "overworld home inverts", net_natural_mob_home( a ) == over_chunk );
        check_bool( "nether home inverts (negative cy)", net_natural_mob_home( b ) == nether_chunk );
        check_bool( "ids are natural", net_is_natural_mob_id( a ) && net_is_natural_mob_id( b ) );
    }

    // ---- Dimension profiles ----
    check_bool( "overworld has sky", dimension_has_sky( DIM_OVERWORLD ) );
    check_bool( "nether has no sky", !dimension_has_sky( DIM_NETHER ) );
    check_bool( "nether forbids water", !dimension_profile( DIM_NETHER )->water_allowed );
    check_bool( "nether beds explode", dimension_profile( DIM_NETHER )->beds_explode );
    check_bool( "nether has an inherent ambient floor", dimension_profile( DIM_NETHER )->min_ambient > 0.0f );
    check_bool( "overworld ambient is user-only", dimension_profile( DIM_OVERWORLD )->min_ambient == 0.0f );

    // ---- Nether generator (MapGen::gen_block_id dispatches on the band) ----
    {
        int netherrack = 0, lava = 0, air = 0, glowstone = 0, quartz = 0, magma = 0, forbidden = 0;
        int floor_gaps = 0, ceiling_gaps = 0;
        for ( int x = -128; x < 128; x += 2 ) {
            for ( int z = -128; z < 128; z += 2 ) {
                for ( int y = DIM_BY_MIN_N; y <= DIM_BY_MAX_N; y += 7 ) {
                    const BlockID b = MapGen::gen_block_id( x, y, z );
                    switch ( b ) {
                        case NETHERRACK:
                        case SOUL_SAND:
                        case SOUL_SOIL:
                        case BASALT:
                        case BLACKSTONE:
                        case CRIMSON_NYLIUM:
                        case WARPED_NYLIUM:
                            netherrack++;
                            break;
                        case LAVA:
                            lava++;
                            break;
                        case AIR:
                            air++;
                            break;
                        case GLOWSTONE:
                            glowstone++;
                            break;
                        case QUARTZ_ORE:
                        case NETHER_GOLD_ORE:
                        case ANCIENT_DEBRIS:
                            quartz++;
                            break;
                        case MAGMA:
                            magma++;
                            break;
                        case BEDROCK:
                            break;
                        default:
                            forbidden++; // overworld-only blocks must never leak in
                            break;
                    }
                }
                // Band floor (ly 0..1) and ceiling (top 2 rows) are always
                // bedrock by construction.
                for ( int ly = 0; ly <= 1; ly++ ) {
                    floor_gaps += MapGen::gen_block_id( x, DIM_BY_MIN_N + ly, z ) != BEDROCK;
                }
                for ( int ly = DIM_BLOCK_SPAN - 2; ly < DIM_BLOCK_SPAN; ly++ ) {
                    ceiling_gaps += MapGen::gen_block_id( x, DIM_BY_MIN_N + ly, z ) != BEDROCK;
                }
            }
        }
        check_bool( "nether floor is bedrock", floor_gaps == 0 );
        check_bool( "nether ceiling is bedrock", ceiling_gaps == 0 );
        check_bool( "netherrack family generates", netherrack > 1000 );
        check_bool( "nether caverns are open", air > 1000 );
        check_bool( "nether lava sea exists", lava > 100 );
        check_bool( "nether ores generate", quartz > 10 );
        check_bool( "nether magma generates", magma > 10 );
        check_bool( "no overworld blocks in nether", forbidden == 0 );
        // glowstone is a nice-to-have, not guaranteed by density.
        (void)glowstone;
    }

    // ---- Bounded roof: past MGN_ROOF_THICK over the highest possible
    // roof_base (+jitter) the band is solid bedrock filler — no caves. ----
    {
        int filler_gaps = 0;
        // roof_base max is MGN_ROOF_MID+~25; cap = that + MGN_ROOF_THICK +
        // 4 jitter ≈ 302. Sample above it with margin.
        for ( int x = -96; x < 96; x += 3 ) {
            for ( int z = -96; z < 96; z += 3 ) {
                for ( int ly = static_cast<int>( MGN_ROOF_MID + MGN_ROOF_THICK + 40 ); ly < DIM_BLOCK_SPAN - 1; ly += 61 ) {
                    filler_gaps += MapGen::gen_block_id( x, DIM_BY_MIN_N + ly, z ) != BEDROCK;
                }
            }
        }
        check_bool( "nether roof is capped by solid bedrock", filler_gaps == 0 );
    }

    // ---- Roof skin: carving stops short of the cap, so no AIR cell may sit
    // directly under BEDROCK in the roof region (bare-bedrock patches). ----
    {
        int exposed = 0;
        for ( int x = -96; x < 96; x += 3 ) {
            for ( int z = -96; z < 96; z += 3 ) {
                for ( int ly = static_cast<int>( MGN_ROOF_MID - 26 ); ly < static_cast<int>( MGN_ROOF_MID + MGN_ROOF_THICK + 34 ); ly++ ) {
                    const int y = DIM_BY_MIN_N + ly;
                    exposed += MapGen::gen_block_id( x, y, z ) == AIR && MapGen::gen_block_id( x, y + 1, z ) == BEDROCK;
                }
            }
        }
        check_bool( "bedrock cap is never exposed to air", exposed == 0 );
    }

    // ---- Pillars: deformed floor/roof masses fuse across the cavern gap.
    // Cave worms pocket the column, so assert majority-solid over the raw
    // gap span rather than fully airtight. ----
    {
        int pillar_cols = 0;
        for ( int x = -192; x < 192; x += 2 ) {
            for ( int z = -192; z < 192; z += 2 ) {
                const float fh = mgn_floor_height( x, z );
                const float rb = mgn_roof_base( x, z );
                int total = 0, open = 0;
                for ( float ly = fh + 8.0f; ly < rb - 8.0f; ly += 5.0f ) {
                    total++;
                    open += MapGen::gen_block_id( x, DIM_BY_MIN_N + static_cast<int>( ly ), z ) == AIR;
                }
                pillar_cols += total > 0 && open * 2 < total;
            }
        }
        check_bool( "nether pillars connect floor to roof", pillar_cols > 100 );
    }

    // ---- Ledges: mid-cavern cells solid with air directly above and below
    // are floating shelf rock, distinct from fused pillar mass. ----
    {
        int ledge_cols = 0;
        for ( int x = -192; x < 192; x += 2 ) {
            for ( int z = -192; z < 192; z += 2 ) {
                const float fh = mgn_floor_height( x, z );
                const float rb = mgn_roof_base( x, z );
                for ( float ly = fh + 12.0f; ly < rb - 12.0f; ly += 3.0f ) {
                    const int y = DIM_BY_MIN_N + static_cast<int>( ly );
                    if ( MapGen::gen_block_id( x, y, z ) != AIR &&
                         MapGen::gen_block_id( x, y - 4, z ) == AIR &&
                         MapGen::gen_block_id( x, y + 4, z ) == AIR ) {
                        ledge_cols++;
                        break;
                    }
                }
            }
        }
        check_bool( "nether has floating mid-cavern ledges", ledge_cols > 400 );
    }

    // ---- Overworld band still generates overworld terrain ----
    {
        int overworld_blocks = 0, nether_leaks = 0;
        for ( int x = -64; x < 64; x += 4 ) {
            for ( int z = -64; z < 64; z += 4 ) {
                for ( int y = -105; y < 60; y += 5 ) {
                    const BlockID b = MapGen::gen_block_id( x, y, z );
                    if ( b == GRASS || b == STONE || b == DIRT || b == WATER || b == SAND || b == BEDROCK ) {
                        overworld_blocks++;
                    }
                    if ( b == NETHERRACK || b == SOUL_SAND || b == CRIMSON_NYLIUM ) {
                        nether_leaks++;
                    }
                }
            }
        }
        check_bool( "overworld terrain still generates", overworld_blocks > 100 );
        check_bool( "no nether blocks in overworld", nether_leaks == 0 );
    }

    // ---- Portal frame detection on a hand-built ring ----
    {
        RepGameState &gs = *new RepGameState( );
        gs.tick_number = 0;
        gs.camera.pos = glm::dvec3( 0, 0, 0 );
        test_setup_world( gs.world );
        World &world = gs.world;
        // 4x5 obsidian ring in the x-y plane at (10..13, 10..14, 10):
        // interior (11..12, 11..13).
        const glm::ivec3 o( 10, 10, 10 );
        for ( int dx = 0; dx < 4; dx++ ) {
            for ( int dy = 0; dy < 5; dy++ ) {
                const bool edge = dx == 0 || dx == 3 || dy == 0 || dy == 4;
                if ( edge ) {
                    world.set_loaded_block( o + glm::ivec3( dx, dy, 0 ), { OBSIDIAN, 0, 0, OBSIDIAN, 0 } );
                }
            }
        }
        PortalFrame frame;
        check_bool( "portal frame detected", portal_find_frame( world, glm::ivec3( 11, 11, 10 ), frame ) );
        check_bool( "frame interior is 2x3 in x", frame.axis == 0 && frame.lo == glm::ivec3( 11, 11, 10 ) && frame.hi == glm::ivec3( 12, 13, 10 ) );
        check_bool( "no frame at a plain air cell", !portal_find_frame( world, glm::ivec3( 20, 20, 20 ), frame ) );
        check_bool( "ignite fills interior with PORTAL", portal_ignite( gs, glm::ivec3( 11, 11, 10 ) ) );
        test_run_game_ticks( gs, 2 );
        check_bool( "interior holds portal blocks", world.get_loaded_block( glm::ivec3( 11, 12, 10 ) ).id == PORTAL );
        // The survive rule: a full frame's cells survive; breaking a frame
        // block pops the adjacent portal cell.
        const BlockState portal_cell = world.get_loaded_block( glm::ivec3( 11, 12, 10 ) );
        check_bool( "portal cell survives in ring", portal_cell_survives( world, glm::ivec3( 11, 12, 10 ), portal_cell ) );
        world.set_loaded_block( glm::ivec3( 10, 12, 10 ), BLOCK_STATE_AIR );
        check_bool( "broken ring kills portal cell", !portal_cell_survives( world, glm::ivec3( 11, 12, 10 ), portal_cell ) );
    }

    // ---- z-facing frames: detection, ignite, build/carve sizing ----
    {
        RepGameState &gs = *new RepGameState( );
        gs.tick_number = 0;
        gs.camera.pos = glm::dvec3( 0, 0, 0 );
        test_setup_world( gs.world );
        World &world = gs.world;
        // Obsidian ring in the y-z plane at x=4: ring z 4..8, y 8..14 —
        // interior (x 4, y 9..13, z 5..7), 3 wide x 5 tall.
        for ( int dz = 4; dz <= 8; dz++ ) {
            for ( int dy = 8; dy <= 14; dy++ ) {
                const bool edge = dz == 4 || dz == 8 || dy == 8 || dy == 14;
                if ( edge ) {
                    world.set_loaded_block( glm::ivec3( 4, dy, dz ), { OBSIDIAN, 0, 0, OBSIDIAN, 0 } );
                }
            }
        }
        PortalFrame frame;
        check_bool( "z-axis frame detected", portal_find_frame( world, glm::ivec3( 4, 10, 6 ), frame ) );
        check_bool( "frame spans z, interior 3x5", frame.axis == 1 && frame.lo == glm::ivec3( 4, 9, 5 ) && frame.hi == glm::ivec3( 4, 13, 7 ) );
        check_bool( "z-axis ignite fills interior", portal_ignite( gs, glm::ivec3( 4, 10, 6 ) ) );
        const BlockState lit = world.get_loaded_block( glm::ivec3( 4, 10, 6 ) );
        check_bool( "lit cell is PORTAL axis 1", lit.id == PORTAL && lit.rotation == 1 );
        // Travel seeds find_frame on a lit cell — it must still report the ring.
        check_bool( "lit interior still detects frame", portal_find_frame( world, glm::ivec3( 4, 11, 7 ), frame ) && frame.axis == 1 );

        // portal_carve_pocket clears rock across the frame's footprint plus
        // 3 deep on each open face (x 17..23 here), sparing obsidian. It
        // runs before the build, same as the arrival path.
        world.set_loaded_block( glm::ivec3( 23, 10, 11 ), { STONE, 0, 0, STONE, 0 } );
        world.set_loaded_block( glm::ivec3( 23, 10, 12 ), { OBSIDIAN, 0, 0, OBSIDIAN, 0 } );
        world.set_loaded_block( glm::ivec3( 23, 11, 10 ), { PORTAL, 1, 0, PORTAL, 0 } );
        world.set_loaded_block( glm::ivec3( 17, 12, 12 ), { BEDROCK, 0, 0, BEDROCK, 0 } );
        portal_carve_pocket( gs, glm::ivec3( 20, 8, 10 ), 1, 3, 5 );
        failures += check_id( world, "carve clears stone", glm::ivec3( 23, 10, 11 ), AIR );
        failures += check_id( world, "carve keeps obsidian", glm::ivec3( 23, 10, 12 ), OBSIDIAN );
        failures += check_id( world, "carve keeps portal", glm::ivec3( 23, 11, 10 ), PORTAL );
        failures += check_id( world, "carve keeps bedrock", glm::ivec3( 17, 12, 12 ), BEDROCK );

        // portal_build_frame honours axis + interior w/h (3x5 along z).
        portal_build_frame( gs, glm::ivec3( 20, 8, 10 ), 1, 3, 5 );
        failures += check_id( world, "built sill", glm::ivec3( 20, 8, 11 ), OBSIDIAN );
        failures += check_id( world, "built lintel", glm::ivec3( 20, 14, 11 ), OBSIDIAN );
        failures += check_id( world, "built side -z", glm::ivec3( 20, 10, 9 ), OBSIDIAN );
        failures += check_id( world, "built side +z", glm::ivec3( 20, 10, 13 ), OBSIDIAN );
        check_bool( "built interior is PORTAL", world.get_loaded_block( glm::ivec3( 20, 10, 10 ) ).id == PORTAL &&
                                                world.get_loaded_block( glm::ivec3( 20, 12, 12 ) ).id == PORTAL );
        check_bool( "built cells store axis", world.get_loaded_block( glm::ivec3( 20, 11, 11 ) ).rotation == 1 );

        // A found destination portal drops the player inside its plate, on
        // the sill row of the nearest interior column. The readiness gate
        // needs the loader window on the hold chunk plus every in-window
        // search-box chunk live.
        gs.camera.eye_height = PLAYER_EYE_HEIGHT;
        gs.world.chunkLoader.chunk_center = glm::ivec3( 0, 0, 0 );
        stage_pending( gs, DIM_OVERWORLD, glm::dvec3( 4.5, 10.4, 6.5 ), glm::ivec3( 4, 10, 6 ) );
        live_search_box( world, gs.portal.src_center, DIM_OVERWORLD, nullptr );
        repgame_portal_tick( gs );
        const glm::dvec3 feet = feet_of( gs );
        check_bool( "arrival inside the plate", floor( feet.x ) == 4 && floor( feet.z ) == 6 && feet.y == 9.0 );
        check_bool( "arrival keeps must_exit", gs.portal.must_exit == 1 );
        check_bool( "arrival cleared pending", gs.portal.pending_build == 0 );
    }

    // ---- Travel copies the source frame; must_exit survives unloaded cells ----
    {
        RepGameState &gs = *new RepGameState( );
        gs.tick_number = 0;
        test_setup_world( gs.world );
        World &world = gs.world;
        gs.camera.eye_height = PLAYER_EYE_HEIGHT;
        // Same 3x5 z-facing ring as above, ignited.
        for ( int dz = 4; dz <= 8; dz++ ) {
            for ( int dy = 8; dy <= 14; dy++ ) {
                const bool edge = dz == 4 || dz == 8 || dy == 8 || dy == 14;
                if ( edge ) {
                    world.set_loaded_block( glm::ivec3( 4, dy, dz ), { OBSIDIAN, 0, 0, OBSIDIAN, 0 } );
                }
            }
        }
        portal_ignite( gs, glm::ivec3( 4, 10, 6 ) );
        // Feet + body inside the lit interior (feet cell (4,10,6)).
        gs.camera.pos = glm::dvec3( 4.5, 10.4 + PLAYER_EYE_HEIGHT, 6.5 );
        for ( int i = 0; i < PORTAL_TRAVEL_TICKS + 2; i++ ) {
            repgame_portal_tick( gs );
        }
        check_bool( "dwell travelled to nether", dimension_of_pos( gs.camera.pos ) == DIM_NETHER );
        check_bool( "destination copies axis", gs.portal.pending_axis == 1 );
        check_bool( "destination copies size", gs.portal.pending_w == 3 && gs.portal.pending_h == 5 );
        check_bool( "arrival sets must_exit", gs.portal.must_exit == 1 && gs.portal.pending_build == 1 );
        // Slow-load regression: the drop cell's chunk isn't live yet (only
        // chunk 0 is loaded) — the tick must not clear must_exit or consume
        // the pending frame.
        repgame_portal_tick( gs );
        check_bool( "unloaded cells keep must_exit", gs.portal.must_exit == 1 );
        check_bool( "unloaded cells keep pending build", gs.portal.pending_build == 1 );
    }

    // ---- /dimension travel: same x/z, lands in the band ----
    {
        RepGameState &gs = *new RepGameState( );
        gs.tick_number = 0;
        gs.camera.pos = glm::dvec3( 8.5, 80.0, 8.5 );
        gs.camera.eye_height = PLAYER_EYE_HEIGHT;
        check_bool( "spare dimension refused", !portal_travel_to_dimension( gs, DIM_SPARE_2 ) );
        check_bool( "same-dimension refused", !portal_travel_to_dimension( gs, DIM_OVERWORLD ) );
        check_bool( "nether travel lands", portal_travel_to_dimension( gs, DIM_NETHER ) );
        const DimensionId now = dimension_of_pos( gs.camera.pos );
        check_bool( "player is in the nether band", now == DIM_NETHER );
        // Feet sit on generated ground (eye is eye_height above the floor).
        const int feet_y = static_cast<int>( floor( gs.camera.pos.y - gs.camera.eye_height ) );
        check_bool( "feet inside nether cavern band", feet_y > DIM_BY_MIN_N + 55 && feet_y < DIM_BY_MIN_N + 220 );
        check_bool( "x/z carried over", abs( int( gs.camera.pos.x ) - 8 ) <= 33 );
    }

    // ---- Pending-travel readiness gate + freeze ----
    // A search-box chunk inside the active window that isn't live yet holds
    // the resolve off; the player is re-snapped to the hold feet each tick.
    {
        RepGameState &gs = *new RepGameState( );
        gs.tick_number = 0;
        test_setup_world( gs.world );
        World &world = gs.world;
        gs.camera.eye_height = PLAYER_EYE_HEIGHT;
        gs.world.chunkLoader.chunk_center = glm::ivec3( 0, 0, 0 );
        // Ground under the hold point so the player has somewhere to stand.
        world.set_loaded_block( glm::ivec3( 10, 7, 10 ), { STONE, 0, 0, STONE, 0 } );
        stage_pending( gs, DIM_OVERWORLD, glm::dvec3( 10.5, 8.0, 10.5 ), glm::ivec3( 10, 10, 10 ) );
        // Every in-window box chunk live except (1,0,1).
        const glm::ivec3 missing( 1, 0, 1 );
        const int n_live = live_search_box( world, gs.portal.src_center, DIM_OVERWORLD, &missing );
        check_bool( "gate: search box covered by test", n_live >= 80 );
        // Drift the camera — the freeze must re-snap it back to the hold.
        gs.camera.pos = glm::dvec3( 30.0, 60.0, 30.0 );
        repgame_portal_tick( gs );
        const glm::dvec3 feet = feet_of( gs );
        check_bool( "gate: not live -> still pending", gs.portal.pending_build == 1 );
        check_bool( "gate: freeze re-snaps to hold feet", glm::distance( feet, glm::dvec3( 10.5, 8.0, 10.5 ) ) < 0.001 );
        repgame_portal_tick( gs );
        check_bool( "gate: still pending on second tick", gs.portal.pending_build == 1 );
        // The missing chunk goes live -> the next tick resolves (a build,
        // since no PORTAL cells exist anywhere).
        make_chunk_live( world, missing, 0 );
        repgame_portal_tick( gs );
        check_bool( "gate: resolves once box is live", gs.portal.pending_build == 0 );
    }

    // ---- Gate step 1: the loader window must reach the hold chunk ----
    {
        RepGameState &gs = *new RepGameState( );
        gs.tick_number = 0;
        test_setup_world( gs.world );
        World &world = gs.world;
        gs.camera.eye_height = PLAYER_EYE_HEIGHT;
        gs.world.chunkLoader.chunk_center = glm::ivec3( 0, 0, 0 );
        stage_pending( gs, DIM_OVERWORLD, glm::dvec3( 10.5, 8.0, 10.5 ), glm::ivec3( 10, 10, 10 ) );
        live_search_box( world, gs.portal.src_center, DIM_OVERWORLD, nullptr );
        // Window still centred elsewhere (right after a hop it holds the
        // old dimension's centre).
        gs.world.chunkLoader.chunk_center = glm::ivec3( 0, -60, 0 );
        repgame_portal_tick( gs );
        check_bool( "gate: wrong centre stays pending", gs.portal.pending_build == 1 );
        // The window catches up: resolve on the next tick.
        gs.world.chunkLoader.chunk_center = glm::ivec3( 0, 0, 0 );
        repgame_portal_tick( gs );
        check_bool( "gate: centre arrived -> resolves", gs.portal.pending_build == 0 );
    }

    // ---- Far existing portal is found (not re-built) ----
    // A lit frame ~50 blocks from src_center and >40 above the hold y —
    // the old ±48/±40 drop-centred box would have missed it.
    {
        RepGameState &gs = *new RepGameState( );
        gs.tick_number = 0;
        test_setup_world( gs.world );
        World &world = gs.world;
        gs.camera.eye_height = PLAYER_EYE_HEIGHT;
        gs.world.chunkLoader.chunk_center = glm::ivec3( 0, 0, 0 );
        // Lit 2x3 x-facing frame: ring x 53..56, y 55..59, z 10; interior
        // (54..55, 56..58).
        for ( int dx = 53; dx <= 56; dx++ ) {
            for ( int dy = 55; dy <= 59; dy++ ) {
                const bool edge = dx == 53 || dx == 56 || dy == 55 || dy == 59;
                world.set_loaded_block( glm::ivec3( dx, dy, 10 ),
                                        edge ? BlockState{ OBSIDIAN, 0, 0, OBSIDIAN, 0 } : BlockState{ PORTAL, 0, 0, PORTAL, 0 } );
            }
        }
        world.set_loaded_block( glm::ivec3( 10, 7, 10 ), { STONE, 0, 0, STONE, 0 } );
        stage_pending( gs, DIM_OVERWORLD, glm::dvec3( 10.5, 8.0, 10.5 ), glm::ivec3( 5, 10, 5 ) );
        live_search_box( world, gs.portal.src_center, DIM_OVERWORLD, nullptr );
        repgame_portal_tick( gs );
        const glm::dvec3 feet = feet_of( gs );
        check_bool( "far portal: resolved", gs.portal.pending_build == 0 );
        check_bool( "far portal: player inside its plate", floor( feet.z ) == 10 && feet.y == 56.0 &&
                                                           feet.x >= 54.0 && feet.x <= 56.0 );
        check_bool( "far portal: keeps must_exit", gs.portal.must_exit == 1 );
        check_bool( "far portal: ring still lit", world.get_loaded_block( glm::ivec3( 54, 57, 10 ) ).id == PORTAL );
        // Nothing built near the hold column.
        bool stray = false;
        for ( int dx = -2; dx <= 2 && !stray; dx++ ) {
            for ( int dy = 5; dy <= 12 && !stray; dy++ ) {
                for ( int dz = -2; dz <= 2 && !stray; dz++ ) {
                    const BlockID id = world.get_loaded_block( glm::ivec3( 10 + dx, dy, 10 + dz ) ).id;
                    stray = id == OBSIDIAN || id == PORTAL;
                }
            }
        }
        check_bool( "far portal: nothing built at the hold site", !stray );
    }

    // ---- Non-destructive build: an existing unlit ring vetoes the site ----
    {
        RepGameState &gs = *new RepGameState( );
        gs.tick_number = 0;
        test_setup_world( gs.world );
        World &world = gs.world;
        gs.camera.eye_height = PLAYER_EYE_HEIGHT;
        gs.world.chunkLoader.chunk_center = glm::ivec3( 0, 0, 0 );
        // Flat ground: stone floor at y=5 over the spiral's early columns.
        for ( int x = 0; x <= 30; x++ ) {
            for ( int z = 0; z <= 30; z++ ) {
                world.set_loaded_block( glm::ivec3( x, 5, z ), { STONE, 0, 0, STONE, 0 } );
            }
        }
        // An unlit obsidian ring sitting on the first site the spiral tries.
        for ( int dx = 9; dx <= 12; dx++ ) {
            world.set_loaded_block( glm::ivec3( dx, 6, 10 ), { OBSIDIAN, 0, 0, OBSIDIAN, 0 } );
            world.set_loaded_block( glm::ivec3( dx, 9, 10 ), { OBSIDIAN, 0, 0, OBSIDIAN, 0 } );
        }
        for ( int dy = 6; dy <= 9; dy++ ) {
            world.set_loaded_block( glm::ivec3( 9, dy, 10 ), { OBSIDIAN, 0, 0, OBSIDIAN, 0 } );
            world.set_loaded_block( glm::ivec3( 12, dy, 10 ), { OBSIDIAN, 0, 0, OBSIDIAN, 0 } );
        }
        stage_pending( gs, DIM_OVERWORLD, glm::dvec3( 10.5, 6.0, 10.5 ), glm::ivec3( 10, 10, 10 ) );
        live_search_box( world, gs.portal.src_center, DIM_OVERWORLD, nullptr );
        repgame_portal_tick( gs );
        check_bool( "veto: resolved", gs.portal.pending_build == 0 );
        check_bool( "veto: planted ring untouched", world.get_loaded_block( glm::ivec3( 10, 9, 10 ) ).id == OBSIDIAN &&
                                                    world.get_loaded_block( glm::ivec3( 9, 7, 10 ) ).id == OBSIDIAN &&
                                                    world.get_loaded_block( glm::ivec3( 12, 8, 10 ) ).id == OBSIDIAN );
        // The build landed elsewhere — a PORTAL cell exists, but not inside
        // the vetoed site column's clearance.
        bool built = false;
        for ( int x = 0; x <= 30 && !built; x++ ) {
            for ( int y = 5; y <= 15 && !built; y++ ) {
                for ( int z = 0; z <= 30 && !built; z++ ) {
                    built = world.get_loaded_block( glm::ivec3( x, y, z ) ).id == PORTAL;
                }
            }
        }
        check_bool( "veto: a frame was built off-site", built );
    }

    // ---- Window-edge site scan: unloaded rows don't veto the column ----
    // The overworld build-site scan range (300..-90) is wider than the
    // loaded chunk window — the gate passes because every in-window
    // search-box chunk is live, but most scan rows are still absent.
    // LAST_BLOCK_ID must skip the row, not poison the column, or every
    // overworld build falls through to the WATER_LEVEL+2 ledge.
    {
        RepGameState &gs = *new RepGameState( );
        gs.tick_number = 0;
        test_setup_world( gs.world );
        World &world = gs.world;
        gs.camera.eye_height = PLAYER_EYE_HEIGHT;
        gs.world.chunkLoader.chunk_center = glm::ivec3( 0, 0, 0 );
        // One loaded surface: stone floor at y=10 under a clear cavity.
        for ( int x = 6; x <= 16; x++ ) {
            for ( int z = 6; z <= 14; z++ ) {
                world.set_loaded_block( glm::ivec3( x, 10, z ), { STONE, 0, 0, STONE, 0 } );
            }
        }
        stage_pending( gs, DIM_OVERWORLD, glm::dvec3( 10.5, 11.0, 10.5 ), glm::ivec3( 10, 10, 10 ) );
        live_search_box( world, gs.portal.src_center, DIM_OVERWORLD, nullptr );
        repgame_portal_tick( gs );
        check_bool( "surface scan: resolved once box is live", gs.portal.pending_build == 0 );
        // Pass A must land on the loaded floor: sill row at y=10, portal
        // interior y 11..13 — NOT the y=2 ledge fallback.
        check_bool( "surface scan: sill on the loaded floor", world.get_loaded_block( glm::ivec3( 9, 10, 10 ) ).id == OBSIDIAN &&
                                                              world.get_loaded_block( glm::ivec3( 12, 10, 10 ) ).id == OBSIDIAN );
        check_bool( "surface scan: portal above the floor", world.get_loaded_block( glm::ivec3( 10, 12, 10 ) ).id == PORTAL &&
                                                            world.get_loaded_block( glm::ivec3( 11, 11, 10 ) ).id == PORTAL );
        check_bool( "surface scan: no ledge frame", world.get_loaded_block( glm::ivec3( 10, 3, 10 ) ).id != PORTAL &&
                                                    world.get_loaded_block( glm::ivec3( 11, 4, 10 ) ).id != PORTAL );
    }

    // ---- MP gate: pending until the diff boxes in flight come back DONE ----
    {
        RepGameState &gs = *new RepGameState( );
        gs.tick_number = 0;
        test_setup_world( gs.world );
        World &world = gs.world;
        gs.camera.eye_height = PLAYER_EYE_HEIGHT;
        gs.world.chunkLoader.chunk_center = glm::ivec3( 0, 0, 0 );
        stage_pending( gs, DIM_OVERWORLD, glm::dvec3( 10.5, 8.0, 10.5 ), glm::ivec3( 10, 10, 10 ) );
        live_search_box( world, gs.portal.src_center, DIM_OVERWORLD, nullptr );
        gs.multiplayer.active = true;
        gs.multiplayer.diff_boxes_sent = 5;
        gs.multiplayer.diff_boxes_done = 3;
        repgame_portal_tick( gs );
        check_bool( "mp gate: unconfirmed boxes hold pending", gs.portal.pending_build == 1 );
        repgame_portal_tick( gs );
        check_bool( "mp gate: snapshot latched", gs.portal.pending_diff_snap == 1 &&
                                                gs.portal.pending_diff_target == 5 );
        // Past the soft timeout an unconfirmed box still holds the resolve.
        gs.portal.pending_start_tick = gs.tick_number - PORTAL_RESOLVE_TIMEOUT_TICKS - 1;
        repgame_portal_tick( gs );
        check_bool( "mp gate: pending past soft timeout", gs.portal.pending_build == 1 &&
                                                        gs.portal.pending_wait_logged == 1 );
        gs.multiplayer.diff_boxes_done = 5;
        repgame_portal_tick( gs );
        check_bool( "mp gate: done catches up -> resolves", gs.portal.pending_build == 0 );
        gs.multiplayer.active = false;
    }

    // ---- MP gate: disconnect resolves; reconnect re-snapshots ----
    {
        RepGameState &gs = *new RepGameState( );
        gs.tick_number = 0;
        test_setup_world( gs.world );
        World &world = gs.world;
        gs.camera.eye_height = PLAYER_EYE_HEIGHT;
        gs.world.chunkLoader.chunk_center = glm::ivec3( 0, 0, 0 );
        world.set_loaded_block( glm::ivec3( 10, 7, 10 ), { STONE, 0, 0, STONE, 0 } );
        stage_pending( gs, DIM_OVERWORLD, glm::dvec3( 10.5, 8.0, 10.5 ), glm::ivec3( 10, 10, 10 ) );
        live_search_box( world, gs.portal.src_center, DIM_OVERWORLD, nullptr );
        gs.multiplayer.active = true;
        gs.multiplayer.diff_boxes_sent = 5;
        gs.multiplayer.diff_boxes_done = 3;
        repgame_portal_tick( gs );
        check_bool( "mp drop: pending while connected", gs.portal.pending_build == 1 );
        gs.multiplayer.active = false;
        repgame_portal_tick( gs );
        check_bool( "mp drop: disconnect resolves", gs.portal.pending_build == 0 );
        // Reconnect resets both counters — sent below the old target must
        // re-snapshot or the gate could never pass.
        stage_pending( gs, DIM_OVERWORLD, glm::dvec3( 10.5, 8.0, 10.5 ), glm::ivec3( 10, 10, 10 ) );
        live_search_box( world, gs.portal.src_center, DIM_OVERWORLD, nullptr );
        gs.multiplayer.active = true;
        gs.multiplayer.diff_boxes_sent = 9;
        gs.multiplayer.diff_boxes_done = 6;
        repgame_portal_tick( gs );
        check_bool( "mp reconn: snapshot taken", gs.portal.pending_diff_target == 9 );
        // "Reconnect": counters restart from a smaller epoch.
        gs.multiplayer.diff_boxes_sent = 2;
        gs.multiplayer.diff_boxes_done = 2;
        repgame_portal_tick( gs );
        check_bool( "mp reconn: target re-snapshotted + resolves", gs.portal.pending_diff_target == 2 &&
                                                                 gs.portal.pending_build == 0 );
        gs.multiplayer.active = false;
    }

    // ---- Give-up: soft timeout logs; the hard cap never builds ----
    {
        RepGameState &gs = *new RepGameState( );
        gs.tick_number = 0;
        test_setup_world( gs.world );
        World &world = gs.world;
        gs.camera.eye_height = PLAYER_EYE_HEIGHT;
        gs.world.chunkLoader.chunk_center = glm::ivec3( 0, 0, 0 );
        world.set_loaded_block( glm::ivec3( 10, 7, 10 ), { STONE, 0, 0, STONE, 0 } );
        stage_pending( gs, DIM_OVERWORLD, glm::dvec3( 10.5, 8.0, 10.5 ), glm::ivec3( 10, 10, 10 ) );
        // The whole search box stays non-live: the gate never passes.
        // Soft timeout only logs — the travel stays pending and builds nothing.
        gs.portal.pending_start_tick = gs.tick_number - PORTAL_RESOLVE_TIMEOUT_TICKS - 1;
        repgame_portal_tick( gs );
        check_bool( "give-up: still pending past soft timeout", gs.portal.pending_build == 1 );
        check_bool( "give-up: wait logged once", gs.portal.pending_wait_logged == 1 );
        check_bool( "give-up: feet still at hold", glm::distance( feet_of( gs ), glm::dvec3( 10.5, 8.0, 10.5 ) ) < 0.001 );
        // Hard cap: pending clears, nothing is built, the player is left at
        // the hold feet.
        gs.portal.pending_start_tick = gs.tick_number - PORTAL_RESOLVE_GIVEUP_TICKS - 1;
        repgame_portal_tick( gs );
        check_bool( "give-up: pending cleared", gs.portal.pending_build == 0 );
        check_bool( "give-up: feet at hold", glm::distance( feet_of( gs ), glm::dvec3( 10.5, 8.0, 10.5 ) ) < 0.001 );
        // A wrongly-built frame would land inside the ±24 site spiral of
        // src_center or on the ledge — all covered by this window.
        bool stray = false;
        for ( int x = -20; x <= 40 && !stray; x++ ) {
            for ( int y = -5; y <= 25 && !stray; y++ ) {
                for ( int z = -20; z <= 40 && !stray; z++ ) {
                    const BlockID id = world.get_loaded_block( glm::ivec3( x, y, z ) ).id;
                    stray = id == PORTAL || id == OBSIDIAN;
                }
            }
        }
        check_bool( "give-up: no frame written in the box", !stray );
    }

    // ---- Give-up adopts an already-loaded portal ----
    {
        RepGameState &gs = *new RepGameState( );
        gs.tick_number = 0;
        test_setup_world( gs.world );
        World &world = gs.world;
        gs.camera.eye_height = PLAYER_EYE_HEIGHT;
        gs.world.chunkLoader.chunk_center = glm::ivec3( 0, 0, 0 );
        // Lit 2x3 x-facing frame, same shape as the far-portal case.
        for ( int dx = 53; dx <= 56; dx++ ) {
            for ( int dy = 55; dy <= 59; dy++ ) {
                const bool edge = dx == 53 || dx == 56 || dy == 55 || dy == 59;
                world.set_loaded_block( glm::ivec3( dx, dy, 10 ),
                                        edge ? BlockState{ OBSIDIAN, 0, 0, OBSIDIAN, 0 } : BlockState{ PORTAL, 0, 0, PORTAL, 0 } );
            }
        }
        stage_pending( gs, DIM_OVERWORLD, glm::dvec3( 10.5, 8.0, 10.5 ), glm::ivec3( 5, 10, 5 ) );
        live_search_box( world, gs.portal.src_center, DIM_OVERWORLD, nullptr );
        // MP diffs outstanding keep the gate unmet until the hard cap.
        gs.multiplayer.active = true;
        gs.multiplayer.diff_boxes_sent = 4;
        gs.multiplayer.diff_boxes_done = 1;
        gs.portal.pending_start_tick = gs.tick_number - PORTAL_RESOLVE_GIVEUP_TICKS - 1;
        repgame_portal_tick( gs );
        const glm::dvec3 feet = feet_of( gs );
        check_bool( "give-up adopt: resolved", gs.portal.pending_build == 0 );
        check_bool( "give-up adopt: inside the loaded plate", floor( feet.z ) == 10 && feet.y == 56.0 &&
                                                              feet.x >= 54.0 && feet.x <= 56.0 );
        check_bool( "give-up adopt: must_exit set", gs.portal.must_exit == 1 );
        gs.multiplayer.active = false;
    }

    // ---- Early adopt: a revealed portal moves the hold mid-wait ----
    // While the gate is unmet the periodic scan adopts any PORTAL the
    // loaded chunks already show — the hold re-aims inside it so the
    // eventual release is a no-op teleport, not a snap.
    {
        RepGameState &gs = *new RepGameState( );
        gs.tick_number = 0;
        test_setup_world( gs.world );
        World &world = gs.world;
        gs.camera.eye_height = PLAYER_EYE_HEIGHT;
        gs.world.chunkLoader.chunk_center = glm::ivec3( 0, 0, 0 );
        // Lit 2x3 x-facing frame, same shape as the give-up adopt case.
        for ( int dx = 53; dx <= 56; dx++ ) {
            for ( int dy = 55; dy <= 59; dy++ ) {
                const bool edge = dx == 53 || dx == 56 || dy == 55 || dy == 59;
                world.set_loaded_block( glm::ivec3( dx, dy, 10 ),
                                        edge ? BlockState{ OBSIDIAN, 0, 0, OBSIDIAN, 0 } : BlockState{ PORTAL, 0, 0, PORTAL, 0 } );
            }
        }
        stage_pending( gs, DIM_OVERWORLD, glm::dvec3( 10.5, 8.0, 10.5 ), glm::ivec3( 5, 10, 5 ) );
        // One in-window box chunk still missing -> the gate stays unmet.
        const glm::ivec3 missing( 1, 0, 1 );
        live_search_box( world, gs.portal.src_center, DIM_OVERWORLD, &missing );
        // Land exactly on a scan tick so the wait's periodic adopt runs.
        gs.portal.pending_start_tick = gs.tick_number - PORTAL_ADOPT_SCAN_TICKS;
        repgame_portal_tick( gs );
        const glm::dvec3 feet = feet_of( gs );
        check_bool( "early adopt: still pending", gs.portal.pending_build == 1 );
        check_bool( "early adopt: inside the loaded plate", floor( feet.z ) == 10 && feet.y == 56.0 &&
                                                            feet.x >= 54.0 && feet.x <= 56.0 );
        check_bool( "early adopt: hold re-aimed at the plate", glm::distance( gs.portal.hold_feet, feet ) < 0.001 );
        check_bool( "early adopt: must_exit set", gs.portal.must_exit == 1 );
        // The missing chunk goes live -> resolve adopts the same plate,
        // so the release teleports nowhere.
        make_chunk_live( world, missing, 0 );
        repgame_portal_tick( gs );
        check_bool( "early adopt: resolve finishes", gs.portal.pending_build == 0 );
        check_bool( "early adopt: feet unchanged at resolve", glm::distance( feet_of( gs ), feet ) < 0.001 );
    }

    return failures;
}
