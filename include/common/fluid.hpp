#pragma once

#include <glm/glm.hpp>

#include "common/block_definitions.hpp"

// Level stored in BlockState::rotation (3 bits): 0 = source, 1..7 flowing.
#define FLUID_MAX_LEVEL 7
// Corner heights are transmitted in 1/64ths of a block (byte per corner).
#define FLUID_HEIGHT_UNITS 64
// Source surface height must match WATER_HEIGHT in block.hpp (0.875 * 64).
#define FLUID_SOURCE_HEIGHT 56

// A fluid is any block with flows != 0 (water, lava).
static inline bool block_is_fluid( const Block *b ) {
    return b->flows != 0;
}

// The fluid id present in a cell: the block id itself, or WATER for a
// waterlogged cell (waterlogged water always counts as a source).
// LAST_BLOCK_ID when there is none.
static inline BlockID fluid_in_cell( const BlockState &s ) {
    if ( s.waterlogged ) {
        return WATER;
    }
    if ( s.id < LAST_BLOCK_ID && block_is_fluid( block_definition_get_definition( s.id ) ) ) {
        return s.id;
    }
    return LAST_BLOCK_ID;
}

// 0 source (incl. waterlogged), 1..FLUID_MAX_LEVEL flowing, -1 no fluid.
static inline int fluid_level( const BlockState &s ) {
    if ( s.waterlogged ) {
        return 0;
    }
    if ( fluid_in_cell( s ) == LAST_BLOCK_ID ) {
        return -1;
    }
    return s.rotation <= FLUID_MAX_LEVEL ? s.rotation : FLUID_MAX_LEVEL;
}

// The canonical BlockState of a fluid cell: { fluid, level, 0, fluid, 0 }.
static inline BlockState fluid_state( BlockID fluid, int level ) {
    return { fluid, static_cast<unsigned char>( level ), 0, fluid, 0 };
}

// Render/sim height of a level cell in 1/64 units:
// source -> FLUID_SOURCE_HEIGHT; else
// FLUID_SOURCE_HEIGHT * (flow_range + 1 - level) / (flow_range + 1).
static inline int fluid_level_height( int level, int flow_range ) {
    return level <= 0 ? FLUID_SOURCE_HEIGHT
                      : FLUID_SOURCE_HEIGHT * ( flow_range + 1 - level ) / ( flow_range + 1 );
}

// One of the (up to) 4 cells sharing a top corner, as seen by the corner
// math: level -1 = not the same fluid; fluid_above = same fluid occupies
// the cell directly above it; solid = collides_with_player (occludes water).
// The caller fills level with the cell's fluid_level_height (1/64 units)
// when it holds the same fluid.
typedef struct {
    int level;
    bool fluid_above;
    bool solid;
} FluidCornerCell;

// Minecraft's average-height rule for a surface corner (1/64 units):
// - same fluid above any contributor -> full 64
// - each same-fluid cell adds fluid_level_height, weighted 10 when the
//   height is >= 80% of source height, else weight 1
// - non-solid non-fluid cells add 0 with weight 1; solid cells are ignored
// - own_height is the cell's own height, used when all contributors are solid
int fluid_corner_height( int own_height, const FluidCornerCell cells[ 4 ] );

class World;
// Surface height of the fluid in the cell at pos, in 0..1 block units:
// 1.0 when the same fluid occupies the cell above, else level_height/64.
// Returns 0 when there is no fluid. Implement in src/common/fluid.cpp.
float fluid_surface_height( World &world, const glm::ivec3 &pos );
