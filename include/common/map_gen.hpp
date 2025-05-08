#pragma once


#include "chunk.hpp"
#define WATER_LEVEL 0
#define MOUNTAIN_CAP_HEIGHT 20
#define DIRT_SURFACE_THICKNESS 3
#define CAVE_THRESHOLD 0.3f

class MapGen {
  public:
    static void load_block_cuda( Chunk *chunk );
    static void load_block_c( const Chunk *chunk );
    static void free_block( Chunk *chunk );
    static int supports_cuda( );
    static int host_supports_cuda( );
    static float calculateTerrainHeight( int x, int z );
};
void map_gen_load_block_cuda( glm::ivec3 *chunk_pos, BlockState *blocks );