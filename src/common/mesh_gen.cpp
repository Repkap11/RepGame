// Host GPUBlockDef table — the compact per-BlockID data every phase-1 mesh
// backend reads (CPU loop directly, HIP/WebGPU upload it to the device).
// Filled once from block_definitions after init; call_once keeps lazy first
// use thread-safe (terrain workers may race the first mesh).

#include <mutex>

#include "common/mesh_gen.hpp"

namespace {

GPUBlockDef g_mesh_defs[ LAST_BLOCK_ID ];
std::once_flag g_mesh_defs_once;

void fill_mesh_defs( ) {
    for ( int i = 0; i < LAST_BLOCK_ID; i++ ) {
        const Block &b = block_definitions[ i ];
        GPUBlockDef &d = g_mesh_defs[ i ];
        d.renderOrder = b.renderOrder;
        d.no_light = b.no_light;
        d.id = b.id;
        d.is_piston = b.is_piston ? 1u : 0u;
        for ( int j = 0; j < NUM_FACES_IN_CUBE; j++ ) {
            d.is_seethrough_face[ j ] = b.calculated.is_seethrough_face[ j ];
            d.hides_self[ j ] = b.calculated.hides_self[ j ];
        }
    }
}

} // namespace

const GPUBlockDef *mesh_def_table( ) {
    std::call_once( g_mesh_defs_once, fill_mesh_defs );
    return g_mesh_defs;
}

#if !defined( REPGAME_WASM )
// WebGPU meshing lives only on WASM; non-WASM builds stub it out (same
// contract as MapGen::load_block_webgpu).
bool mesh_gen_calculate_webgpu( const BlockCell *, WorkingSpace * ) {
    return false;
}
#endif
