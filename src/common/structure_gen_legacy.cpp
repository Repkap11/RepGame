// Legacy structure generation: the pre-overhaul trees/flowers/reeds as they
// existed at commit 101a348, selected when REPGAME_MAP_GEN_LEGACY is 1
// (see constants.hpp). This file compiles to nothing in new-terrain builds.
//
// Faithful port, with two mechanical adaptations only:
//   - chunk.blocks[] direct access -> Chunk::get_block / set_block, which
//     bounds-check (the original could index 1 cell past the +-1 border)
//     and carry the waterlogged-normalization rules.
//   - BlockState gained the trailing `waterlogged` field.
//   - Etho trees: ~1 in 100 trees gets a glowstone top log (gameplay
//     deviation from the faithful port).
//
// Trees are no longer clipped at chunk borders: every chunk re-derives all
// trees whose parts could land in its block array (halo included) from pure
// functions of world coords — forest/tree rolls, a mapgen-only surface scan,
// and a mapgen-only fit check — then writes the cells it owns. Both sides of
// a border therefore compute identical trunks and canopies. The original
// sequential scan's collision rules can't be reproduced chunk-locally, so
// they are replaced by a deterministic local-max rule (see
// sgl_tree_suppressed): tree positions and density differ slightly from the
// original. Decorations (tufts, flowers, reeds) are unchanged — they still
// run as a sequential scan over this chunk's interior columns.
#include "common/structure_gen.hpp"
#include "common/constants.hpp"

#if REPGAME_MAP_GEN_LEGACY

#include "common/RepGame.hpp"
#include "common/perlin_noise.hpp"
#include "common/constants.hpp"
#include "common/block_definitions.hpp"
#include "common/map_gen.hpp"
#include <math.h>

static int sgl_is_forest( const int x, const int z ) {
    const float noise = perlin_noise2d( x, z, 0.02f, 3, MAP_SEED + 6 );
    return noise > 0.7f;
}

static int sgl_is_tree_roll( const int x, const int z, float *out_noise ) {
    const float noise = perlin_noise2d( x, z, 0.9f, 1, MAP_SEED + 7 );
    *out_noise = noise;
    if ( FOREST_DEBUG ) {
        return -1;
    }
    if ( noise < 0.60f ) {
        return 0;
    }
    // The radius-2 branch of the old thresholds is gone: it existed only to
    // keep canopies off the +-1 border, and trees now cross borders freely.
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

// ~1/100 trees: raw hash roll (perlin fields don't reach the tails a 1%
// threshold needs). Pure function of the trunk column, so every chunk agrees.
static int sgl_is_etho_tree( const int x, const int z ) {
    return noise2( x, z, MAP_SEED + 11 ) < 3; // 3/256 ~ 1.2%
}

static int sgl_poll_fits( int x, int y, int z, const int height ) {
    if ( ( y + height ) >= CHUNK_SIZE_INTERNAL_Y - 1 ) {
        return 0;
    }
    return 1;
}

#define LEGACY_MAX_TREE_HEIGHT 6
// The old fit box, evaluated against pure map gen (terrain only — no tree
// parts) so every chunk reaches the same verdict. bound_y 2..5, column-only
// at bound_y 2 and the |dx|+|dz| <= 3 diamond above that, exactly like the
// original's radius-3 shape.
static int sgl_tree_fits_terrain( const int wx, const int surface_y, const int wz ) {
    for ( int bound_y = 2; bound_y < LEGACY_MAX_TREE_HEIGHT; bound_y++ ) {
        const int xz_limit = bound_y == 2 ? 0 : 3;
        for ( int bound_x = -xz_limit; bound_x < ( xz_limit + 1 ); bound_x++ ) {
            for ( int bound_z = -xz_limit; bound_z < ( xz_limit + 1 ); bound_z++ ) {
                if ( abs( bound_x ) + abs( bound_z ) > 3 ) {
                    continue;
                }
                if ( MapGen::gen_block_id( wx + bound_x, surface_y + bound_y, wz + bound_z ) != AIR ) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

// The first air cell directly above the first non-air cell, scanning down —
// the same surface the old chunk-array scan found, but evaluated on pure map
// gen so the answer doesn't depend on which chunk is asking. Also yields the
// ground block (the cell below the surface air).
static int sgl_find_surface_y( const int wx, const int wz, BlockID *out_ground ) {
    const float h = MapGen::calculateTerrainHeight( wx, wz );
    // ceil(max(h, WATER_LEVEL)) is guaranteed AIR: above the heightmap nothing
    // is solid, and above the waterline nothing is water.
    int top = (int)ceilf( h );
    if ( top < WATER_LEVEL ) {
        top = WATER_LEVEL;
    }
    for ( int y = top; y > BEDROCK_LEVEL - 8; y-- ) {
        const BlockID below = MapGen::gen_block_id( wx, y - 1, wz );
        if ( below != AIR ) {
            *out_ground = below;
            return y;
        }
    }
    return -1;
}

// The full per-column tree test as a pure function of world coords: forest
// field, tree roll, grass surface, terrain fit box. Returns the tree type
// (1..6; -1 under FOREST_DEBUG), 0 when the column holds no tree. Every chunk
// computes the same answer for a shared (border-crossing) tree.
static int sgl_eval_tree_column( const int wx, const int wz, int *out_surface_y, float *out_prio ) {
    if ( !sgl_is_forest( wx, wz ) ) {
        return 0;
    }
    float noise;
    const int tree_type = sgl_is_tree_roll( wx, wz, &noise );
    if ( tree_type == 0 ) {
        return 0;
    }
    BlockID ground;
    const int surface_y = sgl_find_surface_y( wx, wz, &ground );
    if ( surface_y < 0 || ground != GRASS ) {
        return 0;
    }
    if ( !FOREST_DEBUG && !sgl_tree_fits_terrain( wx, surface_y, wz ) ) {
        return 0;
    }
    *out_surface_y = surface_y;
    *out_prio = noise;
    return tree_type;
}

// The original scan placed trees greedily in column order, rejecting a tree
// when a previously-placed tree's trunk fell inside its fit box (and when too
// many leaves already filled it). That order can't be reproduced per-chunk —
// the winner of a border-spanning pair would depend on which side you ask —
// so it is replaced by a deterministic rule: a candidate is suppressed when a
// rival valid candidate inside the old trunk-collision diamond rolls a higher
// tree noise. Big trees still crowd out small ones, placed trunks stay at
// least a diamond-4 apart, and every chunk agrees on the survivor.
static int sgl_tree_suppressed( const int wx, const int wz, const int surface_y, const float prio ) {
    for ( int dx = -3; dx <= 3; dx++ ) {
        for ( int dz = -3; dz <= 3; dz++ ) {
            const int d = abs( dx ) + abs( dz );
            if ( d == 0 || d > 3 ) {
                continue;
            }
            int q_surface;
            float q_prio;
            if ( sgl_eval_tree_column( wx + dx, wz + dz, &q_surface, &q_prio ) == 0 ) {
                continue;
            }
            // The old reject needed a rival trunk part inside the fit box.
            // The box rows that check an off-axis column are dy 3..5 above
            // the base (dy 2 is column-only) while rival parts span dy -1..4
            // (dirt under the trunk through the top log) — the ranges can
            // only meet when the surfaces are within [-1, +6].
            const int dy = q_surface - surface_y;
            if ( dy > 6 || dy < -1 ) {
                continue;
            }
            if ( q_prio > prio || ( q_prio == prio && ( dx < 0 || ( dx == 0 && dz < 0 ) ) ) ) {
                return 1;
            }
        }
    }
    return 0;
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
    const int cox = chunk.chunk_pos.x * CHUNK_SIZE_X;
    const int coy = chunk.chunk_pos.y * CHUNK_SIZE_Y;
    const int coz = chunk.chunk_pos.z * CHUNK_SIZE_Z;

    const Block *leaf = block_definition_get_definition( LEAF );
    const Block *oak_log = block_definition_get_definition( OAK_LOG );
    const Block *dirt = block_definition_get_definition( DIRT );
    const Block *reed = block_definition_get_definition( REED );
    const Block *glowstone = block_definition_get_definition( GLOWSTONE );

    const BlockState leaf_state = { LEAF, 0, leaf->initial_redstone_power, LEAF, 0 };
    const BlockState oak_log_state = { OAK_LOG, 0, oak_log->initial_redstone_power, OAK_LOG, 0 };
    const BlockState dirt_state = { DIRT, 0, dirt->initial_redstone_power, DIRT, 0 };
    const BlockState reed_state = { REED, 0, reed->initial_redstone_power, REED, 0 };
    const BlockState glowstone_state = { GLOWSTONE, 0, glowstone->initial_redstone_power, GLOWSTONE, 0 };

    // ---- Pass 1: trees ---------------------------------------------------
    // A canopy reaches +-2 cells from its trunk, and the block array extends
    // 1 cell past the interior on every side, so a tree can only drop parts
    // into this chunk when its trunk column sits within 3 cells of the array
    // edge. Every candidate is re-derived from pure world-coord functions, so
    // the chunk owning the trunk and the neighbors holding stray canopy cells
    // all agree — trees cross chunk borders (x, z and y) seamlessly.
#define LEGACY_TREE_REACH 3
    for ( int lx = -LEGACY_TREE_REACH; lx < CHUNK_SIZE_X + LEGACY_TREE_REACH; lx++ ) {
        const int wx = cox + lx;
        for ( int lz = -LEGACY_TREE_REACH; lz < CHUNK_SIZE_Z + LEGACY_TREE_REACH; lz++ ) {
            const int wz = coz + lz;
            // Cheap slab test before the surface scan: a valid tree grows on
            // the topmost grass cell, so its parts live in [h-2, h+7].
            const float h = MapGen::calculateTerrainHeight( wx, wz );
            if ( h + 8.0f < (float)( coy - 1 ) || h - 3.0f > (float)( coy + CHUNK_SIZE_Y ) ) {
                continue;
            }
            int surface_y;
            float prio;
            const int tree_type = sgl_eval_tree_column( wx, wz, &surface_y, &prio );
            if ( tree_type == 0 ) {
                continue;
            }
            // Parts span world y [surface_y - 1, surface_y + 5]; the array
            // covers [coy - 1, coy + CHUNK_SIZE_Y].
            if ( surface_y + 5 < coy - 1 || surface_y - 1 > coy + CHUNK_SIZE_Y ) {
                continue;
            }
            const int y = surface_y - coy;
            if ( FOREST_DEBUG ) {
                chunk.set_block( glm::ivec3( lx, y, lz ), oak_log_state );
                continue;
            }
            if ( sgl_tree_suppressed( wx, wz, surface_y, prio ) ) {
                continue;
            }

            chunk.set_block( glm::ivec3( lx, y - 1, lz ), dirt_state );
            chunk.set_block( glm::ivec3( lx, y, lz ), oak_log_state );
            chunk.set_block( glm::ivec3( lx, y + 1, lz ), oak_log_state );
            chunk.set_block( glm::ivec3( lx, y + 2, lz ), oak_log_state );

            // Etho trees: ~1/100 get a glowstone top log.
            const BlockState &top_log_state = sgl_is_etho_tree( wx, wz ) ? glowstone_state : oak_log_state;
            // tree_type = 3;
            if ( tree_type == 1 ) { // This is a small tree, but a baby
                chunk.set_block( glm::ivec3( lx, y + 2, lz ), top_log_state );
                chunk.set_block( glm::ivec3( lx, y + 3, lz ), leaf_state );

            } else if ( tree_type <= 3 ) { // This is a small tree
                chunk.set_block( glm::ivec3( lx, y + 3, lz ), top_log_state );
                chunk.set_block( glm::ivec3( lx, y + 4, lz ), leaf_state );

            } else {
                chunk.set_block( glm::ivec3( lx, y + 3, lz ), oak_log_state );
                chunk.set_block( glm::ivec3( lx, y + 4, lz ), top_log_state );
                chunk.set_block( glm::ivec3( lx, y + 5, lz ), leaf_state );
            }
            sgl_place_leaves( chunk, lx, y, lz, tree_type, leaf_state );
        }
    }

    // ---- Pass 2: decorations ---------------------------------------------
    // Unchanged sequential scan over the interior columns (the original
    // skipped the outer ring too — kept faithfully). Running after the tree
    // pass, the scan hits placed trunks/canopies and yields those columns,
    // exactly like the old placed_tree flag plus in-order canopy writes.
    for ( int x = 1; x < CHUNK_SIZE_X - 1; x++ ) {
        const int world_x = cox + x;
        for ( int z = 1; z < CHUNK_SIZE_Z - 1; z++ ) {
            const int world_z = coz + z;
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
                if ( !sgl_is_next_to( chunk, x, y, z, GRASS_TUFT2 ) ) {
                    const int is_forest = sgl_is_forest( world_x, world_z );
                    BlockID grass_type = sgl_is_long_grass_roll( world_x, world_z, is_forest );
                    Block *grass_block = block_definition_get_definition( grass_type );
                    if ( grass_type != AIR ) {
                        chunk.set_block( glm::ivec3( x, y, z ), { grass_type, BLOCK_ROTATE_0, grass_block->initial_redstone_power, grass_type, 0 } );
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
                                // Air-only: a reed never eats a tree part — a
                                // canopy leaf from a border-crossing tree must
                                // read identically in both chunks' copies.
                                if ( chunk.get_block( glm::ivec3( x, y + i, z ) ).id == AIR ) {
                                    chunk.set_block( glm::ivec3( x, y + i, z ), reed_state );
                                }
                            }
                        }
                    }
                }
            } // end loops
        }
    }
}

#endif // REPGAME_MAP_GEN_LEGACY
