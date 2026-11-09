#include "common/fluid.hpp"

#include "common/world.hpp"

int fluid_corner_height( int own_height, const FluidCornerCell cells[ 4 ] ) {
    // Same fluid directly above any contributor: the corner is submerged,
    // so the surface reaches the cell top.
    for ( int i = 0; i < 4; i++ ) {
        if ( cells[ i ].fluid_above ) {
            return FLUID_HEIGHT_UNITS;
        }
    }
    // Weighted average of the contributors' heights (Minecraft's
    // calculateAverageHeight). Cells at 80%+ of the source height weigh
    // 10x so one near-source cell pulls the whole corner near-full.
    int height_sum = 0;
    int weight_sum = 0;
    for ( int i = 0; i < 4; i++ ) {
        if ( cells[ i ].solid ) {
            continue;
        }
        if ( cells[ i ].level < 0 ) {
            // Non-solid non-fluid cell: an air pocket pulls the corner down.
            weight_sum += 1;
            continue;
        }
        const int h = cells[ i ].level;
        const int weight = h * 5 >= FLUID_SOURCE_HEIGHT * 4 ? 10 : 1;
        height_sum += h * weight;
        weight_sum += weight;
    }
    // Every contributor solid: nothing to average — keep the cell's own
    // height so its surface stays put where fluid can't drain in.
    return weight_sum > 0 ? height_sum / weight_sum : own_height;
}

float fluid_surface_height( World &world, const glm::ivec3 &pos ) {
    const BlockState state = world.get_loaded_block( pos );
    const BlockID fluid = fluid_in_cell( state );
    if ( fluid == LAST_BLOCK_ID ) {
        return 0.0f;
    }
    const BlockState above = world.get_loaded_block( pos + glm::ivec3( 0, 1, 0 ) );
    if ( fluid_in_cell( above ) == fluid ) {
        return 1.0f;
    }
    const Block *def = block_definition_get_definition( fluid );
    return static_cast<float>( fluid_level_height( fluid_level( state ), def->flow_range ) ) /
           static_cast<float>( FLUID_HEIGHT_UNITS );
}
