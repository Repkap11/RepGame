#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#include "common/perlin_noise.hpp"
#include "common/RepGame.hpp"
#include "common/constants.hpp"

// The CPU (extern-linkage) instantiation of the shared dialect perlin —
// perlin_noise_shared.hpp is also compiled __device__ into the CUDA/HIP
// mapgen TUs and preprocessed into the WebGPU WGSL kernel.
#define MG_QUAL
#include "common/perlin_noise_shared.hpp"
