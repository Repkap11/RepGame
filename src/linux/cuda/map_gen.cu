#include <stdlib.h>

// #include "common/RepGame.hpp"
#include "common/map_gen.hpp"
#include "common/block_definitions.hpp"
#include "linux/cuda/perlin_noise.hpp"
#include "common/chunk_loader.hpp"
#include "common/chunk.hpp"

__device__ float map_gen_hills_cuda( int x, int z ) {
    float noise = perlin_noise2d_cuda( x, z, 0.02f, 3, MAP_SEED );
    return ( noise - 0.5f ) * 15;
}

__device__ float map_gen_ground_noise_cuda( int x, int z ) {
    float noise = perlin_noise2d_cuda( x, z, 0.1f, 2, MAP_SEED + 1 );
    return ( noise - 0.5f ) * 2;
}

__device__ float map_gen_mountians_cuda( int x, int z ) {
    float noise = perlin_noise2d_cuda( x, z, 0.008f, 3, MAP_SEED + 2 );
    noise = noise - 0.5f;
    if ( noise < 0 ) {
        noise = 0;
    }
    float mountians = noise * noise * noise * 1000;
    return mountians;
}

__device__ float map_gen_mountian_block_cuda( int x, int z ) {
    float noise = perlin_noise2d_cuda( x, z, 0.4f, 2, MAP_SEED + 3 );
    return noise;
}

__device__ float map_gen_under_water_block_cuda( int x, int z ) {
    float noise = perlin_noise2d_cuda( x, z, 0.2f, 2, MAP_SEED + 4 );
    return noise;
}

__device__ float map_gen_level_cuda( int x, int z ) {
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
    int index = blockIdx.x * blockDim.x + threadIdx.x;
    if ( index < CHUNK_BLOCK_SIZE ) {
        int y = ( index / ( CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL ) ) - 1;
        int x = ( ( index / CHUNK_SIZE_INTERNAL ) % CHUNK_SIZE_INTERNAL ) - 1;
        int z = ( index % ( CHUNK_SIZE_INTERNAL ) ) - 1;
        x += chunk_x;
        y += chunk_y;
        z += chunk_z;

        float ground_noise = map_gen_ground_noise_cuda( x, z );
        float hills = map_gen_hills_cuda( x, z );
        float mountians = map_gen_mountians_cuda( x, z );
        float level = map_gen_level_cuda( x, z );
        float terrainHeight = level + mountians + hills + ground_noise;
#include "common/map_logic.hpp"

        blocks[ index ] = {finalBlockId, BLOCK_ROTATE_0, 0, finalBlockId};//Assumes all blocks don't spawn with redstone power
    }
}

#define NUM_THREADS_PER_BLOCK 256

__host__ void map_gen_load_block_cuda( Chunk *chunk ) {

    BlockState *device_blocks;
    cudaMalloc( &device_blocks, CHUNK_BLOCK_SIZE * sizeof( BlockState ) );

    cuda_set_block<<<( CHUNK_BLOCK_SIZE + ( NUM_THREADS_PER_BLOCK - 1 ) ) / NUM_THREADS_PER_BLOCK, NUM_THREADS_PER_BLOCK, 0>>>( device_blocks, chunk->chunk_x * CHUNK_SIZE, chunk->chunk_y * CHUNK_SIZE, chunk->chunk_z * CHUNK_SIZE );

    cudaMemcpy( chunk->blocks, device_blocks, CHUNK_BLOCK_SIZE * sizeof( BlockState ), cudaMemcpyDeviceToHost );
    cudaFree( device_blocks );
}