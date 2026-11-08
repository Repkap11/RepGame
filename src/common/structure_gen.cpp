#include "common/structure_gen.hpp"
#include "common/tree_shapes.hpp"
#include "common/perlin_noise.hpp"
#include "common/constants.hpp"
#include "common/block_definitions.hpp"
#include "common/map_gen.hpp"

#define MG_QUAL static inline
#define MAP_GEN_PERLIN2D( x, z, f, d, s ) perlin_noise2d( x, z, f, d, s )
#define MAP_GEN_PERLIN3D( x, y, z, f, d, s ) perlin_noise3d( x, y, z, f, d, s )
#include "common/map_gen_fields.hpp"

#if !REPGAME_MAP_GEN_LEGACY
// The reworked generator only — under legacy builds every function below is
// unused (StructureGen::place dispatches to place_legacy) and the mg_* fields
// it relies on are compiled out of map_gen_fields.hpp.

// Reads pure map gen on world coords — two chunks always agree.
static int sg_world_is_next_to( const int wx, const int wy, const int wz, const BlockID block ) {
    for ( int i = -1; i < 2; i++ ) {
        for ( int j = -1; j < 2; j++ ) {
            if ( i == 0 && j == 0 ) {
                continue;
            }
            if ( MapGen::gen_block_id( wx + i, wy, wz + j ) == block ) {
                return 1;
            }
        }
    }
    return 0;
}

static void sg_write_decor( Chunk &chunk, const int lx, const int ly, const int lz, const BlockID id ) {
    const BlockID cur = chunk.get_block( glm::ivec3( lx, ly, lz ) ).id;
    if ( cur == AIR || cur == WATER ) {
        const Block *def = block_definition_get_definition( id );
        chunk.set_block( glm::ivec3( lx, ly, lz ), { id, BLOCK_ROTATE_0, def->initial_redstone_power, id, 0 } );
    }
}

// ---------------------------------------------------------------------------
// Biome gates shared by the tree and decoration passes. Species/decor picks
// work off the same mg_* fields the terrain surface pick uses, so a column's
// biome always agrees with the blocks it grows.
// ---------------------------------------------------------------------------

// Cold wastes: frozen lakes, ice spikes, pine stands, powder snow.
static bool sg_is_cold( const float biome ) {
    return biome < 0.22f;
}

// Hot+dry desert (mirrors the surface pick's red-sand gate).
static bool sg_is_desert( const float biome, const float weird ) {
    return biome > 0.72f && weird < 0.55f;
}

// Cherry groves: a narrow band inside temperate latitudes, further cut by a
// patch mask so they show up as occasional small groves, not whole regions.
static bool sg_is_cherry( const float biome, const float weird, const int x, const int z ) {
    return biome > 0.30f && biome < 0.72f && weird > 0.635f && weird <= 0.685f &&
           perlin_noise2d( x, z, 0.0022f, 2, MAP_SEED + 76 ) > 0.52f;
}

// Biome-only pre-gate for mangrove swamps; the full check also needs terrain
// height (mg_is_swamp) and is only evaluated when this passes.
static bool sg_is_swampy( const float biome, const float weird ) {
    return biome > 0.45f && biome < 0.68f && weird > 0.30f && weird < 0.55f;
}

// Per-cell tree decision: position inside the cell, existence (biome-driven
// density), species, size. Pure f(cell coords + world biome fields), so every
// chunk makes the same call for a shared tree.
static bool sg_tree_for_cell( const int cx, const int cz, int *out_tx, int *out_tz, TreeSpec *spec ) {
    const int tx = cx * MAPGEN_TREE_CELL + (int)( sg_roll( cx, cz, 1 ) * MAPGEN_TREE_CELL );
    const int tz = cz * MAPGEN_TREE_CELL + (int)( sg_roll( cx, cz, 2 ) * MAPGEN_TREE_CELL );

    const float biome = mg_biome( tx, tz );
    const float weird = mg_weird( tx, tz );
    const bool pale = mg_is_pale( tx, tz, weird );
    // Mangrove swamps need real terrain height — only evaluated inside the
    // cheap biome pre-gate. This is just a pre-filter so swampy dry flats
    // still roll normal species; place_trees gates on the found surface
    // itself, since a submerged base height can hide an island top above.
    const bool swampy_biome = sg_is_swampy( biome, weird );
    const float th = swampy_biome ? MapGen::calculateTerrainHeight( tx, tz ) : 0.0f;
    const bool swamp = swampy_biome && th != 0.0f && th > WATER_LEVEL - 7.5f && th < WATER_LEVEL + 1.0f;

    float density;
    if ( biome > 0.72f ) {
        density = weird >= 0.55f ? 0.95f : 0.05f; // jungle vs savanna/desert
    } else if ( biome < 0.22f ) {
        density = 0.45f; // snowy pine stands
    } else if ( pale ) {
        density = 0.90f; // pale garden: dense ghostly canopy
    } else if ( weird > 0.88f ) {
        density = 0.25f; // mushroom fields: sparse giant mushrooms
    } else if ( swamp ) {
        density = 0.35f; // mangrove stands along the waterline
    } else if ( sg_is_cherry( biome, weird, tx, tz ) ) {
        density = 0.60f; // cherry groves
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
    spec->hanger = AIR;
    spec->hanger_len = 0;
    spec->hanger_mask = 0;
    if ( biome > 0.72f ) {
        if ( weird >= 0.55f ) {
            spec->log = JUNGLE_LOG;
            spec->leaf = JUNGLE_LEAVES;
            spec->shape = SHAPE_JUNGLE;
            spec->hanger = VINE;
            spec->hanger_len = 4;
            spec->hanger_mask = 3u;
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
            spec->leaf = OAK_LEAVES;
            spec->shape = SHAPE_ACACIA;
            spec->trunk_h = 4 + (int)( hr * 3.0f );
            spec->canopy_r = 2;
            const float lr = sg_roll( cx, cz, 6 );
            spec->lean_x = lr < 0.33f ? 1 : lr < 0.66f ? -1 : 0;
            spec->lean_z = spec->lean_x == 0 ? ( lr < 0.5f ? 1 : -1 ) : 0;
        }
    } else if ( biome < 0.22f ) {
        spec->log = DARK_OAK_LOG;
        spec->leaf = SPRUCE_LEAVES;
        spec->shape = SHAPE_PINE;
        spec->trunk_h = 7 + (int)( hr * 4.0f );
        spec->canopy_r = 2;
    } else if ( pale ) {
        spec->log = PALE_OAK_LOG;
        spec->leaf = PALE_OAK_LEAVES;
        spec->shape = SHAPE_BLOB;
        spec->trunk_h = 7 + (int)( hr * 4.0f );
        spec->canopy_r = 4;
        spec->hanger = PALE_HANGING_MOSS;
        spec->hanger_len = 3;
        spec->hanger_mask = 7u;
    } else if ( weird > 0.88f ) {
        spec->log = MUSHROOM_STEM;
        spec->leaf = sr < 0.6f ? RED_MUSHROOM_BLOCK : BROWN_MUSHROOM_BLOCK;
        spec->shape = SHAPE_MUSHROOM;
        spec->trunk_h = 4 + (int)( hr * 3.0f );
        spec->canopy_r = 3 + ( sr > 0.7f ? 1 : 0 );
    } else if ( swamp ) {
        spec->log = MANGROVE_LOG;
        spec->leaf = MANGROVE_LEAVES;
        spec->shape = SHAPE_MANGROVE;
        // Tall stilt trunks keep the canopy above the waterline even when the
        // seabed sits a few blocks under it — swamps stay swimmable.
        spec->trunk_h = 7 + (int)( hr * 4.0f );
        spec->canopy_r = 3;
        spec->hanger = MANGROVE_PROPAGULE;
        spec->hanger_len = 1;
        spec->hanger_mask = 3u;
    } else if ( sg_is_cherry( biome, weird, tx, tz ) ) {
        spec->log = CHERRY_LOG;
        spec->leaf = CHERRY_LEAVES;
        spec->shape = SHAPE_BLOB;
        spec->trunk_h = 5 + (int)( hr * 3.0f );
        spec->canopy_r = 3;
    } else if ( weird > 0.75f ) {
        spec->log = DARK_OAK_LOG;
        spec->leaf = OAK_LEAVES;
        spec->shape = SHAPE_BLOB;
        spec->trunk_h = 6 + (int)( hr * 3.0f );
        spec->canopy_r = 4;
    } else if ( sr < 0.5f ) {
        spec->log = OAK_LOG;
        spec->leaf = OAK_LEAVES;
        spec->shape = SHAPE_BLOB;
        spec->trunk_h = 4 + (int)( hr * 3.0f );
        spec->canopy_r = 3;
    } else if ( sr < 0.85f ) {
        spec->log = BIRCH_LOG;
        spec->leaf = BIRCH_LEAVES;
        spec->shape = SHAPE_BLOB;
        spec->trunk_h = 5 + (int)( hr * 3.0f );
        spec->canopy_r = 3;
    } else {
        spec->log = DARK_OAK_LOG;
        spec->leaf = SPRUCE_LEAVES;
        spec->shape = SHAPE_PINE;
        spec->trunk_h = 7 + (int)( hr * 4.0f );
        spec->canopy_r = 2;
    }
    *out_tx = tx;
    *out_tz = tz;
    return true;
}

// "No surface" sentinel: can't be a real y (seabeds legitimately sit at
// negative y — a -1 sentinel there would silently reject every underwater
// column, which is exactly what happened before this existed).
#define SG_NO_SURFACE -1000000

// Real surface of a column, derived from pure map gen (not this chunk's block
// array) so every chunk agrees even when the ground lives in a different
// chunk. The column's MapGenColumn comes back through `col` so callers can
// pick blocks without recomputing the column fields. The scan window is the
// Per-place() cache of mg_base_height over the column window [-1, CHUNK+1]
// (the extra ring covers the steepness probes at +1/+1). The heightfield is
// the most expensive 2D field and mg_column_info samples it three times per
// column (center + two neighbors); caching collapses that to one eval per
// column shared across all passes.
struct SgHeightGrid {
    float v[ ( CHUNK_SIZE_X + 3 ) * ( CHUNK_SIZE_Z + 3 ) ];
    unsigned long long ok[ ( ( CHUNK_SIZE_X + 3 ) * ( CHUNK_SIZE_Z + 3 ) + 63 ) / 64 ];
    int cox, coz;

    SgHeightGrid( const int cox_, const int coz_ ) : cox( cox_ ), coz( coz_ ) {
        memset( v, 0, sizeof( v ) );
        memset( ok, 0, sizeof( ok ) );
    }

    float at( const int wx, const int wz ) {
        const int ix = wx - cox + 1, iz = wz - coz + 1;
        if ( ix < 0 || iz < 0 || ix > CHUNK_SIZE_X + 2 || iz > CHUNK_SIZE_Z + 2 ) {
            return mg_base_height( wx, wz );
        }
        const int i = ix * ( CHUNK_SIZE_Z + 3 ) + iz;
        const unsigned long long bit = 1ull << ( i & 63 );
        if ( !( ok[ i >> 6 ] & bit ) ) {
            v[ i ] = mg_base_height( wx, wz );
            ok[ i >> 6 ] |= bit;
        }
        return v[ i ];
    }
};

// Same fields as mg_column_info, but the three heightfield samples come
// from the shared grid — bit-identical results, ~2/3 less heightfield work.
static MapGenColumn sg_column_info( const int x, const int z, SgHeightGrid &hg ) {
    MapGenColumn col;
    col.height = hg.at( x, z );
    col.steepness = fmaxf( fabsf( hg.at( x + 1, z ) - col.height ), fabsf( hg.at( x, z + 1 ) - col.height ) );
    col.biome = mg_biome( x, z );
    col.weird = mg_weird( x, z );
    col.rough_gain = mg_rough_gain( x, z );
    col.island = mg_island_strength( x, z );
    col.island_dy0 = col.island > 0.0f ? mg_island_dy0( x, z ) : 0.0f;
    col.island_tin = col.island > 0.0f ? mg_island_footprint( x, z ) : 0.0f;
    col.desert = mg_desert( x, z );
    col.mesa_riser = mg_mesa_riser( x, z, col.height, col.desert );
    return col;
}

// exact solid-reach bound: overhang sheets need dy < 0.45*rough_gain (the
// ridged field tops out at n=0.45 with gate<=1), islands reach
// island_dy0 + half_thickness, and the below-heightmap notch can carve at
// most 0.5*rough_gain deep — far tighter than OVERHANG_MAX_RISE for flat
// land. Cells are tested with mg_solid_at (cheap) and confirmed with
// mg_pick_block (cave carving can still void an otherwise-solid cell).
static int sg_find_surface_y( const int wx, const int wz, const MapGenColumn &col ) {
    float top = col.height + 0.45f * col.rough_gain + 2.0f;
    if ( col.island > 0.0f ) {
        const float half = fminf( 3.0f + col.island * 24.0f, col.island_dy0 - 4.0f );
        const float ti = col.height + col.island_dy0 + half + 2.0f;
        top = top > ti ? top : ti;
    }
    const int bottom = (int)( col.height - 0.55f * col.rough_gain - 3.0f );
    for ( int y = (int)top; y >= bottom; y-- ) {
        if ( !mg_solid_at( wx, y, wz, col ) ) {
            continue;
        }
        const BlockID b = mg_pick_block( wx, y, wz, col );
        if ( b != AIR && b != WATER ) {
            return y;
        }
    }
    return SG_NO_SURFACE;
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
            const MapGenColumn tcol = mg_column_info( tx, tz );
            const int gy = sg_find_surface_y( tx, tz, tcol );
            if ( gy == SG_NO_SURFACE ) {
                continue;
            }
            const BlockID ground = mg_pick_block( tx, gy, tz, tcol );
            if ( spec.shape == SHAPE_MANGROVE ) {
                // Must actually stand in the water: mg_base_height can be
                // submerged while the found surface is a floating island or
                // dry bank, which would leave the roots dangling in air.
                // A seabed at gy<0 puts the trunk base in water (up to ~3
                // blocks deep); gy == WATER_LEVEL is a waterline hump whose
                // ring roots descend into neighboring water cells.
                if ( gy > WATER_LEVEL || gy < WATER_LEVEL - 4 ) {
                    continue;
                }
                // A waterline mud flat isn't enough either: there the root
                // ring just buries into dirt. Require open water at the
                // waterline next to the trunk so at least one stilt root
                // lands in the water (a trunk base at y<=-1 is wet already).
                bool wet_edge = gy < WATER_LEVEL - 1;
                for ( int dx = -1; dx <= 1 && !wet_edge; dx++ ) {
                    for ( int dz = -1; dz <= 1 && !wet_edge; dz++ ) {
                        if ( dx == 0 && dz == 0 ) {
                            continue;
                        }
                        wet_edge = MapGen::gen_block_id( tx + dx, WATER_LEVEL - 1, tz + dz ) == WATER;
                    }
                }
                if ( !wet_edge ) {
                    continue;
                }
                // Mangroves root in wet soil — including under shallow water.
                if ( ground != MUD && ground != DIRT && ground != SAND && ground != GRAVEL && ground != GRASS ) {
                    continue;
                }
            } else if ( gy < WATER_LEVEL || ( ground != GRASS && ground != PODZOL && ground != MYCELIUM && ground != SNOWY_GRASS && ground != DIRT && ground != SNOW && ground != PALE_MOSS_BLOCK ) ) {
                continue;
            }
            // Skip trees that can't touch this chunk's y range (+/-1 border;
            // mangrove stilt roots dangle up to 5 below the trunk base).
            const int top = gy + spec.trunk_h + 3;
            if ( top < coy - 1 || gy - 5 > coy + CHUNK_SIZE_Y ) {
                continue;
            }
            sg_place_tree( chunk, spec, tx - cox, gy + 1 - coy, tz - coz, glm::ivec3( cox, coy, coz ) );
        }
    }
}

// ---------------------------------------------------------------------------
// Decorations: per-column surface scatter on this chunk's interior cells.
// ---------------------------------------------------------------------------

static BlockID sg_pick_flower( const int wx, const int wz ) {
    const float r = sg_roll( wx, wz, 30 );
    if ( r < 0.20f ) return DANDELION;
    if ( r < 0.35f ) return RED_FLOWER;
    if ( r < 0.45f ) return BLUE_ORCHID;
    if ( r < 0.55f ) return POPPY;
    if ( r < 0.62f ) return RED_TULIP;
    if ( r < 0.68f ) return ORANGE_TULIP;
    if ( r < 0.74f ) return WHITE_TULIP;
    if ( r < 0.80f ) return PINK_TULIP;
    if ( r < 0.88f ) return CORNFLOWER;
    if ( r < 0.94f ) return OXEYE_DAISY;
    return AZURE_BLUET;
}

static BlockID sg_pick_coral( const int wx, const int wz ) {
    const float r = sg_roll( wx, wz, 31 );
    if ( r < 0.15f ) return TUBE_CORAL;
    if ( r < 0.30f ) return TUBE_CORAL_FAN;
    if ( r < 0.45f ) return FIRE_CORAL;
    if ( r < 0.55f ) return FIRE_CORAL_FAN;
    if ( r < 0.70f ) return BUBBLE_CORAL;
    if ( r < 0.80f ) return PURPLE_CORAL2;
    if ( r < 0.90f ) return HORN_CORAL;
    return HORN_CORAL_FAN;
}

// Desert cacti: a short column, sometimes crowned with a cactus flower.
static void sg_place_cactus( Chunk &chunk, const int lx, const int surface_y, const int lz, const int wx, const int wz ) {
    const int h = 1 + (int)( sg_roll( wx, wz, 50 ) * 3.0f );
    for ( int i = 0; i < h && surface_y + i <= CHUNK_SIZE_Y; i++ ) {
        sg_write_decor( chunk, lx, surface_y + i, lz, CACTUS );
    }
    if ( sg_roll( wx, wz, 51 ) < 0.20f && surface_y + h <= CHUNK_SIZE_Y ) {
        sg_write_decor( chunk, lx, surface_y + h, lz, CACTUS_FLOWER );
    }
}

void StructureGen::place_decorations( Chunk &chunk, const int cox, const int coy, const int coz, SgHeightGrid &hg ) {
    // Column loop covers the interior plus the +/-1 halo: decor rolls are
    // per-world-column, so a scatter cell on a chunk edge is written
    // identically by both sides — the halo-mirroring contract needs it.
    for ( int lx = -1; lx <= CHUNK_SIZE_X; lx++ ) {
        const int wx = cox + lx;
        for ( int lz = -1; lz <= CHUNK_SIZE_Z; lz++ ) {
            const int wz = coz + lz;

            // Column surface from pure map gen (not this chunk's array), so
            // every chunk agrees even when the ground sits in a neighbor's
            // y-slab — the halo-mirroring contract needs it. The decoration
            // cell is the air/water cell directly above the surface block;
            // writes go through the +/-1 halo and set_block bounds them.
            // Cheap slab cull first: surfaces can't exceed
            // OVERHANG_MAX_RISE above (or a similar depth below) the base
            // heightfield, so slabs they can't reach skip the column scan.
            const float hb = hg.at( wx, wz );
            if ( hb + 70.0f < coy || hb - 70.0f > coy + CHUNK_SIZE_Y ) {
                continue;
            }
            const MapGenColumn dcol = sg_column_info( wx, wz, hg );
            const int gy = sg_find_surface_y( wx, wz, dcol );
            if ( gy == SG_NO_SURFACE ) {
                continue;
            }
            const int surface_y = gy + 1 - coy;
            if ( surface_y < -1 || surface_y > CHUNK_SIZE_Y ) {
                continue;
            }
            const BlockID ground = mg_pick_block( wx, gy, wz, dcol );
            const bool under_water = gy < WATER_LEVEL;

            const float roll = sg_roll( wx, wz, 40 );

            if ( under_water ) {
                // Frozen surfaces in the cold biome: a patchy ice sheet on
                // the waterline cell, the odd snow drift on top.
                if ( sg_is_cold( mg_biome( wx, wz ) ) ) {
                    const int icy = WATER_LEVEL - 1 - coy;
                    if ( mg_pick_block( wx, WATER_LEVEL - 1, wz, dcol ) == WATER &&
                         perlin_noise2d( wx, wz, 0.01f, 2, MAP_SEED + 74 ) > 0.45f ) {
                        sg_write_block( chunk, lx, icy, lz, ICE );
                        if ( roll < 0.10f ) {
                            sg_write_decor( chunk, lx, icy + 1, lz, SNOW_LAYER );
                        }
                    }
                }
                // Shallow seabeds only, so deep oceans stay barren. The plant
                // cell (gy+1) must itself be under water, and tall quads need
                // an extra cell of headroom — otherwise decor sits on top of
                // the surface or pokes through it.
                if ( ( ground == SAND || ground == GRAVEL || ground == DIRT || ground == MUD ) && gy > WATER_LEVEL - 12 && gy <= WATER_LEVEL - 2 ) {
                    const bool tall_ok = gy <= WATER_LEVEL - 3;
                    if ( mg_is_swamp( dcol.biome, dcol.weird, dcol.height ) ) {
                        // Swamp water stays murky — no coral, just sparse
                        // seagrass and the odd tall tuft.
                        if ( roll < 0.10f ) {
                            sg_write_decor( chunk, lx, surface_y, lz, SEAGRASS );
                        } else if ( roll < 0.14f && tall_ok ) {
                            sg_write_decor( chunk, lx, surface_y, lz, UNDERWATER_TUFT_TALL );
                        }
                    } else if ( perlin_noise2d( wx, wz, 0.004f, 2, MAP_SEED + 72 ) > 0.62f ) {
                        if ( roll < 0.20f ) {
                            sg_write_decor( chunk, lx, surface_y, lz, sg_pick_coral( wx, wz ) );
                        } else if ( roll < 0.28f ) {
                            sg_write_decor( chunk, lx, surface_y, lz, SEA_PICKLE );
                        } else if ( roll < 0.36f && tall_ok ) {
                            sg_write_decor( chunk, lx, surface_y, lz, KELP );
                        } else if ( roll < 0.44f ) {
                            sg_write_decor( chunk, lx, surface_y, lz, tall_ok && roll > 0.40f ? UNDERWATER_TUFT_TALL : SEAGRASS );
                        }
                    } else if ( roll < 0.015f ) {
                        sg_write_decor( chunk, lx, surface_y, lz, sg_pick_coral( wx, wz ) );
                    } else if ( roll < 0.06f && tall_ok ) {
                        sg_write_decor( chunk, lx, surface_y, lz, KELP );
                    } else if ( roll < 0.10f ) {
                        sg_write_decor( chunk, lx, surface_y, lz, tall_ok && roll > 0.08f ? UNDERWATER_TUFT_TALL : SEAGRASS );
                    }
                }
                continue;
            }

            // Pale-garden ground is PALE_MOSS_BLOCK, not grass — just carpet,
            // no other scatter underneath the ghost canopy.
            if ( ground == PALE_MOSS_BLOCK ) {
                if ( roll < 0.30f ) {
                    sg_write_decor( chunk, lx, surface_y, lz, PALE_MOSS_CARPET );
                }
                continue;
            }

            switch ( ground ) {
                case GRASS: {
                    const float biome = mg_biome( wx, wz );
                    const float weird = mg_weird( wx, wz );
                    if ( sg_is_cherry( biome, weird, wx, wz ) ) {
                        // Cherry groves: flowering-azalea undergrowth, pink
                        // flowers.
                        if ( roll < 0.10f ) {
                            sg_write_decor( chunk, lx, surface_y, lz, sg_roll( wx, wz, 46 ) < 0.5f ? FLOWERING_AZALEA : AZALEA );
                        } else if ( roll < 0.12f ) {
                            const float fr = sg_roll( wx, wz, 30 );
                            sg_write_decor( chunk, lx, surface_y, lz, fr < 0.4f ? PINK_TULIP : fr < 0.7f ? ALLIUM : sg_pick_flower( wx, wz ) );
                        }
                    } else if ( sg_is_swampy( biome, weird ) && roll < 0.08f &&
                                mg_is_swamp( biome, weird, MapGen::calculateTerrainHeight( wx, wz ) ) ) {
                        // Propagules rooted in the mud flats.
                        sg_write_decor( chunk, lx, surface_y, lz, MANGROVE_PROPAGULE );
                    } else if ( weird <= 0.40f && perlin_noise2d( wx, wz, 0.006f, 2, MAP_SEED + 73 ) > 0.64f ) {
                        // Flower fields: scattered blooms inside a rare mask.
                        if ( roll < 0.07f ) {
                            sg_write_decor( chunk, lx, surface_y, lz, SUNFLOWER );
                        } else if ( roll < 0.14f ) {
                            const float fr = sg_roll( wx, wz, 47 );
                            sg_write_decor( chunk, lx, surface_y, lz, fr < 0.33f ? LILAC : fr < 0.66f ? PEONY : ROSE_BUSH );
                        } else if ( roll < 0.28f ) {
                            sg_write_decor( chunk, lx, surface_y, lz, sg_roll( wx, wz, 48 ) < 0.5f ? sg_pick_flower( wx, wz ) : SHORT_GRASS );
                        }
                    } else if ( biome < 0.72f && weird > 0.30f && weird < 0.60f &&
                                perlin_noise2d( wx, wz, 0.02f, 1, MAP_SEED + 75 ) > 0.80f && roll < 0.015f ) {
                        // Pumpkin/melon patches in temperate country.
                        sg_write_decor( chunk, lx, surface_y, lz, sg_roll( wx, wz, 49 ) < 0.6f ? PUMPKIN : MELON_BLOCK );
                    } else if ( biome > 0.72f && weird >= 0.55f && roll < 0.06f &&
                                gy <= WATER_LEVEL + 3 && sg_world_is_next_to( wx, WATER_LEVEL - 1, wz, WATER ) ) {
                        // Jungle-edge bamboo, a few blocks tall.
                        const int h = 2 + (int)( sg_roll( wx, wz, 41 ) * 3.0f );
                        for ( int i = 0; i < h && surface_y + i <= CHUNK_SIZE_Y; i++ ) {
                            sg_write_decor( chunk, lx, surface_y + i, lz, BAMBOO_SAPLING );
                        }
                    } else if ( weird > 0.35f && weird < 0.60f && roll < 0.16f ) {
                        // Meadows: a few tufts and flowers.
                        sg_write_decor( chunk, lx, surface_y, lz, roll < 0.05f ? sg_pick_flower( wx, wz ) : ( roll < 0.10f ? FERN : SHORT_GRASS ) );
                    } else if ( roll < 0.08f ) {
                        sg_write_decor( chunk, lx, surface_y, lz, roll < 0.02f ? sg_pick_flower( wx, wz ) : SHORT_GRASS );
                    }
                    break;
                }
                case PODZOL:
                    if ( roll < 0.10f ) {
                        sg_write_decor( chunk, lx, surface_y, lz, sg_roll( wx, wz, 43 ) < 0.5f ? BROWN_MUSHROOM : RED_MUSHROOM );
                    } else if ( roll < 0.20f ) {
                        // Dark-forest floor ferns.
                        sg_write_decor( chunk, lx, surface_y, lz, sg_roll( wx, wz, 53 ) < 0.5f ? LARGE_FERN : FERN );
                    }
                    break;
                case MYCELIUM:
                    if ( roll < 0.25f ) {
                        sg_write_decor( chunk, lx, surface_y, lz, sg_roll( wx, wz, 44 ) < 0.5f ? BROWN_MUSHROOM : RED_MUSHROOM );
                    }
                    break;
                case SNOWY_GRASS:
                case SNOW:
                    if ( ground == SNOWY_GRASS && roll < 0.02f ) {
                        sg_write_decor( chunk, lx, surface_y, lz, PINECONE );
                    } else if ( sg_is_cold( mg_biome( wx, wz ) ) && roll < 0.14f ) {
                        // Snow drifts — occasionally a powder-snow trap.
                        sg_write_decor( chunk, lx, surface_y, lz, sg_roll( wx, wz, 52 ) < 0.85f ? SNOW_LAYER : POWDER_SNOW );
                    }
                    break;
                case SAND:
                    // Reeds on sand next to water.
                    if ( roll < 0.05f && gy <= WATER_LEVEL + 3 && sg_world_is_next_to( wx, WATER_LEVEL - 1, wz, WATER ) && surface_y + 2 <= CHUNK_SIZE_Y ) {
                        for ( int i = 0; i < 3; i++ ) {
                            sg_write_decor( chunk, lx, surface_y + i, lz, REEDS );
                        }
                    } else if ( roll < 0.015f && sg_is_desert( mg_biome( wx, wz ), mg_weird( wx, wz ) ) ) {
                        sg_place_cactus( chunk, lx, surface_y, lz, wx, wz );
                    }
                    break;
                case RED_SAND:
                case WHITE_TERRACOTTA:
                case YELLOW_TERRACOTTA:
                case ORANGE_TERRACOTTA:
                case RED_TERRACOTTA:
                case BROWN_TERRACOTTA:
                case LIGHT_GRAY_TERRACOTTA:
                case GRAY_TERRACOTTA:
                    // Badlands scrub, sparse: rare cacti, dry tufts, leafy
                    // and dead bushes, the odd golden dandelion or firefly
                    // bush.
                    if ( roll < 0.0025f ) {
                        sg_place_cactus( chunk, lx, surface_y, lz, wx, wz );
                    } else if ( roll < 0.005f ) {
                        sg_write_decor( chunk, lx, surface_y, lz, sg_roll( wx, wz, 45 ) < 0.5f ? GRASS_TUFT : SHORT_GRASS );
                    } else if ( roll < 0.0065f ) {
                        sg_write_decor( chunk, lx, surface_y, lz, BUSH );
                    } else if ( roll < 0.008f ) {
                        sg_write_decor( chunk, lx, surface_y, lz, sg_roll( wx, wz, 46 ) < 0.5f ? DARK_DEAD_SAPLING : DEADBUSH );
                    } else if ( roll < 0.0085f ) {
                        sg_write_decor( chunk, lx, surface_y, lz, sg_roll( wx, wz, 47 ) < 0.6f ? GOLDEN_DANDELION : DANDELION );
                    } else if ( roll < 0.00875f ) {
                        sg_write_decor( chunk, lx, surface_y, lz, FIREFLY_BUSH );
                    }
                    break;
                case MUD:
                    if ( roll < 0.06f ) {
                        sg_write_decor( chunk, lx, surface_y, lz, MANGROVE_PROPAGULE );
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
    SG_FEAT_MONOLITH, // stone-brick pillar topped with glowstone + torch
    SG_FEAT_ICESPIKE, // packed-ice spikes on cold land, blue-ice tips
    SG_FEAT_ICEBERG,  // packed/blue ice cone floating on cold oceans
};

typedef struct {
    int kind;
    float ax, az; // anchor / center column
    // spike clusters:
    int n_sub;
    float sx[ SG_FEATURE_MAX_SUB ], sz[ SG_FEATURE_MAX_SUB ], speak[ SG_FEATURE_MAX_SUB ];
    // radial cones (icebergs):
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
    const float anchor_h = MapGen::calculateTerrainHeight( (int)f->ax, (int)f->az );
    const float biome = mg_biome( (int)f->ax, (int)f->az );
    // Features stand on land — except icebergs, which need open cold ocean.
    if ( anchor_h < WATER_LEVEL + 1.5f ) {
        if ( r < 0.10f && biome < 0.22f && anchor_h < WATER_LEVEL - 4.0f ) {
            f->kind = SG_FEAT_ICEBERG;
            f->vradius = 5.0f + sg_roll( cx, cz, 90 ) * 6.0f;
            f->vpeak = 5.0f + sg_roll( cx, cz, 91 ) * 7.0f;
            return true;
        }
        return false;
    }
    if ( r < 0.13f ) {
        // Packed-ice spikes in the frozen wastes.
        if ( !sg_is_cold( biome ) ) {
            return false;
        }
        f->kind = SG_FEAT_ICESPIKE;
        f->n_sub = 2 + (int)( sg_roll( cx, cz, 63 ) * (float)( SG_FEATURE_MAX_SUB - 3 ) ); // 2..5
        for ( int i = 0; i < f->n_sub; i++ ) {
            f->sx[ i ] = f->ax + ( sg_roll( cx, cz, 70 + i * 2 ) - 0.5f ) * 30.0f;
            f->sz[ i ] = f->az + ( sg_roll( cx, cz, 71 + i * 2 ) - 0.5f ) * 30.0f;
            f->speak[ i ] = 9.0f + sg_roll( cx, cz, 80 + i ) * 12.0f;
        }
        return true;
    }
    if ( r < 0.21f ) {
        return false;
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
    float dist; // column distance to the winning anchor
    float aux;  // iceberg radius
} SGClaim;

static SGClaim sg_column_feature( const SGFeature *feats, const int n, const int wx, const int wz ) {
    SGClaim best = { SG_FEAT_NONE, 0.0f, 0.0f, 0.0f };
    for ( int i = 0; i < n; i++ ) {
        const SGFeature &f = feats[ i ];
        float h = 0.0f, d = 0.0f;
        if ( f.kind == SG_FEAT_ICESPIKE ) {
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
        } else if ( f.kind == SG_FEAT_ICEBERG ) {
            const float dx = (float)wx - f.ax;
            const float dz = (float)wz - f.az;
            d = sqrtf( dx * dx + dz * dz );
            if ( d < f.vradius ) {
                h = f.vpeak - d * 1.6f;
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
        }
    }
    return best;
}

// Write one feature/crystal cell: only ever into air, water, or plants — a
// feature yields to terrain, trunks and other solids instead of eating them.
// The +/-1 halo rows are writable so border cells mirror; returns false when
// blocked or out of range, so callers can stop a column at the obstacle.
static bool sg_write_feature( Chunk &chunk, const int lx, const int ly, const int lz, const BlockID id ) {
    if ( ly < -1 || ly > CHUNK_SIZE_Y ) {
        return false;
    }
    if ( !sg_can_replace( chunk, lx, ly, lz ) ) {
        return false;
    }
    const Block *def = block_definition_get_definition( id );
    chunk.set_block( glm::ivec3( lx, ly, lz ), { id, BLOCK_ROTATE_0, def->initial_redstone_power, id, 0 } );
    return true;
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

    // Column loop covers the interior plus the +/-1 halo: feature claims are
    // per-world-column, so a feature cell on a chunk edge is written
    // identically by both sides — the halo-mirroring contract needs it.
    for ( int lx = -1; lx <= CHUNK_SIZE_X; lx++ ) {
        const int wx = cox + lx;
        for ( int lz = -1; lz <= CHUNK_SIZE_Z; lz++ ) {
            const int wz = coz + lz;
            const SGClaim c = sg_column_feature( feats, n_feats, wx, wz );
            if ( c.kind == SG_FEAT_NONE ) {
                continue;
            }
            // Column surface from pure map gen (not this chunk's array), so
            // every chunk agrees even when the ground sits in a neighbor's
            // y-slab — the halo-mirroring contract needs it. Shaft cells below
            // this chunk's reach are skipped, not fatal; the owner writes them.
            const MapGenColumn fcol = mg_column_info( wx, wz );
            const int gy = sg_find_surface_y( wx, wz, fcol );
            if ( gy == SG_NO_SURFACE ) {
                continue;
            }
            const int surface_y = gy + 1 - coy;
            if ( surface_y > CHUNK_SIZE_Y ) {
                continue;
            }
            const BlockID ground = mg_pick_block( wx, gy, wz, fcol );
            const bool under_water = gy < WATER_LEVEL;
            // Submerged seabeds only carry icebergs — land features on a
            // seabed were never reachable before and would look wrong.
            if ( under_water && c.kind != SG_FEAT_ICEBERG ) {
                continue;
            }
            // Features need real ground — not leaf canopies or trunks.
            if ( ground >= LAST_BLOCK_ID || !block_definition_get_definition( ground )->is_solid ) {
                continue;
            }
            if ( c.kind == SG_FEAT_ICEBERG ) {
                if ( !under_water ) {
                    continue;
                }
                // Floating berg: a cone rising over the waterline; below it
                // only the inner half of the disc survives → tapering keel.
                const int wy_lo = coy - 1 > WATER_LEVEL - 4 ? coy - 1 : WATER_LEVEL - 4;
                const int wy_hi = coy + CHUNK_SIZE_Y < WATER_LEVEL + (int)c.h ? coy + CHUNK_SIZE_Y : WATER_LEVEL + (int)c.h;
                for ( int wy = wy_lo; wy <= wy_hi; wy++ ) {
                    if ( wy < WATER_LEVEL && c.dist > c.aux * 0.55f ) {
                        continue; // keel only under the berg's core
                    }
                    const unsigned int hh = sg_hash( wx, wy, wz, 95 );
                    const BlockID id = wy < WATER_LEVEL && ( hh & 7u ) == 0u ? BLUE_ICE : PACKED_ICE;
                    sg_write_feature( chunk, lx, wy - coy, lz, id );
                }
                continue;
            }
            const int h_max = CHUNK_SIZE_Y + 1 - surface_y;
            const int h = (int)c.h < h_max ? (int)c.h : h_max;
            if ( h < 3 ) {
                continue;
            }
            if ( c.kind == SG_FEAT_ICESPIKE ) {
                if ( under_water ) {
                    continue;
                }
                for ( int i = 0; i < h; i++ ) {
                    const int ly = surface_y + i;
                    if ( ly < -1 ) {
                        continue;
                    }
                    const BlockID id = i == h - 1 ? BLUE_ICE : PACKED_ICE;
                    if ( !sg_write_feature( chunk, lx, ly, lz, id ) ) {
                        break;
                    }
                }
                continue;
            }
            { // SG_FEAT_MONOLITH
                if ( under_water ) {
                    continue;
                }
                for ( int i = 0; i < h; i++ ) {
                    const int ly = surface_y + i;
                    if ( ly < -1 ) {
                        continue;
                    }
                    const BlockID id = i == h - 1 ? GLOWSTONE : STONE_BRICKS;
                    if ( !sg_write_feature( chunk, lx, ly, lz, id ) ) {
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
// Formations & sheltered glow: per-cell deterministic decorations anchored
// to solid ceilings and floors. Deep caves get plain stone stalactites and
// stalagmites; sheltered rock near the surface (overhang lips, cave mouths,
// island undersides) gets glow-lichen colonies and rare glow-berry strands.
// Clusters live in 16-block neighborhoods.
// ---------------------------------------------------------------------------

static bool sg_is_solid_block( const BlockID id ) {
    if ( id >= LAST_BLOCK_ID ) {
        return false;
    }
    return block_definition_get_definition( id )->is_solid;
}

static void sg_set_block( Chunk &chunk, const int lx, const int ly, const int lz, const BlockID id ) {
    const Block *def = block_definition_get_definition( id );
    chunk.set_block( glm::ivec3( lx, ly, lz ), { id, BLOCK_ROTATE_0, def->initial_redstone_power, id, 0 } );
}

void StructureGen::place_formations( Chunk &chunk, const int cox, const int coy, const int coz, SgHeightGrid &hg ) {
    // Cells cover the +/-1 halo on all axes: formation anchors and lengths
    // are derived from pure map gen (a roof can live in the neighbor's
    // y-slab), so a formation cell on a chunk edge is written identically by
    // both sides — the halo-mirroring contract needs it.
    for ( int lx = -1; lx <= CHUNK_SIZE_X; lx++ ) {
        const int wx = cox + lx;
        for ( int lz = -1; lz <= CHUNK_SIZE_Z; lz++ ) {
            const int wz = coz + lz;
            if ( sg_roll( wx >> 4, wz >> 4, 55 ) >= 0.38f ) {
                continue;
            }
            const float h = hg.at( wx, wz );
            // Lichen colonies: a patchy 2D mask so sheltered rock gets
            // clustered patches instead of an even sprinkle.
            const bool lichen_zone = perlin_noise2d( wx, wz, 0.02f, 2, MAP_SEED + 77 ) > 0.55f;
            // Map gen is the authority for roof/floor solidity (the halo
            // contract), but the chunk array is a safe negative: an array-AIR
            // neighbor means map gen is air/water there too — structures only
            // ever add blocks — so the expensive pick only runs under/above
            // something solid. The column fields are computed once per column
            // instead of once per cell (gen_block_id recomputes them).
            MapGenColumn col = {};
            bool col_init = false;
            auto pick = [&]( const int py ) {
                if ( !col_init ) {
                    col = sg_column_info( wx, wz, hg );
                    col_init = true;
                }
                return mg_pick_block( wx, py, wz, col );
            };
            for ( int ly = -1; ly <= CHUNK_SIZE_Y; ly++ ) {
                if ( chunk.get_block( glm::ivec3( lx, ly, lz ) ).id != AIR ) {
                    continue;
                }
                const int wy = coy + ly;
                if ( chunk.get_block( glm::ivec3( lx, ly + 1, lz ) ).id != AIR && sg_is_solid_block( pick( wy + 1 ) ) ) {
                    const unsigned int hh = sg_hash( wx, wy, wz, 56 );
                    if ( wy >= h - 5.0f ) {
                        // Sheltered rock near/above the surface: lichen
                        // patches on overhang lips, cave mouths and island
                        // undersides; rare glow-berry strands on low ledges.
                        if ( lichen_zone && ( hh & 255u ) < 90u ) {
                            sg_set_block( chunk, lx, ly, lz, GLOW_LICHEN );
                        } else if ( wy < h + 8.0f && ( hh & 2047u ) < 10u ) {
                            const int len = 1 + (int)( ( hh >> 11 ) & 3u );
                            int n = 0;
                            while ( n < len && chunk.get_block( glm::ivec3( lx, ly - n, lz ) ).id == AIR ) {
                                n++;
                            }
                            for ( int i = 0; i < n; i++ ) {
                                const int fy = ly - i;
                                if ( fy < -1 || fy > CHUNK_SIZE_Y ) {
                                    continue;
                                }
                                sg_set_block( chunk, lx, fy, lz, i == n - 1 ? CAVE_VINES_HEAD_WITH_BERRIES : CAVE_VINES_BODY_WITH_BERRIES );
                            }
                        }
                    } else if ( ( hh & 1023u ) < 16u ) {
                        // Deep cave roofs: plain stone stalactites.
                        const int len = 1 + (int)( ( hh >> 10 ) & 3u );
                        int n = 0;
                        while ( n < len && pick( wy - 1 - n ) == AIR ) {
                            n++;
                        }
                        for ( int i = 0; i < n; i++ ) {
                            const int fy = ly - 1 - i;
                            if ( fy < -1 || fy > CHUNK_SIZE_Y || chunk.get_block( glm::ivec3( lx, fy, lz ) ).id != AIR ) {
                                continue;
                            }
                            sg_set_block( chunk, lx, fy, lz, STONE );
                        }
                    }
                    continue;
                }
                // Floor formations: cave floors only, above the lava layer.
                if ( wy < h - 5.0f && wy > LAVA_LEVEL + 2 &&
                     chunk.get_block( glm::ivec3( lx, ly - 1, lz ) ).id != AIR ) {
                    const BlockID below = pick( wy - 1 );
                    if ( sg_is_solid_block( below ) ) {
                        const unsigned int hh = sg_hash( wx, wy, wz, 57 );
                        if ( ( hh & 1023u ) < 11u ) {
                            // Plain stone stalagmites.
                            const int len = 1 + (int)( ( hh >> 10 ) & 3u );
                            int n = 0;
                            while ( n < len && pick( wy + n ) == AIR ) {
                                n++;
                            }
                            for ( int i = 0; i < n; i++ ) {
                                const int fy = ly + i;
                                if ( fy < -1 || fy > CHUNK_SIZE_Y || chunk.get_block( glm::ivec3( lx, fy, lz ) ).id != AIR ) {
                                    continue;
                                }
                                sg_set_block( chunk, lx, fy, lz, STONE );
                            }
                        } else if ( wy < LAVA_LEVEL + 12 && below == STONE && ( hh & 2047u ) < 6u ) {
                            // Ember seams: floor stone near the lava band
                            // occasionally swaps to magma for a faint glow.
                            sg_set_block( chunk, lx, ly - 1, lz, MAGMA );
                        }
                    }
                }
            }
        }
    }
}

#endif // !REPGAME_MAP_GEN_LEGACY

void StructureGen::place( Chunk &chunk ) {
#if REPGAME_MAP_GEN_LEGACY
    place_legacy( chunk );
#else
    const int cox = chunk.chunk_pos.x * CHUNK_SIZE_X;
    const int coy = chunk.chunk_pos.y * CHUNK_SIZE_Y;
    const int coz = chunk.chunk_pos.z * CHUNK_SIZE_Z;
    SgHeightGrid hg( cox, coz );
    place_trees( chunk, cox, coy, coz );
    place_features( chunk, cox, coy, coz );
    place_decorations( chunk, cox, coy, coz, hg );
    place_formations( chunk, cox, coy, coz, hg );
#endif
}
