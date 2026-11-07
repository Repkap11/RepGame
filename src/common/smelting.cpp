#include "common/smelting.hpp"

#include "smelting_gen.inc"

const int SMELT_RECIPE_COUNT = static_cast<int>( sizeof( SMELT_RECIPES ) / sizeof( SMELT_RECIPES[ 0 ] ) );
const int FURNACE_FUEL_COUNT = static_cast<int>( sizeof( FURNACE_FUELS ) / sizeof( FURNACE_FUELS[ 0 ] ) );

ItemID smelt_result_item( ItemID input ) {
    for ( int i = 0; i < SMELT_RECIPE_COUNT; i++ ) {
        if ( SMELT_RECIPES[ i ].input == input ) {
            return SMELT_RECIPES[ i ].output;
        }
    }
    return LAST_BLOCK_ID;
}

int furnace_fuel_ticks( ItemID item ) {
    for ( int i = 0; i < FURNACE_FUEL_COUNT; i++ ) {
        if ( FURNACE_FUELS[ i ].item == item ) {
            return FURNACE_FUELS[ i ].ticks;
        }
    }
    return 0;
}
