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
