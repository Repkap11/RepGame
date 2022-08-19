#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/map_gen.hpp"
#include "common/block_definitions.hpp"
#include "common/perlin_noise.hpp"
#include "common/chunk_loader.hpp"
#include "common/chunk.hpp"

float map_gen_hills( int x, int z ) {
    float noise = perlin_noise2d( x, z, 0.02f, 3, MAP_SEED );
    return ( noise - 0.5f ) * 15;
}

float map_gen_ground_noise( int x, int z ) {
    float noise = perlin_noise2d( x, z, 0.1f, 2, MAP_SEED + 1 );
    return ( noise - 0.5f ) * 2;
}

float map_gen_mountians( int x, int z ) {
    float noise = perlin_noise2d( x, z, 0.008f, 3, MAP_SEED + 2 );
    noise = noise - 0.5f;
    if ( noise < 0 ) {
        noise = 0;
    }
    float mountians = noise * noise * noise * 1000;
    return mountians;
}

float map_gen_mountian_block( int x, int z ) {
    float noise = perlin_noise2d( x, z, 0.4f, 2, MAP_SEED + 3 );
    return noise;
}

float map_gen_under_water_block( int x, int z ) {
    float noise = perlin_noise2d( x, z, 0.2f, 2, MAP_SEED + 4 );
    return noise;
}

float map_gen_level( int x, int z ) {
    float noise_orig = perlin_noise2d( x, z, 0.004f, 2, MAP_SEED + 5 );
    noise_orig = ( noise_orig - 0.5f ) * 10;
    float noise = fabs( noise_orig );
    noise = noise * noise_orig;
    noise = noise > 1 ? 1 : noise;
    noise = noise < -1 ? -1 : noise;

    return noise * 10;
}

float map_gen_cave_density( int x, int y, int z ) {
    float noise = perlin_noise3d( x, y, z, 0.03f, 3, MAP_SEED + 6 );
    return noise;
}

float map_gen_is_iron_ore_instead_of_stone( int x, int y, int z ) {
    float noise = perlin_noise3d( x, y, z, 0.2f, 4, MAP_SEED + 7 );
    noise = noise * noise;
    return noise;
}

float map_gen_is_coal_ore_instead_of_stone( int x, int y, int z ) {
    float noise = perlin_noise3d( x, y, z, 0.2f, 4, MAP_SEED + 8 );
    noise = noise * noise;
    return noise;
}

float map_gen_is_gold_ore_instead_of_stone( int x, int y, int z ) {
    float noise = perlin_noise3d( x, y, z, 0.2f, 4, MAP_SEED + 9 );
    noise = noise * noise;
    return noise;
}

float map_gen_inverse_lerp( float min, float max, float value ) {
    if ( value < min ) {
        return 0.0f;
    }
    if ( value > max ) {
        return 1.0f;
    }
    return ( value - min ) / ( max - min );
}

#define MAP_GEN( func, ... ) map_gen_##func( __VA_ARGS__ )

void map_gen_load_block_c( Chunk *chunk ) {
    glm::ivec3 chunk_offset = chunk->chunk_pos * CHUNK_SIZE;

    for ( int x = chunk_offset.x - 1; x < chunk_offset.x + CHUNK_SIZE_INTERNAL - 1; x++ ) {
        for ( int z = chunk_offset.z - 1; z < chunk_offset.z + CHUNK_SIZE_INTERNAL - 1; z++ ) {
            float ground_noise = map_gen_ground_noise( x, z );
            float hills = map_gen_hills( x, z );
            float mountians = map_gen_mountians( x, z );
            float level = map_gen_level( x, z );
            float terrainHeight = level + mountians + hills + ground_noise;
            // terrainHeight = biome;

            for ( int y = chunk_offset.y - 1; y < chunk_offset.y + CHUNK_SIZE_INTERNAL - 1; y++ ) {
                glm::ivec3 offset = glm::ivec3( x, y, z );
                int index = chunk_get_index_from_coords( offset - chunk_offset );
#include "common/map_logic.hpp"
                chunk->blocks[ index ] = { finalBlockId, BLOCK_ROTATE_0, 0, finalBlockId }; // Assumes all blocks don't spawn with redstone power
            }
        }
    }
}
int map_gen_supports_cuda( ) {

#if !defined( REPGAME_BUILD_WITH_CUDA )
    return 0;
#else
    return map_gen_host_supports_cuda();
#endif
}
