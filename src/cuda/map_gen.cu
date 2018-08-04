#include "RepGame.h"
#include "map_gen.h"
#include "block_definitions.h"
#include "cuda/perlin_noise.h"
#include "chunk_loader.h"
#include "chunk.h"

#include <stdlib.h>
#define WATER_LEVEL 0
#define MOUNTAN_CAP_HEIGHT 50

__device__ float map_gen_hills( int x, int z ) {
    float noise = perlin_noise_cuda( x, z, 0.02f, 3, MAP_SEED );
    return ( noise - 0.5f ) * 15;
}

__device__ float map_gen_ground_noise( int x, int z ) {
    float noise = perlin_noise_cuda( x, z, 0.1f, 2, MAP_SEED + 1 );
    return ( noise - 0.5f ) * 2;
}

__device__ float map_gen_level( int x, int z ) {
    float noise_orig = perlin_noise_cuda( x, z, 0.004f, 2, MAP_SEED + 5 );
    noise_orig = ( noise_orig - 0.5f ) * 10;
    float noise = fabs( noise_orig );
    noise = noise * noise_orig;
    noise = noise > 1 ? 1 : noise;
    noise = noise < -1 ? -1 : noise;

    return noise * 10;
}

__device__ float map_gen_mountians( int x, int z ) {
    float noise = perlin_noise_cuda( x, z, 0.008f, 3, MAP_SEED + 2 );
    noise = noise - 0.5f;
    if ( noise < 0 ) {
        noise = 0;
    }
    float mountians = noise * noise * noise * 1000;
    return mountians;
}

__device__ float map_gen_mountian_block( int x, int z ) {
    float noise = perlin_noise_cuda( x, z, 0.8f, 8, MAP_SEED + 3 );
    return noise;
}

__device__ float map_gen_under_water_block( int x, int z ) {
    float noise = perlin_noise_cuda( x, z, 0.2f, 2, MAP_SEED + 4 );
    return noise;
}

__global__ void cuda_set_block(BlockID* blocks, int chunk_x, int chunk_y, int chunk_z){
    int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < CHUNK_BLOCK_SIZE){
        int y = ( index / ( CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL ) ) - 1;
        int x = ( ( index / CHUNK_SIZE_INTERNAL ) % CHUNK_SIZE_INTERNAL ) - 1;
        int z = ( index % ( CHUNK_SIZE_INTERNAL ) ) - 1;
        x += chunk_x;
        y += chunk_y;
        z += chunk_z;

        float ground_noise = map_gen_ground_noise(x, z);
        float hills = map_gen_hills(x, z);
        float mountians = map_gen_mountians( x, z);
        float level = map_gen_level(x, z);
        float terrainHeight = level + mountians + hills + ground_noise;
#include "map_logic.h"

        blocks[index] = finalBlockId;
    }
}

#define NUM_THREADS_PER_BLOCK 256

__host__ void map_gen_load_block_cuda( Chunk *chunk ) {

    BlockID* device_blocks;
    cudaMalloc(&device_blocks, CHUNK_BLOCK_SIZE * sizeof( BlockID ));

    cuda_set_block<<<(CHUNK_BLOCK_SIZE + (NUM_THREADS_PER_BLOCK-1))/NUM_THREADS_PER_BLOCK , NUM_THREADS_PER_BLOCK, 0>>>(device_blocks,
        chunk->chunk_x * CHUNK_SIZE,
        chunk->chunk_y * CHUNK_SIZE,
        chunk->chunk_z * CHUNK_SIZE);

    cudaMemcpy(chunk->blocks, device_blocks, CHUNK_BLOCK_SIZE * sizeof( BlockID) , cudaMemcpyDeviceToHost);
    cudaFree(device_blocks);
}