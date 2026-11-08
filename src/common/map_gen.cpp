#include <stdlib.h>
#include <vector>

#include "common/RepGame.hpp"
#include "common/map_gen.hpp"
#include "common/block_definitions.hpp"
#include "common/perlin_noise.hpp"
#include "common/chunk_loader.hpp"
#include "common/utils/env_config.hpp"
#include "common/chunk.hpp"

#define MG_QUAL static inline
#define MAP_GEN_PERLIN2D( x, z, f, d, s ) perlin_noise2d( x, z, f, d, s )
#define MAP_GEN_PERLIN3D( x, y, z, f, d, s ) perlin_noise3d( x, y, z, f, d, s )
#include "common/map_gen_fields.hpp"
#include "common/creative_order_gen.hpp"

// Block-world order: the creative list first (material-grouped), then every
// other defined non-AIR id in ascending order. Built once.
static const std::vector<BlockID> &block_world_table( ) {
    static const std::vector<BlockID> table = []( ) {
        std::vector<BlockID> t;
        unsigned char in_list[ LAST_BLOCK_ID ] = { 0 };
        for ( unsigned int i = 0; i < CREATIVE_BLOCK_COUNT; i++ ) {
            // The creative list ends with pure items — not placeable blocks.
            if ( !item_is_block( CREATIVE_BLOCKS[ i ] ) ) {
                continue;
            }
            t.push_back( static_cast<BlockID>( CREATIVE_BLOCKS[ i ] ) );
            in_list[ CREATIVE_BLOCKS[ i ] ] = 1;
        }
        in_list[ AIR ] = 1;
        for ( unsigned int id = 0; id < (unsigned int)LAST_BLOCK_ID; id++ ) {
            if ( !in_list[ id ] && block_id_defined( id ) ) {
                t.push_back( static_cast<BlockID>( id ) );
            }
        }
        return t;
    }( );
    return table;
}

BlockID MapGen::block_world_id( const int x, const int y, const int z ) {
    if ( y <= 0 ) {
        // Solid ground: an air void under the floor would make the light
        // cascade run removal BFS over the whole connected void volume.
        return SANDSTONE;
    }
    // Door showcase in the free z<0 region: WOODEN, IRON and BAMBOO pairs
    // (lower at y=1, upper at y=2). Mapgen can't set rotation, so all doors
    // are rotation-0 closed, hinge-left.
    if ( z == -11 && ( y == 1 || y == 2 ) ) {
        static const struct {
            int x;
            BlockID lower;
            BlockID upper;
        } door_showcase[] = {
            { -9, WOODEN_DOOR_LOWER, WOODEN_DOOR_UPPER },
            { -7, IRON_DOOR_LOWER, IRON_DOOR_UPPER },
            { -5, BAMBOO_DOOR, BAMBOO_DOOR_UPPER },
        };
        for ( const auto &d : door_showcase ) {
            if ( d.x == x ) {
                return y == 1 ? d.lower : d.upper;
            }
        }
        return AIR;
    }
    if ( y != 1 ) {
        return AIR;
    }
    // Fence showcase in the free z<0 region (rotation can't be set from
    // mapgen, so all gates are rotation-0 closed and span x). Covers every
    // connection shape: a run with an inline gate, an L corner, a T, a +
    // cross, a lone post, a planks connection, the wooden/nether non-
    // connection, and a nether brick run.
    if ( z < 0 ) {
        static const struct {
            int x, z;
            BlockID id;
        } showcase[] = {
            // Oak run along x with an inline gate.
            { -9, -2, OAK_FENCE }, { -8, -2, OAK_FENCE }, { -7, -2, OAK_FENCE }, { -6, -2, FENCE_GATE }, { -5, -2, OAK_FENCE }, { -4, -2, OAK_FENCE },
            // L corner.
            { -9, -5, OAK_FENCE }, { -8, -5, OAK_FENCE }, { -9, -6, OAK_FENCE },
            // T junction: crossbar along x, stem along -z.
            { -5, -5, OAK_FENCE }, { -4, -5, OAK_FENCE }, { -3, -5, OAK_FENCE }, { -4, -6, OAK_FENCE }, { -4, -7, OAK_FENCE },
            // + cross.
            { 0, -5, OAK_FENCE }, { -1, -5, OAK_FENCE }, { 1, -5, OAK_FENCE }, { 0, -4, OAK_FENCE }, { 0, -6, OAK_FENCE },
            // Single post on its own.
            { 4, -4, OAK_FENCE },
            // Fence against planks connects.
            { 6, -4, OAK_FENCE }, { 7, -4, OAK_PLANKS },
            // Oak touching nether brick must NOT connect.
            { 6, -8, OAK_FENCE }, { 7, -8, NETHER_BRICK_FENCE },
            // Nether brick run.
            { 9, -8, NETHER_BRICK_FENCE }, { 10, -8, NETHER_BRICK_FENCE }, { 11, -8, NETHER_BRICK_FENCE },
            // Stairs (rotation is always 0, so every showcase stair faces
            // -z): a run along z to walk up, a row along x to eyeball the
            // straight model's silhouette, and a mixed-material pair.
            { -2, -13, OAK_STAIRS }, { -2, -14, OAK_STAIRS }, { -2, -15, OAK_STAIRS }, { -2, -16, OAK_STAIRS },
            { 2, -14, OAK_STAIRS }, { 4, -14, OAK_STAIRS }, { 6, -14, STONE_STAIRS }, { 8, -14, COBBLED_DEEPSLATE_STAIRS },
        };
        for ( const auto &s : showcase ) {
            if ( s.x == x && s.z == z ) {
                return s.id;
            }
        }
        return AIR;
    }
    // 32 per row along +x starting at x=2, rows stepping +z from z=2; one
    // air cell between neighbours in both directions.
    if ( x < 2 || z < 2 || ( ( x - 2 ) & 1 ) || ( ( z - 2 ) & 1 ) ) {
        return AIR;
    }
    const int col = ( x - 2 ) / 2;
    const int row = ( z - 2 ) / 2;
    if ( col >= 32 ) {
        return AIR;
    }
    const size_t k = (size_t)row * 32 + (size_t)col;
    const std::vector<BlockID> &t = block_world_table( );
    return k < t.size( ) ? t[ k ] : AIR;
}

BlockID MapGen::flat_world_id( const int x, const int y, const int z ) {
    // Solid ground like block_world: an air void under the floor would make
    // the light cascade run removal BFS over the whole connected void volume.
    return y <= 0 ? SANDSTONE : AIR;
}

float MapGen::calculateTerrainHeight( const int x, const int z ) {
#if REPGAME_MAP_GEN_LEGACY
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
#if REPGAME_MAP_GEN_LEGACY
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
        // Mesa terracing can add up to MESA_MAX_RISE on desert columns;
        // OVERHANG_MAX_RISE covers both overhang sheets and island tops.
        const float bound = max_ground + max_hills + max_level + max_rolling + mountains + MESA_MAX_RISE + OVERHANG_MAX_RISE;
        pr_debug( "maxTerrainHeight: ground=%.2f hills=%.2f level=%.2f rolling=%.2f mountains=%.2f mesa=%.2f overhang=%.2f → %.2f",
            max_ground, max_hills, max_level, max_rolling, mountains, MESA_MAX_RISE, OVERHANG_MAX_RISE, bound );
        return bound;
#endif
    }( );
    return max_height;
}

BlockID MapGen::gen_block_id( const int x, const int y, const int z ) {
    if ( repgame_block_world( ) ) {
        return block_world_id( x, y, z );
    }
    if ( repgame_flat_world( ) ) {
        return flat_world_id( x, y, z );
    }
#if REPGAME_MAP_GEN_LEGACY
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
#if REPGAME_MAP_GEN_LEGACY
            const float terrainHeight = mgl_base_height( x, z );
#else
            const MapGenColumn col = mg_column_info( x, z );
#endif
            for ( int y = chunk_offset.y - 1; y < chunk_offset.y + CHUNK_SIZE_INTERNAL_Y - 1; y++ ) {
                glm::ivec3 offset = glm::ivec3( x, y, z );
                const int index = Chunk::get_index_from_coords( offset - chunk_offset );
#include "common/map_logic.hpp"

                chunk->cells[ index ] = static_cast<BlockCell>( finalBlockId ); // Canonical state; waterlogged bit 0. Assumes no block spawns with redstone power.
            }
        }
    }
}

#if !defined( REPGAME_BUILD_WITH_CUDA )
int MapGen::supports_cuda( ) {
    return 0;
}
void map_gen_load_block_cuda( glm::ivec3 *chunk_pos, BlockCell *cells ) {
}
void MapGen::load_block_cuda( Chunk *chunk ) {
}

#else
int MapGen::supports_cuda( ) {
    // Resolved once: queried per chunk on the mapgen path, and device
    // presence can't change mid-process.
    static const int supported = MapGen::host_supports_cuda( );
    return supported;
}

void MapGen::load_block_cuda( Chunk *chunk ) {
    glm::ivec3 *chunk_pos = &chunk->chunk_pos;
    map_gen_load_block_cuda( chunk_pos, chunk->cells );
}

#endif

#if !defined( REPGAME_BUILD_WITH_HIP )
int MapGen::supports_hip( ) {
    return 0;
}
bool map_gen_load_block_hip( glm::ivec3 *chunk_pos, BlockCell *cells ) {
    return false;
}
bool MapGen::load_block_hip( Chunk *chunk ) {
    return false;
}

#else
int MapGen::supports_hip( ) {
    // REPGAME_NO_HIP=1 forces the CPU path — used to isolate GPU memory
    // corruption seen as rare single-frame garbage pixels during load-in.
    // Resolved once: queried per chunk (mapgen, mesh, overlay) and device
    // presence can't change mid-process — a lost device still falls back
    // per-chunk via load_block_hip returning false.
    static const int supported = []( ) {
        if ( env_is_set( "REPGAME_NO_HIP" ) ) {
            return 0;
        }
        return MapGen::host_supports_hip( );
    }( );
    return supported;
}

bool MapGen::load_block_hip( Chunk *chunk ) {
    glm::ivec3 *chunk_pos = &chunk->chunk_pos;
    return map_gen_load_block_hip( chunk_pos, chunk->cells );
}

#endif

#if !defined( REPGAME_WASM )
// WebGPU mapgen lives only on WASM; non-WASM builds stub it out.
int MapGen::supports_webgpu( ) {
    return 0;
}
bool MapGen::load_block_webgpu( Chunk *chunk ) {
    return false;
}
#endif
