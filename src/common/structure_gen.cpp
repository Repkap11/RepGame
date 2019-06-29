#include "common/structure_gen.hpp"
#include "common/RepGame.hpp"
#include "common/perlin_noise.hpp"
#include "common/constants.hpp"
#include "common/block_definitions.hpp"

int structure_gen_is_forest( int x, int z ) {
    float noise = perlin_noise( x, z, 0.02f, 3, MAP_SEED + 6 );
    return noise > 0.7;
}
int structure_gen_is_tree_roll( int x, int z ) {
    float noise = perlin_noise( x, z, 1.0f, 8, MAP_SEED + 7 );
    return noise > 0.7;
}

int strcuture_gen_tree_fits( Chunk *chunk, int x, int y, int z ) {
    if ( y + 4 >= CHUNK_SIZE_INTERNAL - 2 ) {
        return 0;
    }
    for ( int bound_x = -1; bound_x < 2; bound_x++ ) {
        for ( int bound_z = -1; bound_z < 2; bound_z++ ) {
            for ( int bound_y = 0; bound_y < 5; bound_y++ ) {
                if ( chunk->blocks[ chunk_get_index_from_coords( x + bound_x, y + bound_y, z + bound_z ) ] != AIR ) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

void structure_gen_place_trees( Chunk *chunk ) {
    int chunk_offset_x = chunk->chunk_x * CHUNK_SIZE;
    int chunk_offset_y = chunk->chunk_y * CHUNK_SIZE;
    int chunk_offset_z = chunk->chunk_z * CHUNK_SIZE;
    for ( int x = 1; x < CHUNK_SIZE - 1; x++ ) {
        int world_x = chunk_offset_x + x;
        for ( int z = 1; z < CHUNK_SIZE - 1; z++ ) {
            int world_z = chunk_offset_z + z;
            if ( !structure_gen_is_forest( world_x, world_z ) ) {
                continue;
            }
            if ( !structure_gen_is_tree_roll( world_x, world_z ) ) {
                continue;
            }
            for ( int y = CHUNK_SIZE - 1; y > -1; y-- ) {
                // If this block is air
                if ( chunk->blocks[ chunk_get_index_from_coords( x, y, z ) ] != AIR ) {
                    break;
                }
                // And the block below is solid
                BlockID block_below = chunk->blocks[ chunk_get_index_from_coords( x, y - 1, z ) ];
                if ( block_below != GRASS ) {
                    continue;
                }
                // And the blocks in the bounding box are not solid
                if ( strcuture_gen_tree_fits( chunk, x, y, z ) ) {
                    chunk->blocks[ chunk_get_index_from_coords( x, y-1, z ) ] = DIRT;
                    chunk->blocks[ chunk_get_index_from_coords( x, y, z ) ] = OAK_LOG;
                    chunk->blocks[ chunk_get_index_from_coords( x, y + 1, z ) ] = OAK_LOG;
                    chunk->blocks[ chunk_get_index_from_coords( x, y + 2, z ) ] = OAK_LOG;
                    chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z ) ] = LEAF;
                    chunk->blocks[ chunk_get_index_from_coords( x, y + 4, z ) ] = LEAF;

                    chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 3, z ) ] = LEAF;
                    chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 3, z ) ] = LEAF;
                    chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 3, z + 1 ) ] = LEAF;
                    chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 3, z + 1 ) ] = LEAF;
                    chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 3, z - 1 ) ] = LEAF;
                    chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 3, z - 1 ) ] = LEAF;
                    chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z + 1 ) ] = LEAF;
                    chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z - 1 ) ] = LEAF;

                    chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 2, z ) ] = LEAF;
                    chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 2, z ) ] = LEAF;
                    chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 2, z + 1 ) ] = LEAF;
                    chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 2, z + 1 ) ] = LEAF;
                    chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 2, z - 1 ) ] = LEAF;
                    chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 2, z - 1 ) ] = LEAF;
                    chunk->blocks[ chunk_get_index_from_coords( x, y + 2, z + 1 ) ] = LEAF;
                    chunk->blocks[ chunk_get_index_from_coords( x, y + 2, z - 1 ) ] = LEAF;
                }
            }
        }
    }
}