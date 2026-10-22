#pragma once

#include "common/mesh_gen_shared.hpp" // GPUBlockDef (single source — CPU/HIP/WebGPU)
#include "common/chunk.hpp"

// Output struct matching WorkingSpace layout exactly for direct GPU readback
// into workingSpace.
struct MeshResult {
    bool can_be_seen;
    bool visible;
    bool has_been_drawn; // always false (set by memset)
    bool solid;          // always false (set by memset)
    unsigned int packed_lighting[ NUM_FACES_IN_CUBE ];
};
static_assert( sizeof( MeshResult ) == sizeof( WorkingSpace ), "MeshResult must match WorkingSpace layout" );

// Initialize GPU block definition table. Call once after block_definitions_initilize_definitions.
// Returns true on success.
bool mesh_gen_init_hip( );

// Calculate visibility + lighting on GPU. Results written directly into workingSpace.
// Only cells are uploaded — rotation and the powered flag ride in the cell
// word, so extras stay host-side. Returns true on success,
// false if HIP not available (caller falls back to CPU).
bool mesh_gen_calculate_hip( const BlockCell *cells, WorkingSpace *workingSpace );
