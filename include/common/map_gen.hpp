#pragma once


#include "chunk.hpp"
#define WATER_LEVEL 0
#define MOUNTAIN_CAP_HEIGHT 20
#define DIRT_SURFACE_THICKNESS 3
#define CAVE_THRESHOLD 0.3f

class MapGen {
  public:
    static void load_block_cuda( Chunk *chunk );
    static void load_block_hip( Chunk *chunk );
    static void load_block_c( const Chunk *chunk );
    static void free_block( Chunk *chunk );
    static int supports_cuda( );
    static int host_supports_cuda( );
    static int supports_hip( );
    static int host_supports_hip( );
    static float calculateTerrainHeight( int x, int z );
    // Theoretical maximum of calculateTerrainHeight, derived from component formulas.
    // Used for O(1) empty-chunk detection without perlin noise evaluation.
    static float maxTerrainHeight( );
};
void map_gen_load_block_cuda( glm::ivec3 *chunk_pos, BlockState *blocks );
void map_gen_load_block_hip( glm::ivec3 *chunk_pos, BlockState *blocks );