#include "common/structure_gen.hpp"
#include "common/RepGame.hpp"
#include "common/perlin_noise.hpp"
#include "common/constants.hpp"
#include "common/block_definitions.hpp"
#include "common/map_gen.hpp"

#define MAP_GEN_QUAL static inline
#define MAP_GEN_PERLIN2D( x, z, f, d, s ) perlin_noise2d( x, z, f, d, s )
#define MAP_GEN_PERLIN3D( x, y, z, f, d, s ) perlin_noise3d( x, y, z, f, d, s )
#include "common/map_gen_fields.hpp"

// ---------------------------------------------------------------------------
// Deterministic hashes. Structure gen only runs on the CPU, so these don't
// need GPU parity — but every chunk must evaluate the same rolls for shared
// (border-crossing) trees, so all rolls are pure functions of world coords.
// ---------------------------------------------------------------------------

static unsigned int sg_hash( const int x, const int y, const int z, const int seed ) {
    unsigned int h = 2166136261u;
    h = ( h ^ (unsigned int)x ) * 16777619u;
    h = ( h ^ (unsigned int)y ) * 16777619u;
    h = ( h ^ (unsigned int)z ) * 16777619u;
    h = ( h ^ (unsigned int)seed ) * 16777619u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

static float sg_roll( const int x, const int z, const int seed ) {
    return (float)( sg_hash( x, 0, z, seed ) & 0xFFFFFFu ) / 16777216.0f;
}

static int sg_is_next_to( const Chunk &chunk, const int x, const int y, const int z, const BlockID block ) {
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

// ---------------------------------------------------------------------------
// Bounded writes. Local coords are in the chunk's array space: [-1, CHUNK_SIZE]
// per axis (the +/-1 border cells are included). Chunk::get_block/set_block
// already bounds-check, so out-of-range cells are no-ops — the chunk that owns
// them writes them on its own pass.
// ---------------------------------------------------------------------------

// Can a tree part overwrite this cell: air, liquids, and non-colliding plants.
static inline bool sg_can_replace( const Chunk &chunk, const int lx, const int ly, const int lz ) {
    const BlockID cur = chunk.get_block( glm::ivec3( lx, ly, lz ) ).id;
    if ( cur == AIR ) {
        return true;
    }
    if ( cur >= LAST_BLOCK_ID ) {
        return false;
    }
    return !block_definition_get_definition( cur )->collides_with_player;
}

static void sg_write_trunk( Chunk &chunk, const int lx, const int ly, const int lz, const BlockState &log_state, const BlockID leaf_id ) {
    const BlockID cur = chunk.get_block( glm::ivec3( lx, ly, lz ) ).id;
    if ( sg_can_replace( chunk, lx, ly, lz ) || cur == leaf_id ) {
        chunk.set_block( glm::ivec3( lx, ly, lz ), log_state );
    }
}

// Leaves only fill air: they never eat trunks, terrain, or a neighbor tree's
// canopy (contact seams read as natural gaps between crowns).
static void sg_write_leaf( Chunk &chunk, const int lx, const int ly, const int lz, const BlockState &leaf_state ) {
    if ( chunk.get_block( glm::ivec3( lx, ly, lz ) ).id == AIR ) {
        chunk.set_block( glm::ivec3( lx, ly, lz ), leaf_state );
    }
}

// Dirt under the trunk: rewrites the ground cell, but never eats a log from a
// neighboring tree's trunk.
static void sg_write_ground( Chunk &chunk, const int lx, const int ly, const int lz, const BlockState &dirt_state ) {
    const BlockID cur = chunk.get_block( glm::ivec3( lx, ly, lz ) ).id;
    switch ( cur ) {
        case OAK_LOG:
        case BIRTCH_LOG:
        case JUNGLE_LOG:
        case ACACIA_LOG:
        case DARK_OAK_LOG:
        case LAST_BLOCK_ID:
            return;
        default:
            chunk.set_block( glm::ivec3( lx, ly, lz ), dirt_state );
    }
}

static void sg_write_decor( Chunk &chunk, const int lx, const int ly, const int lz, const BlockID id ) {
    const BlockID cur = chunk.get_block( glm::ivec3( lx, ly, lz ) ).id;
    if ( cur == AIR || cur == WATER ) {
        const Block *def = block_definition_get_definition( id );
        chunk.set_block( glm::ivec3( lx, ly, lz ), { id, BLOCK_ROTATE_0, def->initial_redstone_power, id, 0 } );
    }
}

// ---------------------------------------------------------------------------
// Trees
// ---------------------------------------------------------------------------

typedef enum {
    SHAPE_BLOB,   // oak / birch / dark oak: ellipsoid crown
    SHAPE_PINE,   // spruce: shrinking stacked rings
    SHAPE_JUNGLE, // tall trunk, wide flat crown
    SHAPE_ACACIA, // leaning trunk, flat top
} TreeShape;

typedef struct {
    BlockID log;
    BlockID leaf;
    TreeShape shape;
    int trunk_h;
    int canopy_r;
    int lean_x;
    int lean_z;
    int giant; // 2x2 trunk
} TreeSpec;

// Per-cell tree decision: position inside the cell, existence (biome-driven
// density), species, size. Pure f(cell coords + world biome fields), so every
// chunk makes the same call for a shared tree.
static bool sg_tree_for_cell( const int cx, const int cz, int *out_tx, int *out_tz, TreeSpec *spec ) {
    const int tx = cx * MAPGEN_TREE_CELL + (int)( sg_roll( cx, cz, 1 ) * MAPGEN_TREE_CELL );
    const int tz = cz * MAPGEN_TREE_CELL + (int)( sg_roll( cx, cz, 2 ) * MAPGEN_TREE_CELL );

    const float biome = mg_biome( tx, tz );
    const float weird = mg_weird( tx, tz );

    float density;
    if ( biome > 0.72f ) {
        density = weird >= 0.55f ? 0.95f : 0.05f; // jungle vs savanna/desert
    } else if ( biome < 0.22f ) {
        density = 0.45f; // snowy pine stands
    } else if ( weird > 0.88f ) {
        density = 0.04f; // mushroom fields are nearly barren
    } else if ( weird > 0.75f ) {
        density = 0.95f; // dark forest
    } else if ( weird > 0.40f ) {
        density = 0.85f; // temperate forest
    } else {
        density = 0.03f; // plains: mostly empty, rare lone trees
    }
    // Forest clearings: a ~60-block field modulates density so stands come
    // in groves and glades instead of a uniform carpet. Deep dips punch
    // real clearings; peaks make thickets where canopies interlock.
    const float clearing = mg_inverse_lerp( 0.30f, 0.52f, perlin_noise2d( tx, tz, 0.016f, 2, MAP_SEED + 71 ) );
    density *= clearing;
    if ( sg_roll( cx, cz, 3 ) > density ) {
        return false;
    }

    const float sr = sg_roll( cx, cz, 4 );
    const float hr = sg_roll( cx, cz, 5 );
    spec->lean_x = 0;
    spec->lean_z = 0;
    spec->giant = 0;
    if ( biome > 0.72f ) {
        if ( weird >= 0.55f ) {
            spec->log = JUNGLE_LOG;
            spec->leaf = JUNGLE_LEAF;
            spec->shape = SHAPE_JUNGLE;
            if ( sg_roll( cx, cz, 7 ) < 0.20f ) {
                // Rare emergent giants: 2x2 trunk, tall crown over the canopy.
                spec->giant = 1;
                spec->trunk_h = 16 + (int)( hr * 8.0f );
                spec->canopy_r = 5 + ( sr > 0.5f ? 1 : 0 );
            } else {
                spec->trunk_h = 9 + (int)( hr * 6.0f );
                spec->canopy_r = 3 + ( sr > 0.6f ? 1 : 0 );
            }
        } else {
            spec->log = ACACIA_LOG;
            spec->leaf = LEAF;
            spec->shape = SHAPE_ACACIA;
            spec->trunk_h = 4 + (int)( hr * 3.0f );
            spec->canopy_r = 2;
            const float lr = sg_roll( cx, cz, 6 );
            spec->lean_x = lr < 0.33f ? 1 : lr < 0.66f ? -1 : 0;
            spec->lean_z = spec->lean_x == 0 ? ( lr < 0.5f ? 1 : -1 ) : 0;
        }
    } else if ( biome < 0.22f ) {
        spec->log = DARK_OAK_LOG;
        spec->leaf = PINE_LEAF;
        spec->shape = SHAPE_PINE;
        spec->trunk_h = 7 + (int)( hr * 4.0f );
        spec->canopy_r = 2;
    } else if ( weird > 0.75f ) {
        spec->log = DARK_OAK_LOG;
        spec->leaf = LEAF;
        spec->shape = SHAPE_BLOB;
        spec->trunk_h = 6 + (int)( hr * 3.0f );
        spec->canopy_r = 4;
    } else if ( sr < 0.5f ) {
        spec->log = OAK_LOG;
        spec->leaf = LEAF;
        spec->shape = SHAPE_BLOB;
        spec->trunk_h = 4 + (int)( hr * 3.0f );
        spec->canopy_r = 3;
    } else if ( sr < 0.85f ) {
        spec->log = BIRTCH_LOG;
        spec->leaf = BIRTCH_LEAVES;
        spec->shape = SHAPE_BLOB;
        spec->trunk_h = 5 + (int)( hr * 3.0f );
        spec->canopy_r = 3;
    } else {
        spec->log = DARK_OAK_LOG;
        spec->leaf = PINE_LEAF;
        spec->shape = SHAPE_PINE;
        spec->trunk_h = 7 + (int)( hr * 4.0f );
        spec->canopy_r = 2;
    }
    *out_tx = tx;
    *out_tz = tz;
    return true;
}

// Real surface of a column, derived from pure map gen (not this chunk's block
// array) so every chunk agrees even when the ground lives in a different
// chunk. Overhang sheets and floating islands can push the visible surface up
// to OVERHANG_MAX_RISE above the heightmap, so the scan starts above that.
static int sg_find_surface_y( const int wx, const int wz ) {
    const MapGenColumn col = mg_column_info( wx, wz );
    const int top = (int)( col.height + OVERHANG_MAX_RISE + 4.0f );
    for ( int y = top; y >= (int)( col.height - 8.0f ); y-- ) {
        const BlockID b = mg_pick_block( wx, y, wz, col );
        if ( b != AIR && b != WATER ) {
            return y;
        }
    }
    return -1;
}

// Ellipsoid crown with hash-eaten edges.
static void sg_canopy_blob( Chunk &chunk, const int cx, const int cy, const int cz, const int rx, const int ry, const int rz, const BlockState &leaf, const int seed ) {
    const float fx = (float)( rx * rx ), fy = (float)( ry * ry ), fz = (float)( rz * rz );
    for ( int dy = -ry; dy <= ry; dy++ ) {
        for ( int dx = -rx; dx <= rx; dx++ ) {
            for ( int dz = -rz; dz <= rz; dz++ ) {
                const float d = (float)( dx * dx ) / fx + (float)( dy * dy ) / fy + (float)( dz * dz ) / fz;
                if ( d > 1.0f ) {
                    continue;
                }
                if ( d > 0.45f && ( sg_hash( cx + dx, cy + dy, cz + dz, seed ) & 7u ) < 3u ) {
                    continue;
                }
                sg_write_leaf( chunk, cx + dx, cy + dy, cz + dz, leaf );
            }
        }
    }
}

// Flat diamond-ish disc, with a sparser second layer above.
static void sg_canopy_disc( Chunk &chunk, const int cx, const int cy, const int cz, const int r, const BlockState &leaf, const int seed ) {
    for ( int dx = -r; dx <= r; dx++ ) {
        for ( int dz = -r; dz <= r; dz++ ) {
            const int d = abs( dx ) + abs( dz );
            if ( d > r + 1 ) {
                continue;
            }
            if ( d == r + 1 && ( sg_hash( cx + dx, cy, cz + dz, seed ) & 3u ) != 0u ) {
                continue; // eat most rim cells
            }
            sg_write_leaf( chunk, cx + dx, cy, cz + dz, leaf );
            if ( d < r - 1 && ( sg_hash( cx + dx, cy + 1, cz + dz, seed ) & 3u ) == 0u ) {
                sg_write_leaf( chunk, cx + dx, cy + 1, cz + dz, leaf );
            }
        }
    }
}

// Spruce: shrinking diamond rings every other layer up the trunk.
static void sg_canopy_pine( Chunk &chunk, const int bx, const int by, const int bz, const int trunk_h, const BlockState &leaf, const int seed ) {
    for ( int i = 2; i <= trunk_h; i += 2 ) {
        const int r = i < trunk_h - 3 ? 2 : 1;
        const int y = by + i;
        for ( int dx = -r; dx <= r; dx++ ) {
            for ( int dz = -r; dz <= r; dz++ ) {
                const int d = abs( dx ) + abs( dz );
                if ( d > r + 1 ) {
                    continue;
                }
                if ( abs( dx ) == r && abs( dz ) == r && ( sg_hash( bx + dx, y, bz + dz, seed ) & 3u ) != 0u ) {
                    continue;
                }
                sg_write_leaf( chunk, bx + dx, y, bz + dz, leaf );
            }
        }
    }
    sg_write_leaf( chunk, bx, by + trunk_h + 1, bz, leaf );
}

static void sg_place_tree( Chunk &chunk, const TreeSpec &spec, const int bx, const int by, const int bz ) {
    const Block *log_def = block_definition_get_definition( spec.log );
    const Block *leaf_def = block_definition_get_definition( spec.leaf );
    const BlockState log_state = { spec.log, BLOCK_ROTATE_0, log_def->initial_redstone_power, spec.log, 0 };
    const BlockState leaf_state = { spec.leaf, BLOCK_ROTATE_0, leaf_def->initial_redstone_power, spec.leaf, 0 };
    const BlockState dirt_state = { DIRT, BLOCK_ROTATE_0, block_definition_get_definition( DIRT )->initial_redstone_power, DIRT, 0 };

    if ( spec.giant ) {
        // 2x2 trunk: ground + log for each of the four columns.
        sg_write_ground( chunk, bx, by - 1, bz, dirt_state );
        sg_write_ground( chunk, bx + 1, by - 1, bz, dirt_state );
        sg_write_ground( chunk, bx, by - 1, bz + 1, dirt_state );
        sg_write_ground( chunk, bx + 1, by - 1, bz + 1, dirt_state );
        for ( int i = 0; i < spec.trunk_h; i++ ) {
            sg_write_trunk( chunk, bx, by + i, bz, log_state, spec.leaf );
            sg_write_trunk( chunk, bx + 1, by + i, bz, log_state, spec.leaf );
            sg_write_trunk( chunk, bx, by + i, bz + 1, log_state, spec.leaf );
            sg_write_trunk( chunk, bx + 1, by + i, bz + 1, log_state, spec.leaf );
        }
    } else {
        sg_write_ground( chunk, bx, by - 1, bz, dirt_state );
        for ( int i = 0; i < spec.trunk_h; i++ ) {
            const int lx = spec.lean_x != 0 && i >= spec.trunk_h - 2 ? bx + spec.lean_x : bx;
            const int lz = spec.lean_z != 0 && i >= spec.trunk_h - 2 ? bz + spec.lean_z : bz;
            sg_write_trunk( chunk, lx, by + i, lz, log_state, spec.leaf );
        }
    }
    const int tip_x = spec.lean_x != 0 ? bx + spec.lean_x : bx;
    const int tip_z = spec.lean_z != 0 ? bz + spec.lean_z : bz;
    const int top_y = by + spec.trunk_h;

    switch ( spec.shape ) {
        case SHAPE_BLOB:
            sg_canopy_blob( chunk, bx, top_y - 1, bz, spec.canopy_r, 2, spec.canopy_r, leaf_state, 11 );
            break;
        case SHAPE_PINE:
            sg_canopy_pine( chunk, bx, by, bz, spec.trunk_h, leaf_state, 12 );
            break;
        case SHAPE_JUNGLE:
            sg_canopy_disc( chunk, bx, top_y, bz, spec.canopy_r, leaf_state, 13 );
            sg_canopy_blob( chunk, bx, top_y + 1, bz, spec.canopy_r - 2, 1, spec.canopy_r - 2, leaf_state, 14 );
            break;
        case SHAPE_ACACIA:
            sg_canopy_disc( chunk, tip_x, top_y, tip_z, spec.canopy_r, leaf_state, 15 );
            break;
    }
}

// Evaluate every tree cell whose canopy could reach this chunk and write the
// parts that land inside it — trees cross chunk borders seamlessly.
void StructureGen::place_trees( Chunk &chunk, const int cox, const int coy, const int coz ) {
    const int reach = MAPGEN_MAX_CANOPY_RADIUS + MAPGEN_TREE_CELL;
    const int cx0 = ( cox - reach ) / MAPGEN_TREE_CELL - 1;
    const int cx1 = ( cox + CHUNK_SIZE_X + reach ) / MAPGEN_TREE_CELL + 1;
    const int cz0 = ( coz - reach ) / MAPGEN_TREE_CELL - 1;
    const int cz1 = ( coz + CHUNK_SIZE_Z + reach ) / MAPGEN_TREE_CELL + 1;
    for ( int cx = cx0; cx <= cx1; cx++ ) {
        for ( int cz = cz0; cz <= cz1; cz++ ) {
            TreeSpec spec;
            int tx, tz;
            if ( !sg_tree_for_cell( cx, cz, &tx, &tz, &spec ) ) {
                continue;
            }
            const int gy = sg_find_surface_y( tx, tz );
            if ( gy < 0 ) {
                continue;
            }
            const BlockID ground = MapGen::gen_block_id( tx, gy, tz );
            if ( ground != GRASS && ground != PODZEL && ground != MYCELIUM && ground != SNOWY_GRASS && ground != DIRT && ground != SNOW ) {
                continue;
            }
            // Skip trees that can't touch this chunk's y range (+/-1 border).
            const int top = gy + spec.trunk_h + 3;
            if ( top < coy - 1 || gy + 1 > coy + CHUNK_SIZE_Y + 1 ) {
                continue;
            }
            sg_place_tree( chunk, spec, tx - cox, gy + 1 - coy, tz - coz );
        }
    }
}

// ---------------------------------------------------------------------------
// Decorations: per-column surface scatter on this chunk's interior cells.
// ---------------------------------------------------------------------------

static BlockID sg_pick_flower( const int wx, const int wz ) {
    const float r = sg_roll( wx, wz, 30 );
    if ( r < 0.20f ) return YELLOW_FLOWER;
    if ( r < 0.35f ) return RED_FLOWER;
    if ( r < 0.45f ) return BLUE_FLOWER;
    if ( r < 0.55f ) return POPPY_FLOWER;
    if ( r < 0.62f ) return RED_TULIP;
    if ( r < 0.68f ) return ORANGE_TULIP;
    if ( r < 0.74f ) return WHITE_TULIP;
    if ( r < 0.80f ) return PINK_TULIP;
    if ( r < 0.88f ) return BLUE_FLOWER2;
    if ( r < 0.94f ) return WHITE_FLOWER;
    return LARGE_WHITE_FLOWER;
}

static BlockID sg_pick_coral( const int wx, const int wz ) {
    const float r = sg_roll( wx, wz, 31 );
    if ( r < 0.15f ) return BLUE_CORAL;
    if ( r < 0.30f ) return BLUE_CORAL2;
    if ( r < 0.45f ) return RED_CORAL;
    if ( r < 0.55f ) return RED_CORAL2;
    if ( r < 0.70f ) return PURPLE_CORAL;
    if ( r < 0.80f ) return PURPLE_CORAL2;
    if ( r < 0.90f ) return YELLOW_CORAL;
    return YELLOW_CORAL2;
}

void StructureGen::place_decorations( Chunk &chunk, const int cox, const int coy, const int coz ) {
    for ( int lx = 0; lx < CHUNK_SIZE_X; lx++ ) {
        const int wx = cox + lx;
        for ( int lz = 0; lz < CHUNK_SIZE_Z; lz++ ) {
            const int wz = coz + lz;

            // Scan down: the decoration cell is the air/water cell directly
            // above the first solid-ish block. Water is skipped but noted so
            // seabeds can get their own decoration set.
            int surface_y = -1;
            bool under_water = false;
            BlockID ground = AIR;
            for ( int ly = CHUNK_SIZE_Y - 1; ly > -1; ly-- ) {
                const BlockID b = chunk.get_block( glm::ivec3( lx, ly, lz ) ).id;
                if ( b == AIR ) {
                    continue;
                }
                if ( b == WATER ) {
                    under_water = true;
                    continue;
                }
                ground = b;
                surface_y = ly + 1;
                break;
            }
            if ( surface_y < 0 || surface_y > CHUNK_SIZE_Y ) {
                continue;
            }

            const float roll = sg_roll( wx, wz, 40 );

            if ( under_water ) {
                // Shallow seabeds only, so deep oceans stay barren.
                if ( ( ground == SAND || ground == GRAVEL || ground == DIRT ) && surface_y > WATER_LEVEL - 12 ) {
                    if ( roll < 0.03f ) {
                        sg_write_decor( chunk, lx, surface_y, lz, sg_pick_coral( wx, wz ) );
                    } else if ( roll < 0.10f ) {
                        sg_write_decor( chunk, lx, surface_y, lz, SEAWEED );
                    } else if ( roll < 0.16f ) {
                        sg_write_decor( chunk, lx, surface_y, lz, roll < 0.13f ? UNDERWATER_TUFT_SHORT : UNDERWATER_TUFT_TALL );
                    }
                }
                continue;
            }

            switch ( ground ) {
                case GRASS: {
                    const float biome = mg_biome( wx, wz );
                    const float weird = mg_weird( wx, wz );
                    if ( biome > 0.72f && weird >= 0.55f && roll < 0.06f && sg_is_next_to( chunk, lx, surface_y - 1, lz, WATER ) ) {
                        // Jungle-edge bamboo, a few blocks tall.
                        const int h = 2 + (int)( sg_roll( wx, wz, 41 ) * 3.0f );
                        for ( int i = 0; i < h && surface_y + i < CHUNK_SIZE_Y; i++ ) {
                            sg_write_decor( chunk, lx, surface_y + i, lz, BAMBOO );
                        }
                    } else if ( weird > 0.35f && weird < 0.60f && roll < 0.30f ) {
                        // Meadows: denser tufts and flowers.
                        sg_write_decor( chunk, lx, surface_y, lz, roll < 0.10f ? sg_pick_flower( wx, wz ) : ( roll < 0.20f ? GRASS_TUFT3 : GRASS_TUFT2 ) );
                    } else if ( roll < 0.10f ) {
                        sg_write_decor( chunk, lx, surface_y, lz, roll < 0.03f ? sg_pick_flower( wx, wz ) : GRASS_TUFT2 );
                    } else if ( roll > 0.985f ) {
                        // Rare saplings matching the local tree mix.
                        const BlockID sap = biome < 0.22f ? DARK_OAK_SAPPLING : biome > 0.72f ? ACACIA_SAPPLING : sg_roll( wx, wz, 42 ) < 0.5f ? SAPPLING : BIRTCH_SAPPLING;
                        sg_write_decor( chunk, lx, surface_y, lz, sap );
                    }
                    break;
                }
                case PODZEL:
                    if ( roll < 0.10f ) {
                        sg_write_decor( chunk, lx, surface_y, lz, sg_roll( wx, wz, 43 ) < 0.5f ? BROWN_MUSHROOM : RED_MUSHROOM );
                    }
                    break;
                case MYCELIUM:
                    if ( roll < 0.25f ) {
                        sg_write_decor( chunk, lx, surface_y, lz, sg_roll( wx, wz, 44 ) < 0.5f ? BROWN_MUSHROOM : RED_MUSHROOM );
                    }
                    break;
                case SNOWY_GRASS:
                    if ( roll < 0.02f ) {
                        sg_write_decor( chunk, lx, surface_y, lz, PINECONE );
                    }
                    break;
                case SAND:
                    // Reeds on sand next to water.
                    if ( roll < 0.05f && sg_is_next_to( chunk, lx, surface_y - 1, lz, WATER ) && surface_y + 2 < CHUNK_SIZE_Y ) {
                        for ( int i = 0; i < 3; i++ ) {
                            sg_write_decor( chunk, lx, surface_y + i, lz, REED );
                        }
                    }
                    break;
                case ORANGE_SAND:
                    if ( roll < 0.08f ) {
                        sg_write_decor( chunk, lx, surface_y, lz, sg_roll( wx, wz, 45 ) < 0.5f ? DARK_DEAD_SAPPLING : LIGHT_DEAD_SAPPLING );
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Point-of-interest features: deterministic world-grid cells each rolling at
// most one feature. Every column independently re-derives all nearby
// features' contribution from the cell hash alone, so no cross-chunk state is
// needed and each cell of a feature is written exactly once — by the chunk
// whose interior contains it (columns never straddle x/z chunk edges).
// ---------------------------------------------------------------------------

#define SG_FEATURE_CELL 80
#define SG_FEATURE_REACH 48 // farthest a feature may reach past its anchor
#define SG_FEATURE_MAX_SUB 6

enum {
    SG_FEAT_NONE,
    SG_FEAT_SPIRE,    // cluster of colored-glass crystal spires, glowing tips
    SG_FEAT_VENT,     // volcanic cinder cone with a lava crater
    SG_FEAT_MONOLITH, // stone-brick pillar topped with glowstone + torch
};

typedef struct {
    int kind;
    float ax, az; // anchor / center column
    // spire cluster:
    int n_sub;
    float sx[ SG_FEATURE_MAX_SUB ], sz[ SG_FEATURE_MAX_SUB ], speak[ SG_FEATURE_MAX_SUB ];
    BlockID glass;
    // vent:
    float vradius, vpeak;
    // monolith:
    int mono_h;
} SGFeature;

static bool sg_feature_for_cell( const int cx, const int cz, SGFeature *f ) {
    *f = SGFeature{ };
    const float r = sg_roll( cx, cz, 60 );
    if ( r >= 0.28f ) {
        return false; // ~1 in 4 cells holds a feature
    }
    f->ax = cx * SG_FEATURE_CELL + 12.0f + sg_roll( cx, cz, 61 ) * ( SG_FEATURE_CELL - 24.0f );
    f->az = cz * SG_FEATURE_CELL + 12.0f + sg_roll( cx, cz, 62 ) * ( SG_FEATURE_CELL - 24.0f );
    // Features stand on land. Islands still work: the per-column surface scan
    // plants on the real top (which can be an island top).
    if ( MapGen::calculateTerrainHeight( (int)f->ax, (int)f->az ) < WATER_LEVEL + 1.5f ) {
        return false;
    }
    const float biome = mg_biome( (int)f->ax, (int)f->az );
    const float weird = mg_weird( (int)f->ax, (int)f->az );
    if ( r < 0.13f ) {
        // Crystal spire clusters live in the weird zones and the cold wastes.
        if ( weird <= 0.60f && biome >= 0.20f ) {
            return false;
        }
        f->kind = SG_FEAT_SPIRE;
        f->n_sub = 3 + (int)( sg_roll( cx, cz, 63 ) * (float)( SG_FEATURE_MAX_SUB - 2 ) ); // 3..6
        const float hr = sg_roll( cx, cz, 64 );
        f->glass = hr < 0.3f ? GLASS_PALE_BLUE : hr < 0.55f ? GLASS_LIGHT_BLUE : hr < 0.8f ? GLASS_LIGHT_GREEN : GLASS_WHITE;
        for ( int i = 0; i < f->n_sub; i++ ) {
            f->sx[ i ] = f->ax + ( sg_roll( cx, cz, 70 + i * 2 ) - 0.5f ) * 44.0f;
            f->sz[ i ] = f->az + ( sg_roll( cx, cz, 71 + i * 2 ) - 0.5f ) * 44.0f;
            f->speak[ i ] = 7.0f + sg_roll( cx, cz, 80 + i ) * 10.0f;
        }
        return true;
    }
    if ( r < 0.21f ) {
        // Volcanic vents — anywhere but the mushroom fields.
        if ( weird >= 0.85f ) {
            return false;
        }
        f->kind = SG_FEAT_VENT;
        f->vradius = 6.0f + sg_roll( cx, cz, 90 ) * 4.0f;
        f->vpeak = 7.0f + sg_roll( cx, cz, 91 ) * 5.0f;
        return true;
    }
    // Beacon monoliths.
    f->kind = SG_FEAT_MONOLITH;
    f->mono_h = 5 + (int)( sg_roll( cx, cz, 92 ) * 5.0f );
    return true;
}

// A column's winning feature claim: the tallest contribution wins.
typedef struct {
    int kind;
    float h;
    float dist;    // column distance to the winning anchor
    float aux;     // vent radius
    BlockID glass; // spire hue
} SGClaim;

static SGClaim sg_column_feature( const SGFeature *feats, const int n, const int wx, const int wz ) {
    SGClaim best = { SG_FEAT_NONE, 0.0f, 0.0f, 0.0f, AIR };
    for ( int i = 0; i < n; i++ ) {
        const SGFeature &f = feats[ i ];
        float h = 0.0f, d = 0.0f;
        if ( f.kind == SG_FEAT_SPIRE ) {
            for ( int s = 0; s < f.n_sub; s++ ) {
                const float dx = (float)wx - f.sx[ s ];
                const float dz = (float)wz - f.sz[ s ];
                const float di = sqrtf( dx * dx + dz * dz );
                const float hi = f.speak[ s ] - di * 2.0f;
                if ( hi > h ) {
                    h = hi;
                    d = di;
                }
            }
        } else if ( f.kind == SG_FEAT_VENT ) {
            const float dx = (float)wx - f.ax;
            const float dz = (float)wz - f.az;
            d = sqrtf( dx * dx + dz * dz );
            if ( d < f.vradius ) {
                h = f.vpeak - d * 1.4f;
            }
        } else {
            const float dx = (float)wx - f.ax;
            const float dz = (float)wz - f.az;
            d = sqrtf( dx * dx + dz * dz );
            if ( d < 1.2f ) {
                h = (float)f.mono_h;
            }
        }
        if ( h > best.h ) {
            best.kind = f.kind;
            best.h = h;
            best.dist = d;
            best.aux = f.vradius;
            best.glass = f.glass;
        }
    }
    return best;
}

// Write one feature/crystal cell: only ever into air, water, or plants — a
// feature yields to terrain, trunks and other solids instead of eating them.
// Returns false when blocked, so callers can stop a column at the obstacle.
static bool sg_write_feature( Chunk &chunk, const int lx, const int ly, const int lz, const BlockID id ) {
    if ( ly < 0 || ly >= CHUNK_SIZE_Y ) {
        return false; // interior cells only — the owning chunk writes its own
    }
    if ( !sg_can_replace( chunk, lx, ly, lz ) ) {
        return false;
    }
    const Block *def = block_definition_get_definition( id );
    chunk.set_block( glm::ivec3( lx, ly, lz ), { id, BLOCK_ROTATE_0, def->initial_redstone_power, id, 0 } );
    return true;
}

// Scorch a terrain cell under a vent: surface soils become cobblestone.
static void sg_scorch_ground( Chunk &chunk, const int lx, const int ly, const int lz ) {
    switch ( chunk.get_block( glm::ivec3( lx, ly, lz ) ).id ) {
        case GRASS:
        case DIRT:
        case SAND:
        case ORANGE_SAND:
        case GRAVEL:
        case SNOWY_GRASS:
        case SNOW:
        case PODZEL:
        case MYCELIUM:
            chunk.set_block( glm::ivec3( lx, ly, lz ), { COBBLESTONE, BLOCK_ROTATE_0, 0, COBBLESTONE, 0 } );
            break;
        default:
            break;
    }
}

void StructureGen::place_features( Chunk &chunk, const int cox, const int coy, const int coz ) {
    // Decode every feature cell whose reach could touch this chunk.
    SGFeature feats[ 48 ];
    int n_feats = 0;
    const int cx0 = ( cox - SG_FEATURE_REACH ) / SG_FEATURE_CELL - 1;
    const int cx1 = ( cox + CHUNK_SIZE_X + SG_FEATURE_REACH ) / SG_FEATURE_CELL + 1;
    const int cz0 = ( coz - SG_FEATURE_REACH ) / SG_FEATURE_CELL - 1;
    const int cz1 = ( coz + CHUNK_SIZE_Z + SG_FEATURE_REACH ) / SG_FEATURE_CELL + 1;
    for ( int cx = cx0; cx <= cx1 && n_feats < 48; cx++ ) {
        for ( int cz = cz0; cz <= cz1 && n_feats < 48; cz++ ) {
            if ( sg_feature_for_cell( cx, cz, &feats[ n_feats ] ) ) {
                n_feats++;
            }
        }
    }
    if ( n_feats == 0 ) {
        return;
    }

    for ( int lx = 0; lx < CHUNK_SIZE_X; lx++ ) {
        const int wx = cox + lx;
        for ( int lz = 0; lz < CHUNK_SIZE_Z; lz++ ) {
            const int wz = coz + lz;
            const SGClaim c = sg_column_feature( feats, n_feats, wx, wz );
            if ( c.kind == SG_FEAT_NONE ) {
                continue;
            }
            // Column surface, scanned from the chunk's own array including
            // the +/-1 halo rows: a base sitting exactly on a y-border is
            // seen as halo by the chunk above, which then owns the feature.
            // Writes stay inside this chunk's y-interior, so a feature
            // crossing a border simply reads as a shorter one (its tip is
            // drawn at the clipped height — never a chopped shaft).
            int surface_y = -1;
            bool under_water = false;
            BlockID ground = AIR;
            for ( int ly = CHUNK_SIZE_Y; ly >= -1; ly-- ) {
                const BlockID b = chunk.get_block( glm::ivec3( lx, ly, lz ) ).id;
                if ( b == AIR ) {
                    continue;
                }
                if ( b == WATER ) {
                    under_water = true;
                    continue;
                }
                ground = b;
                surface_y = ly + 1;
                break;
            }
            if ( surface_y < 0 ) {
                continue;
            }
            // Features need real ground — not leaf canopies or trunks.
            if ( ground >= LAST_BLOCK_ID || !block_definition_get_definition( ground )->is_solid ) {
                continue;
            }
            const int h_max = CHUNK_SIZE_Y - surface_y;
            const int h = (int)c.h < h_max ? (int)c.h : h_max;
            if ( h < 3 ) {
                continue;
            }
            if ( c.kind == SG_FEAT_SPIRE ) {
                for ( int i = 0; i < h; i++ ) {
                    const BlockID id = ( i == h - 1 || ( h >= 9 && i == h * 2 / 3 ) ) ? GLOWSTONE : c.glass;
                    if ( !sg_write_feature( chunk, lx, surface_y + i, lz, id ) ) {
                        break;
                    }
                }
            } else if ( c.kind == SG_FEAT_VENT ) {
                if ( under_water ) {
                    continue;
                }
                const bool crater = c.dist < 1.9f;
                for ( int i = 0; i < h; i++ ) {
                    const BlockID id = ( i == h - 1 ) ? ( crater ? LAVA : COBBLESTONE ) : STONE;
                    if ( !sg_write_feature( chunk, lx, surface_y + i, lz, id ) ) {
                        break;
                    }
                }
                if ( c.dist < c.aux * 0.75f && surface_y > 0 ) {
                    sg_scorch_ground( chunk, lx, surface_y - 1, lz );
                }
            } else { // SG_FEAT_MONOLITH
                if ( under_water ) {
                    continue;
                }
                for ( int i = 0; i < h; i++ ) {
                    const BlockID id = i == h - 1 ? GLOWSTONE : STONE_BRICK;
                    if ( !sg_write_feature( chunk, lx, surface_y + i, lz, id ) ) {
                        break;
                    }
                }
                if ( h >= 4 ) {
                    sg_write_feature( chunk, lx, surface_y + h, lz, TORCH );
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Cave crystals & stalactites: per-cell deterministic decorations anchored to
// solid ceilings (cave roofs, overhang undersides, floating-island bottoms)
// and floors. Clusters live in 16-block neighborhoods so caves alternate
// between bare and glittering.
// ---------------------------------------------------------------------------

static bool sg_is_solid_block( const BlockID id ) {
    if ( id >= LAST_BLOCK_ID ) {
        return false;
    }
    return block_definition_get_definition( id )->is_solid;
}

static BlockID sg_crystal_glass( const unsigned int h ) {
    switch ( ( h >> 21 ) & 3u ) {
        case 0:
            return GLASS_PALE_BLUE;
        case 1:
            return GLASS_LIGHT_BLUE;
        case 2:
            return GLASS_LIGHT_GREEN;
        default:
            return GLASS_WHITE;
    }
}

static void sg_set_block( Chunk &chunk, const int lx, const int ly, const int lz, const BlockID id ) {
    const Block *def = block_definition_get_definition( id );
    chunk.set_block( glm::ivec3( lx, ly, lz ), { id, BLOCK_ROTATE_0, def->initial_redstone_power, id, 0 } );
}

void StructureGen::place_crystals( Chunk &chunk, const int cox, const int coy, const int coz ) {
    for ( int lx = 0; lx < CHUNK_SIZE_X; lx++ ) {
        const int wx = cox + lx;
        for ( int lz = 0; lz < CHUNK_SIZE_Z; lz++ ) {
            const int wz = coz + lz;
            if ( sg_roll( wx >> 4, wz >> 4, 55 ) >= 0.38f ) {
                continue;
            }
            const float h = MapGen::calculateTerrainHeight( wx, wz );
            for ( int ly = 0; ly < CHUNK_SIZE_Y; ly++ ) {
                if ( chunk.get_block( glm::ivec3( lx, ly, lz ) ).id != AIR ) {
                    continue;
                }
                const int wy = coy + ly;
                // Ceiling formations hang off cave roofs (below the surface)
                // and the undersides of overhangs / floating islands.
                if ( wy < h - 5.0f || wy > h + 12.0f ) {
                    const BlockID above = chunk.get_block( glm::ivec3( lx, ly + 1, lz ) ).id;
                    if ( sg_is_solid_block( above ) ) {
                        const unsigned int hh = sg_hash( wx, wy, wz, 56 );
                        if ( ( hh & 1023u ) < 16u ) {
                            const int len = 1 + (int)( ( hh >> 10 ) & 3u );
                            const bool crystal = ( ( hh >> 17 ) & 15u ) == 0u;
                            const bool glow = crystal || ( ( hh >> 14 ) & 7u ) < 3u;
                            const BlockID body = crystal ? sg_crystal_glass( hh ) : STONE;
                            int n = 0;
                            while ( n < len && ly - 1 - n >= 0 && chunk.get_block( glm::ivec3( lx, ly - 1 - n, lz ) ).id == AIR ) {
                                n++;
                            }
                            for ( int i = 0; i < n; i++ ) {
                                sg_set_block( chunk, lx, ly - 1 - i, lz, i == n - 1 ? ( glow ? GLOWSTONE : STONE ) : body );
                            }
                        }
                        continue;
                    }
                }
                // Floor formations: cave floors only, above the lava layer.
                if ( wy < h - 5.0f && wy > LAVA_LEVEL + 2 && ly > 0 ) {
                    const BlockID below = chunk.get_block( glm::ivec3( lx, ly - 1, lz ) ).id;
                    if ( sg_is_solid_block( below ) ) {
                        const unsigned int hh = sg_hash( wx, wy, wz, 57 );
                        if ( ( hh & 1023u ) < 11u ) {
                            const int len = 1 + (int)( ( hh >> 10 ) & 3u );
                            const bool crystal = ( ( hh >> 17 ) & 15u ) == 0u;
                            const bool glow = crystal || ( ( hh >> 14 ) & 7u ) < 2u;
                            const BlockID body = crystal ? sg_crystal_glass( hh ) : STONE;
                            int n = 0;
                            while ( n < len && ly + n < CHUNK_SIZE_Y && chunk.get_block( glm::ivec3( lx, ly + n, lz ) ).id == AIR ) {
                                n++;
                            }
                            for ( int i = 0; i < n; i++ ) {
                                sg_set_block( chunk, lx, ly + i, lz, i == n - 1 ? ( glow ? GLOWSTONE : STONE ) : body );
                            }
                        } else if ( below == STONE && ( hh & 4095u ) < 5u ) {
                            // Rare embedded glow dot in the cave floor.
                            sg_set_block( chunk, lx, ly - 1, lz, GLOWSTONE );
                        }
                    }
                }
            }
        }
    }
}

void StructureGen::place( Chunk &chunk ) {
#if REPGAME_MAP_GEN_LEGACY
    place_legacy( chunk );
#else
    const int cox = chunk.chunk_pos.x * CHUNK_SIZE_X;
    const int coy = chunk.chunk_pos.y * CHUNK_SIZE_Y;
    const int coz = chunk.chunk_pos.z * CHUNK_SIZE_Z;
    place_trees( chunk, cox, coy, coz );
    place_features( chunk, cox, coy, coz );
    place_decorations( chunk, cox, coy, coz );
    place_crystals( chunk, cox, coy, coz );
#endif
}
