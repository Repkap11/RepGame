#include <stdlib.h>

#include <hip/hip_runtime.h>

#include "common/Logging.hpp"
#include "linux/hip/perlin_noise.hpp"
#include "perlin_noise_impl.inl"

#define MAP_GEN_QUAL __device__
#define MAP_GEN_PERLIN2D( x, z, f, d, s ) perlin_noise2d_hip( x, z, f, d, s )
#define MAP_GEN_PERLIN3D( x, y, z, f, d, s ) perlin_noise3d_hip( x, y, z, f, d, s )
#include "common/map_gen_fields.hpp"

__global__ void hip_set_block( BlockState *blocks, int chunk_x, int chunk_y, int chunk_z ) {
    int col_idx = blockIdx.x * blockDim.x + threadIdx.x;
    if ( col_idx < CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Z ) {
        int x = ( col_idx / CHUNK_SIZE_INTERNAL_Z ) - 1 + chunk_x;
        int z = ( col_idx % CHUNK_SIZE_INTERNAL_Z ) - 1 + chunk_z;

        // Compute 2D column context once per column (biome, height, steepness).
#if REPGAME_MAP_GEN_LEGACY
        const float terrainHeight = mgl_base_height( x, z );
#else
        const MapGenColumn col = mg_column_info( x, z );
#endif

        // Loop over Y; only 3D noise (overhang, caves, ores) is recomputed per Y
        for ( int y = chunk_y - 1; y < chunk_y + CHUNK_SIZE_Y + 1; y++ ) {
            int index = ( y - chunk_y + 1 ) * CHUNK_SIZE_INTERNAL_Z * CHUNK_SIZE_INTERNAL_X + col_idx;
#include "common/map_logic.hpp"

            blocks[ index ] = { finalBlockId, BLOCK_ROTATE_0, 0, finalBlockId, 0 }; // Assumes all blocks don't spawn with redstone power
        }
    }
}

#define NUM_THREADS_PER_BLOCK 256

#define HIP_CHECK( expr )                                                                                              \
    do {                                                                                                                \
        hipError_t err = ( expr );                                                                                      \
        if ( err != hipSuccess ) {                                                                                      \
            pr_debug( "HIP error at %s:%d: %s", __FILE__, __LINE__, hipGetErrorString( err ) );                          \
            return false;                                                                                               \
        }                                                                                                               \
    } while ( 0 )

// Persistent per-worker device buffer — avoids hipMalloc/hipFree page-table
// churn for every chunk during load-in. Same pattern as mesh_gen.hip's
// thread_local buffers.
static thread_local BlockState *d_mapgen_blocks = nullptr;

__host__ bool map_gen_load_block_hip( glm::ivec3 *chunk_pos, BlockState *blocks ) {

    if ( !d_mapgen_blocks ) {
        HIP_CHECK( hipMalloc( &d_mapgen_blocks, CHUNK_BLOCK_SIZE * sizeof( BlockState ) ) );
    }

    int num_columns = CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Z;
    hip_set_block<<<( num_columns + ( NUM_THREADS_PER_BLOCK - 1 ) ) / NUM_THREADS_PER_BLOCK, NUM_THREADS_PER_BLOCK, 0>>>( d_mapgen_blocks, chunk_pos->x * CHUNK_SIZE_X, chunk_pos->y * CHUNK_SIZE_Y, chunk_pos->z * CHUNK_SIZE_Z );
    // Kernel launch errors (e.g. no code object for this GPU's arch) are
    // reported asynchronously; check both the launch and the execution.
    HIP_CHECK( hipGetLastError( ) );
    HIP_CHECK( hipDeviceSynchronize( ) );

    HIP_CHECK( hipMemcpy( blocks, d_mapgen_blocks, CHUNK_BLOCK_SIZE * sizeof( BlockState ), hipMemcpyDeviceToHost ) );

    return true;
}

__host__ int MapGen::host_supports_hip( ) {
    int dev = 0;
    return hipGetDevice( &dev ) == hipSuccess;
}
