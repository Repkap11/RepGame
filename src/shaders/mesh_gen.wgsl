// ---------------------------------------------------------------------------
// WebGPU mesh kernel — SOURCE TEMPLATE, not valid WGSL as-is. Built into
// out/wasm/fs/src/shaders/mesh_gen.wgsl by wasm.mk's WGSL_PP rule
// (gcc -E -DREPGAME_WGSL_GEN), same as map_gen.wgsl.
//
// Phase-1 meshing (per-cell visibility + packed lighting) comes from the
// single-source dialect — mesh_gen_shared.hpp, mesh_gen_fns.hpp,
// mesh_vis_logic.hpp — the same text the CPU loop (chunk.cpp) and the HIP
// kernel (mesh_gen.hip) compile. Only this wrapper (bindings, the u16->u32
// cell unpack, the draw-range dispatch, and the border zero-fill) is
// WebGPU-specific.
// ---------------------------------------------------------------------------

#include "common/mesh_gen_fns.hpp"

// Result record matching WorkingSpace byte-for-byte: byte0 = can_be_seen,
// byte1 = visible, bytes 2-3 = has_been_drawn/solid (always 0), then the
// six packed_lighting u32s. The host memcpys it straight into workingSpace.
struct MeshResult {
    flags : u32,
    packed_lighting : array<u32, 6>,
};

// Cell words are u16 packed two-per-u32 (WGSL has no u16 storage element).
@group(0) @binding(0) var<storage, read> cells_packed : array<u32>;
// The compact per-BlockID table — same host data every backend uses.
@group(0) @binding(1) var<storage, read> block_defs : array<GPUBlockDef>;
@group(0) @binding(2) var<storage, read_write> results : array<MeshResult>;

fn mesh_cell_at( i : i32 ) -> u32 {
    let w = cells_packed[ u32( i ) >> 1u ];
    return ( w >> ( ( u32( i ) & 1u ) * 16u ) ) & 0xFFFFu;
}
#define MESH_CELL_AT( i ) mesh_cell_at( i )
// can_be_seen in byte0, visible in byte1 — the bool layout of WorkingSpace.
#define MESH_OUT_FLAGS( idx, seen, vis ) results[ u32( idx ) ].flags = u32( seen ) | ( u32( vis ) << 8u );
#define MESH_OUT_LIGHT( idx, f, v ) results[ u32( idx ) ].packed_lighting[ u32( f ) ] = ( v );

@compute @workgroup_size( 256 )
fn main( @builtin( global_invocation_id ) gid : vec3<u32> ) {
    let index = i32( gid.x );
    if ( index >= CHUNK_BLOCK_SIZE ) {
        return;
    }
    // The draw index range covers a slab that still contains cells whose
    // coords fall outside [0, CHUNK_SIZE_*) — the CPU loop skips those via
    // get_coords_from_index's return value, so zero them here identically.
    let y = ( index / ( CHUNK_SIZE_INTERNAL_Z * CHUNK_SIZE_INTERNAL_X ) ) - 1;
    let x = ( ( index / CHUNK_SIZE_INTERNAL_Z ) % CHUNK_SIZE_INTERNAL_X ) - 1;
    let z = ( index % CHUNK_SIZE_INTERNAL_Z ) - 1;
    if ( index < CHUNK_BLOCK_DRAW_START || index >= CHUNK_BLOCK_DRAW_STOP ||
         x < 0 || y < 0 || z < 0 || x >= CHUNK_SIZE_X || y >= CHUNK_SIZE_Y || z >= CHUNK_SIZE_Z ) {
        // Halo/border cells get the same zeros the host memset leaves —
        // readback memcpys the whole array into workingSpace.
        results[ u32( index ) ].flags = 0u;
        for ( var i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
            results[ u32( index ) ].packed_lighting[ u32( i ) ] = 0u;
        }
        return;
    }

#include "common/mesh_vis_logic.hpp"
}
