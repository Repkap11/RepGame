#include <functional>

#include "common/crafting.hpp"

#include "recipes_gen.inc"

const int CRAFT_RECIPE_COUNT = static_cast<int>( sizeof( CRAFT_RECIPES ) / sizeof( CRAFT_RECIPES[ 0 ] ) );

static bool ingredient_accepts( const CraftIngredient &ing, ItemID id ) {
    if ( ing.count == 0 ) {
        return id == LAST_BLOCK_ID;
    }
    for ( int i = 0; i < ing.count; i++ ) {
        if ( ing.items[ i ] == id ) {
            return true;
        }
    }
    return false;
}

static bool shaped_matches( const CraftRecipe &r, const ItemID *grid, int dim, bool mirror ) {
    int min_r = dim, max_r = -1, min_c = dim, max_c = -1;
    for ( int y = 0; y < dim; y++ ) {
        for ( int x = 0; x < dim; x++ ) {
            if ( grid[ y * dim + x ] == LAST_BLOCK_ID ) {
                continue;
            }
            min_r = y < min_r ? y : min_r;
            max_r = y > max_r ? y : max_r;
            min_c = x < min_c ? x : min_c;
            max_c = x > max_c ? x : max_c;
        }
    }
    if ( max_r < 0 ) {
        return false;
    }
    if ( max_c - min_c + 1 != r.width || max_r - min_r + 1 != r.height ) {
        return false;
    }
    for ( int ry = 0; ry < r.height; ry++ ) {
        for ( int rx = 0; rx < r.width; rx++ ) {
            const int gx = min_c + ( mirror ? r.width - 1 - rx : rx );
            const ItemID id = grid[ ( min_r + ry ) * dim + gx ];
            if ( !ingredient_accepts( r.cells[ ry * r.width + rx ], id ) ) {
                return false;
            }
        }
    }
    return true;
}

static bool shapeless_matches( const CraftRecipe &r, const ItemID *grid, int dim ) {
    int num_ingredients = 0;
    while ( num_ingredients < 9 && r.cells[ num_ingredients ].count > 0 ) {
        num_ingredients++;
    }
    ItemID filled[ 9 ];
    int num_filled = 0;
    for ( int i = 0; i < dim * dim; i++ ) {
        if ( grid[ i ] != LAST_BLOCK_ID ) {
            filled[ num_filled++ ] = grid[ i ];
        }
    }
    if ( num_filled != num_ingredients ) {
        return false;
    }
    // Assign each ingredient a distinct filled cell; overlapping option
    // lists need backtracking, not greedy matching.
    bool used[ 9 ] = { false };
    const std::function<bool( int )> assign = [ & ]( int ing ) {
        if ( ing == num_ingredients ) {
            return true;
        }
        for ( int i = 0; i < num_filled; i++ ) {
            if ( !used[ i ] && ingredient_accepts( r.cells[ ing ], filled[ i ] ) ) {
                used[ i ] = true;
                if ( assign( ing + 1 ) ) {
                    return true;
                }
                used[ i ] = false;
            }
        }
        return false;
    };
    return assign( 0 );
}

bool crafting_recipe_matches( const CraftRecipe &r, const ItemID *grid, int dim ) {
    if ( r.shaped ) {
        return shaped_matches( r, grid, dim, false ) || shaped_matches( r, grid, dim, true );
    }
    return shapeless_matches( r, grid, dim );
}

int crafting_find_recipe( const ItemID *grid, int dim ) {
    for ( int i = 0; i < CRAFT_RECIPE_COUNT; i++ ) {
        if ( crafting_recipe_matches( CRAFT_RECIPES[ i ], grid, dim ) ) {
            return i;
        }
    }
    return -1;
}
