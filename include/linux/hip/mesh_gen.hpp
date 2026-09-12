#pragma once

#include "common/block_definitions.hpp"
#include "common/chunk.hpp"

// Compact block definition for GPU meshing (only fields needed for visibility + lighting).
struct GPUBlockDef {
    int renderOrder;
    int no_light;
    int id; // BlockID
    int is_seethrough_face[ NUM_FACES_IN_CUBE ];
    int hides_self[ NUM_FACES_IN_CUBE ];
};

// Output struct matching WorkingSpace layout exactly for direct GPU readback into workingSpace.
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
// Returns true on success, false if HIP not available (caller falls back to CPU).
bool mesh_gen_calculate_hip( const BlockState *blocks, WorkingSpace *workingSpace );
