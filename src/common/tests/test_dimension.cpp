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
        int netherrack = 0, lava = 0, air = 0, glowstone = 0, quartz = 0, forbidden = 0;
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
                        case BEDROCK:
                        case MAGMA:
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
        check_bool( "no overworld blocks in nether", forbidden == 0 );
        // glowstone is a nice-to-have, not guaranteed by density.
        (void)glowstone;
    }

    // ---- Bounded roof: past MGN_ROOF_THICK over the highest possible
    // roof_base (+jitter) the band is solid bedrock filler — no caves. ----
    {
        int filler_gaps = 0;
        // roof_base max is MGN_ROOF_MID+20; cap = that + MGN_ROOF_THICK + 4
        // jitter ≈ 262. Sample above it with margin.
        for ( int x = -96; x < 96; x += 3 ) {
            for ( int z = -96; z < 96; z += 3 ) {
                for ( int ly = 270; ly < DIM_BLOCK_SPAN - 1; ly += 61 ) {
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
                for ( int ly = 190; ly < 266; ly++ ) {
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
        check_bool( "feet inside nether cavern band", feet_y > DIM_BY_MIN_N + 30 && feet_y < DIM_BY_MIN_N + 200 );
        check_bool( "x/z carried over", abs( int( gs.camera.pos.x ) - 8 ) <= 33 );
    }

    return failures;
}
