#ifndef HEADER_MAP_GEN_H
#define HEADER_MAP_GEN_H

#include "chunk.hpp"
#define WATER_LEVEL 0
#define MOUNTAN_CAP_HEIGHT 20
#define DIRT_SURFACE_THCKNESS 3
#define CAVE_THRESHOLD 0.3f

class MapGen {
  public:
    static void load_block_cuda( Chunk *chunk );
    static void load_block_c( Chunk *chunk );
    static void free_block( Chunk *chunk );
    static int supports_cuda( );
    static int host_supports_cuda( );
};
void map_gen_load_block_cuda( glm::ivec3 *chunk_pos, BlockState *blocks );
#endif