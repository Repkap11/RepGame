// Item/inventory/crafting tests — split out of rep_tests.cpp.
#include "test_util.hpp"
// Item id space (Phase 1 non-block items): block items share the BlockID
// value, pure items live at ITEM_ID_BASE+, LAST_BLOCK_ID stays the empty
// sentinel. Covers item_id_valid/item_is_block/item_max_stack, the wire
// slot format, and the generated block drop table.
int test_items( ) {
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

    check_bool( "item: block id is a block item", item_is_block( GRASS ) );
    check_bool( "item: pure item is not a block item", !item_is_block( STICK ) );
    check_bool( "item: defined block is valid", item_id_valid( GRASS ) );
    check_bool( "item: defined item is valid", item_id_valid( STICK ) && item_id_valid( APPLE ) );
    check_bool( "item: ITEM_ID_END is invalid", !item_id_valid( ITEM_ID_END ) );
    check_bool( "item: sentinel is invalid", !item_id_valid( LAST_BLOCK_ID ) );
    check_bool( "item: block max stack", item_max_stack( GRASS ) == MAX_STACK_SIZE );
    check_bool( "item: pure item max stack default", item_max_stack( STICK ) == 64 );
    check_bool( "item: icon for pure item", item_icon_texture( STICK ) != item_icon_texture( DIAMOND ) );
    check_bool( "item: pure item is flat icon", !item_icon_is_isometric( STICK ) );
    check_bool( "item: item_name", item_name( STICK ) != nullptr && item_name( GRASS ) != nullptr );

    // Wire slot round trip: a real item survives; an undefined item-range id
    // is normalized to empty.
    {
        NetChestSetSlotPayload out_p = { 1, 2, 3, 4, { STICK, 7 } };
        PacketWriter w;
        net_serialize_chest_set_slot( w, out_p );
        std::vector<uint8_t> buf = w.take_buf( );
        PacketReader r( buf.data( ), buf.size( ) );
        NetChestSetSlotPayload in_p;
        memset( &in_p, 0, sizeof( in_p ) );
        check_bool( "net slot: deserialize", net_deserialize_chest_set_slot( r, in_p ) );
        check_bool( "net slot: item id survives", in_p.slot_value.item_id == STICK && in_p.slot_value.quantity == 7 );

        NetChestSetSlotPayload bad_p = { 1, 2, 3, 4, { ITEM_ID_END, 7 } };
        PacketWriter w2;
        net_serialize_chest_set_slot( w2, bad_p );
        std::vector<uint8_t> buf2 = w2.take_buf( );
        PacketReader r2( buf2.data( ), buf2.size( ) );
        NetChestSetSlotPayload in_p2;
        memset( &in_p2, 0, sizeof( in_p2 ) );
        check_bool( "net slot: deserialize bad id", net_deserialize_chest_set_slot( r2, in_p2 ) );
        check_bool( "net slot: undefined id clamps empty", in_p2.slot_value.item_id == LAST_BLOCK_ID && in_p2.slot_value.quantity == 0 );
    }

    // Block drop table (deterministic rng): table entries roll, no-table
    // blocks drop themselves, drops:[] drops nothing.
    {
        uint32_t rng = 12345;
        ItemStack drops[ 4 ];
        check_bool( "drops: stone -> 1 cobblestone", block_roll_drops( STONE, &rng, drops, 4 ) == 1 && drops[ 0 ].item_id == COBBLESTONE && drops[ 0 ].count == 1 );
        rng = 12345;
        check_bool( "drops: glass -> nothing", block_roll_drops( GLASS, &rng, drops, 4 ) == 0 );
        rng = 12345;
        check_bool( "drops: diamond ore -> diamond", block_roll_drops( DIAMOND_ORE, &rng, drops, 4 ) == 1 && drops[ 0 ].item_id == DIAMOND && drops[ 0 ].count == 1 );
        rng = 12345;
        check_bool( "drops: unlisted block -> itself", block_roll_drops( DIRT, &rng, drops, 4 ) == 1 && drops[ 0 ].item_id == DIRT && drops[ 0 ].count == 1 );
        // Lapis ore rolls 4-8 lapis.
        rng = 999;
        int n = block_roll_drops( LAPIS_ORE, &rng, drops, 4 );
        check_bool( "drops: lapis ore -> 4-8 lapis", n == 1 && drops[ 0 ].item_id == LAPIS_LAZULI && drops[ 0 ].count >= 4 && drops[ 0 ].count <= 8 );
        // Gravel's two <1.0 chances are mutually exclusive: exactly one item
        // per roll, either flint or gravel, over many seeds.
        int gravel_ok = 1, saw_flint = 0, saw_gravel = 0;
        for ( uint32_t seed = 1; seed <= 500; seed++ ) {
            rng = seed * 2654435761u;
            const int m = block_roll_drops( GRAVEL, &rng, drops, 4 );
            if ( m != 1 || ( drops[ 0 ].item_id != FLINT && drops[ 0 ].item_id != GRAVEL ) ) {
                gravel_ok = 0;
            } else if ( drops[ 0 ].item_id == FLINT ) {
                saw_flint = 1;
            } else {
                saw_gravel = 1;
            }
        }
        check_bool( "drops: gravel rolls exactly one item", gravel_ok && saw_flint && saw_gravel );
        // Oak leaves: sapling/stick/apple are mutually exclusive too.
        int leaves_ok = 1;
        for ( uint32_t seed = 1; seed <= 500; seed++ ) {
            rng = seed * 40503u;
            if ( block_roll_drops( OAK_LEAVES, &rng, drops, 4 ) > 1 ) {
                leaves_ok = 0;
            }
        }
        check_bool( "drops: oak leaves at most one item", leaves_ok );
    }
    return failures;
}

// Dropped item entities: falling/resting, water float, merging, despawn,
// chunk freeze, pickup remainder, cap eviction.
int test_item_entities( ) {
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
    // Stone floor at y=10 (top surface 11) over a generous area, plus a
    // walled water column at (25, *, 25) standing 5 deep on the floor.
    for ( int x = 5; x <= 30; x++ ) {
        for ( int z = 5; z <= 30; z++ ) {
            world.set_loaded_block( glm::ivec3( x, 10, z ), stone );
        }
    }
    for ( int y = 11; y <= 15; y++ ) {
        world.set_loaded_block( glm::ivec3( 25, y, 25 ), water );
        // Contain the column so it can't pour out.
        world.set_loaded_block( glm::ivec3( 24, y, 25 ), stone );
        world.set_loaded_block( glm::ivec3( 26, y, 25 ), stone );
        world.set_loaded_block( glm::ivec3( 25, y, 24 ), stone );
        world.set_loaded_block( glm::ivec3( 25, y, 26 ), stone );
    }

    // --- fall + rest --------------------------------------------------------
    {
        ItemEntities items;
        items.spawn( STICK, 3, glm::dvec3( 10.5, 14.0, 10.5 ), glm::dvec3( 0.0 ), 0 );
        for ( int i = 0; i < 200 && !items.debug_items( )[ 0 ].grounded; i++ ) {
            gs.tick_number++;
            items.tick( gs );
        }
        const ItemEntities::ItemEntity it = items.debug_items( )[ 0 ];
        check_bool( "item: lands and grounds", it.grounded == 1 );
        check_near( "item: rests at floor top", it.pos.y, 11.0, 0.02 );
    }

    // --- water float ---------------------------------------------------------
    {
        ItemEntities items;
        items.spawn( STICK, 1, glm::dvec3( 25.5, 11.2, 25.5 ), glm::dvec3( 0.0 ), 0 );
        double top = -1;
        for ( int i = 0; i < 400; i++ ) {
            gs.tick_number++;
            items.tick( gs );
            top = items.debug_items( )[ 0 ].pos.y;
        }
        // Buoyancy + rise cap should leave it bobbing near the water surface
        // (top water cell spans 15..16): resting pos near 16.
        check_near( "item: floats to water surface", top, 16.0, 0.6 );
    }

    // --- merge ---------------------------------------------------------------
    {
        ItemEntities items;
        items.spawn( STICK, 10, glm::dvec3( 10.4, 11.0, 10.4 ), glm::dvec3( 0.0 ), 0 );
        items.spawn( STICK, 10, glm::dvec3( 10.6, 11.0, 10.6 ), glm::dvec3( 0.0 ), 0 );
        items.spawn( APPLE, 5, glm::dvec3( 10.5, 11.0, 10.9 ), glm::dvec3( 0.0 ), 0 );
        for ( int i = 0; i < 25; i++ ) {
            gs.tick_number++;
            items.tick( gs );
        }
        int sticks = 0, apples = 0, entities = 0;
        for ( const auto &d : items.debug_items( ) ) {
            entities++;
            if ( d.item == STICK ) {
                sticks += d.count;
            } else if ( d.item == APPLE ) {
                apples += d.count;
            }
        }
        check_bool( "item: same items merge", entities == 2 && sticks == 20 && apples == 5 );
    }

    // --- merge respects max_stack --------------------------------------------
    {
        ItemEntities items;
        items.spawn( STICK, 60, glm::dvec3( 10.4, 11.0, 10.4 ), glm::dvec3( 0.0 ), 0 );
        items.spawn( STICK, 60, glm::dvec3( 10.6, 11.0, 10.6 ), glm::dvec3( 0.0 ), 0 );
        for ( int i = 0; i < 25; i++ ) {
            gs.tick_number++;
            items.tick( gs );
        }
        int total = 0, entities = 0, over = 0;
        for ( const auto &d : items.debug_items( ) ) {
            entities++;
            total += d.count;
            if ( d.count > item_max_stack( STICK ) ) {
                over++;
            }
        }
        check_bool( "item: merge capped at max_stack", entities == 2 && total == 120 && over == 0 );
    }

    // --- despawn -------------------------------------------------------------
    {
        ItemEntities items;
        items.spawn( STICK, 1, glm::dvec3( 10.5, 11.0, 10.5 ), glm::dvec3( 0.0 ), 0 );
        check_bool( "item: alive before despawn", items.count( ) == 1 );
        for ( int i = 0; i < ItemEntities::ITEM_DESPAWN_TICKS + 5; i++ ) {
            gs.tick_number++;
            items.tick( gs );
        }
        check_bool( "item: despawns at 5 minutes", items.count( ) == 0 );
    }

    // --- frozen in a non-live chunk -------------------------------------------
    {
        ItemEntities items;
        items.spawn( STICK, 1, glm::dvec3( 1000.5, 60.0, 1000.5 ), glm::dvec3( 0.0, 0.5, 0.0 ), 0 );
        for ( int i = 0; i < 40; i++ ) {
            gs.tick_number++;
            items.tick( gs );
        }
        const ItemEntities::ItemEntity it = items.debug_items( )[ 0 ];
        check_bool( "item: frozen in unloaded chunk", it.pos == glm::dvec3( 1000.5, 60.0, 1000.5 ) && it.age_ticks == 0 );
    }

    // --- pickup into a full inventory leaves the remainder ---------------------
    {
        test_setup_inventories( gs ); // fresh empty inventories
        check_bool( "give: empty inventory takes all", RepGame::give_item_to_player( gs, STICK, 3 ) == 0 );
        check_bool( "give: went to hotbar", gs.hotbar.findSlotWithItem( STICK, false ) >= 0 );
        for ( int i = 0; i < gs.hotbar.num_blocks_max; i++ ) {
            gs.hotbar.slots[ i ].item_id = COBBLESTONE;
            gs.hotbar.slots[ i ].quantity = 64;
        }
        for ( int i = 0; i < gs.survival_inventory.num_slots; i++ ) {
            gs.survival_inventory.slots[ i ].item_id = COBBLESTONE;
            gs.survival_inventory.slots[ i ].quantity = 64;
        }
        check_bool( "give: full inventory returns all", RepGame::give_item_to_player( gs, APPLE, 5 ) == 5 );
    }

    // --- pickup: empty inventory collects, full keeps the remainder ------------
    {
        test_setup_inventories( gs );
        ItemEntities items;
        // Feet at y=11 on the floor; the item spawns inside the feet AABB.
        gs.camera.pos = glm::dvec3( 10.5, 11.0 + PLAYER_EYE_HEIGHT, 10.5 );
        items.spawn( STICK, 7, glm::dvec3( 10.5, 11.0, 10.5 ), glm::dvec3( 0.0 ), 0 );
        gs.tick_number++;
        items.tick( gs );
        check_bool( "item: picked up into empty hotbar", items.count( ) == 0 && gs.hotbar.findSlotWithItem( STICK, false ) >= 0 );
        // Full inventory: the entity keeps its whole stack.
        for ( int i = 0; i < gs.hotbar.num_blocks_max; i++ ) {
            gs.hotbar.slots[ i ].item_id = COBBLESTONE;
            gs.hotbar.slots[ i ].quantity = 64;
        }
        for ( int i = 0; i < gs.survival_inventory.num_slots; i++ ) {
            gs.survival_inventory.slots[ i ].item_id = COBBLESTONE;
            gs.survival_inventory.slots[ i ].quantity = 64;
        }
        items.spawn( APPLE, 4, glm::dvec3( 10.5, 11.0, 10.5 ), glm::dvec3( 0.0 ), 0 );
        gs.tick_number++;
        items.tick( gs );
        check_bool( "item: full inventory keeps the stack", items.count( ) == 1 && items.debug_items( )[ 0 ].count == 4 );
        gs.camera.pos = glm::dvec3( 1.5, 60.0, 1.5 ); // back out of pickup range
    }

    // --- cap eviction ----------------------------------------------------------
    {
        ItemEntities items;
        for ( int i = 0; i < static_cast<int>( ItemEntities::MAX_ITEM_ENTITIES ) + 5; i++ ) {
            items.spawn( STICK, 1, glm::dvec3( 10.5 + ( i % 8 ) * 0.1, 11.0, 10.5 + ( i % 8 ) * 0.1 ), glm::dvec3( 0.0 ), 0 );
        }
        check_bool( "item: cap holds", items.count( ) == ItemEntities::MAX_ITEM_ENTITIES );
        // The 5 oldest were evicted: lowest surviving id is 6.
        uint32_t min_id = UINT32_MAX;
        for ( const auto &d : items.debug_items( ) ) {
            min_id = std::min( min_id, d.id );
        }
        check_bool( "item: oldest evicted first", min_id == 6 );
    }

    // --- persistence: record round trip, invalid records, adopt/persist ------
    {
        ScopedTempHome temp_home( "/tmp/repgame_test_items_home" );
        MapStorage storage;
        storage.init( "TestItems" );

        // Record round trip.
        ItemFileRecord rec = { 0, 0, 0.0, 0.0, 0.0, 0.0f, 0.0f, 0.0f, 0 };
        rec.item = STICK;
        rec.count = 5;
        rec.x = 3.5;
        rec.y = 12.0;
        rec.z = 4.5;
        rec.vx = 0.1f;
        rec.vy = -0.2f;
        rec.vz = 0.3f;
        rec.age_ticks = 1234;
        storage.persist_items( glm::ivec3( 0, 0, 0 ), { rec } );
        std::vector<ItemFileRecord> out;
        check_bool( "persist: item file round trips", storage.load_items( glm::ivec3( 0, 0, 0 ), out ) == 1 && out.size( ) == 1 && out[ 0 ].item == STICK && out[ 0 ].count == 5 && out[ 0 ].age_ticks == 1234 && fabs( out[ 0 ].x - 3.5 ) < 1e-9 && fabs( out[ 0 ].vy + 0.2f ) < 1e-6 );

        // Invalid id / zero count are skipped.
        ItemFileRecord bad_id = rec;
        bad_id.item = 999999u;
        ItemFileRecord bad_count = rec;
        bad_count.count = 0;
        storage.persist_items( glm::ivec3( 0, 0, 0 ), { bad_id, bad_count, rec } );
        check_bool( "persist: invalid records skipped", storage.load_items( glm::ivec3( 0, 0, 0 ), out ) == 1 && out.size( ) == 1 && out[ 0 ].item == STICK );

        // A larger on-disk record_size still parses the shared prefix.
        {
            char file_name[ CHUNK_NAME_MAX_LENGTH ];
            snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_ITEMS, storage.get_map_name( ), 0, 0, 0 );
            FILE *fp = fopen( file_name, "wb" );
            const uint32_t big_size = sizeof( ItemFileRecord ) + 8;
            uint32_t one = 1;
            fwrite( &big_size, sizeof( uint32_t ), 1, fp );
            fwrite( &one, sizeof( uint32_t ), 1, fp );
            fwrite( &rec, sizeof( ItemFileRecord ), 1, fp );
            const char pad[ 8 ] = { 0 };
            fwrite( pad, 1, sizeof( pad ), fp );
            fclose( fp );
            check_bool( "persist: larger record_size parses prefix", storage.load_items( glm::ivec3( 0, 0, 0 ), out ) == 1 && out.size( ) == 1 && out[ 0 ].item == STICK && out[ 0 ].count == 5 );
        }
        // A tiny record_size leaves count 0 -> skipped.
        {
            char file_name[ CHUNK_NAME_MAX_LENGTH ];
            snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_ITEMS, storage.get_map_name( ), 0, 0, 0 );
            FILE *fp = fopen( file_name, "wb" );
            const uint32_t tiny_size = 4;
            uint32_t one = 1;
            fwrite( &tiny_size, sizeof( uint32_t ), 1, fp );
            fwrite( &one, sizeof( uint32_t ), 1, fp );
            fwrite( &rec.item, sizeof( uint32_t ), 1, fp );
            fclose( fp );
            check_bool( "persist: tiny record_size skipped", storage.load_items( glm::ivec3( 0, 0, 0 ), out ) == 1 && out.empty( ) );
        }

        // Adopt drains pending records into live entities; persist writes
        // them back out for the owned chunk.
        ItemEntities items;
        items.init_storage( &storage );
        Chunk chunk;
        chunk.chunk_pos = glm::ivec3( 0, 0, 0 );
        chunk.pending_items.push_back( rec );
        chunk.items_file_present = 1;
        items.adopt_chunk( chunk );
        check_bool( "persist: adopt spawns record", items.count( ) == 1 && chunk.pending_items.empty( ) );
        if ( items.count( ) == 1 ) {
            const ItemEntities::ItemEntity it = items.debug_items( )[ 0 ];
            check_bool( "persist: adopt restores fields", it.item == STICK && it.count == 5 && it.age_ticks == 1234 && it.pickup_delay == 0 && fabs( it.vel.y + 0.2 ) < 1e-6 );
        }
        items.persist_all( );
        check_bool( "persist: persist_all writes item", storage.load_items( glm::ivec3( 0, 0, 0 ), out ) == 1 && out.size( ) == 1 && out[ 0 ].item == STICK && out[ 0 ].count == 5 );
        // Non-persistent entities (REPGAME_ITEM_TEST) never reach the file.
        items.spawn( APPLE, 2, glm::dvec3( 4.0, 12.0, 4.0 ), glm::dvec3( 0.0 ), 0, 0 );
        items.persist_all( );
        check_bool( "persist: non-persistent excluded", storage.load_items( glm::ivec3( 0, 0, 0 ), out ) == 1 && out.size( ) == 1 );
    }

    return failures;
}

// Creative inventory ordering: generated CREATIVE_BLOCKS must contain only
// defined pickable blocks with no duplicates, must include the material
// representatives, and must keep every legacy block with a unique look.
int test_creative( ) {
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

    unsigned char seen[ LAST_BLOCK_ID ] = { 0 };
    unsigned char seen_items[ ITEM_ID_END - ITEM_ID_BASE ] = { 0 };
    int dupes = 0, bad = 0, item_bad = 0;
    for ( unsigned int i = 0; i < CREATIVE_BLOCK_COUNT; i++ ) {
        const ItemID iid = CREATIVE_BLOCKS[ i ];
        if ( !item_is_block( iid ) ) {
            // Pure items tail the list: defined and non-duplicated.
            if ( !item_id_defined( iid ) || seen_items[ iid - ITEM_ID_BASE ] ) {
                item_bad++;
                continue;
            }
            seen_items[ iid - ITEM_ID_BASE ] = 1;
            continue;
        }
        const BlockID bid = static_cast<BlockID>( iid );
        if ( seen[ bid ] ) {
            dupes++;
            continue;
        }
        seen[ bid ] = 1;
        if ( !block_id_defined( bid ) || !block_definition_get_definition( bid )->is_pickable ) {
            bad++;
        }
    }
    check_bool( "creative: no duplicate ids", dupes == 0 );
    check_bool( "creative: all entries defined and pickable", bad == 0 );
    check_bool( "creative: item tail defined and unique", item_bad == 0 );
    check_bool( "creative: STICK item listed", seen_items[ STICK - ITEM_ID_BASE ] != 0 );

    auto contains = [ & ]( BlockID bid ) { return seen[ bid ] != 0; };
    check_bool( "creative: ACACIA_PLANKS listed", contains( ACACIA_PLANKS ) );
    check_bool( "creative: ACACIA_FENCE listed", contains( ACACIA_FENCE ) );
    check_bool( "creative: FENCE (oak fence) listed", contains( FENCE ) );
    check_bool( "creative: FENCE_GATE (oak gate) listed", contains( FENCE_GATE ) );
    check_bool( "creative: OAK_FENCE hidden behind FENCE", !contains( OAK_FENCE ) );
    check_bool( "creative: ACACIA_STAIRS listed", contains( ACACIA_STAIRS ) );
    check_bool( "creative: ACACIA_DOUBLE_SLAB hidden", !contains( ACACIA_DOUBLE_SLAB ) );
    check_bool( "creative: GRASS listed", contains( GRASS ) );
    check_bool( "creative: STONE listed", contains( STONE ) );
    check_bool( "creative: TNT listed", contains( TNT ) );
    check_bool( "creative: OAK_LEAVES listed", contains( OAK_LEAVES ) );
    check_bool( "creative: COBBLESTONE_SLAB listed", contains( COBBLESTONE_SLAB ) );
    check_bool( "creative: STONE_BUTTON listed", contains( STONE_BUTTON ) );
    check_bool( "creative: WHITE_WOOL listed", contains( WHITE_WOOL ) );
    check_bool( "creative: WOOL (legacy name) hidden", !contains( WOOL ) );
    check_bool( "creative: WHITE_CONCRETE_POWDER listed", contains( WHITE_CONCRETE_POWDER ) );
    check_bool( "creative: ACACIA_LEAVES listed", contains( ACACIA_LEAVES ) );
    check_bool( "creative: OAK_LOG listed", contains( OAK_LOG ) );
    check_bool( "creative: LIGHT_BLUE_STAINED_GLASS listed", contains( LIGHT_BLUE_STAINED_GLASS ) );
    check_bool( "creative: GLASS_PALE_BLUE hidden", !contains( GLASS_PALE_BLUE ) );
    return failures;
}

// Crafting recipe matching (src/common/crafting.cpp + generated
// recipes_gen.inc): shaped bounding-box semantics with mirroring, shapeless
// backtracking assignment, and the starter recipe table.
int test_crafting( ) {
    int failures = 0;
    const auto report = [ &failures ]( const char *name, bool ok ) {
        pr_test( "  %-55s %s", name, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };
    const auto expect = [ &report ]( const char *name, const ItemID *grid, int dim, ItemID want, int want_count ) {
        const int idx = crafting_find_recipe( grid, dim );
        report( name, idx >= 0 && CRAFT_RECIPES[ idx ].result == want && CRAFT_RECIPES[ idx ].count == want_count );
    };
    const auto expect_none = [ &report ]( const char *name, const ItemID *grid, int dim ) {
        report( name, crafting_find_recipe( grid, dim ) < 0 );
    };

    // Table sanity: non-empty, every result is a real item.
    bool sane = CRAFT_RECIPE_COUNT > 0;
    for ( int i = 0; i < CRAFT_RECIPE_COUNT; i++ ) {
        sane = sane && item_id_valid( CRAFT_RECIPES[ i ].result );
    }
    report( "recipe table non-empty, results valid", sane );

    // Log -> planks: a single OAK_LOG in any cell crafts OAK_PLANKS x4, in
    // both the 2x2 and 3x3 grids. STRIPPED_OAK_LOG works too.
    for ( int dim = 2; dim <= 3; dim++ ) {
        for ( int i = 0; i < dim * dim; i++ ) {
            ItemID grid[ 9 ];
            for ( ItemID &g : grid ) {
                g = LAST_BLOCK_ID;
            }
            grid[ i ] = OAK_LOG;
            char name[ 80 ];
            snprintf( name, sizeof( name ), "OAK_LOG cell %d in %dx%d -> OAK_PLANKS x4", i, dim, dim );
            expect( name, grid, dim, OAK_PLANKS, 4 );
        }
        ItemID grid[ 9 ];
        for ( ItemID &g : grid ) {
            g = LAST_BLOCK_ID;
        }
        grid[ 0 ] = STRIPPED_OAK_LOG;
        char name[ 80 ];
        snprintf( name, sizeof( name ), "STRIPPED_OAK_LOG in %dx%d -> OAK_PLANKS x4", dim, dim );
        expect( name, grid, dim, OAK_PLANKS, 4 );
    }

    // Sticks: a vertical plank pair. Left column in 2x2, right column in
    // 3x3, and mixed wood types all match; a horizontal pair does not.
    {
        ItemID grid[ 4 ] = { OAK_PLANKS, LAST_BLOCK_ID, OAK_PLANKS, LAST_BLOCK_ID };
        expect( "sticks, left column 2x2", grid, 2, STICK, 4 );
    }
    {
        ItemID grid[ 9 ] = { LAST_BLOCK_ID, LAST_BLOCK_ID, OAK_PLANKS,
                             LAST_BLOCK_ID, LAST_BLOCK_ID, SPRUCE_PLANKS,
                             LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID };
        expect( "sticks, right column 3x3 (mixed planks)", grid, 3, STICK, 4 );
    }
    {
        ItemID grid[ 4 ] = { OAK_PLANKS, OAK_PLANKS, LAST_BLOCK_ID, LAST_BLOCK_ID };
        expect_none( "sticks, horizontal pair 2x2 -> no match", grid, 2 );
    }

    // Crafting table: a mixed-planks 2x2 in the 2x2 grid, and an offset
    // 2x2 block in the 3x3 grid.
    {
        ItemID grid[ 4 ] = { OAK_PLANKS, SPRUCE_PLANKS, BIRCH_PLANKS, OAK_PLANKS };
        expect( "crafting table, mixed planks 2x2", grid, 2, CRAFTING_TABLE, 1 );
    }
    {
        ItemID grid[ 9 ] = { LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID,
                             LAST_BLOCK_ID, OAK_PLANKS, SPRUCE_PLANKS,
                             LAST_BLOCK_ID, BIRCH_PLANKS, OAK_PLANKS };
        expect( "crafting table, offset 2x2 in 3x3", grid, 3, CRAFTING_TABLE, 1 );
    }

    // Chest: the 3x3 ring matches (result is the container block), a filled
    // centre does not, and the ring can't fit the 2x2 grid.
    {
        ItemID ring[ 9 ] = { OAK_PLANKS, OAK_PLANKS, OAK_PLANKS,
                             OAK_PLANKS, LAST_BLOCK_ID, OAK_PLANKS,
                             OAK_PLANKS, OAK_PLANKS, OAK_PLANKS };
        expect( "chest ring in 3x3 -> CHEST", ring, 3, CHEST, 1 );
        ItemID filled[ 9 ] = { OAK_PLANKS, OAK_PLANKS, OAK_PLANKS,
                               OAK_PLANKS, OAK_PLANKS, OAK_PLANKS,
                               OAK_PLANKS, OAK_PLANKS, OAK_PLANKS };
        expect_none( "chest ring with filled centre -> no match", filled, 3 );
        // A 3x3 recipe can never match a 2x2 grid (the bounding box can't
        // exceed the grid).
        int chest_idx = -1;
        for ( int i = 0; i < CRAFT_RECIPE_COUNT; i++ ) {
            if ( CRAFT_RECIPES[ i ].result == CHEST ) {
                chest_idx = i;
            }
        }
        report( "chest recipe exists in the table", chest_idx >= 0 );
        ItemID full2[ 4 ] = { OAK_PLANKS, OAK_PLANKS, OAK_PLANKS, OAK_PLANKS };
        report( "chest recipe can't match in 2x2", chest_idx < 0 || !crafting_recipe_matches( CRAFT_RECIPES[ chest_idx ], full2, 2 ) );
    }

    // Torch: COAL over STICK gives TORCH x4; reversed does not.
    {
        ItemID grid[ 9 ] = { LAST_BLOCK_ID, COAL, LAST_BLOCK_ID,
                             LAST_BLOCK_ID, STICK, LAST_BLOCK_ID,
                             LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID };
        expect( "torch, COAL over STICK", grid, 3, TORCH, 4 );
        ItemID reversed[ 9 ] = { LAST_BLOCK_ID, STICK, LAST_BLOCK_ID,
                                 LAST_BLOCK_ID, COAL, LAST_BLOCK_ID,
                                 LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID };
        expect_none( "torch, STICK over COAL -> no match", reversed, 3 );
    }

    // Mirrored shaped matching: an asymmetric recipe (top-left empty)
    // matches a grid holding the mirrored layout.
    {
        const ItemID opts[] = { OAK_PLANKS };
        CraftRecipe asym = {};
        asym.shaped = 1;
        asym.width = 2;
        asym.height = 2;
        asym.cells[ 0 ] = { nullptr, 0 };   // top-left empty
        asym.cells[ 1 ] = { opts, 1 };      // top-right
        asym.cells[ 2 ] = { opts, 1 };      // bottom-left
        asym.cells[ 3 ] = { opts, 1 };      // bottom-right
        asym.result = TORCH;
        asym.count = 1;
        ItemID plain[ 9 ] = { LAST_BLOCK_ID, OAK_PLANKS, LAST_BLOCK_ID,
                              OAK_PLANKS, OAK_PLANKS, LAST_BLOCK_ID,
                              LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID };
        report( "synthetic shaped, layout as written", crafting_recipe_matches( asym, plain, 3 ) );
        ItemID mirrored[ 9 ] = { OAK_PLANKS, LAST_BLOCK_ID, LAST_BLOCK_ID,
                                 OAK_PLANKS, OAK_PLANKS, LAST_BLOCK_ID,
                                 LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID };
        report( "synthetic shaped, mirrored layout", crafting_recipe_matches( asym, mirrored, 3 ) );
    }

    // Shapeless backtracking: ingredient options [OAK,SPRUCE] then [OAK].
    // The grid lists OAK first, so a greedy assignment would feed OAK to
    // the wide option and strand SPRUCE; backtracking succeeds.
    {
        const ItemID both[] = { OAK_PLANKS, SPRUCE_PLANKS };
        const ItemID oak_only[] = { OAK_PLANKS };
        CraftRecipe shapeless = {};
        shapeless.shaped = 0;
        shapeless.cells[ 0 ] = { both, 2 };
        shapeless.cells[ 1 ] = { oak_only, 1 };
        shapeless.result = TORCH;
        shapeless.count = 1;
        ItemID grid[ 4 ] = { OAK_PLANKS, SPRUCE_PLANKS, LAST_BLOCK_ID, LAST_BLOCK_ID };
        report( "synthetic shapeless, backtracking assignment", crafting_recipe_matches( shapeless, grid, 2 ) );
    }

    // Empty grid matches nothing.
    {
        ItemID grid[ 9 ];
        for ( ItemID &g : grid ) {
            g = LAST_BLOCK_ID;
        }
        expect_none( "empty grid -> -1", grid, 3 );
    }

    // Slabs: a full row of planks. Bottom row of the 3x3 works; the 1x3
    // recipe can't fit the 2x2 grid.
    {
        ItemID grid[ 9 ] = { LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID,
                             LAST_BLOCK_ID, LAST_BLOCK_ID, LAST_BLOCK_ID,
                             OAK_PLANKS, OAK_PLANKS, OAK_PLANKS };
        expect( "slab, bottom row of 3x3", grid, 3, OAK_SLAB, 6 );
        int slab_idx = -1;
        for ( int i = 0; i < CRAFT_RECIPE_COUNT; i++ ) {
            if ( CRAFT_RECIPES[ i ].result == OAK_SLAB ) {
                slab_idx = i;
            }
        }
        report( "slab recipe exists in the table", slab_idx >= 0 );
        ItemID full2[ 4 ] = { OAK_PLANKS, OAK_PLANKS, OAK_PLANKS, OAK_PLANKS };
        report( "slab recipe can't match in 2x2", slab_idx < 0 || !crafting_recipe_matches( CRAFT_RECIPES[ slab_idx ], full2, 2 ) );
    }

    // Stairs: the L-shape in cobblestone gives STONE_STAIRS, mirrored too.
    {
        ItemID grid[ 9 ] = { COBBLESTONE, LAST_BLOCK_ID, LAST_BLOCK_ID,
                             COBBLESTONE, COBBLESTONE, LAST_BLOCK_ID,
                             COBBLESTONE, COBBLESTONE, COBBLESTONE };
        expect( "stairs, cobblestone L", grid, 3, STONE_STAIRS, 4 );
        ItemID mirrored[ 9 ] = { LAST_BLOCK_ID, LAST_BLOCK_ID, COBBLESTONE,
                                 LAST_BLOCK_ID, COBBLESTONE, COBBLESTONE,
                                 COBBLESTONE, COBBLESTONE, COBBLESTONE };
        expect( "stairs, cobblestone L mirrored", mirrored, 3, STONE_STAIRS, 4 );
    }

    // Tools: mixed plank types still match via the #planks tag; a stone
    // sword accepts either stone in an off-centre column; diamond shovel.
    {
        ItemID grid[ 9 ] = { OAK_PLANKS, BIRCH_PLANKS, SPRUCE_PLANKS,
                             LAST_BLOCK_ID, STICK, LAST_BLOCK_ID,
                             LAST_BLOCK_ID, STICK, LAST_BLOCK_ID };
        expect( "wooden pickaxe, mixed planks", grid, 3, WOODEN_PICKAXE, 1 );
    }
    {
        ItemID grid[ 9 ] = { LAST_BLOCK_ID, COBBLED_DEEPSLATE, LAST_BLOCK_ID,
                             LAST_BLOCK_ID, COBBLED_DEEPSLATE, LAST_BLOCK_ID,
                             LAST_BLOCK_ID, STICK, LAST_BLOCK_ID };
        expect( "stone sword, off-centre column", grid, 3, STONE_SWORD, 1 );
    }
    {
        ItemID grid[ 9 ] = { DIAMOND, LAST_BLOCK_ID, LAST_BLOCK_ID,
                             STICK, LAST_BLOCK_ID, LAST_BLOCK_ID,
                             STICK, LAST_BLOCK_ID, LAST_BLOCK_ID };
        expect( "diamond shovel", grid, 3, DIAMOND_SHOVEL, 1 );
    }
    {
        // Axe ["##","#S"," S"] with mixed planks; the mirrored layout with
        // cobblestone gives the stone axe, not something else.
        ItemID grid[ 9 ] = { OAK_PLANKS, BIRCH_PLANKS, LAST_BLOCK_ID,
                             SPRUCE_PLANKS, STICK, LAST_BLOCK_ID,
                             LAST_BLOCK_ID, STICK, LAST_BLOCK_ID };
        expect( "wooden axe, mixed planks", grid, 3, WOODEN_AXE, 1 );
        ItemID mirrored[ 9 ] = { COBBLESTONE, COBBLESTONE, LAST_BLOCK_ID,
                                 STICK, COBBLESTONE, LAST_BLOCK_ID,
                                 STICK, LAST_BLOCK_ID, LAST_BLOCK_ID };
        expect( "stone axe, mirrored", mirrored, 3, STONE_AXE, 1 );
    }
    {
        // Hoe ["##"," S"," S"] in the right two columns.
        ItemID grid[ 9 ] = { LAST_BLOCK_ID, DIAMOND, DIAMOND,
                             LAST_BLOCK_ID, LAST_BLOCK_ID, STICK,
                             LAST_BLOCK_ID, LAST_BLOCK_ID, STICK };
        expect( "diamond hoe, right columns", grid, 3, DIAMOND_HOE, 1 );
    }
    report( "tools stack to 1", item_max_stack( WOODEN_PICKAXE ) == 1 && item_max_stack( DIAMOND_SWORD ) == 1 );

    // Drag crafting: distribute a held stack over the dragged slots.
    {
        InventorySlot slots[ 9 ];
        for ( InventorySlot &s : slots ) {
            s.item_id = LAST_BLOCK_ID;
            s.quantity = 0;
        }
        InventorySlot held = { OAK_PLANKS, 64 };
        const int targets[] = { 0, 1, 2 };
        crafting_drag_distribute( slots, targets, 3, held, false );
        report( "drag split 64 over 3 slots", slots[ 0 ].quantity == 21 && slots[ 1 ].quantity == 21 && slots[ 2 ].quantity == 21 && held.quantity == 1 );
    }
    {
        InventorySlot slots[ 9 ];
        for ( InventorySlot &s : slots ) {
            s.item_id = LAST_BLOCK_ID;
            s.quantity = 0;
        }
        slots[ 0 ].item_id = OAK_PLANKS;
        slots[ 0 ].quantity = 60;
        InventorySlot held = { OAK_PLANKS, 10 };
        const int targets[] = { 0, 1 };
        crafting_drag_distribute( slots, targets, 2, held, false );
        report( "drag split capped by slot room", slots[ 0 ].quantity == 64 && slots[ 1 ].quantity == 5 && held.quantity == 1 );
    }
    {
        InventorySlot slots[ 9 ];
        for ( InventorySlot &s : slots ) {
            s.item_id = LAST_BLOCK_ID;
            s.quantity = 0;
        }
        InventorySlot held = { OAK_PLANKS, 2 };
        const int targets[] = { 0, 1 };
        crafting_drag_distribute( slots, targets, 2, held, true );
        report( "right-drag one per slot", slots[ 0 ].quantity == 1 && slots[ 1 ].quantity == 1 && held.item_id == LAST_BLOCK_ID && held.quantity == 0 );
    }
    {
        const InventorySlot empty = { LAST_BLOCK_ID, 0 };
        const InventorySlot room = { OAK_PLANKS, 5 };
        const InventorySlot full = { OAK_PLANKS, 64 };
        const InventorySlot other = { SPRUCE_PLANKS, 1 };
        const InventorySlot held = { OAK_PLANKS, 10 };
        report( "drag accepts an empty slot", crafting_drag_accepts( empty, held ) );
        report( "drag accepts same item with room", crafting_drag_accepts( room, held ) );
        report( "drag rejects same item full", !crafting_drag_accepts( full, held ) );
        report( "drag rejects a different item", !crafting_drag_accepts( other, held ) );
    }

    return failures;
}

