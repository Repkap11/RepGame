#pragma once

// Furnace smelting data: data/smelting.json compiled by
// tools/blocks_import.py into src/common/smelting_gen.inc. Pure logic (no
// GL) so headless tests and the dedicated server can use it.

#include "common/item.hpp"

typedef struct {
    ItemID input;
    ItemID output;
} SmeltRecipe;

typedef struct {
    ItemID item;
    int ticks;
} FurnaceFuel;

extern const SmeltRecipe SMELT_RECIPES[];
extern const int SMELT_RECIPE_COUNT;
extern const FurnaceFuel FURNACE_FUELS[];
extern const int FURNACE_FUEL_COUNT;
// Ticks (at UPS_RATE) one input item needs in the furnace.
extern const int SMELT_TICKS;

// Smelt output for `input`, or LAST_BLOCK_ID when it isn't smeltable.
ItemID smelt_result_item( ItemID input );
// Burn ticks one `item` provides as furnace fuel, or 0 when it isn't fuel.
int furnace_fuel_ticks( ItemID item );
