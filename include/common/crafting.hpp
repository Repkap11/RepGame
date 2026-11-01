#pragma once

// Data-driven crafting recipes: data/recipes/*.json compiled by
// tools/blocks_import.py into src/common/recipes_gen.inc. This header is
// pure logic (no GL) so headless tests can drive the matcher.

#include "common/item.hpp"

#define CRAFTING_MAX_DIM 3

typedef struct {
    const ItemID *items;
    int count;
} CraftIngredient; // { nullptr, 0 } = empty cell

typedef struct {
    int shaped;
    int width;
    int height;
    CraftIngredient cells[ 9 ];
    ItemID result;
    int count;
} CraftRecipe;
// shaped: cells row-major top-first, width*height used; shapeless: first n
// cells used.

extern const CraftRecipe CRAFT_RECIPES[];
extern const int CRAFT_RECIPE_COUNT;

// grid: dim*dim ItemIDs, row-major TOP row first, LAST_BLOCK_ID = empty.
bool crafting_recipe_matches( const CraftRecipe &r, const ItemID *grid, int dim );
int crafting_find_recipe( const ItemID *grid, int dim ); // first match in table order, -1 if none

// A drag can cover `slot` when it's empty or holds held's item with room.
bool crafting_drag_accepts( const InventorySlot &slot, const InventorySlot &held );
// Distribute `held` over slots[targets[0..n)]: left-drag (one_each=false)
// gives each target floor(held.quantity / n), capped by its room to
// item_max_stack; right-drag (one_each=true) gives each target 1. Whatever
// isn't placed stays in held (held becomes {LAST_BLOCK_ID,0} when emptied).
// Targets must satisfy crafting_drag_accepts; caller guarantees n >= 1 and
// n <= held.quantity.
void crafting_drag_distribute( InventorySlot *slots, const int *targets, int n, InventorySlot &held, bool one_each );
