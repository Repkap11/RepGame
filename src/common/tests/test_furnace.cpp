// Furnace tests: smelting/fuel tables, furnace_tick progression, and the
// FURNACE_* wire payloads.
#include "test_util.hpp" // IWYU pragma: keep -- shared test preamble (packet.hpp et al via RepGame.hpp)
#include "common/furnace.hpp"
#include "common/smelting.hpp"

int test_furnace( ) {
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

    // Smelt table: ores (block and raw forms), logs -> charcoal, meats.
    check_bool( "smelt: iron ore block -> ingot", smelt_result_item( IRON_ORE ) == IRON_INGOT );
    check_bool( "smelt: deepslate iron ore -> ingot", smelt_result_item( DEEPSLATE_IRON_ORE ) == IRON_INGOT );
    check_bool( "smelt: raw iron -> ingot", smelt_result_item( RAW_IRON ) == IRON_INGOT );
    check_bool( "smelt: gold ore -> ingot", smelt_result_item( GOLD_ORE ) == GOLD_INGOT );
    check_bool( "smelt: copper ore -> ingot", smelt_result_item( COPPER_ORE ) == COPPER_INGOT );
    check_bool( "smelt: log -> charcoal", smelt_result_item( LOG ) == CHARCOAL || smelt_result_item( OAK_LOG ) == CHARCOAL );
    check_bool( "smelt: beef -> cooked beef", smelt_result_item( BEEF ) == COOKED_BEEF );
    check_bool( "smelt: porkchop -> cooked", smelt_result_item( PORKCHOP ) == COOKED_PORKCHOP );
    check_bool( "smelt: chicken -> cooked", smelt_result_item( CHICKEN ) == COOKED_CHICKEN );
    check_bool( "smelt: mutton -> cooked", smelt_result_item( MUTTON ) == COOKED_MUTTON );
    check_bool( "smelt: stick not smeltable", smelt_result_item( STICK ) == LAST_BLOCK_ID );
    check_bool( "smelt: dirt not smeltable", smelt_result_item( DIRT ) == LAST_BLOCK_ID );

    // Fuel table.
    check_bool( "fuel: coal burns", furnace_fuel_ticks( COAL ) > 0 );
    check_bool( "fuel: charcoal burns", furnace_fuel_ticks( CHARCOAL ) > 0 );
    check_bool( "fuel: planks burn", furnace_fuel_ticks( OAK_PLANKS ) > 0 );
    check_bool( "fuel: log burns", furnace_fuel_ticks( OAK_LOG ) > 0 || furnace_fuel_ticks( LOG ) > 0 );
    check_bool( "fuel: stick burns", furnace_fuel_ticks( STICK ) > 0 );
    check_bool( "fuel: stone does not burn", furnace_fuel_ticks( STONE ) == 0 );
    check_bool( "fuel: ingot does not burn", furnace_fuel_ticks( IRON_INGOT ) == 0 );

    // furnace_tick: fuel ignites, progress accrues, one item smelts.
    {
        FurnaceContents f;
        f.slots[ FURNACE_SLOT_INPUT ] = { RAW_IRON, 1 };
        f.slots[ FURNACE_SLOT_FUEL ] = { COAL, 1 };
        bool changed = false;
        bool lit_seen = false;
        for ( int i = 0; i < SMELT_TICKS + 2; i++ ) {
            if ( furnace_tick( f, &changed ) ) {
                lit_seen = true;
            }
        }
        check_bool( "tick: furnace lit during smelt", lit_seen );
        check_bool( "tick: raw iron consumed", f.slots[ FURNACE_SLOT_INPUT ].item_id == LAST_BLOCK_ID );
        check_bool( "tick: ingot produced", f.slots[ FURNACE_SLOT_OUTPUT ].item_id == IRON_INGOT && f.slots[ FURNACE_SLOT_OUTPUT ].quantity == 1 );
        check_bool( "tick: coal consumed once", f.slots[ FURNACE_SLOT_FUEL ].item_id == LAST_BLOCK_ID );
        check_bool( "tick: still burning after smelt", f.burn_ticks > 0 );
        check_bool( "tick: progress reset", f.cook_progress == 0 );
    }

    // No fuel: nothing progresses.
    {
        FurnaceContents f;
        f.slots[ FURNACE_SLOT_INPUT ] = { RAW_IRON, 1 };
        bool changed = false;
        for ( int i = 0; i < SMELT_TICKS * 2; i++ ) {
            furnace_tick( f, &changed );
        }
        check_bool( "tick: no fuel, no progress", f.cook_progress == 0 && f.burn_ticks == 0 );
        check_bool( "tick: no fuel, input kept", f.slots[ FURNACE_SLOT_INPUT ].item_id == RAW_IRON );
    }

    // Unsmeltable input with fuel: nothing to smelt, so the fuel never
    // ignites and stays in the slot.
    {
        FurnaceContents f;
        f.slots[ FURNACE_SLOT_INPUT ] = { DIRT, 1 };
        f.slots[ FURNACE_SLOT_FUEL ] = { COAL, 1 };
        bool changed = false;
        for ( int i = 0; i < SMELT_TICKS; i++ ) {
            furnace_tick( f, &changed );
        }
        check_bool( "tick: junk input never ignites", f.cook_progress == 0 && f.burn_ticks == 0 );
        check_bool( "tick: junk input keeps fuel", f.slots[ FURNACE_SLOT_FUEL ].item_id == COAL && f.slots[ FURNACE_SLOT_FUEL ].quantity == 1 );
    }

    // Input pulled mid-burn: the lit fire burns out but never relights.
    {
        FurnaceContents f;
        f.slots[ FURNACE_SLOT_INPUT ] = { RAW_IRON, 1 };
        f.slots[ FURNACE_SLOT_FUEL ] = { COAL, 2 };
        bool changed = false;
        for ( int i = 0; i < 10; i++ ) {
            furnace_tick( f, &changed );
        }
        check_bool( "tick: lit before input pulled", f.burn_ticks > 0 );
        f.slots[ FURNACE_SLOT_INPUT ].item_id = LAST_BLOCK_ID;
        f.slots[ FURNACE_SLOT_INPUT ].quantity = 0;
        const int burn_at_pull = f.burn_ticks;
        for ( int i = 0; i < burn_at_pull + 2; i++ ) {
            furnace_tick( f, &changed );
        }
        check_bool( "tick: fire burns out without relight", f.burn_ticks == 0 );
        check_bool( "tick: leftover fuel kept", f.slots[ FURNACE_SLOT_FUEL ].item_id == COAL && f.slots[ FURNACE_SLOT_FUEL ].quantity == 1 );
    }

    // Full output of a different item stalls the cook but the fire burns.
    {
        FurnaceContents f;
        f.slots[ FURNACE_SLOT_INPUT ] = { RAW_IRON, 2 };
        f.slots[ FURNACE_SLOT_FUEL ] = { COAL, 1 };
        f.slots[ FURNACE_SLOT_OUTPUT ] = { DIRT, 64 };
        bool changed = false;
        for ( int i = 0; i < SMELT_TICKS; i++ ) {
            furnace_tick( f, &changed );
        }
        check_bool( "tick: blocked output stalls", f.cook_progress == 0 && f.slots[ FURNACE_SLOT_INPUT ].quantity == 2 );
    }
    {
        // Same item but full stack also stalls.
        FurnaceContents f;
        f.slots[ FURNACE_SLOT_INPUT ] = { RAW_IRON, 2 };
        f.slots[ FURNACE_SLOT_FUEL ] = { COAL, 1 };
        f.slots[ FURNACE_SLOT_OUTPUT ] = { IRON_INGOT, 64 };
        bool changed = false;
        for ( int i = 0; i < SMELT_TICKS; i++ ) {
            furnace_tick( f, &changed );
        }
        check_bool( "tick: full output stack stalls", f.cook_progress == 0 && f.slots[ FURNACE_SLOT_OUTPUT ].quantity == 64 );
    }

    // Output stacking: two smelts land in one stack.
    {
        FurnaceContents f;
        f.slots[ FURNACE_SLOT_INPUT ] = { RAW_IRON, 2 };
        f.slots[ FURNACE_SLOT_FUEL ] = { COAL, 1 };
        f.slots[ FURNACE_SLOT_OUTPUT ] = { IRON_INGOT, 1 };
        bool changed = false;
        for ( int i = 0; i < SMELT_TICKS * 2 + 2; i++ ) {
            furnace_tick( f, &changed );
        }
        check_bool( "tick: outputs stack", f.slots[ FURNACE_SLOT_OUTPUT ].quantity == 3 && f.slots[ FURNACE_SLOT_INPUT ].item_id == LAST_BLOCK_ID );
    }

    // Wire round trips (CONTAINER_* family — furnace rides the generic
    // container protocol; aux[0]=cook_progress, aux[1]=burn_ticks).
    {
        NetContainerContentsPayload out_p;
        out_p.kind = NetContainerKind::FURNACE;
        out_p.x = 4;
        out_p.y = 5;
        out_p.z = 6;
        out_p.revision = 12;
        out_p.slots.assign( FURNACE_INVENTORY_SLOTS, InventorySlot{ LAST_BLOCK_ID, 0 } );
        out_p.slots[ FURNACE_SLOT_INPUT ] = { RAW_IRON, 3 };
        out_p.slots[ FURNACE_SLOT_FUEL ] = { COAL, 1 };
        out_p.slots[ FURNACE_SLOT_OUTPUT ] = { IRON_INGOT, 2 };
        out_p.aux = { 700, 5000 };
        PacketWriter w;
        net_serialize_container_contents( w, out_p );
        std::vector<uint8_t> buf = w.take_buf( );
        PacketReader r( buf.data( ), buf.size( ) );
        NetContainerContentsPayload in_p = {};
        check_bool( "net furnace contents: deserialize", net_deserialize_container_contents( r, in_p ) );
        check_bool( "net furnace contents: slots+progress",
                    in_p.slots.size( ) == FURNACE_INVENTORY_SLOTS && in_p.slots[ 0 ].item_id == RAW_IRON && in_p.slots[ 0 ].quantity == 3 &&
                        in_p.slots[ 2 ].item_id == IRON_INGOT && in_p.aux.size( ) == 2 && in_p.aux[ 0 ] == 700 && in_p.aux[ 1 ] == 5000 &&
                        in_p.revision == 12 );

        NetContainerSetSlotPayload slot_p = { NetContainerKind::FURNACE, 1, 2, 3, FURNACE_SLOT_FUEL, { CHARCOAL, 5 }, 0 };
        PacketWriter w2;
        net_serialize_container_set_slot( w2, slot_p );
        std::vector<uint8_t> buf2 = w2.take_buf( );
        PacketReader r2( buf2.data( ), buf2.size( ) );
        NetContainerSetSlotPayload in_p2;
        memset( &in_p2, 0, sizeof( in_p2 ) );
        check_bool( "net furnace set slot: deserialize", net_deserialize_container_set_slot( r2, in_p2 ) );
        check_bool( "net furnace set slot: value", in_p2.slot_value.item_id == CHARCOAL && in_p2.slot_value.quantity == 5 );

        // Out-of-range slot index is rejected (against the kind's arity).
        NetContainerSetSlotPayload bad_p = { NetContainerKind::FURNACE, 1, 2, 3, 99, { CHARCOAL, 1 }, 0 };
        PacketWriter w3;
        net_serialize_container_set_slot( w3, bad_p );
        std::vector<uint8_t> buf3 = w3.take_buf( );
        PacketReader r3( buf3.data( ), buf3.size( ) );
        NetContainerSetSlotPayload in_p3;
        check_bool( "net furnace set slot: bad index rejected", !net_deserialize_container_set_slot( r3, in_p3 ) );

        // A chest slot index that overflows the chest arity is rejected too.
        NetContainerSetSlotPayload bad_chest = { NetContainerKind::CHEST, 1, 2, 3, CHEST_SLOTS, { CHARCOAL, 1 }, 0 };
        PacketWriter w4;
        net_serialize_container_set_slot( w4, bad_chest );
        std::vector<uint8_t> buf4 = w4.take_buf( );
        PacketReader r4( buf4.data( ), buf4.size( ) );
        NetContainerSetSlotPayload in_p4;
        check_bool( "net chest set slot: bad index rejected", !net_deserialize_container_set_slot( r4, in_p4 ) );

        // Kind/arity mismatch on the wire: a CONTENTS carrying the wrong
        // slot count for its kind fails instead of desyncing the stream.
        NetContainerContentsPayload bad_kind = out_p;
        bad_kind.kind = NetContainerKind::CHEST; // 3 slots != 27
        PacketWriter w5;
        net_serialize_container_contents( w5, bad_kind );
        std::vector<uint8_t> buf5 = w5.take_buf( );
        PacketReader r5( buf5.data( ), buf5.size( ) );
        NetContainerContentsPayload in_p5;
        check_bool( "net contents: kind arity mismatch rejected", !net_deserialize_container_contents( r5, in_p5 ) );
    }

    // slot_tickers: a furnace created through get_or_create marks its slot
    // and tick_furnaces smelts it; breaking the block erases the contents
    // and the next tick clears the byte.
    {
        World world;
        test_setup_world( world );
        ChunkLoader &cl = world.chunkLoader;
        const size_t slot = static_cast<size_t>( chunk_slot_from_pos( glm::ivec3( 0, 0, 0 ) ) );
        const glm::ivec3 pos( 10, 10, 10 );
        world.set_loaded_block( pos, { FURNACE, 0, 0, FURNACE, 0 } );
        FurnaceContents *contents = world.get_or_create_furnace_contents( pos );
        check_bool( "slot_tickers: get_or_create returns contents", contents != nullptr );
        if ( contents != nullptr ) {
            contents->slots[ FURNACE_SLOT_INPUT ] = { RAW_IRON, 1 };
            contents->slots[ FURNACE_SLOT_FUEL ] = { COAL, 1 };
            check_bool( "slot_tickers: create marks slot", cl.slot_tickers[ slot ] == 1 );
            for ( int i = 0; i < SMELT_TICKS + 2; i++ ) {
                world.tick_furnaces( );
            }
            check_bool( "slot_tickers: tick smelts", contents->slots[ FURNACE_SLOT_OUTPUT ].item_id == IRON_INGOT );
            world.set_loaded_block( pos, BLOCK_STATE_AIR );
            check_bool( "slot_tickers: break still marked until tick", cl.slot_tickers[ slot ] == 1 );
            world.tick_furnaces( );
            check_bool( "slot_tickers: tick clears empty slot", cl.slot_tickers[ slot ] == 0 );
        }
    }

    // A worker-populated furnaces map on a LIVE chunk ticks once the slot
    // is marked (finalize_loaded_chunk does this in game; it needs a live
    // Multiplayer so the test calls the same mark helper directly).
    {
        World world;
        test_setup_world( world );
        ChunkLoader &cl = world.chunkLoader;
        Chunk &chunk = cl.chunkArray[ chunk_slot_from_pos( glm::ivec3( 0, 0, 0 ) ) ];
        const size_t slot = static_cast<size_t>( &chunk - cl.chunkArray );
        const glm::ivec3 pos( 20, 10, 20 );
        world.set_loaded_block( pos, { FURNACE, 0, 0, FURNACE, 0 } );
        const int index = Chunk::get_index_from_coords( pos - chunk.chunk_pos * CHUNK_SIZE_I );
        FurnaceContents &contents = chunk.containers.furnaces[ index ];
        contents.slots[ FURNACE_SLOT_INPUT ] = { RAW_IRON, 1 };
        contents.slots[ FURNACE_SLOT_FUEL ] = { COAL, 1 };
        check_bool( "slot_tickers: direct fill unmarked", cl.slot_tickers[ slot ] == 0 );
        cl.mark_slot_ticking( chunk );
        check_bool( "slot_tickers: mark sets byte", cl.slot_tickers[ slot ] == 1 );
        world.tick_furnaces( );
        check_bool( "slot_tickers: worker-filled furnace ticks", contents.burn_ticks > 0 || contents.cook_progress > 0 );
    }

    return failures;
}
