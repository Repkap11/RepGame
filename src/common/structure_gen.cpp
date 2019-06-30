#include "common/structure_gen.hpp"
#include "common/RepGame.hpp"
#include "common/perlin_noise.hpp"
#include "common/constants.hpp"
#include "common/block_definitions.hpp"

int structure_gen_is_forest( int x, int z ) {
    float noise = perlin_noise( x, z, 0.02f, 3, MAP_SEED + 6 );
    return noise > 0.7f;
}
int structure_gen_is_tree_roll( int x, int z, int max_tree_size ) {
    float noise = perlin_noise( x, z, 0.9f, 1, MAP_SEED + 7 );
    if ( FOREST_DEBUG ) {
        return -1;
    }
    if ( noise < 0.60f ) {
        return 0;
    }
    if ( max_tree_size == 3 ) {
        if ( noise < 0.7f ) {
            return 1;
        }
        if ( noise < 0.85f ) {
            return 2;
        }
        return 3;
    } else {
        if ( noise < 0.65f ) {
            return 1;
        }
        if ( noise < 0.68f ) {
            return 2;
        }
        if ( noise < 0.72f ) {
            return 3;
        }
        if ( noise < 0.77f ) {
            return 4;
        }
        if ( noise < 0.85f ) {
            return 5;
        }
        return 6;
    }
}

#define MAX_TREE_HEIGHT 6
int strcuture_gen_tree_fits( Chunk *chunk, int x, int y, int z ) {
    if ( y + MAX_TREE_HEIGHT >= CHUNK_SIZE_INTERNAL - 1 ) {
        return 0;
    }
    int num_colliding_leafs = 0;
    for ( int bound_x = -2; bound_x < 3; bound_x++ ) {
        for ( int bound_z = -2; bound_z < 3; bound_z++ ) {
            for ( int bound_y = 0; bound_y < MAX_TREE_HEIGHT; bound_y++ ) {
                BlockID colliding_blockid = chunk->blocks[ chunk_get_index_from_coords( x + bound_x, y + bound_y, z + bound_z ) ];
                if ( colliding_blockid == LEAF ) {
                    num_colliding_leafs++;
                    continue;
                }
                if ( colliding_blockid != AIR ) {
                    return 0;
                }
            }
        }
    }
    if ( num_colliding_leafs < 4 ) {
        return 1;
    } else {
        return 0;
    }
}

void structure_gen_place_trees( Chunk *chunk ) {
    int chunk_offset_x = chunk->chunk_x * CHUNK_SIZE;
    int chunk_offset_y = chunk->chunk_y * CHUNK_SIZE;
    int chunk_offset_z = chunk->chunk_z * CHUNK_SIZE;
    for ( int x = 1; x < CHUNK_SIZE - 1; x++ ) {
        int world_x = chunk_offset_x + x;
        for ( int z = 1; z < CHUNK_SIZE - 1; z++ ) {
            int world_z = chunk_offset_z + z;
            int max_tree_size;
            if ( x == 1 || z == 1 || z == CHUNK_SIZE - 1 || z == CHUNK_SIZE - 2 ) {
                max_tree_size = 3;
            } else {
                max_tree_size = 5;
            }
            if ( !structure_gen_is_forest( world_x, world_z ) ) {
                continue;
            }
            int tree_type = structure_gen_is_tree_roll( world_x, world_z, max_tree_size );
            if ( tree_type == 0 ) {
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
                if ( tree_type == -1 ) {
                    chunk->blocks[ chunk_get_index_from_coords( x, y, z ) ] = OAK_LOG;
                    continue;
                }
                // And the blocks in the bounding box are not solid
                if ( strcuture_gen_tree_fits( chunk, x, y, z ) ) {
                    chunk->blocks[ chunk_get_index_from_coords( x, y - 1, z ) ] = DIRT;
                    chunk->blocks[ chunk_get_index_from_coords( x, y, z ) ] = OAK_LOG;
                    chunk->blocks[ chunk_get_index_from_coords( x, y + 1, z ) ] = OAK_LOG;
                    chunk->blocks[ chunk_get_index_from_coords( x, y + 2, z ) ] = OAK_LOG;

                    // tree_type = 3;
                    if ( tree_type == 1 ) { // This is a small tree, but a baby
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z ) ] = ( BlockID )( tree_type - 1 + IRON_BLOCK );

                    } else if ( tree_type <= 3 ) { // This is a small tree
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z ) ] = OAK_LOG;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 4, z ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x, y + 4, z ) ] = ( BlockID )( tree_type - 1 + IRON_BLOCK );

                    } else {
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z ) ] = OAK_LOG;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 4, z ) ] = OAK_LOG;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 5, z ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x, y + 5, z ) ] = ( BlockID )( tree_type - 1 + IRON_BLOCK );
                    }

                    if ( tree_type == 1 ) { // This is a small tree, place up to 4 tall
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 3, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 3, z ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 3, z + 1 ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 3, z + 1 ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 3, z - 1 ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 3, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z - 1 ) ] = LEAF;

                        // chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 1, z ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 1, z ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x, y + 1, z + 1 ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x, y + 1, z - 1 ) ] = LEAF;

                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 2, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 2, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 2, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 2, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 2, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 2, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 2, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 2, z - 1 ) ] = LEAF;
                    }
                    if ( tree_type == 2 ) { // This is a small tree, place up to 4 tall
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 4, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 4, z ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 4, z + 1 ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 4, z + 1 ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 4, z - 1 ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 4, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 4, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 4, z - 1 ) ] = LEAF;

                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 3, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 3, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 3, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 3, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 3, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 3, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z - 1 ) ] = LEAF;

                        // chunk->blocks[ chunk_get_index_from_coords( x - 2, y + 2, z ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x + 2, y + 2, z ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x, y + 2, z + 2 ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x, y + 2, z - 2 ) ] = LEAF;

                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 2, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 2, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 2, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 2, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 2, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 2, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 2, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 2, z - 1 ) ] = LEAF;
                    }
                    if ( tree_type == 3 ) { // This is a small tree, place up to 4 tall
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 4, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 4, z ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 4, z + 1 ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 4, z + 1 ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 4, z - 1 ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 4, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 4, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 4, z - 1 ) ] = LEAF;

                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 3, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 3, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 3, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 3, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 3, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 3, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z - 1 ) ] = LEAF;

                        // chunk->blocks[ chunk_get_index_from_coords( x - 2, y + 2, z ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x + 2, y + 2, z ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x, y + 2, z + 2 ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x, y + 2, z - 2 ) ] = LEAF;

                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 2, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 2, z ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 4, z + 1 ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 4, z + 1 ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 4, z - 1 ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 4, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 2, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 2, z - 1 ) ] = LEAF;
                    }
                    if ( tree_type == 4 ) { // Large tree, place to to 5 tall
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 4, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 4, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 4, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 4, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 4, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 4, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 4, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 4, z - 1 ) ] = LEAF;

                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 3, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 3, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 3, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 3, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 3, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 3, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z - 1 ) ] = LEAF;

                        chunk->blocks[ chunk_get_index_from_coords( x - 2, y + 3, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 2, y + 3, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z + 2 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z - 2 ) ] = LEAF;

                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 2, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 2, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 2, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 2, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 2, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 2, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 2, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 2, z - 1 ) ] = LEAF;

                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 5, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 5, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 5, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 5, z - 1 ) ] = LEAF;
                    }
                    if ( tree_type == 5 ) {
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 4, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 4, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 4, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 4, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 4, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 4, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 4, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 4, z - 1 ) ] = LEAF;

                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 3, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 3, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 3, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 3, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 3, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 3, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z - 1 ) ] = LEAF;

                        chunk->blocks[ chunk_get_index_from_coords( x - 2, y + 3, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 2, y + 3, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z + 2 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z - 2 ) ] = LEAF;

                        // chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 2, z ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 2, z ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 2, z + 1 ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 2, z + 1 ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 2, z - 1 ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 2, z - 1 ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x, y + 2, z + 1 ) ] = LEAF;
                        // chunk->blocks[ chunk_get_index_from_coords( x, y + 2, z - 1 ) ] = LEAF;

                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 5, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 5, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 5, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 5, z - 1 ) ] = LEAF;
                    }
                    if ( tree_type == 6 ) {
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 4, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 4, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 4, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 4, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 4, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 4, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 4, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 4, z - 1 ) ] = LEAF;

                        chunk->blocks[ chunk_get_index_from_coords( x - 2, y + 4, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 2, y + 4, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 4, z + 2 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 4, z - 2 ) ] = LEAF;

                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 3, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 3, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 3, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 3, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 3, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 3, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z - 1 ) ] = LEAF;

                        chunk->blocks[ chunk_get_index_from_coords( x - 2, y + 3, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 2, y + 3, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z + 2 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 3, z - 2 ) ] = LEAF;

                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 2, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 2, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 2, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 2, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 2, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 2, z - 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 2, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 2, z - 1 ) ] = LEAF;

                        chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 5, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 5, z ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 5, z + 1 ) ] = LEAF;
                        chunk->blocks[ chunk_get_index_from_coords( x, y + 5, z - 1 ) ] = LEAF;
                    }
                }
            }
        }
    }
}