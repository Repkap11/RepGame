// ---------------------------------------------------------------------------
// WebGPU terrain-generation kernel — SOURCE TEMPLATE, not valid WGSL as-is.
// Built into out/wasm/fs/src/shaders/map_gen.wgsl by wasm.mk's WGSL_PP rule
// (gcc -E -DREPGAME_WGSL_GEN), the same way .glsl sources are preprocessed.
//
// All generation logic below comes from the single-source dialect files —
// perlin_noise_shared.hpp, map_gen_fields{,_legacy}.hpp, map_logic.hpp —
// which also compile as plain C++ into the CPU/CUDA/HIP backends. Only this
// wrapper (bindings + dispatch + the per-column/y loop, mirroring
// hip_set_block in src/linux/hip/map_gen.hip) is WebGPU-specific.
// ---------------------------------------------------------------------------

// BlockID constants (const AIR : u32 = 0; ...) generated from
// block_definitions.hpp at build time by tools/gen_wgsl_block_ids.awk.
#include "block_ids.inc"

// WGSL perlin sampler macros: argument casts are f32(...) (identity on f32
// args, the required conversion on i32/AbstractInt args). The C++ backends
// define these to plain calls — implicit int->float there.
#define MAP_GEN_PERLIN2D( x, z, f, d, s ) perlin_noise2d( f32( x ), f32( z ), f, d, s )
#define MAP_GEN_PERLIN3D( x, y, z, f, d, s ) perlin_noise3d( f32( x ), f32( y ), f32( z ), f, d, s )

#include "common/perlin_noise_shared.hpp"
#include "common/map_gen_fields.hpp"

struct MapGenParams {
    chunk_x : i32,
    chunk_y : i32,
    chunk_z : i32,
};

@group(0) @binding(0) var<uniform> params : MapGenParams;
// One u32 per cell (WGSL has no u16): the host repacks to BlockCell (u16) on
// readback. Canonical state — waterlogged/powered bits 0.
@group(0) @binding(1) var<storage, read_write> cells : array<u32>;

@compute @workgroup_size( 256 )
fn main( @builtin( global_invocation_id ) gid : vec3<u32> ) {
    let col_idx = i32( gid.x );
    if ( col_idx >= CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Z ) {
        return;
    }
    let x = ( col_idx / CHUNK_SIZE_INTERNAL_Z ) - 1 + params.chunk_x;
    let z = ( col_idx % CHUNK_SIZE_INTERNAL_Z ) - 1 + params.chunk_z;

    // Compute 2D column context once per column (biome, height, steepness).
#if REPGAME_MAP_GEN_LEGACY
    let terrainHeight = mgl_base_height( x, z );
#else
    let col = mg_column_info( x, z );
#endif

    // Loop over Y; only 3D noise (overhang, caves, ores) is recomputed per Y.
    for ( var y = params.chunk_y - 1; y < params.chunk_y + CHUNK_SIZE_Y + 1; y++ ) {
        let index = ( y - params.chunk_y + 1 ) * CHUNK_SIZE_INTERNAL_Z * CHUNK_SIZE_INTERNAL_X + col_idx;
#include "common/map_logic.hpp"
        cells[ index ] = finalBlockId;
    }
}
