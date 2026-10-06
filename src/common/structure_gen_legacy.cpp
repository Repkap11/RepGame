// Legacy structure generation: the pre-overhaul trees/flowers/reeds as they
// existed at commit 101a348, selected when REPGAME_MAP_GEN_LEGACY is 1
// (see constants.hpp). This file compiles to nothing in new-terrain builds.
//
// Faithful port, with two mechanical adaptations only:
//   - chunk.blocks[] direct access -> Chunk::get_block / set_block, which
//     bounds-check (the original could index 1 cell past the +-1 border)
//     and carry the waterlogged-normalization rules.
//   - BlockState gained the trailing `waterlogged` field.
#include "common/structure_gen.hpp"
#include "common/constants.hpp"

#if REPGAME_MAP_GEN_LEGACY

#include "common/RepGame.hpp"
#include "common/perlin_noise.hpp"
#include "common/constants.hpp"
#include "common/block_definitions.hpp"

static int sgl_is_forest( const int x, const int z ) {
    const float noise = perlin_noise2d( x, z, 0.02f, 3, MAP_SEED + 6 );
    return noise > 0.7f;
}

static int sgl_is_tree_roll( const int x, const int z, const int max_tree_radius ) {
    float noise = perlin_noise2d( x, z, 0.9f, 1, MAP_SEED + 7 );
    if ( FOREST_DEBUG ) {
        return -1;
    }
    if ( noise < 0.60f ) {
        return 0;
    }
    if ( max_tree_radius == 2 ) {
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

static BlockID sgl_is_long_grass_roll( int x, int z, int is_forest ) {
    int is_tuftland = 0;
    if ( is_forest ) {
        is_tuftland = 1;
    } else {
        const float noise = perlin_noise2d( x, z, 0.03f, 1, MAP_SEED + 8 );
        if ( noise > 0.7f ) {
            is_tuftland = 1;
        }
    }
    if ( is_tuftland ) {
        const float noise2 = perlin_noise2d( x, z, 1.0f, 3, MAP_SEED + 9 );
        if ( noise2 > 0.95 ) {
            return YELLOW_FLOWER;
        }

        if ( noise2 > 0.93 ) {
            return RED_FLOWER;
        }

        if ( noise2 > 0.91 ) {
            return BLUE_FLOWER;
        }

        if ( noise2 > 0.8 ) {
            return GRASS_TUFT2;
        }
    }

    return AIR;
}

static int sgl_is_reed_roll( int x, int z ) {
    float noise = perlin_noise2d( x, z, 1.0f, 1, MAP_SEED + 10 );
    return noise > 0.95f;
}

static int sgl_poll_fits( int x, int y, int z, const int height ) {
    if ( ( y + height ) >= CHUNK_SIZE_INTERNAL_Y - 1 ) {
        return 0;
    }
    return 1;
}

#define LEGACY_MAX_TREE_HEIGHT 6
static int sgl_tree_fits( const Chunk &chunk, int x, const int y, const int z, const int max_tree_radius ) {
    if ( y + LEGACY_MAX_TREE_HEIGHT - 3 + max_tree_radius >= CHUNK_SIZE_INTERNAL_Y - 1 ) {
        return 0;
    }
    int num_colliding_leafs = 0;
    for ( int bound_y = 2; bound_y < LEGACY_MAX_TREE_HEIGHT; bound_y++ ) {
        int xz_limit = max_tree_radius;
        xz_limit = bound_y < xz_limit ? bound_y - 2 : xz_limit;

        for ( int bound_x = -xz_limit; bound_x < ( xz_limit + 1 ); bound_x++ ) {
            for ( int bound_z = -xz_limit; bound_z < ( xz_limit + 1 ); bound_z++ ) {
                if ( abs( bound_x ) + abs( bound_z ) > max_tree_radius ) {
                    continue;
                }
                BlockID colliding_block_id = chunk.get_block( glm::ivec3( x + bound_x, y + bound_y, z + bound_z ) ).id;
                if ( colliding_block_id == LEAF ) {
                    num_colliding_leafs++;
                    continue;
                }
                if ( colliding_block_id != AIR ) {
                    return 0;
                }
            }
        }
    }
    if ( num_colliding_leafs < max_tree_radius * 3 ) {
        return 1;
    } else {
        return 0;
    }
}

static int sgl_is_next_to( const Chunk &chunk, const int x, const int y, const int z, const BlockID block ) {
    for ( int i = -1; i < 2; i++ ) {
        for ( int j = -1; j < 2; j++ ) {
            if ( i == 0 && j == 0 ) {
                continue;
            }
            if ( chunk.get_block( glm::ivec3( x + i, y, z + j ) ).id == block ) {
                return 1;
            }
        }
    }
    return 0;
}

static void sgl_place_leaves( Chunk &chunk, const int x, const int y, const int z, const int tree_type, const BlockState &leaf_state ) {
    if ( tree_type == 1 ) { // This is a small tree, place up to 4 tall
        chunk.set_block( glm::ivec3( x - 1, y + 3, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 3, z ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 3, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 3, z - 1 ), leaf_state );

        chunk.set_block( glm::ivec3( x - 1, y + 2, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 2, z ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 2, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 2, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 2, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 2, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 2, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 2, z - 1 ), leaf_state );
    }
    if ( tree_type == 2 ) { // This is a small tree, place up to 4 tall
        chunk.set_block( glm::ivec3( x - 1, y + 4, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 4, z ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 4, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 4, z - 1 ), leaf_state );

        chunk.set_block( glm::ivec3( x - 1, y + 3, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 3, z ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 3, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 3, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 3, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 3, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 3, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 3, z - 1 ), leaf_state );

        chunk.set_block( glm::ivec3( x - 1, y + 2, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 2, z ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 2, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 2, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 2, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 2, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 2, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 2, z - 1 ), leaf_state );
    }
    if ( tree_type == 3 ) { // This is a small tree, place up to 4 tall
        chunk.set_block( glm::ivec3( x - 1, y + 4, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 4, z ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 4, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 4, z - 1 ), leaf_state );

        chunk.set_block( glm::ivec3( x - 1, y + 3, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 3, z ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 3, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 3, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 3, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 3, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 3, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 3, z - 1 ), leaf_state );

        chunk.set_block( glm::ivec3( x - 1, y + 2, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 2, z ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 2, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 2, z - 1 ), leaf_state );
    }
    if ( tree_type == 4 ) { // Large tree, place to to 5 tall
        chunk.set_block( glm::ivec3( x - 1, y + 4, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 4, z ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 4, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 4, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 4, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 4, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 4, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 4, z - 1 ), leaf_state );

        chunk.set_block( glm::ivec3( x - 1, y + 3, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 3, z ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 3, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 3, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 3, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 3, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 3, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 3, z - 1 ), leaf_state );

        chunk.set_block( glm::ivec3( x - 2, y + 3, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 2, y + 3, z ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 3, z + 2 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 3, z - 2 ), leaf_state );

        chunk.set_block( glm::ivec3( x - 1, y + 2, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 2, z ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 2, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 2, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 2, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 2, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 2, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 2, z - 1 ), leaf_state );

        chunk.set_block( glm::ivec3( x - 1, y + 5, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 5, z ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 5, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 5, z - 1 ), leaf_state );
    }
    if ( tree_type == 5 ) {
        chunk.set_block( glm::ivec3( x - 1, y + 4, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 4, z ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 4, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 4, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 4, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 4, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 4, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 4, z - 1 ), leaf_state );

        chunk.set_block( glm::ivec3( x - 1, y + 3, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 3, z ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 3, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 3, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 3, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 3, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 3, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 3, z - 1 ), leaf_state );

        chunk.set_block( glm::ivec3( x - 2, y + 3, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 2, y + 3, z ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 3, z + 2 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 3, z - 2 ), leaf_state );

        chunk.set_block( glm::ivec3( x - 1, y + 5, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 5, z ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 5, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 5, z - 1 ), leaf_state );
    }
    if ( tree_type == 6 ) {
        chunk.set_block( glm::ivec3( x - 1, y + 4, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 4, z ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 4, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 4, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 4, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 4, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 4, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 4, z - 1 ), leaf_state );

        chunk.set_block( glm::ivec3( x - 2, y + 4, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 2, y + 4, z ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 4, z + 2 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 4, z - 2 ), leaf_state );

        chunk.set_block( glm::ivec3( x - 1, y + 3, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 3, z ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 3, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 3, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 3, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 3, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 3, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 3, z - 1 ), leaf_state );

        chunk.set_block( glm::ivec3( x - 2, y + 3, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 2, y + 3, z ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 3, z + 2 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 3, z - 2 ), leaf_state );

        chunk.set_block( glm::ivec3( x - 1, y + 2, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 2, z ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 2, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 2, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x - 1, y + 2, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 2, z - 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 2, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 2, z - 1 ), leaf_state );

        chunk.set_block( glm::ivec3( x - 1, y + 5, z ), leaf_state );
        chunk.set_block( glm::ivec3( x + 1, y + 5, z ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 5, z + 1 ), leaf_state );
        chunk.set_block( glm::ivec3( x, y + 5, z - 1 ), leaf_state );
    }
}

void StructureGen::place_legacy( Chunk &chunk ) {
    const float chunk_offset_x = chunk.chunk_pos.x * CHUNK_SIZE_X;
    const float chunk_offset_y = chunk.chunk_pos.y * CHUNK_SIZE_Y;
    const float chunk_offset_z = chunk.chunk_pos.z * CHUNK_SIZE_Z;
    const glm::vec3 chunk_offset = glm::vec3( chunk_offset_x, chunk_offset_y, chunk_offset_z );

    const Block *leaf = block_definition_get_definition( LEAF );
    const Block *oak_log = block_definition_get_definition( OAK_LOG );
    const Block *dirt = block_definition_get_definition( DIRT );
    const Block *reed = block_definition_get_definition( REED );

    const BlockState leaf_state = { LEAF, 0, leaf->initial_redstone_power, LEAF, 0 };
    const BlockState oak_log_state = { OAK_LOG, 0, oak_log->initial_redstone_power, OAK_LOG, 0 };
    const BlockState dirt_state = { DIRT, 0, dirt->initial_redstone_power, DIRT, 0 };
    const BlockState reed_state = { REED, 0, reed->initial_redstone_power, REED, 0 };

    for ( int x = 1; x < CHUNK_SIZE_X - 1; x++ ) {
        const int world_x = chunk_offset.x + x;
        for ( int z = 1; z < CHUNK_SIZE_Z - 1; z++ ) {
            const int world_z = chunk_offset.z + z;
            int max_tree_radius;
            if ( x == 1 || z == 1 || z == CHUNK_SIZE_Z - 2 ) {
                max_tree_radius = 2;
            } else {
                max_tree_radius = 3;
            }
            int surface_y = -1;
            BlockID block_below = AIR;
            for ( int y = CHUNK_SIZE_Y - 1; y > -1; y-- ) {
                // If this block isn't air
                if ( chunk.get_block( glm::ivec3( x, y, z ) ).id != AIR ) {
                    break;
                }
                // And the block below is solid
                block_below = chunk.get_block( glm::ivec3( x, y - 1, z ) ).id;
                if ( block_below == AIR ) {
                    continue;
                }
                surface_y = y;
                break;
            }
            if ( surface_y == -1 ) {
                continue;
            }
            const int y = surface_y;
            if ( block_below == GRASS && y != 0 ) {
                int is_forest = 0;
                int placed_tree = 0;
                is_forest = sgl_is_forest( world_x, world_z );
                if ( is_forest ) {
                    int tree_type = sgl_is_tree_roll( world_x, world_z, max_tree_radius );
                    if ( tree_type != 0 ) {
                        if ( FOREST_DEBUG ) {
                            chunk.set_block( glm::ivec3( x, y, z ), oak_log_state );
                            continue;
                        }
                        // And the blocks in the bounding box are not solid
                        if ( sgl_tree_fits( chunk, x, y, z, max_tree_radius ) ) {

                            placed_tree = 1;
                            chunk.set_block( glm::ivec3( x, y - 1, z ), dirt_state );
                            chunk.set_block( glm::ivec3( x, y, z ), oak_log_state );
                            chunk.set_block( glm::ivec3( x, y + 1, z ), oak_log_state );
                            chunk.set_block( glm::ivec3( x, y + 2, z ), oak_log_state );

                            // tree_type = 3;
                            if ( tree_type == 1 ) { // This is a small tree, but a baby
                                chunk.set_block( glm::ivec3( x, y + 3, z ), leaf_state );

                            } else if ( tree_type <= 3 ) { // This is a small tree
                                chunk.set_block( glm::ivec3( x, y + 3, z ), oak_log_state );
                                chunk.set_block( glm::ivec3( x, y + 4, z ), leaf_state );

                            } else {
                                chunk.set_block( glm::ivec3( x, y + 3, z ), oak_log_state );
                                chunk.set_block( glm::ivec3( x, y + 4, z ), oak_log_state );
                                chunk.set_block( glm::ivec3( x, y + 5, z ), leaf_state );
                            }
                            sgl_place_leaves( chunk, x, y, z, tree_type, leaf_state );
                        }
                    }
                }
                if ( !placed_tree ) {
                    if ( !sgl_is_next_to( chunk, x, y, z, GRASS_TUFT2 ) ) {
                        BlockID grass_type = sgl_is_long_grass_roll( world_x, world_z, is_forest );
                        Block *grass_block = block_definition_get_definition( grass_type );
                        if ( grass_type != AIR ) {
                            chunk.set_block( glm::ivec3( x, y, z ), { grass_type, BLOCK_ROTATE_0, grass_block->initial_redstone_power, grass_type, 0 } );
                        }
                    }
                }
            } // End block below is grass
            if ( block_below == SAND ) {
#define LEGACY_REED_HEIGHT 3
                if ( sgl_is_next_to( chunk, x, y - 1, z, WATER ) ) {
                    if ( sgl_poll_fits( x, y, z, LEGACY_REED_HEIGHT ) ) {
                        // The original rolled reeds on chunk-local (x, z), not
                        // world coords — kept faithfully, quirks and all.
                        if ( sgl_is_reed_roll( x, z ) ) {
                            for ( int i = 0; i < LEGACY_REED_HEIGHT; i++ ) {
                                chunk.set_block( glm::ivec3( x, y + i, z ), reed_state );
                            }
                        }
                    }
                }
            } // end loops
        }
    }
}

#endif // REPGAME_MAP_GEN_LEGACY
