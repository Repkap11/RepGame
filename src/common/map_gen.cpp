#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/map_gen.hpp"
#include "common/block_definitions.hpp"
#include "common/perlin_noise.hpp"
#include "common/chunk_loader.hpp"
#include "common/chunk.hpp"

float map_gen_hills( const int x, const int z ) {
    const float noise = perlin_noise2d( x, z, 0.02f, 3, MAP_SEED );
    return ( noise - 0.5f ) * 15;
}

float map_gen_ground_noise( const int x, const int z ) {
    const float noise = perlin_noise2d( x, z, 0.1f, 2, MAP_SEED + 1 );
    return ( noise - 0.5f ) * 2;
}

float map_gen_mountains( const int x, const int z ) {
    float noise = perlin_noise2d( x, z, 0.008f, 3, MAP_SEED + 2 );
    noise = noise - 0.5f;
    if ( noise < 0 ) {
        noise = 0;
    }
    const float mountains = noise * noise * noise * 1000;
    return mountains;
}

float map_gen_mountains_block( const int x, const int z ) {
    const float noise = perlin_noise2d( x, z, 0.4f, 2, MAP_SEED + 3 );
    return noise;
}

float map_gen_under_water_block( const int x, const int z ) {
    float noise = perlin_noise2d( x, z, 0.2f, 2, MAP_SEED + 4 );
    return noise;
}

float map_gen_level( const int x, const int z ) {
    float noise_orig = perlin_noise2d( x, z, 0.004f, 2, MAP_SEED + 5 );
    noise_orig = ( noise_orig - 0.5f ) * 10;
    float noise = fabs( noise_orig );
    noise = noise * noise_orig;
    noise = noise > 1 ? 1 : noise;
    noise = noise < -1 ? -1 : noise;

    return noise * 10;
}

float map_gen_cave_density( const int x, const int y, const int z ) {
    float noise = perlin_noise3d( x, y, z, 0.03f, 3, MAP_SEED + 6 );
    return noise;
}

float map_gen_is_iron_ore_instead_of_stone( const int x, const int y, const int z ) {
    float noise = perlin_noise3d( x, y, z, 0.2f, 4, MAP_SEED + 7 );
    noise = noise * noise;
    return noise;
}

float map_gen_is_coal_ore_instead_of_stone( const int x, const int y, const int z ) {
    float noise = perlin_noise3d( x, y, z, 0.2f, 4, MAP_SEED + 8 );
    noise = noise * noise;
    return noise;
}

float map_gen_is_gold_ore_instead_of_stone( const int x, const int y, const int z ) {
    float noise = perlin_noise3d( x, y, z, 0.2f, 4, MAP_SEED + 9 );
    noise = noise * noise;
    return noise;
}

float map_gen_inverse_lerp( const float min, const float max, const float value ) {
    if ( value < min ) {
        return 0.0f;
    }
    if ( value > max ) {
        return 1.0f;
    }
    return ( value - min ) / ( max - min );
}

#define MAP_GEN( func, ... ) map_gen_##func( __VA_ARGS__ )

float MapGen::calculateTerrainHeight( const int x, const int z ) {
    const float ground_noise = map_gen_ground_noise( x, z );
    const float hills = map_gen_hills( x, z );
    const float mountains = map_gen_mountains( x, z );
    const float level = map_gen_level( x, z );
    return level + mountains + hills + ground_noise;
}

void MapGen::load_block_c( const Chunk *chunk ) {
    glm::ivec3 chunk_size = glm::vec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    glm::ivec3 chunk_offset = chunk->chunk_pos * chunk_size;

    for ( int x = chunk_offset.x - 1; x < chunk_offset.x + CHUNK_SIZE_INTERNAL_X - 1; x++ ) {
        for ( int z = chunk_offset.z - 1; z < chunk_offset.z + CHUNK_SIZE_INTERNAL_Z - 1; z++ ) {
            const float terrainHeight = calculateTerrainHeight( x, z );
            // terrainHeight = biome;

            for ( int y = chunk_offset.y - 1; y < chunk_offset.y + CHUNK_SIZE_INTERNAL_Y - 1; y++ ) {
                glm::ivec3 offset = glm::ivec3( x, y, z );
                const int index = Chunk::get_index_from_coords( offset - chunk_offset );
#include "common/map_logic.hpp"

                chunk->blocks[ index ] = { finalBlockId, BLOCK_ROTATE_0, 0, finalBlockId }; // Assumes all blocks don't spawn with redstone power
            }
        }
    }
}

#if !defined( REPGAME_BUILD_WITH_CUDA )
int MapGen::supports_cuda( ) {
    return 0;
}
void map_gen_load_block_cuda( glm::ivec3 *chunk_pos, BlockState *blocks ) {
}
void MapGen::load_block_cuda( Chunk *chunk ) {
}

#else
int MapGen::supports_cuda( ) {
    return MapGen::host_supports_cuda( );
}

void MapGen::load_block_cuda( Chunk *chunk ) {
    glm::ivec3 *chunk_pos = &chunk->chunk_pos;
    BlockState *blocks = chunk->blocks;
    map_gen_load_block_cuda( chunk_pos, blocks );
}

#endif
