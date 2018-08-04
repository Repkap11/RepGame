#include "RepGame.h"
#include "map_gen.h"
#include "block_definitions.h"

__global__ void cuda_set_block(BlockID* blocks, int chunk_x, int chunk_y, int chunk_z){
    int index = blockIdx.x * blockDim.x + threadIdx.x;
    int y = ( index / ( CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL ) ) - 1;
    int x = ( ( index / CHUNK_SIZE_INTERNAL ) % CHUNK_SIZE_INTERNAL ) - 1;
    int z = ( index % ( CHUNK_SIZE_INTERNAL ) ) - 1;

    //x += chunk_x;
    y += chunk_y;
    //z += chunk_z;

    BlockID finalBlockId = AIR;
    if (y < x+z){
        finalBlockId = GRASS;
    }
    blocks[index] = finalBlockId;
}

#define NUM_THREADS_PER_BLOCK 256

__host__ void map_gen_load_block_cuda( Chunk *chunk ) {

    BlockID* device_blocks;
    cudaMalloc(&device_blocks, CHUNK_BLOCK_SIZE * sizeof( BlockID ));

    cuda_set_block<<<(CHUNK_BLOCK_SIZE + (NUM_THREADS_PER_BLOCK-1))/NUM_THREADS_PER_BLOCK , NUM_THREADS_PER_BLOCK>>>(device_blocks,
        chunk->chunk_x * CHUNK_SIZE,
        chunk->chunk_y * CHUNK_SIZE,
        chunk->chunk_z * CHUNK_SIZE);

    cudaMemcpy(chunk->blocks, device_blocks, CHUNK_BLOCK_SIZE * sizeof( BlockID) , cudaMemcpyDeviceToHost);
    cudaFree(device_blocks);
}