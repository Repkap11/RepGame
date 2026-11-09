// Item/inventory/crafting tests — split out of rep_tests.cpp.
#include "test_util.hpp"
#include "common/fluid.hpp"
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
        NetContainerSetSlotPayload out_p = { NetContainerKind::CHEST, 1, 2, 3, 4, { STICK, 7 }, 0 };
        PacketWriter w;
        net_serialize_container_set_slot( w, out_p );
        std::vector<uint8_t> buf = w.take_buf( );
        PacketReader r( buf.data( ), buf.size( ) );
        NetContainerSetSlotPayload in_p;
        memset( &in_p, 0, sizeof( in_p ) );
        check_bool( "net slot: deserialize", net_deserialize_container_set_slot( r, in_p ) );
        check_bool( "net slot: item id survives", in_p.slot_value.item_id == STICK && in_p.slot_value.quantity == 7 );

        NetContainerSetSlotPayload bad_p = { NetContainerKind::CHEST, 1, 2, 3, 4, { ITEM_ID_END, 7 }, 0 };
        PacketWriter w2;
        net_serialize_container_set_slot( w2, bad_p );
        std::vector<uint8_t> buf2 = w2.take_buf( );
        PacketReader r2( buf2.data( ), buf2.size( ) );
        NetContainerSetSlotPayload in_p2;
        memset( &in_p2, 0, sizeof( in_p2 ) );
        check_bool( "net slot: deserialize bad id", net_deserialize_container_set_slot( r2, in_p2 ) );
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

    // Mining tool classes: Block::mineable is importer-derived from block
    // names; item_mining_speed returns the tool's multiplier only when its
    // tool_class matches the target's class.
    check_bool( "mineable: stone -> pickaxe", block_definitions[ STONE ].mineable == MINEABLE_PICKAXE );
    check_bool( "mineable: cobblestone -> pickaxe", block_definitions[ COBBLESTONE ].mineable == MINEABLE_PICKAXE );
    check_bool( "mineable: iron door -> pickaxe", block_definitions[ IRON_DOOR_LOWER ].mineable == MINEABLE_PICKAXE );
    check_bool( "mineable: nether brick fence -> pickaxe", block_definitions[ NETHER_BRICK_FENCE ].mineable == MINEABLE_PICKAXE );
    check_bool( "mineable: concrete -> pickaxe", block_definitions[ BLACK_CONCRETE ].mineable == MINEABLE_PICKAXE );
    check_bool( "mineable: oak log -> axe", block_definitions[ OAK_LOG ].mineable == MINEABLE_AXE );
    check_bool( "mineable: oak stairs -> axe", block_definitions[ OAK_STAIRS ].mineable == MINEABLE_AXE );
    check_bool( "mineable: chiseled bookshelf -> axe", block_definitions[ CHISELED_BOOKSHELF ].mineable == MINEABLE_AXE );
    check_bool( "mineable: dirt -> shovel", block_definitions[ DIRT ].mineable == MINEABLE_SHOVEL );
    check_bool( "mineable: grass block -> shovel", block_definitions[ GRASS ].mineable == MINEABLE_SHOVEL );
    check_bool( "mineable: concrete powder -> shovel", block_definitions[ BLACK_CONCRETE_POWDER ].mineable == MINEABLE_SHOVEL );
    check_bool( "mineable: oak leaves -> shears", block_definitions[ OAK_LEAVES ].mineable == MINEABLE_SHEARS );
    check_bool( "mineable: wool -> shears", block_definitions[ WHITE_WOOL ].mineable == MINEABLE_SHEARS );
    check_bool( "mineable: air -> none", block_definitions[ AIR ].mineable == MINEABLE_NONE );
    check_bool( "tool speed: iron pickaxe on stone = 6", item_mining_speed( IRON_PICKAXE, STONE ) == 6.0f );
    check_bool( "tool speed: wooden pickaxe on stone = 2", item_mining_speed( WOODEN_PICKAXE, STONE ) == 2.0f );
    check_bool( "tool speed: diamond axe on log = 8", item_mining_speed( DIAMOND_AXE, OAK_LOG ) == 8.0f );
    check_bool( "tool speed: stone shovel on dirt = 4", item_mining_speed( STONE_SHOVEL, DIRT ) == 4.0f );
    check_bool( "tool speed: shears on leaves = 8", item_mining_speed( SHEARS, OAK_LEAVES ) == 8.0f );
    check_bool( "tool speed: wrong tool = 1", item_mining_speed( IRON_PICKAXE, OAK_LOG ) == 1.0f );
    check_bool( "tool speed: axe on stone = 1", item_mining_speed( STONE_AXE, STONE ) == 1.0f );
    check_bool( "tool speed: sword = 1", item_mining_speed( IRON_SWORD, STONE ) == 1.0f );
    check_bool( "tool speed: block item = 1", item_mining_speed( STONE, STONE ) == 1.0f );
    check_bool( "tool speed: empty hand = 1", item_mining_speed( LAST_BLOCK_ID, STONE ) == 1.0f );
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
        double lo = 1e9, hi = -1e9;
        for ( int i = 0; i < 600; i++ ) {
            gs.tick_number++;
            items.tick( gs );
            if ( i >= 500 ) {
                const double y = items.debug_items( )[ 0 ].pos.y;
                lo = std::min( lo, y );
                hi = std::max( hi, y );
            }
        }
        const ItemEntities::ItemEntity it = items.debug_items( )[ 0 ];
        // The submersion spring rests with the bottom face ~0.06 under the
        // local surface — the open diagonal pulls each corner to 50/64, so
        // that's ~15.72 rather than a flat 15.875 — and once settled it
        // holds still instead of bouncing across the surface every tick.
        check_near( "item: floats to water surface", it.pos.y, 15.72, 0.05 );
        check_bool( "item: in_water at the surface", it.in_water == 1 );
        check_near( "item: settled float, no fast bob", hi - lo, 0.0, 0.05 );
    }

    // --- water current pushes downstream --------------------------------------
    {
        // Hand-laid sloped surface (the flow sim isn't running here):
        // levels climb 1->7 eastward, so the surface slopes down toward +x.
        for ( int i = 0; i < 4; i++ ) {
            world.set_loaded_block( glm::ivec3( 18 + i, 11, 20 ), fluid_state( WATER, 1 + i * 2 ) );
        }
        ItemEntities items;
        items.spawn( STICK, 1, glm::dvec3( 19.5, 11.8, 20.5 ), glm::dvec3( 0.0 ), 0 );
        for ( int i = 0; i < 400; i++ ) {
            gs.tick_number++;
            items.tick( gs );
        }
        const ItemEntities::ItemEntity it = items.debug_items( )[ 0 ];
        check_bool( "item: drifts with the current", it.pos.x > 21.0 );
        for ( int i = 0; i < 4; i++ ) {
            world.set_loaded_block( glm::ivec3( 18 + i, 11, 20 ), BLOCK_STATE_AIR );
        }
    }

    // --- a falling water column carries items down -----------------------------
    {
        // Walled column at (28,*,20): a level-1 fall under a source top.
        world.set_loaded_block( glm::ivec3( 28, 14, 20 ), water );
        for ( int y = 11; y <= 13; y++ ) {
            world.set_loaded_block( glm::ivec3( 28, y, 20 ), fluid_state( WATER, 1 ) );
        }
        for ( int y = 11; y <= 14; y++ ) {
            world.set_loaded_block( glm::ivec3( 27, y, 20 ), stone );
            world.set_loaded_block( glm::ivec3( 29, y, 20 ), stone );
            world.set_loaded_block( glm::ivec3( 28, y, 19 ), stone );
            world.set_loaded_block( glm::ivec3( 28, y, 21 ), stone );
        }
        ItemEntities items;
        items.spawn( STICK, 1, glm::dvec3( 28.5, 14.5, 20.5 ), glm::dvec3( 0.0 ), 0 );
        for ( int i = 0; i < 400; i++ ) {
            gs.tick_number++;
            items.tick( gs );
        }
        const ItemEntities::ItemEntity it = items.debug_items( )[ 0 ];
        check_near( "item: carried down the fall", it.pos.y, 11.0, 0.5 );
        world.set_loaded_block( glm::ivec3( 28, 14, 20 ), BLOCK_STATE_AIR );
        for ( int y = 11; y <= 13; y++ ) {
            world.set_loaded_block( glm::ivec3( 28, y, 20 ), BLOCK_STATE_AIR );
        }
        for ( int y = 11; y <= 14; y++ ) {
            world.set_loaded_block( glm::ivec3( 27, y, 20 ), BLOCK_STATE_AIR );
            world.set_loaded_block( glm::ivec3( 29, y, 20 ), BLOCK_STATE_AIR );
            world.set_loaded_block( glm::ivec3( 28, y, 19 ), BLOCK_STATE_AIR );
            world.set_loaded_block( glm::ivec3( 28, y, 21 ), BLOCK_STATE_AIR );
        }
    }

    // --- lava destroys items ---------------------------------------------------
    {
        const BlockState lava = { LAVA, 0, 0, LAVA, 0 };
        world.set_loaded_block( glm::ivec3( 20, 11, 15 ), lava );
        ItemEntities items;
        items.spawn( STICK, 1, glm::dvec3( 20.5, 13.0, 15.5 ), glm::dvec3( 0.0 ), 0 );
        for ( int i = 0; i < 300 && items.count( ) > 0; i++ ) {
            gs.tick_number++;
            items.tick( gs );
        }
        check_bool( "item: lava burns the drop", items.count( ) == 0 );
        world.set_loaded_block( glm::ivec3( 20, 11, 15 ), BLOCK_STATE_AIR );
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

    // --- buried by a placed block: side escape, then float up ----------------
    {
        // Open neighbors: the item slides out of the block's cell sideways.
        ItemEntities items;
        items.spawn( STICK, 1, glm::dvec3( 12.5, 11.0, 12.5 ), glm::dvec3( 0.0 ), 0 );
        gs.tick_number++;
        items.tick( gs );
        world.set_loaded_block( glm::ivec3( 12, 11, 12 ), stone );
        gs.tick_number++;
        items.tick( gs );
        const ItemEntities::ItemEntity side = items.debug_items( )[ 0 ];
        check_bool( "item: buried item pushed out the side", side.pos.y < 11.5 && ( floor( side.pos.x ) != 12 || floor( side.pos.z ) != 12 ) );
        world.set_loaded_block( glm::ivec3( 12, 11, 12 ), BLOCK_STATE_AIR );

        // Walled in: every side is solid, so it floats up onto the covering
        // block — and again out of a taller column when re-buried deeper.
        ItemEntities walled;
        walled.spawn( STICK, 1, glm::dvec3( 15.5, 11.0, 15.5 ), glm::dvec3( 0.0 ), 0 );
        gs.tick_number++;
        walled.tick( gs );
        for ( int y = 11; y <= 13; y++ ) {
            world.set_loaded_block( glm::ivec3( 14, y, 15 ), stone );
            world.set_loaded_block( glm::ivec3( 16, y, 15 ), stone );
            world.set_loaded_block( glm::ivec3( 15, y, 14 ), stone );
            world.set_loaded_block( glm::ivec3( 15, y, 16 ), stone );
        }
        world.set_loaded_block( glm::ivec3( 15, 11, 15 ), stone );
        for ( int i = 0; i < 3; i++ ) {
            gs.tick_number++;
            walled.tick( gs );
        }
        check_near( "item: walled-in item floats up", walled.debug_items( )[ 0 ].pos.y, 12.0, 0.05 );
        world.set_loaded_block( glm::ivec3( 15, 12, 15 ), stone );
        for ( int i = 0; i < 3; i++ ) {
            gs.tick_number++;
            walled.tick( gs );
        }
        check_near( "item: re-buried item floats up again", walled.debug_items( )[ 0 ].pos.y, 13.0, 0.05 );
        for ( int y = 11; y <= 13; y++ ) {
            world.set_loaded_block( glm::ivec3( 14, y, 15 ), BLOCK_STATE_AIR );
            world.set_loaded_block( glm::ivec3( 16, y, 15 ), BLOCK_STATE_AIR );
            world.set_loaded_block( glm::ivec3( 15, y, 14 ), BLOCK_STATE_AIR );
            world.set_loaded_block( glm::ivec3( 15, y, 16 ), BLOCK_STATE_AIR );
        }
        world.set_loaded_block( glm::ivec3( 15, 11, 15 ), BLOCK_STATE_AIR );
        world.set_loaded_block( glm::ivec3( 15, 12, 15 ), BLOCK_STATE_AIR );
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
        chunk.sidecars.items.push_back( rec );
        chunk.sidecars.items_file_present = 1;
        items.adopt_chunk( chunk );
        check_bool( "persist: adopt spawns record", items.count( ) == 1 && chunk.sidecars.items.empty( ) );
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

    // Search box: the name matcher is case-insensitive with '_' acting as
    // a space on both sides, so raw queries like 'oak_planks' work.
    check_bool( "search: 'torch' matches TORCH", creative_search_matches( "TORCH", "torch" ) );
    check_bool( "search: 'redstone torch' matches REDSTONE_TORCH", creative_search_matches( "REDSTONE_TORCH", "redstone torch" ) );
    check_bool( "search: 'oak_planks' underscore query", creative_search_matches( "OAK_PLANKS", "oak_planks" ) );
    check_bool( "search: substring anywhere", creative_search_matches( "STRIPPED_OAK_LOG", "ripped" ) );
    check_bool( "search: no match", !creative_search_matches( "STONE", "torch" ) );

    // Drive the slot fill headlessly — init() does GL work, so fields are
    // filled directly via friend access (mirrors test_setup_inventories).
    {
        CreativeInventory ci;
        ci.width = 4;
        ci.height = 3;
        ci.num_blocks_max = ci.width * ci.height;
        ci.slots = static_cast<InventorySlot *>( calloc( ci.num_blocks_max, sizeof( InventorySlot ) ) );
        ci.selected_page = 0;
        ci.search_len = 0;
        ci.search_text[ 0 ] = '\0';
        InventoryRenderer &cir = ci.inventory_renderer;
        cir.width = ci.width;
        cir.height = ci.height;
        cir.num_blocks_max = ci.num_blocks_max;
        cir.screen_width = 800;
        cir.screen_height = 600;
        cir.inv_items_x = 0;
        cir.inv_items_y = 0;
        cir.inv_cell_stride = 1.0f;
        cir.inv_block_size = 1;
        cir.inv_block_offset = 0;
        cir.slots_entities = static_cast<entt::entity *>( calloc( cir.num_blocks_max * STAIRS_MAX_BOXES, sizeof( entt::entity ) ) );
        for ( int i = 0; i < cir.num_blocks_max * STAIRS_MAX_BOXES; i++ ) {
            cir.slots_entities[ i ] = entt::null;
        }

        ci.load_blocks_for_selected_page( );
        // The fill loops y from height-1 down, so the common list's first
        // entry (GRASS) lands on the top-left cell — slot (h-1)*w.
        check_bool( "search: page 0 still starts at GRASS", ci.slots[ ( ci.height - 1 ) * ci.width ].item_id == GRASS );

        for ( const char c : { 't', 'o', 'r', 'c', 'h' } ) {
            ci.searchAppendChar( c );
        }
        // The first page of matches must be the leading 'torch' matches in
        // CREATIVE_BLOCKS order, in the same top-row-first slot order the
        // fill uses (y from height-1 down, x left to right).
        int expected_i = 0;
        bool order_ok = true;
        for ( int y = ci.height - 1; y >= 0; y-- ) {
            for ( int x = 0; x < ci.width; x++ ) {
                ItemID want = LAST_BLOCK_ID;
                for ( ; expected_i < ( int )CREATIVE_BLOCK_COUNT; expected_i++ ) {
                    const char *n = item_name( CREATIVE_BLOCKS[ expected_i ] );
                    if ( n != nullptr && creative_search_matches( n, "torch" ) ) {
                        want = CREATIVE_BLOCKS[ expected_i++ ];
                        break;
                    }
                }
                order_ok = order_ok && ci.slots[ y * ci.width + x ].item_id == want;
            }
        }
        check_bool( "search: 'torch' page is the leading matches", order_ok );
        check_bool( "search: 'torch' page has TORCH", ci.slots[ ( ci.height - 1 ) * ci.width ].item_id == BLACK_TORCH );

        ci.searchBackspace( );
        check_bool( "search: backspace shortens", ci.search_len == 4 );
        ci.searchClear( );
        check_bool( "search: clear restores page 0", ci.search_len == 0 && ci.slots[ ( ci.height - 1 ) * ci.width ].item_id == GRASS );

        free( cir.slots_entities );
        free( ci.slots );
    }
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
        InventorySlot slots[ 9 ] = { };
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
        InventorySlot slots[ 9 ] = { };
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
        InventorySlot slots[ 9 ] = { };
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

