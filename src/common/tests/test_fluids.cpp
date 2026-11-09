// Fluid sim tests: bounded spread by level, falls, recede, slope-guided
// direction choice, infinite sources, waterloggable/breakable cells, and
// lava-water reactions.
#include "test_util.hpp"
#include "common/fluid.hpp"
#include "common/block_update_events/FluidUpdateEvent.hpp"

// Heap, not stack: RepGameState embeds megabytes of light queues — a stack
// local overflows the default 8MB stack. Leaked on purpose like the other
// test states.
static RepGameState &new_fluid_state( ) {
    RepGameState &gs = *new RepGameState( );
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 0, 0, 0 );
    gs.game_mode = GameMode_Creative;
    test_setup_world( gs.world );
    return gs;
}

static void place( RepGameState &gs, const glm::ivec3 &pos, const BlockState &state ) {
    gs.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( gs.tick_number, pos, state, false );
}

// Water advances one flow step per `flows` (36) ticks plus event latency,
// so settling needs hundreds of ticks; draining a recede wave even more.
static void settle( RepGameState &gs, int ticks ) {
    for ( int i = 0; i < ticks; i++ ) {
        gs.tick_number++;
        gs.blockUpdateQueue.processAllBlockUpdates( gs, gs.tick_number );
    }
}

static void fill_floor( World &world, int y, int x0, int x1, int z0, int z1, const BlockState &state ) {
    for ( int x = x0; x <= x1; x++ ) {
        for ( int z = z0; z <= z1; z++ ) {
            world.set_loaded_block( glm::ivec3( x, y, z ), state );
        }
    }
}

static int count_fluid( World &world, int y, int x0, int x1, int z0, int z1 ) {
    int count = 0;
    for ( int x = x0; x <= x1; x++ ) {
        for ( int z = z0; z <= z1; z++ ) {
            if ( fluid_in_cell( world.get_loaded_block( glm::ivec3( x, y, z ) ) ) != LAST_BLOCK_ID ) {
                count++;
            }
        }
    }
    return count;
}

static const BlockState fluid_stone = { STONE, 0, 0, STONE, 0 };

int test_fluids( ) {
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
    auto check_rot = [ & ]( const char *name, World &world, const glm::ivec3 &pos, int expected ) {
        const BlockState state = world.get_loaded_block( pos );
        const bool ok = state.id == WATER && fluid_level( state ) == expected;
        pr_test( "  %-55s id=%-3d level=%-2d expected=WATER/%d  %s", name, state.id, fluid_level( state ), expected, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };
    auto check_drained = [ & ]( const char *name, RepGameState &gs ) {
        // Run out the tail of the settle and require the pending set to be
        // empty — catches the old re-queue-forever regression.
        settle( gs, 300 );
        check_bool( name, gs.pending_fluid_updates.empty( ) );
    };

    // --- Corner heights: the pure surface-shape math the mesher packs per
    // cell (chunk.cpp's fluid_pack_corners feeds these into BlockCoords).
    {
        const FluidCornerCell level5 = { fluid_level_height( 5, 7 ), false, false }; // 21/64
        const FluidCornerCell air_cell = { -1, false, false };
        const FluidCornerCell solid_cell = { -1, false, true };
        const FluidCornerCell source_cell = { FLUID_SOURCE_HEIGHT, false, false };
        const FluidCornerCell submerged = { 21, true, false };

        // One level-5 cell (21) against three airs: (21*1)/(1+3*1) = 5.
        const FluidCornerCell c_air[ 4 ] = { level5, air_cell, air_cell, air_cell };
        check_bool( "corner: one level-5 + air averages down", fluid_corner_height( 21, c_air ) == 5 );

        // Same fluid above any contributor forces the corner full-height.
        const FluidCornerCell c_sub[ 4 ] = { submerged, air_cell, air_cell, air_cell };
        check_bool( "corner: fluid_above forces 64", fluid_corner_height( 21, c_sub ) == FLUID_HEIGHT_UNITS );

        // All-solid corner keeps the cell's own height.
        const FluidCornerCell c_solid[ 4 ] = { solid_cell, solid_cell, solid_cell, solid_cell };
        check_bool( "corner: all-solid keeps own height", fluid_corner_height( 21, c_solid ) == 21 );

        // A 56 (>=80% of source) contributor weighs 10x: 560/13 = 43, far
        // above the unweighted 56/4 = 14.
        const FluidCornerCell c_src[ 4 ] = { source_cell, air_cell, air_cell, air_cell };
        check_bool( "corner: near-source contributor dominates", fluid_corner_height( 8, c_src ) == 43 );

        // Air pockets pull a strong corner down: two 21s + two airs
        // average 42/4 = 10 instead of 21.
        const FluidCornerCell c_air2[ 4 ] = { level5, level5, air_cell, air_cell };
        check_bool( "corner: air pockets pull the surface down", fluid_corner_height( 21, c_air2 ) == 10 );
    }


    // --- Spread range: a source on a flat floor levels by manhattan
    // distance up to flow_range (7), and never writes level 8. -----------
    {
        RepGameState &gs = new_fluid_state( );
        World &world = gs.world;
        fill_floor( world, 10, 28, 48, 28, 48, fluid_stone );
        place( gs, glm::ivec3( 38, 11, 38 ), { WATER, 0, 0, WATER, 0 } );
        settle( gs, 1500 );
        bool levels_ok = true;
        for ( int d = 1; d <= 7; d++ ) {
            const BlockState s = world.get_loaded_block( glm::ivec3( 38 + d, 11, 38 ) );
            levels_ok &= s.id == WATER && fluid_level( s ) == d;
        }
        check_bool( "spread: +x levels are distance 1..7", levels_ok );
        failures += check_id( world, "spread: stops at flow_range", glm::ivec3( 38 + 8, 11, 38 ), AIR );
        const int fluid_cells = count_fluid( world, 11, 25, 51, 25, 51 );
        pr_test( "  %-55s count=%d  %s", "spread: fluid cells bounded", fluid_cells, ( fluid_cells > 0 && fluid_cells < 200 ) ? "PASS" : "FAIL" );
        if ( !( fluid_cells > 0 && fluid_cells < 200 ) ) {
            failures++;
        }

        // --- Recede: stone over the source drains the whole pool. -------
        place( gs, glm::ivec3( 38, 11, 38 ), fluid_stone );
        settle( gs, 3000 );
        check_bool( "recede: all flowing water gone", count_fluid( world, 11, 25, 51, 25, 51 ) == 0 );
        check_drained( "recede: pending fluid updates drained", gs );
    }

    // --- Fall: a source at a platform edge pours down the side and the
    // bottom column spreads at level 1 like a fresh source. --------------
    {
        RepGameState &gs = new_fluid_state( );
        World &world = gs.world;
        fill_floor( world, 10, 10, 50, 25, 50, fluid_stone );
        fill_floor( world, 14, 20, 27, 34, 42, fluid_stone ); // raised platform
        world.set_loaded_block( glm::ivec3( 27, 15, 38 ), { WATER, 0, 0, WATER, 0 } );
        // Waking the source normally goes through a neighbor change.
        place( gs, glm::ivec3( 27, 15, 39 ), fluid_stone );
        settle( gs, 2500 );
        check_rot( "fall: edge cell holds level 1", world, glm::ivec3( 28, 15, 38 ), 1 );
        bool column_ok = true;
        for ( int y = 11; y <= 14; y++ ) {
            const BlockState s = world.get_loaded_block( glm::ivec3( 28, y, 38 ) );
            column_ok &= s.id == WATER && fluid_level( s ) == 1;
        }
        check_bool( "fall: full column is level 1", column_ok );
        check_rot( "fall: bottom spread starts at 1", world, glm::ivec3( 29, 11, 38 ), 1 );
        check_rot( "fall: bottom spread reaches 2", world, glm::ivec3( 30, 11, 38 ), 2 );
        check_rot( "fall: bottom spread reaches 6", world, glm::ivec3( 34, 11, 38 ), 6 );
        check_rot( "fall: bottom spread reaches 7", world, glm::ivec3( 35, 11, 38 ), 7 );
        failures += check_id( world, "fall: bottom spread bounded", glm::ivec3( 36, 11, 38 ), AIR );
        check_drained( "fall: pending fluid updates drained", gs );
    }

    // --- Slope: a hole two cells east makes the slope search pick east
    // only; western cells stay dry while the drop rules the flow. --------
    {
        RepGameState &gs = new_fluid_state( );
        World &world = gs.world;
        // Two-layer floor so the pit bottoms out at (40,10,38).
        fill_floor( world, 9, 28, 48, 28, 48, fluid_stone );
        fill_floor( world, 10, 28, 48, 28, 48, fluid_stone );
        world.set_loaded_block( glm::ivec3( 40, 10, 38 ), BLOCK_STATE_AIR ); // the hole
        // Deterministic direction check on the slope search itself.
        check_bool( "slope: east search finds the hole at 2",
                    fluid_slope_distance( world, glm::ivec3( 38, 11, 38 ), glm::ivec3( 1, 0, 0 ), WATER, 4 ) == 2 );
        check_bool( "slope: west search finds nothing",
                    fluid_slope_distance( world, glm::ivec3( 38, 11, 38 ), glm::ivec3( -1, 0, 0 ), WATER, 4 ) > 4 );
        place( gs, glm::ivec3( 38, 11, 38 ), { WATER, 0, 0, WATER, 0 } );
        settle( gs, 2500 );
        check_rot( "slope: path cell 1 gets water", world, glm::ivec3( 39, 11, 38 ), 1 );
        check_rot( "slope: path cell 2 gets water", world, glm::ivec3( 40, 11, 38 ), 2 );
        check_rot( "slope: the hole fills from above", world, glm::ivec3( 40, 10, 38 ), 1 );
        // Every passable path to (36,11,38) is longer than flow_range: the
        // wrap-around flow can never reach it, so it stays dry forever.
        failures += check_id( world, "slope: west cell stays dry", glm::ivec3( 36, 11, 38 ), AIR );
        check_drained( "slope: pending fluid updates drained", gs );
    }

    // --- Infinite source: two sources flanking a cell over solid ground
    // merge it into a new source; over a non-solid cell they don't. ------
    {
        RepGameState &gs = new_fluid_state( );
        World &world = gs.world;
        fill_floor( world, 10, 20, 44, 20, 44, fluid_stone );
        world.set_loaded_block( glm::ivec3( 30, 11, 30 ), { WATER, 0, 0, WATER, 0 } );
        world.set_loaded_block( glm::ivec3( 32, 11, 30 ), { WATER, 0, 0, WATER, 0 } );
        place( gs, glm::ivec3( 34, 11, 34 ), fluid_stone ); // wake the sources
        place( gs, glm::ivec3( 29, 11, 30 ), fluid_stone );
        settle( gs, 1500 );
        check_rot( "infinite: middle cell becomes a source", world, glm::ivec3( 31, 11, 30 ), 0 );
        check_drained( "infinite: pending fluid updates drained", gs );
    }
    {
        RepGameState &gs = new_fluid_state( );
        World &world = gs.world;
        fill_floor( world, 10, 20, 44, 20, 44, fluid_stone );
        world.set_loaded_block( glm::ivec3( 31, 9, 30 ), fluid_stone );
        world.set_loaded_block( glm::ivec3( 31, 10, 30 ), { KELP, 0, 0, KELP, 0 } ); // non-solid below
        world.set_loaded_block( glm::ivec3( 30, 11, 30 ), { WATER, 0, 0, WATER, 0 } );
        world.set_loaded_block( glm::ivec3( 32, 11, 30 ), { WATER, 0, 0, WATER, 0 } );
        place( gs, glm::ivec3( 29, 11, 30 ), fluid_stone );
        settle( gs, 1500 );
        const BlockState mid = world.get_loaded_block( glm::ivec3( 31, 11, 30 ) );
        check_bool( "infinite: no source over a non-solid cell", mid.id == WATER && fluid_level( mid ) == 1 );
        const BlockState below = world.get_loaded_block( glm::ivec3( 31, 10, 30 ) );
        check_bool( "infinite: waterloggable cell below untouched", below.id == KELP && below.waterlogged == 0 );
        check_drained( "infinite2: pending fluid updates drained", gs );
    }

    // --- Plants: a dry waterloggable cell is a wall the flow wraps around,
    // a break-in-liquid flower is replaced, and a direct water write onto
    // kelp waterlogs it through block_state_normalize. -------------------
    {
        RepGameState &gs = new_fluid_state( );
        World &world = gs.world;
        fill_floor( world, 10, 28, 56, 28, 56, fluid_stone );
        world.set_loaded_block( glm::ivec3( 39, 11, 38 ), { KELP, 0, 0, KELP, 0 } );   // waterloggable wall
        world.set_loaded_block( glm::ivec3( 37, 11, 38 ), { POPPY, 0, 0, POPPY, 0 } ); // break-in-liquid
        world.set_loaded_block( glm::ivec3( 45, 11, 45 ), { KELP, 0, 0, KELP, 0 } );
        place( gs, glm::ivec3( 38, 11, 38 ), { WATER, 0, 0, WATER, 0 } );
        place( gs, glm::ivec3( 45, 11, 45 ), { WATER, 0, 0, WATER, 0 } ); // waterlogs the kelp
        settle( gs, 1500 );
        const BlockState kelp = world.get_loaded_block( glm::ivec3( 39, 11, 38 ) );
        check_bool( "plants: kelp cell is a wall", kelp.id == KELP && kelp.waterlogged == 0 );
        check_rot( "plants: flow wraps around the kelp", world, glm::ivec3( 40, 11, 38 ), 4 );
        check_rot( "plants: poppy is replaced by water", world, glm::ivec3( 37, 11, 38 ), 1 );
        const BlockState wet_kelp = world.get_loaded_block( glm::ivec3( 45, 11, 45 ) );
        check_bool( "plants: placed water waterlogs kelp", wet_kelp.id == KELP && wet_kelp.waterlogged == 1 );
        check_drained( "plants: pending fluid updates drained", gs );
    }

    // --- Reactions: lava + water petrifies — obsidian at a source,
    // cobblestone at a flowing cell, stone where lava flows over water. --
    {
        RepGameState &gs = new_fluid_state( );
        World &world = gs.world;
        fill_floor( world, 10, 32, 56, 30, 56, fluid_stone );
        world.set_loaded_block( glm::ivec3( 40, 11, 40 ), { LAVA, 0, 0, LAVA, 0 } );
        place( gs, glm::ivec3( 41, 11, 40 ), { WATER, 0, 0, WATER, 0 } );
        settle( gs, 60 ); // the +1 reaction event lands almost instantly
        failures += check_id( world, "react: lava source becomes obsidian", glm::ivec3( 40, 11, 40 ), OBSIDIAN );
        check_drained( "react1: pending fluid updates drained", gs );
    }
    {
        RepGameState &gs = new_fluid_state( );
        World &world = gs.world;
        fill_floor( world, 10, 38, 56, 38, 56, fluid_stone );
        world.set_loaded_block( glm::ivec3( 45, 11, 44 ), { LAVA, 1, 0, LAVA, 0 } ); // flowing lava
        place( gs, glm::ivec3( 45, 11, 45 ), { WATER, 0, 0, WATER, 0 } );
        settle( gs, 60 );
        failures += check_id( world, "react: flowing lava becomes cobble", glm::ivec3( 45, 11, 44 ), COBBLESTONE );
        check_drained( "react2: pending fluid updates drained", gs );
    }
    {
        RepGameState &gs = new_fluid_state( );
        World &world = gs.world;
        fill_floor( world, 10, 40, 56, 40, 56, fluid_stone );
        world.set_loaded_block( glm::ivec3( 48, 12, 48 ), { LAVA, 0, 0, LAVA, 0 } ); // floating lava source
        place( gs, glm::ivec3( 48, 11, 48 ), { WATER, 0, 0, WATER, 0 } );   // directly below it
        settle( gs, 2500 );                                               // lava's flows delay is slow
        failures += check_id( world, "react: lava over water makes stone", glm::ivec3( 48, 11, 48 ), STONE );
        check_drained( "react3: pending fluid updates drained", gs );
    }

    // --- Buckets: repgame_use_bucket drives off block_selection fields +
    // the hotbar. A scoop only takes a plain fluid source; a pour writes a
    // source at the flowing hit cell, the fluid-adjacent cell, or
    // pos_create. Survival swaps the held item; creative keeps it. --------
    {
        // Survival scoop: a water source drains into a WATER_BUCKET.
        RepGameState &gs = new_fluid_state( );
        gs.game_mode = GameMode_Survival;
        test_setup_inventories( gs );
        World &world = gs.world;
        fill_floor( world, 10, 28, 48, 28, 48, fluid_stone );
        world.set_loaded_block( glm::ivec3( 38, 11, 38 ), { WATER, 0, 0, WATER, 0 } );
        gs.hotbar.addItemWithQuantity( BUCKET, 1 );
        gs.block_selection.fluidSelectionInBounds = 1;
        gs.block_selection.pos_fluid = glm::ivec3( 38, 11, 38 );
        check_bool( "bucket: scoop a source returns true", repgame_use_bucket( gs ) );
        settle( gs, 1 );
        failures += check_id( world, "bucket: scooped cell is air", glm::ivec3( 38, 11, 38 ), AIR );
        check_bool( "bucket: scoop swaps to WATER_BUCKET", gs.hotbar.getSelectedItem( ) == WATER_BUCKET );
        check_drained( "bucket: scoop pending drained", gs );
    }
    {
        // A flowing cell is not a source: no scoop, no item change.
        RepGameState &gs = new_fluid_state( );
        gs.game_mode = GameMode_Survival;
        test_setup_inventories( gs );
        World &world = gs.world;
        fill_floor( world, 10, 28, 48, 28, 48, fluid_stone );
        world.set_loaded_block( glm::ivec3( 38, 11, 38 ), { WATER, 1, 0, WATER, 0 } );
        gs.hotbar.addItemWithQuantity( BUCKET, 1 );
        gs.block_selection.fluidSelectionInBounds = 1;
        gs.block_selection.pos_fluid = glm::ivec3( 38, 11, 38 );
        check_bool( "bucket: flowing cell rejects the scoop", !repgame_use_bucket( gs ) );
        settle( gs, 1 );
        check_rot( "bucket: flowing cell untouched", world, glm::ivec3( 38, 11, 38 ), 1 );
        check_bool( "bucket: still holding BUCKET", gs.hotbar.getSelectedItem( ) == BUCKET );
    }
    {
        // A waterlogged cell reports WATER but can't be drained. Write water
        // first, then kelp into it — block_state_normalize keeps the water
        // as the cell's waterlogged flag.
        RepGameState &gs = new_fluid_state( );
        gs.game_mode = GameMode_Survival;
        test_setup_inventories( gs );
        World &world = gs.world;
        fill_floor( world, 10, 28, 48, 28, 48, fluid_stone );
        world.set_loaded_block( glm::ivec3( 38, 11, 38 ), { WATER, 0, 0, WATER, 0 } );
        world.set_loaded_block( glm::ivec3( 38, 11, 38 ), { KELP, 0, 0, KELP, 0 } );
        gs.hotbar.addItemWithQuantity( BUCKET, 1 );
        gs.block_selection.fluidSelectionInBounds = 1;
        gs.block_selection.pos_fluid = glm::ivec3( 38, 11, 38 );
        check_bool( "bucket: waterlogged kelp rejects the scoop", !repgame_use_bucket( gs ) );
        const BlockState kelp = world.get_loaded_block( glm::ivec3( 38, 11, 38 ) );
        check_bool( "bucket: waterlogged kelp untouched", kelp.id == KELP && kelp.waterlogged == 1 );
        check_bool( "bucket: still holding BUCKET", gs.hotbar.getSelectedItem( ) == BUCKET );
    }
    {
        // Survival pour: WATER_BUCKET writes a source at pos_create and
        // swaps back to the empty BUCKET.
        RepGameState &gs = new_fluid_state( );
        gs.game_mode = GameMode_Survival;
        test_setup_inventories( gs );
        World &world = gs.world;
        fill_floor( world, 10, 28, 48, 28, 48, fluid_stone );
        gs.hotbar.addItemWithQuantity( WATER_BUCKET, 1 );
        gs.block_selection.selectionInBounds = 1;
        gs.block_selection.pos_destroy = glm::ivec3( 38, 10, 38 );
        gs.block_selection.pos_create = glm::ivec3( 38, 11, 38 );
        check_bool( "bucket: pour on empty floor returns true", repgame_use_bucket( gs ) );
        settle( gs, 1 );
        check_rot( "bucket: poured cell is a source", world, glm::ivec3( 38, 11, 38 ), 0 );
        check_bool( "bucket: pour swaps to BUCKET", gs.hotbar.getSelectedItem( ) == BUCKET );
        settle( gs, 1500 );
        check_bool( "bucket: pour pending drained", gs.pending_fluid_updates.empty( ) );
    }
    {
        // Creative pour: the source still lands but the held bucket stays.
        RepGameState &gs = new_fluid_state( );
        test_setup_inventories( gs );
        World &world = gs.world;
        fill_floor( world, 10, 28, 48, 28, 48, fluid_stone );
        gs.hotbar.addItemWithQuantity( WATER_BUCKET, 1 );
        gs.block_selection.selectionInBounds = 1;
        gs.block_selection.pos_destroy = glm::ivec3( 38, 10, 38 );
        gs.block_selection.pos_create = glm::ivec3( 38, 11, 38 );
        check_bool( "bucket: creative pour returns true", repgame_use_bucket( gs ) );
        settle( gs, 1 );
        check_rot( "bucket: creative pour places the source", world, glm::ivec3( 38, 11, 38 ), 0 );
        check_bool( "bucket: creative keeps WATER_BUCKET", gs.hotbar.getSelectedItem( ) == WATER_BUCKET );
        settle( gs, 1500 );
        check_bool( "bucket: creative pour pending drained", gs.pending_fluid_updates.empty( ) );
    }
    {
        // Pour-back: a flowing hit cell fills in place, no face offset.
        RepGameState &gs = new_fluid_state( );
        gs.game_mode = GameMode_Survival;
        test_setup_inventories( gs );
        World &world = gs.world;
        fill_floor( world, 10, 28, 48, 28, 48, fluid_stone );
        world.set_loaded_block( glm::ivec3( 38, 11, 38 ), { WATER, 2, 0, WATER, 0 } );
        gs.hotbar.addItemWithQuantity( WATER_BUCKET, 1 );
        gs.block_selection.fluidSelectionInBounds = 1;
        gs.block_selection.pos_fluid = glm::ivec3( 38, 11, 38 );
        gs.block_selection.pos_fluid_create = glm::ivec3( 38, 11, 39 );
        check_bool( "bucket: pour on flowing cell returns true", repgame_use_bucket( gs ) );
        settle( gs, 1 );
        check_rot( "bucket: flowing cell fills to a source", world, glm::ivec3( 38, 11, 38 ), 0 );
        settle( gs, 1500 );
        check_bool( "bucket: pour-back pending drained", gs.pending_fluid_updates.empty( ) );
    }
    {
        // Pour onto an existing source: the bucket empties but nothing is
        // written — in particular not into pos_fluid_create next door.
        RepGameState &gs = new_fluid_state( );
        gs.game_mode = GameMode_Survival;
        test_setup_inventories( gs );
        World &world = gs.world;
        fill_floor( world, 10, 28, 48, 28, 48, fluid_stone );
        world.set_loaded_block( glm::ivec3( 38, 11, 38 ), { WATER, 0, 0, WATER, 0 } );
        gs.hotbar.addItemWithQuantity( WATER_BUCKET, 1 );
        gs.block_selection.fluidSelectionInBounds = 1;
        gs.block_selection.pos_fluid = glm::ivec3( 38, 11, 38 );
        gs.block_selection.pos_fluid_create = glm::ivec3( 38, 11, 39 );
        check_bool( "bucket: pour on a source returns true", repgame_use_bucket( gs ) );
        settle( gs, 1 );
        check_rot( "bucket: existing source unchanged", world, glm::ivec3( 38, 11, 38 ), 0 );
        failures += check_id( world, "bucket: no write into the adjacent cell", glm::ivec3( 38, 11, 39 ), AIR );
        check_bool( "bucket: pour on source swaps to BUCKET", gs.hotbar.getSelectedItem( ) == BUCKET );
        settle( gs, 1500 );
        check_bool( "bucket: pour-on-source pending drained", gs.pending_fluid_updates.empty( ) );
    }
    {
        // Full hotbar: the filled bucket can't stack (max_stack 1) and every
        // slot is taken, so it drops as an item entity instead of vanishing.
        RepGameState &gs = new_fluid_state( );
        gs.game_mode = GameMode_Survival;
        test_setup_inventories( gs );
        World &world = gs.world;
        fill_floor( world, 10, 28, 48, 28, 48, fluid_stone );
        world.set_loaded_block( glm::ivec3( 38, 11, 38 ), { WATER, 0, 0, WATER, 0 } );
        for ( int i = 0; i < HOTBAR_WIDTH * HOTBAR_HEIGHT; i++ ) {
            gs.hotbar.setSlotContents( i, { STONE, MAX_STACK_SIZE } );
        }
        gs.hotbar.setSlotContents( 0, { BUCKET, 16 } );
        gs.hotbar.setSelectedSlot( 0 );
        const size_t items_before = gs.world.items.count( );
        gs.block_selection.fluidSelectionInBounds = 1;
        gs.block_selection.pos_fluid = glm::ivec3( 38, 11, 38 );
        check_bool( "bucket: full-inventory scoop returns true", repgame_use_bucket( gs ) );
        settle( gs, 1 );
        failures += check_id( world, "bucket: full-inventory scoop drains", glm::ivec3( 38, 11, 38 ), AIR );
        const InventorySlot slot = gs.hotbar.getSlotContents( 0 );
        check_bool( "bucket: consumed one of the stack", slot.item_id == BUCKET && slot.quantity == 15 );
        check_bool( "bucket: filled bucket drops as an item", gs.world.items.count( ) == items_before + 1 );
        check_drained( "bucket: full-inventory pending drained", gs );
    }

    return failures;
}
