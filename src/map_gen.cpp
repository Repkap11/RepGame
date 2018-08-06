#include "RepGame.h"
#include "map_gen.h"
#include "block_definitions.h"
#include "perlin_noise.h"
#include "chunk_loader.h"
#include "chunk.h"

#include <stdlib.h>
#define WATER_LEVEL 0
#define MOUNTAN_CAP_HEIGHT 50

float map_gen_hills( int x, int z ) {
    float noise = perlin_noise( x, z, 0.02f, 3, MAP_SEED );
    return ( noise - 0.5f ) * 15;
}

float map_gen_ground_noise( int x, int z ) {
    float noise = perlin_noise( x, z, 0.1f, 2, MAP_SEED + 1 );
    return ( noise - 0.5f ) * 2;
}

float map_gen_level( int x, int z ) {
    float noise_orig = perlin_noise( x, z, 0.004f, 2, MAP_SEED + 5 );
    noise_orig = ( noise_orig - 0.5f ) * 10;
    float noise = fabs( noise_orig );
    noise = noise * noise_orig;
    noise = noise > 1 ? 1 : noise;
    noise = noise < -1 ? -1 : noise;

    return noise * 10;
}

float map_gen_mountians( int x, int z ) {
    float noise = perlin_noise( x, z, 0.008f, 3, MAP_SEED + 2 );
    noise = noise - 0.5f;
    if ( noise < 0 ) {
        noise = 0;
    }
    float mountians = noise * noise * noise * 1000;
    return mountians;
}

float map_gen_mountian_block( int x, int z ) {
    float noise = perlin_noise( x, z, 0.8f, 8, MAP_SEED + 3 );
    return noise;
}

float map_gen_under_water_block( int x, int z ) {
    float noise = perlin_noise( x, z, 0.2f, 2, MAP_SEED + 4 );
    return noise;
}

#define MAP_GEN( func, ... ) map_gen_##func( __VA_ARGS__ )

void map_gen_load_block_c( Chunk *chunk ) {
    int chunk_offset_x = chunk->chunk_x * CHUNK_SIZE;
    int chunk_offset_y = chunk->chunk_y * CHUNK_SIZE;
    int chunk_offset_z = chunk->chunk_z * CHUNK_SIZE;
    for ( int x = chunk_offset_x - 1; x < chunk_offset_x + CHUNK_SIZE_INTERNAL - 1; x++ ) {
        for ( int z = chunk_offset_z - 1; z < chunk_offset_z + CHUNK_SIZE_INTERNAL - 1; z++ ) {
            float ground_noise = map_gen_ground_noise( x, z );
            float hills = map_gen_hills( x, z );
            float mountians = map_gen_mountians( x, z );
            float level = map_gen_level( x, z );
            float terrainHeight = level + mountians + hills + ground_noise;
            // terrainHeight = biome;

            for ( int y = chunk_offset_y - 1; y < chunk_offset_y + CHUNK_SIZE_INTERNAL - 1; y++ ) {

                int index = chunk_get_index_from_coords( x, y, z );
#include "map_logic.h"
                chunk->blocks[ index ] = finalBlockId;
            }
        }
    }
}