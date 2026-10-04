#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/map_gen.hpp"
#include "common/block_definitions.hpp"
#include "common/perlin_noise.hpp"
#include "common/chunk_loader.hpp"
#include "common/chunk.hpp"

#define MAP_GEN_QUAL static inline
#define MAP_GEN_PERLIN2D( x, z, f, d, s ) perlin_noise2d( x, z, f, d, s )
#define MAP_GEN_PERLIN3D( x, y, z, f, d, s ) perlin_noise3d( x, y, z, f, d, s )
#include "common/map_gen_fields.hpp"

float MapGen::calculateTerrainHeight( const int x, const int z ) {
#if defined( REPGAME_MAP_GEN_LEGACY )
    return mgl_base_height( x, z );
#else
    return mg_base_height( x, z );
#endif
}

float MapGen::maxTerrainHeight( ) {
    // perlin_noise2d returns values in [0, 1]. Each terrain component applies a pure
    // transformation to that bounded input. We compute the exact max of each transform
    // by evaluating it across the [0, 1] noise range at startup, then sum them.
    // This is pure arithmetic — no perlin noise evaluation, no world sampling.
    // If a transform formula changes, the max updates automatically.
    static const float max_height = [] {
        constexpr int STEPS = 10000;
        float max_ground = -1e30f, max_hills = -1e30f, max_level = -1e30f;
#if defined( REPGAME_MAP_GEN_LEGACY )
        float max_mountains = -1e30f;
        for ( int i = 0; i <= STEPS; i++ ) {
            float noise = (float)i / STEPS; // [0, 1]
            max_ground = fmax( max_ground, mgl_ground_transform( noise ) );
            max_hills = fmax( max_hills, mgl_hills_transform( noise ) );
            max_mountains = fmax( max_mountains, mgl_mountains_transform( noise ) );
            max_level = fmax( max_level, mgl_level_transform( noise ) );
        }
        const float bound = max_ground + max_hills + max_mountains + max_level;
        pr_debug( "maxTerrainHeight (legacy): ground=%.2f hills=%.2f mountains=%.2f level=%.2f → %.2f",
            max_ground, max_hills, max_mountains, max_level, bound );
        return bound;
#else
        float max_mask = -1e30f, max_ridge = -1e30f, max_rolling = -1e30f;
        for ( int i = 0; i <= STEPS; i++ ) {
            float noise = (float)i / STEPS; // [0, 1]
            max_ground = fmax( max_ground, mg_ground_transform( noise ) );
            max_hills = fmax( max_hills, mg_hills_transform( noise ) );
            max_mask = fmax( max_mask, mg_mountain_mask( noise ) );
            max_ridge = fmax( max_ridge, mg_mountain_ridge( noise ) );
            max_level = fmax( max_level, mg_level_transform( noise ) );
            max_rolling = fmax( max_rolling, mg_rolling_transform( noise ) );
        }
        const float mountains = max_mask * max_ridge * MAX_MOUNTAIN_HEIGHT;
        const float bound = max_ground + max_hills + max_level + max_rolling + mountains + OVERHANG_MAX_RISE;
        pr_debug( "maxTerrainHeight: ground=%.2f hills=%.2f level=%.2f rolling=%.2f mountains=%.2f overhang=%.2f → %.2f",
            max_ground, max_hills, max_level, max_rolling, mountains, OVERHANG_MAX_RISE, bound );
        return bound;
#endif
    }( );
    return max_height;
}

BlockID MapGen::gen_block_id( const int x, const int y, const int z ) {
#if defined( REPGAME_MAP_GEN_LEGACY )
    return mgl_pick_block( x, y, z, mgl_base_height( x, z ) );
#else
    const MapGenColumn col = mg_column_info( x, z );
    return mg_pick_block( x, y, z, col );
#endif
}

void MapGen::load_block_c( const Chunk *chunk ) {
    glm::ivec3 chunk_size = glm::vec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    glm::ivec3 chunk_offset = chunk->chunk_pos * chunk_size;

    for ( int x = chunk_offset.x - 1; x < chunk_offset.x + CHUNK_SIZE_INTERNAL_X - 1; x++ ) {
        for ( int z = chunk_offset.z - 1; z < chunk_offset.z + CHUNK_SIZE_INTERNAL_Z - 1; z++ ) {
#if defined( REPGAME_MAP_GEN_LEGACY )
            const float terrainHeight = mgl_base_height( x, z );
#else
            const MapGenColumn col = mg_column_info( x, z );
#endif
            for ( int y = chunk_offset.y - 1; y < chunk_offset.y + CHUNK_SIZE_INTERNAL_Y - 1; y++ ) {
                glm::ivec3 offset = glm::ivec3( x, y, z );
                const int index = Chunk::get_index_from_coords( offset - chunk_offset );
#include "common/map_logic.hpp"

                chunk->blocks[ index ] = { finalBlockId, BLOCK_ROTATE_0, 0, finalBlockId, 0 }; // Assumes all blocks don't spawn with redstone power
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

#if !defined( REPGAME_BUILD_WITH_HIP )
int MapGen::supports_hip( ) {
    return 0;
}
bool map_gen_load_block_hip( glm::ivec3 *chunk_pos, BlockState *blocks ) {
    return false;
}
bool MapGen::load_block_hip( Chunk *chunk ) {
    return false;
}

#else
int MapGen::supports_hip( ) {
    return MapGen::host_supports_hip( );
}

bool MapGen::load_block_hip( Chunk *chunk ) {
    glm::ivec3 *chunk_pos = &chunk->chunk_pos;
    BlockState *blocks = chunk->blocks;
    return map_gen_load_block_hip( chunk_pos, blocks );
}

#endif
