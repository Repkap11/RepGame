#pragma once

// The per-cell instance emitter shared by chunk meshing and block bodies:
// given a block state plus its 3x3x3 neighbourhood it produces the exact
// BlockCoords instance set a chunk cell emits — multi-box fence/gate/stair/
// sign models, the redstone-torch orb, repeater torches + glow, the piston
// head arm and extended-base stub, cactus spine shells, rail underside
// tile/cull, dust side strips, wall-torch offsets and fluid metadata.
// Extracted from chunk.cpp phase 2 so a sliding/falling block draws its
// real model through the shared chunk shader.

#include <glm/glm.hpp>
#include "common/block.hpp"

// 3x3x3 neighbourhood around the emitted cell, centred on it:
// index (dx+1) + 3 * ((dy+1) + 3 * (dz+1)), dx/dy/dz in [-1,1].
// Neighbour cells drive SHAPE derivation (fence connections, stair corner
// shapes, dust side strips, wall mounts) and, for chunks only, the
// boundary-cover culls (rail underside over opaque support, spine-shell
// top/bottom). Cells outside the world read as AIR.
struct BlockEmitNeighbors {
    BlockState cells[ 27 ];
    const BlockState &at( int dx, int dy, int dz ) const {
        return this->cells[ ( dx + 1 ) + 3 * ( ( dy + 1 ) + 3 * ( dz + 1 ) ) ];
    }
    BlockState &at( int dx, int dy, int dz ) {
        return this->cells[ ( dx + 1 ) + 3 * ( ( dy + 1 ) + 3 * ( dz + 1 ) ) ];
    }
};

struct BlockEmitParams {
    const Block *block;          // def of state->display_id
    const BlockState *state;
    glm::vec3 origin;            // world min corner of the run's first cell (fractional for bodies)
    int mesh_x, mesh_y, mesh_z;  // merged-run extent in cells (1 for bodies)
    unsigned int packed_lighting[ NUM_FACES_IN_CUBE ];
    BlockEmitNeighbors nb;
    unsigned int fluid_corners;  // the chunk's s_fluid_corners value; FLUID_CORNERS_FLAT for bodies
    bool cull_against_neighbors; // chunks: true. Bodies: false — nb still shapes the model but no boundary cull applies
};

// Appends instances to render orders: `alloc`/`ctx` is the emitting
// block's own order; `alloc2`/`ctx2` is an optional second channel used by
// bamboo to emit its leaf crosses into RenderOrder_Flowers (nullptr when
// the caller has no second order — the extra instances are dropped).
struct BlockEmitSink {
    void *ctx;
    // Returns the slot to fill, nullptr to drop the instance.
    BlockCoords *( *alloc )( void *ctx );
    void *ctx2;
    BlockCoords *( *alloc2 )( void *ctx2 );
};

// True when this block's emit path reads `nb` — callers may skip filling
// the neighbourhood for cells that never consult it.
bool block_emit_needs_neighbors( const Block *block );
// Count of instances block_emit_cell will append — `extra` gets the count
// bound for the sink's second channel (bamboo leaf crosses), the return
// value is the block's own-order count. The chunk capacity pre-check.
// nb must already be filled when needs_neighbors is true.
int block_emit_count( const BlockEmitParams &p, int *extra );
// Emits the full per-cell instance set through `sink`. Returns the number
// appended.
int block_emit_cell( const BlockEmitParams &p, const BlockEmitSink &sink );
