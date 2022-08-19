#ifndef HEADER_MAP_GEN_H
#define HEADER_MAP_GEN_H

#include "chunk.hpp"
#define WATER_LEVEL 0
#define MOUNTAN_CAP_HEIGHT 20
#define DIRT_SURFACE_THCKNESS 3
#define CAVE_THRESHOLD 0.3f

void map_gen_load_block_cuda( Chunk *chunk );
void map_gen_load_block_c( Chunk *chunk );
void map_gen_free_block( Chunk *chunk );
int map_gen_supports_cuda();
int map_gen_host_supports_cuda();
#endif