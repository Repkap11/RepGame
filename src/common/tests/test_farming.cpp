// Farming tests — crop chains, the farmland survival rule, tilling,
// trampling, drop tables, item helpers and random-tick growth.
#include "test_util.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/block_update_events/BlockNextToChangeEvent.hpp"

int test_farming( ) {
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

    // --- Item helpers: hoes and plant targets ------------------------------
    check_bool( "hoe: wooden hoe", item_is_hoe( WOODEN_HOE ) );
    check_bool( "hoe: diamond hoe", item_is_hoe( DIAMOND_HOE ) );
    check_bool( "hoe: seeds are not a hoe", !item_is_hoe( WHEAT_SEEDS ) );
    check_bool( "hoe: a block is not a hoe", !item_is_hoe( STONE ) );
    check_bool( "plant: wheat seeds -> WHEAT_0", item_plant_block( WHEAT_SEEDS ) == WHEAT_0 );
    check_bool( "plant: carrot -> CARROT_0", item_plant_block( CARROT ) == CARROT_0 );
    check_bool( "plant: potato -> POTATOES", item_plant_block( POTATO ) == POTATOES );
    check_bool( "plant: beetroot seeds -> BEETROOT_0", item_plant_block( BEETROOT_SEEDS ) == BEETROOT_0 );
    check_bool( "plant: bread plants nothing", item_plant_block( BREAD ) == LAST_BLOCK_ID );
    check_bool( "plant: a block plants nothing", item_plant_block( STONE ) == LAST_BLOCK_ID );
    check_bool( "pick: wheat stage -> seeds", item_for_crop_block( WHEAT_0 ) == WHEAT_SEEDS && item_for_crop_block( WHEAT_7 ) == WHEAT_SEEDS );
    check_bool( "pick: carrot stage -> carrot", item_for_crop_block( CARROT_4 ) == CARROT );
    check_bool( "pick: potato stage -> potato", item_for_crop_block( POTATOES_3 ) == POTATO );
    check_bool( "pick: beetroot stage -> seeds", item_for_crop_block( BEETROOT_2 ) == BEETROOT_SEEDS );
    check_bool( "pick: stone has no crop item", item_for_crop_block( STONE ) == LAST_BLOCK_ID );

    // --- Crop chains: every stage is_crop and links to the next ------------
    {
        const BlockID chains[][ 9 ] = {
            { WHEAT_0, WHEAT_1, WHEAT_2, WHEAT_3, WHEAT_4, WHEAT_5, WHEAT_6, WHEAT_7, LAST_BLOCK_ID },
            { CARROT_0, CARROT_1, CARROT_2, CARROT_3, CARROT_4, LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID },
            { POTATOES, POTATOES_1, POTATOES_2, POTATOES_3, LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID },
            { BEETROOT_0, BEETROOT_1, BEETROOT_2, BEETROOTS, LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID },
        };
        for ( const auto &chain : chains ) {
            for ( int i = 0; chain[ i ] != LAST_BLOCK_ID; i++ ) {
                const Block *def = block_definition_get_definition( chain[ i ] );
                const bool mature = chain[ i + 1 ] == LAST_BLOCK_ID;
                check_bool( "chain: stage is a crop", def->is_crop );
                check_bool( "chain: crop_next links", def->crop_next == ( mature ? LAST_BLOCK_ID : chain[ i + 1 ] ) );
            }
        }
    }

    // --- Survival rule: crops live only on farmland -------------------------
    {
        const BlockState wheat0 = { WHEAT_0, 0, 0, WHEAT_0, 0 };
        const glm::ivec3 soil( 10, 11, 10 );
        const glm::ivec3 cell = soil + glm::ivec3( 0, 1, 0 );
        world.set_loaded_block( soil, { FARMLAND, 0, 0, FARMLAND, 0 } );
        check_bool( "survive: crop on farmland", block_can_survive_at( world, cell, wheat0 ) );
        check_bool( "place: valid above farmland", player_placement_valid( world, cell, wheat0 ) );
        world.set_loaded_block( soil, { DIRT, 0, 0, DIRT, 0 } );
        check_bool( "survive: crop on dirt fails", !block_can_survive_at( world, cell, wheat0 ) );
        check_bool( "place: rejected above dirt", !player_placement_valid( world, cell, wheat0 ) );
        world.set_loaded_block( soil, { STONE, 0, 0, STONE, 0 } );
        check_bool( "survive: crop on stone fails", !block_can_survive_at( world, cell, wheat0 ) );
        // The farmland cell itself isn't replaceable — seeds can't occupy it.
        world.set_loaded_block( soil, { FARMLAND, 0, 0, FARMLAND, 0 } );
        check_bool( "place: can't plant into the farmland cell", !player_placement_valid( world, soil, wheat0 ) );
    }

    // --- Soil conversion: till to farmland, podzol to dirt ------------------
    {
        const glm::ivec3 grass_pos( 12, 11, 10 );
        world.set_loaded_block( grass_pos, { GRASS, 0, 0, GRASS, 0 } );
        repgame_convert_block( gs, grass_pos, FARMLAND );
        failures += check_id( world, "till: grass -> farmland", grass_pos, FARMLAND );
        const glm::ivec3 podzol_pos( 13, 11, 10 );
        world.set_loaded_block( podzol_pos, { PODZOL, 0, 0, PODZOL, 0 } );
        repgame_convert_block( gs, podzol_pos, DIRT );
        failures += check_id( world, "till: podzol -> dirt", podzol_pos, DIRT );
    }

    // --- Trample: farmland -> dirt pops the crop via the kill path ----------
    {
        const glm::ivec3 field( 15, 11, 10 );
        world.set_loaded_block( field, { FARMLAND, 0, 0, FARMLAND, 0 } );
        world.set_loaded_block( field + glm::ivec3( 0, 1, 0 ), { WHEAT_0, 0, 0, WHEAT_0, 0 } );
        repgame_convert_block( gs, field, DIRT );
        for ( int i = 0; i < 4; i++ ) {
            gs.tick_number++;
            gs.blockUpdateQueue.processAllBlockUpdates( gs, gs.tick_number );
        }
        failures += check_id( world, "trample: crop popped", field + glm::ivec3( 0, 1, 0 ), AIR );
    }

    // --- Drop tables ---------------------------------------------------------
    {
        uint32_t rng = 0x9E3779B9u;
        ItemStack drops[ 8 ];
        const int nw = block_roll_drops( WHEAT_7, &rng, drops, 8 );
        bool saw_wheat = false, saw_seeds = false;
        for ( int i = 0; i < nw; i++ ) {
            saw_wheat |= drops[ i ].item_id == WHEAT;
            saw_seeds |= drops[ i ].item_id == WHEAT_SEEDS;
        }
        check_bool( "drops: mature wheat gives wheat + seeds", nw >= 2 && saw_wheat && saw_seeds );
        rng = 0x9E3779B9u;
        const int n0 = block_roll_drops( WHEAT_0, &rng, drops, 8 );
        check_bool( "drops: young wheat gives seeds only", n0 == 1 && drops[ 0 ].item_id == WHEAT_SEEDS );
        rng = 0x9E3779B9u;
        const int nc = block_roll_drops( CARROT_4, &rng, drops, 8 );
        check_bool( "drops: mature carrot gives 2+ carrots", nc == 1 && drops[ 0 ].item_id == CARROT && drops[ 0 ].count >= 2 );
        rng = 0x9E3779B9u;
        const int np = block_roll_drops( POTATOES_3, &rng, drops, 8 );
        check_bool( "drops: mature potato gives 2+ potatoes", np == 1 && drops[ 0 ].item_id == POTATO && drops[ 0 ].count >= 2 );
        rng = 0x9E3779B9u;
        const int nb = block_roll_drops( BEETROOTS, &rng, drops, 8 );
        bool saw_beet = false, saw_beet_seeds = false;
        for ( int i = 0; i < nb; i++ ) {
            saw_beet |= drops[ i ].item_id == BEETROOT;
            saw_beet_seeds |= drops[ i ].item_id == BEETROOT_SEEDS;
        }
        check_bool( "drops: mature beetroot gives beet + seeds", saw_beet && saw_beet_seeds );
        // Grasses: 1/8 wheat-seed roll — a seeded rng over many rolls hits.
        rng = 12345u;
        bool saw_grass_seed = false;
        for ( int i = 0; i < 200 && !saw_grass_seed; i++ ) {
            const int n = block_roll_drops( SHORT_GRASS, &rng, drops, 8 );
            for ( int j = 0; j < n; j++ ) {
                saw_grass_seed |= drops[ j ].item_id == WHEAT_SEEDS;
            }
        }
        check_bool( "drops: grass sometimes gives wheat seeds", saw_grass_seed );
    }

    // --- Random ticks: a whole field of stage-0 wheat advances --------------
    {
        // Every interior column is farmland + wheat, so every random-tick
        // sample inside the band is a growth candidate.
        const BlockState farmland = { FARMLAND, 0, 0, FARMLAND, 0 };
        const BlockState wheat0 = { WHEAT_0, 0, 0, WHEAT_0, 0 };
        for ( int x = 1; x < CHUNK_SIZE_X - 1; x++ ) {
            for ( int z = 1; z < CHUNK_SIZE_Z - 1; z++ ) {
                world.set_loaded_block( glm::ivec3( x, 10, z ), farmland );
                world.set_loaded_block( glm::ivec3( x, 11, z ), wheat0 );
            }
        }
        // Each write above ran an inline light recheck, leaving a real but
        // dark volume — the field needs full skylight to pass the grow
        // check, so force every cell to sky=15 after the fill.
        Chunk *chunk = world.chunkLoader.get_chunk( glm::ivec3( 0, 0, 0 ) );
        if ( chunk->light == nullptr ) {
            chunk->light = chunk_alloc_light( );
        }
        std::fill_n( chunk->light, CHUNK_BLOCK_SIZE, static_cast<unsigned short>( LIGHT_RAW_MAX << 8 ) );
        chunk->light_uniform = static_cast<unsigned short>( LIGHT_RAW_MAX << 8 );
        for ( int t = 0; t < 200; t++ ) {
            world.process_random_ticks( gs );
        }
        int advanced = 0;
        for ( int x = 1; x < CHUNK_SIZE_X - 1; x++ ) {
            for ( int z = 1; z < CHUNK_SIZE_Z - 1; z++ ) {
                if ( world.get_loaded_block( glm::ivec3( x, 11, z ) ).id != WHEAT_0 ) {
                    advanced++;
                }
            }
        }
        check_bool( "random tick: wheat stages advance", advanced > 0 );
    }
    return failures;
}
