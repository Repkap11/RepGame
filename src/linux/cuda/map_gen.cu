#include <stdlib.h>

#include "linux/cuda/perlin_noise.hpp"

__device__ float map_gen_hills_cuda( int x, int z ) {
    float noise = perlin_noise2d_cuda( x, z, 0.02f, 3, MAP_SEED );
    return ( noise - 0.5f ) * 15;
}

__device__ float map_gen_ground_noise_cuda( const int x, const int z ) {
    float noise = perlin_noise2d_cuda( x, z, 0.1f, 2, MAP_SEED + 1 );
    return ( noise - 0.5f ) * 2;
}

__device__ float map_gen_mountains_cuda( const int x, const int z ) {
    float noise = perlin_noise2d_cuda( x, z, 0.008f, 3, MAP_SEED + 2 );
    noise = noise - 0.5f;
    if ( noise < 0 ) {
        noise = 0;
    }
    float mountians = noise * noise * noise * 1000;
    return mountians;
}

__device__ float map_gen_mountains_block_cuda( const int x, const int z ) {
    float noise = perlin_noise2d_cuda( x, z, 0.4f, 2, MAP_SEED + 3 );
    return noise;
}

__device__ float map_gen_under_water_block_cuda( const int x, const int z ) {
    float noise = perlin_noise2d_cuda( x, z, 0.2f, 2, MAP_SEED + 4 );
    return noise;
}

__device__ float map_gen_level_cuda( const int x, const int z ) {
    float noise_orig = perlin_noise2d_cuda( x, z, 0.004f, 2, MAP_SEED + 5 );
    noise_orig = ( noise_orig - 0.5f ) * 10;
    float noise = fabs( noise_orig );
    noise = noise * noise_orig;
    noise = noise > 1 ? 1 : noise;
    noise = noise < -1 ? -1 : noise;

    return noise * 10;
}

__device__ float map_gen_cave_density_cuda( int x, int y, int z ) {
    float noise = perlin_noise3d_cuda( x, y, z, 0.03f, 3, MAP_SEED + 6 );
    return noise;
}

__device__ float map_gen_is_iron_ore_instead_of_stone_cuda( int x, int y, int z ) {
    float noise = perlin_noise3d_cuda( x, y, z, 0.2f, 4, MAP_SEED + 7 );
    noise = noise * noise;
    return noise;
}

__device__ float map_gen_is_coal_ore_instead_of_stone_cuda( int x, int y, int z ) {
    float noise = perlin_noise3d_cuda( x, y, z, 0.2f, 4, MAP_SEED + 8 );
    noise = noise * noise;
    return noise;
}

__device__ float map_gen_is_gold_ore_instead_of_stone_cuda( int x, int y, int z ) {
    float noise = perlin_noise3d_cuda( x, y, z, 0.2f, 4, MAP_SEED + 9 );
    noise = noise * noise;
    return noise;
}

__device__ float map_gen_inverse_lerp_cuda( float min, float max, float value ) {
    if ( value < min ) {
        return 0.0f;
    }
    if ( value > max ) {
        return 1.0f;
    }
    return ( value - min ) / ( max - min );
}

#define MAP_GEN( func, ... ) map_gen_##func##_cuda( __VA_ARGS__ )

__global__ void cuda_set_block( BlockState *blocks, int chunk_x, int chunk_y, int chunk_z ) {
    int col = blockIdx.x * blockDim.x + threadIdx.x;
    if ( col < CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Z ) {
        int x = ( col / CHUNK_SIZE_INTERNAL_Z ) - 1 + chunk_x;
        int z = ( col % CHUNK_SIZE_INTERNAL_Z ) - 1 + chunk_z;

        // Compute 2D terrain height once per column (10 octaves of noise)
        float ground_noise = map_gen_ground_noise_cuda( x, z );
        float hills = map_gen_hills_cuda( x, z );
        float mountians = map_gen_mountains_cuda( x, z );
        float level = map_gen_level_cuda( x, z );
        float terrainHeight = level + mountians + hills + ground_noise;

        // Loop over Y; only 3D noise (caves, ores) is recomputed per Y
        for ( int y = chunk_y - 1; y < chunk_y + CHUNK_SIZE_Y + 1; y++ ) {
            int index = ( y - chunk_y + 1 ) * CHUNK_SIZE_INTERNAL_Z * CHUNK_SIZE_INTERNAL_X + col;
#include "common/map_logic.hpp"

            blocks[ index ] = { finalBlockId, BLOCK_ROTATE_0, 0, finalBlockId }; // Assumes all blocks don't spawn with redstone power
        }
    }
}

#define NUM_THREADS_PER_BLOCK 256

__host__ void map_gen_load_block_cuda( glm::ivec3 *chunk_pos, BlockState *blocks ) {

    BlockState *device_blocks;
    cudaMalloc( &device_blocks, CHUNK_BLOCK_SIZE * sizeof( BlockState ) );

    int num_columns = CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Z;
    cuda_set_block<<<( num_columns + ( NUM_THREADS_PER_BLOCK - 1 ) ) / NUM_THREADS_PER_BLOCK, NUM_THREADS_PER_BLOCK, 0>>>( device_blocks, chunk_pos->x * CHUNK_SIZE_X, chunk_pos->y * CHUNK_SIZE_Y, chunk_pos->z * CHUNK_SIZE_Z );

    cudaMemcpy( blocks, device_blocks, CHUNK_BLOCK_SIZE * sizeof( BlockState ), cudaMemcpyDeviceToHost );
    cudaFree( device_blocks );
}

__host__ int MapGen::host_supports_cuda( ) {
    int dev = 0;
    return cudaGetDevice( &dev ) == cudaSuccess;
}
