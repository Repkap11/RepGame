#include "common/furnace.hpp"
#include "common/smelting.hpp"

// Advances one furnace by the caller's tick cadence. Shared by the client
// (World::tick_furnaces, singleplayer only) and the dedicated server
// (ServerLogic::tickFurnaces), so both simulate identically.
//
// Fuel is consumed lazily: when the burn timer empties AND the input slot
// holds a smeltable item, one fuel item is eaten for its tick value — fuel
// sitting in the slot with nothing to smelt never ignites. While burning,
// a smeltable input with room in the output slot accrues cook_progress;
// at SMELT_TICKS the input is consumed and the result lands in the output.
// A full/mismatched output pauses cooking but the fire keeps burning.
// Returns true when the lit state (burn_ticks > 0) may have flipped — the
// caller swaps the FURNACE/LIT_FURNACE block and notifies peers.
// `changed` is set when any contents/progress field was mutated.
bool furnace_tick( FurnaceContents &f, bool *changed ) {
    const bool was_lit = f.burn_ticks > 0;
    *changed = false;

    InventorySlot &input = f.slots[ FURNACE_SLOT_INPUT ];
    const ItemID result = input.item_id == LAST_BLOCK_ID ? LAST_BLOCK_ID : smelt_result_item( input.item_id );

    if ( f.burn_ticks <= 0 && result != LAST_BLOCK_ID ) {
        InventorySlot &fuel = f.slots[ FURNACE_SLOT_FUEL ];
        const int fuel_ticks = fuel.item_id == LAST_BLOCK_ID ? 0 : furnace_fuel_ticks( fuel.item_id );
        if ( fuel_ticks > 0 ) {
            fuel.quantity--;
            if ( fuel.quantity <= 0 ) {
                fuel.item_id = LAST_BLOCK_ID;
                fuel.quantity = 0;
            }
            f.burn_ticks = fuel_ticks;
            *changed = true;
        }
    }

    if ( f.burn_ticks > 0 ) {
        f.burn_ticks--;
        *changed = true;

        if ( result != LAST_BLOCK_ID ) {
            InventorySlot &output = f.slots[ FURNACE_SLOT_OUTPUT ];
            const bool output_ready = output.item_id == LAST_BLOCK_ID ||
                                      ( output.item_id == result && output.quantity < item_max_stack( result ) );
            if ( output_ready ) {
                f.cook_progress++;
                if ( f.cook_progress >= SMELT_TICKS ) {
                    f.cook_progress = 0;
                    input.quantity--;
                    if ( input.quantity <= 0 ) {
                        input.item_id = LAST_BLOCK_ID;
                        input.quantity = 0;
                    }
                    if ( output.item_id == LAST_BLOCK_ID ) {
                        output.item_id = result;
                        output.quantity = 1;
                    } else {
                        output.quantity++;
                    }
                }
            }
        }
    } else if ( f.cook_progress != 0 ) {
        // Fire went out mid-smelt: the progress is lost (matches vanilla —
        // no half-smelted items survive an empty fuel slot).
        f.cook_progress = 0;
        *changed = true;
    }

    return ( f.burn_ticks > 0 ) != was_lit;
}
