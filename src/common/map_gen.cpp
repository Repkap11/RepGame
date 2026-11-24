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
#include "common/map_gen_nether.hpp"
#include "common/map_gen_end.hpp"
#include "common/creative_order_gen.hpp"
#include "common/texture_ids_gen.hpp"

static_assert( DIM_BLOCK_SPAN == DIM_CHUNK_SPAN * CHUNK_SIZE_Y, "dimension band height mismatch" );

// A block is "animated" when any face texture lands in the flipbook span
// ([TEXTURE_ANIM_BASE, TEXTURE_COUNT)) — the fragment shader steps those
// layers on u_Time.
static bool block_world_animated( BlockID id ) {
    const Block *def = block_definition_get_definition( id );
    for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
        if ( def->textures[ f ] >= TEXTURE_ANIM_BASE ) {
            return true;
        }
    }
    return false;
}

// Block-world order: the creative list first (material-grouped), then every
// other defined non-AIR id in ascending order — split so animated blocks
// get their own table for the mirrored -x section. Built once each.
static std::vector<BlockID> block_world_table_build( bool animated ) {
    std::vector<BlockID> t;
    unsigned char in_list[ LAST_BLOCK_ID ] = { 0 };
    for ( unsigned int i = 0; i < CREATIVE_BLOCK_COUNT; i++ ) {
        // The creative list ends with pure items — not placeable blocks.
        if ( !item_is_block( CREATIVE_BLOCKS[ i ] ) ) {
            continue;
        }
        const BlockID bid = static_cast<BlockID>( CREATIVE_BLOCKS[ i ] );
        if ( block_world_animated( bid ) == animated ) {
            t.push_back( bid );
        }
        in_list[ CREATIVE_BLOCKS[ i ] ] = 1;
    }
    in_list[ AIR ] = 1;
    for ( unsigned int id = 0; id < (unsigned int)LAST_BLOCK_ID; id++ ) {
        if ( !in_list[ id ] && block_id_defined( id ) && block_world_animated( static_cast<BlockID>( id ) ) == animated ) {
            t.push_back( static_cast<BlockID>( id ) );
        }
    }
    return t;
}
static const std::vector<BlockID> &block_world_table( bool animated ) {
    static const std::vector<BlockID> table_still = block_world_table_build( false );
    static const std::vector<BlockID> table_anim = block_world_table_build( true );
    return animated ? table_anim : table_still;
}

BlockCell MapGen::block_world_cell( const int x, const int y, const int z ) {
    if ( y <= 0 ) {
        // Solid ground: an air void under the floor would make the light
        // cascade run removal BFS over the whole connected void volume.
        return block_cell_pack( SANDSTONE, 0 );
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
                return block_cell_pack( y == 1 ? d.lower : d.upper, 0 );
            }
        }
        return block_cell_pack( AIR, 0 );
    }
    // Hanging signs (needs_place_on_any_solid.bottom => support at y+1):
    // a stone beam at y=2 with signs below it at y=1.
    if ( z == -24 && ( y == 1 || y == 2 ) ) {
        static const struct {
            int x;
            BlockID id;
        } hanging_showcase[] = {
            { -9, OAK_HANGING_SIGN }, { -7, ACACIA_HANGING_SIGN }, { -5, SPRUCE_HANGING_SIGN }, { -3, WARPED_HANGING_SIGN },
        };
        if ( y == 2 ) {
            return block_cell_pack( x >= -9 && x <= -3 ? STONE : AIR, 0 );
        }
        for ( const auto &h : hanging_showcase ) {
            if ( h.x == x ) {
                return block_cell_pack( h.id, 0 );
            }
        }
        return block_cell_pack( AIR, 0 );
    }
    // Bed showcase in the free z<0 region: foot+head pairs at yaw 0, so
    // the head cell sits one block at -z of the foot.
    if ( ( z == -30 || z == -31 ) && y == 1 ) {
        static const int bed_xs[] = { -9, -7, -5 };
        for ( const int bx : bed_xs ) {
            if ( x == bx ) {
                return block_cell_pack( z == -30 ? BED : BED_HEAD, 0 );
            }
        }
        return block_cell_pack( AIR, 0 );
    }
    // Scaffolding showcase: a 4-high column at x=-9 with a 6-cell arm off
    // its top (horizontal distances 1-6 — the Java support limit), plus a
    // freestanding frame at (0,-34) for the lone-cell look.
    if ( z == -33 && y >= 1 && y <= 4 ) {
        if ( x == -9 || ( y == 4 && x >= -8 && x <= -3 ) ) {
            return block_cell_pack( SCAFFOLDING, 0 );
        }
        return block_cell_pack( AIR, 0 );
    }
    if ( z == -34 && y == 1 ) {
        return block_cell_pack( x == 0 ? SCAFFOLDING : AIR, 0 );
    }
    if ( y != 1 ) {
        return block_cell_pack( AIR, 0 );
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
            unsigned char rot = 0;
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
            // Signs: standing signs on the floor; wall signs mounted on
            // the +z face of the stones at z=-21 (rotation 0 puts the
            // plate on the cell's -z edge, support at z-1).
            { -9, -19, STANDING_SIGN }, { -7, -19, SPRUCE_STANDING_SIGN }, { -5, -19, BIRCH_STANDING_SIGN }, { -3, -19, ACACIA_STANDING_SIGN },
            { -9, -21, STONE }, { -7, -21, STONE }, { -5, -21, STONE }, { -3, -21, STONE },
            { -9, -20, WALL_SIGN }, { -7, -20, SPRUCE_WALL_SIGN }, { -5, -20, BIRCH_WALL_SIGN }, { -3, -20, ACACIA_WALL_SIGN },
            // Iron/copper bars (is_bars): a mixed-material run (all bars
            // connect to each other), a solid-block end, an L corner, a T
            // junction, a lone post, and a pair beside a glass pane (no
            // connection) and a glass block (connects — full-face).
            { -9, -26, IRON_BARS }, { -8, -26, IRON_BARS }, { -7, -26, COPPER_BARS }, { -6, -26, COPPER_BARS },
            { -4, -26, IRON_BARS }, { -3, -26, STONE },
            { 0, -26, IRON_BARS }, { 0, -27, IRON_BARS }, { 1, -27, IRON_BARS },
            { 3, -27, IRON_BARS }, { 4, -27, IRON_BARS }, { 5, -27, IRON_BARS }, { 4, -28, IRON_BARS },
            { 8, -26, IRON_BARS },
            { 10, -26, IRON_BARS }, { 10, -27, IRON_BARS }, { 11, -26, GLASS_PANE }, { 11, -27, GLASS },
            { 13, -26, IRON_BARS }, { 14, -26, OAK_FENCE },
            // Coral showcase: a pillar per fan species with its wall form
            // mounted on all four faces (rot 0 faces +z, 2 faces -z,
            // 3 faces +x, 1 faces -x — the plate hugs the support edge),
            // the standing fan a row in front, then standing plants and
            // solid coral blocks. Live family at z=-38..-41, dead at
            // z=-44..-47, plants at z=-50/-52, blocks at z=-56/-58.
            { -9, -40, STONE }, { -9, -41, TUBE_CORAL_WALL_FAN, 2 }, { -9, -39, TUBE_CORAL_WALL_FAN }, { -10, -40, TUBE_CORAL_WALL_FAN, 1 }, { -8, -40, TUBE_CORAL_WALL_FAN, 3 },
            { -5, -40, STONE }, { -5, -41, BRAIN_CORAL_WALL_FAN, 2 }, { -5, -39, BRAIN_CORAL_WALL_FAN }, { -6, -40, BRAIN_CORAL_WALL_FAN, 1 }, { -4, -40, BRAIN_CORAL_WALL_FAN, 3 },
            { -1, -40, STONE }, { -1, -41, BUBBLE_CORAL_WALL_FAN, 2 }, { -1, -39, BUBBLE_CORAL_WALL_FAN }, { -2, -40, BUBBLE_CORAL_WALL_FAN, 1 }, { 0, -40, BUBBLE_CORAL_WALL_FAN, 3 },
            { 3, -40, STONE }, { 3, -41, FIRE_CORAL_WALL_FAN, 2 }, { 3, -39, FIRE_CORAL_WALL_FAN }, { 2, -40, FIRE_CORAL_WALL_FAN, 1 }, { 4, -40, FIRE_CORAL_WALL_FAN, 3 },
            { 7, -40, STONE }, { 7, -41, HORN_CORAL_WALL_FAN, 2 }, { 7, -39, HORN_CORAL_WALL_FAN }, { 6, -40, HORN_CORAL_WALL_FAN, 1 }, { 8, -40, HORN_CORAL_WALL_FAN, 3 },
            // Old family: CORAL_FAN's wall form is CORAL_FAN_HANG; HANG2/3
            // are unpaired purple/yellow wall forms.
            { 11, -40, STONE }, { 11, -41, CORAL_FAN_HANG, 2 }, { 11, -39, CORAL_FAN_HANG }, { 10, -40, CORAL_FAN_HANG, 1 }, { 12, -40, CORAL_FAN_HANG, 3 },
            { 15, -40, STONE }, { 15, -41, CORAL_FAN_HANG2, 2 }, { 15, -39, CORAL_FAN_HANG2 }, { 14, -40, CORAL_FAN_HANG2, 1 }, { 16, -40, CORAL_FAN_HANG2, 3 },
            // Standing live fans.
            { -9, -38, TUBE_CORAL_FAN }, { -5, -38, BRAIN_CORAL_FAN }, { -1, -38, BUBBLE_CORAL_FAN }, { 3, -38, FIRE_CORAL_FAN }, { 7, -38, HORN_CORAL_FAN },
            { 11, -38, CORAL_FAN }, { 15, -38, CORAL_FAN_DEAD },
            // Dead wall-fan pillars.
            { -9, -46, STONE }, { -9, -47, DEAD_TUBE_CORAL_WALL_FAN, 2 }, { -9, -45, DEAD_TUBE_CORAL_WALL_FAN }, { -10, -46, DEAD_TUBE_CORAL_WALL_FAN, 1 }, { -8, -46, DEAD_TUBE_CORAL_WALL_FAN, 3 },
            { -5, -46, STONE }, { -5, -47, DEAD_BRAIN_CORAL_WALL_FAN, 2 }, { -5, -45, DEAD_BRAIN_CORAL_WALL_FAN }, { -6, -46, DEAD_BRAIN_CORAL_WALL_FAN, 1 }, { -4, -46, DEAD_BRAIN_CORAL_WALL_FAN, 3 },
            { -1, -46, STONE }, { -1, -47, DEAD_BUBBLE_CORAL_WALL_FAN, 2 }, { -1, -45, DEAD_BUBBLE_CORAL_WALL_FAN }, { -2, -46, DEAD_BUBBLE_CORAL_WALL_FAN, 1 }, { 0, -46, DEAD_BUBBLE_CORAL_WALL_FAN, 3 },
            { 3, -46, STONE }, { 3, -47, DEAD_FIRE_CORAL_WALL_FAN, 2 }, { 3, -45, DEAD_FIRE_CORAL_WALL_FAN }, { 2, -46, DEAD_FIRE_CORAL_WALL_FAN, 1 }, { 4, -46, DEAD_FIRE_CORAL_WALL_FAN, 3 },
            { 7, -46, STONE }, { 7, -47, DEAD_HORN_CORAL_WALL_FAN, 2 }, { 7, -45, DEAD_HORN_CORAL_WALL_FAN }, { 6, -46, DEAD_HORN_CORAL_WALL_FAN, 1 }, { 8, -46, DEAD_HORN_CORAL_WALL_FAN, 3 },
            { 11, -46, STONE }, { 11, -47, CORAL_FAN_HANG3, 2 }, { 11, -45, CORAL_FAN_HANG3 }, { 10, -46, CORAL_FAN_HANG3, 1 }, { 12, -46, CORAL_FAN_HANG3, 3 },
            // Standing dead fans.
            { -9, -44, DEAD_TUBE_CORAL_FAN }, { -5, -44, DEAD_BRAIN_CORAL_FAN }, { -1, -44, DEAD_BUBBLE_CORAL_FAN }, { 3, -44, DEAD_FIRE_CORAL_FAN }, { 7, -44, DEAD_HORN_CORAL_FAN },
            // Standing plants, live then dead.
            { -9, -50, TUBE_CORAL }, { -5, -50, BRAIN_CORAL }, { -1, -50, BUBBLE_CORAL }, { 3, -50, FIRE_CORAL }, { 7, -50, HORN_CORAL }, { 11, -50, PURPLE_CORAL2 }, { 15, -50, CORAL },
            { -9, -52, DEAD_TUBE_CORAL }, { -5, -52, DEAD_BRAIN_CORAL }, { -1, -52, DEAD_BUBBLE_CORAL }, { 3, -52, DEAD_FIRE_CORAL }, { 7, -52, DEAD_HORN_CORAL },
            // Solid coral blocks, live then dead.
            { -9, -56, TUBE_CORAL_BLOCK }, { -5, -56, BRAIN_CORAL_BLOCK }, { -1, -56, BUBBLE_CORAL_BLOCK }, { 3, -56, FIRE_CORAL_BLOCK }, { 7, -56, HORN_CORAL_BLOCK }, { 11, -56, CORAL_BLOCK },
            { -9, -58, DEAD_TUBE_CORAL_BLOCK }, { -5, -58, DEAD_BRAIN_CORAL_BLOCK }, { -1, -58, DEAD_BUBBLE_CORAL_BLOCK }, { 3, -58, DEAD_FIRE_CORAL_BLOCK }, { 7, -58, DEAD_HORN_CORAL_BLOCK },
        };
        for ( const auto &s : showcase ) {
            if ( s.x == x && s.z == z ) {
                return block_cell_pack( s.id, 0, s.rot );
            }
        }
        return block_cell_pack( AIR, 0 );
    }
    // 32 per row along +x starting at x=2, rows stepping +z from z=2; one
    // air cell between neighbours in both directions. Animated blocks get
    // their own section: the same grid mirrored off to -x (slots at
    // x=-8, -10, ...) — offset from the mob showcase column at x=-3 so the
    // mobs stand clear of the moving textures.
    if ( z < 2 || ( ( z - 2 ) & 1 ) ) {
        return block_cell_pack( AIR, 0 );
    }
    const int row = ( z - 2 ) / 2;
    int col = -1;
    const std::vector<BlockID> *t = nullptr;
    if ( x >= 2 && !( ( x - 2 ) & 1 ) ) {
        col = ( x - 2 ) / 2;
        t = &block_world_table( false );
    } else if ( x <= -8 && !( ( -x - 8 ) & 1 ) ) {
        col = ( -x - 8 ) / 2;
        t = &block_world_table( true );
    }
    if ( col < 0 || col >= 32 ) {
        return block_cell_pack( AIR, 0 );
    }
    const size_t k = (size_t)row * 32 + (size_t)col;
    return block_cell_pack( k < t->size( ) ? ( *t )[ k ] : AIR, 0 );
}

BlockID MapGen::block_world_id( const int x, const int y, const int z ) {
    return block_cell_id( block_world_cell( x, y, z ) );
}

BlockID MapGen::flat_world_id( const int x, const int y, const int z ) {
    // Solid ground like block_world: an air void under the floor would make
    // the light cascade run removal BFS over the whole connected void volume.
    return y <= 0 ? SANDSTONE : AIR;
}

float MapGen::calculateTerrainHeight( const int x, const int z ) {
    // Overworld-band terrain height: the legacy generator's field. Repheim's
    // rep-frame height is mg_base_height at the same x/z, available to the
    // repheim band only (the two generators never share a band).
    return mgl_base_height( x, z );
}

float MapGen::dimTerrainHeight( const DimensionId dim, const int x, const int z ) {
    return dim == DIM_REPHEIM ? mg_base_height( x, z ) : mgl_base_height( x, z );
}

float MapGen::maxTerrainHeight( const DimensionId dim ) {
    // perlin_noise2d returns values in [0, 1]. Each terrain component applies a pure
    // transformation to that bounded input. We compute the exact max of each transform
    // by evaluating it across the [0, 1] noise range at startup, then sum them.
    // This is pure arithmetic — no perlin noise evaluation, no world sampling.
    // If a transform formula changes, the max updates automatically.
    // The repheim band runs the reworked generator (its rep frame shares the
    // overworld's y range); every other dimension takes the legacy bound.
    static float max_heights[ 2 ] = { 0.0f, 0.0f };
    float &max_height = max_heights[ dim == DIM_REPHEIM ];
    if ( max_height == 0.0f ) {
        constexpr int STEPS = 10000;
        float max_ground = -1e30f, max_hills = -1e30f, max_level = -1e30f;
        if ( dim != DIM_REPHEIM ) {
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
            max_height = bound;
        } else {
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
            max_height = bound;
        }
    }
    return max_height;
}

BlockID MapGen::gen_block_id( const int x, const int y, const int z ) {
    if ( repgame_block_world( ) ) {
        return block_world_id( x, y, z );
    }
    if ( repgame_flat_world( ) ) {
        return flat_world_id( x, y, z );
    }
    if ( DIM_IS_NETHER_BY( y ) ) {
        return mgn_pick_block( x, y, z );
    }
    if ( DIM_IS_END_BY( y ) ) {
        return mge_pick_block( x, y, z );
    }
    if ( DIM_IS_REPHEIM_BY( y ) ) {
        const MapGenColumn col = mg_column_info( x, z );
        return mg_pick_block( x, repheim_gen_block_y( y ), z, col );
    }
    return mgl_pick_block( x, y, z, mgl_base_height( x, z ) );
}

void MapGen::load_block_c( const Chunk *chunk ) {
    glm::ivec3 chunk_size = glm::vec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    glm::ivec3 chunk_offset = chunk->chunk_pos * chunk_size;

    // One 2D context per column: the reworked generator's MapGenColumn for
    // repheim-band chunks, the legacy terrain height for every other band.
    const bool repheim_band = DIM_IS_REPHEIM_CY( chunk->chunk_pos.y );
    for ( int x = chunk_offset.x - 1; x < chunk_offset.x + CHUNK_SIZE_INTERNAL_X - 1; x++ ) {
        for ( int z = chunk_offset.z - 1; z < chunk_offset.z + CHUNK_SIZE_INTERNAL_Z - 1; z++ ) {
            const MapGenColumn col = repheim_band ? mg_column_info( x, z ) : MapGenColumn{ };
            const float terrainHeight = repheim_band ? 0.0f : mgl_base_height( x, z );
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
