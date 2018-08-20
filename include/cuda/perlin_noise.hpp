#ifndef HEADER_PERLIN_NOISE_CUDA_H
#define HEADER_PERLIN_NOISE_CUDA_H

#include "RepGame.hpp"
#include "map_gen.hpp"
#include "block_definitions.hpp"

__device__ float perlin_noise_cuda( float x, float y, float freq, int depth, int seed );

#endif