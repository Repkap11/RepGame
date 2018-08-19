#ifndef HEADER_PERLIN_NOISE_CUDA_H
#define HEADER_PERLIN_NOISE_CUDA_H

#include "RepGame.h"
#include "map_gen.h"
#include "block_definitions.h"

__device__ float perlin_noise_cuda( float x, float y, float freq, int depth, int seed );

#endif