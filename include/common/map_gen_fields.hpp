#pragma once

// Shared terrain-generation fields, included by every map_gen backend.
// The includer must first define:
//   MAP_GEN_QUAL    - function qualifier: "static inline" on CPU,
//                     "__device__" on CUDA/HIP
//   MAP_GEN_PERLIN2D(x, z, freq, depth, seed) - perlin sampler in [0, 1]
//   MAP_GEN_PERLIN3D(x, y, z, freq, depth, seed)
// Because every backend includes this same file, all generated worlds are
// identical by construction (modulo float rounding).
#if !defined( MAP_GEN_QUAL ) || !defined( MAP_GEN_PERLIN2D ) || !defined( MAP_GEN_PERLIN3D )
#error "map_gen_fields.hpp requires MAP_GEN_QUAL, MAP_GEN_PERLIN2D and MAP_GEN_PERLIN3D"
#endif

#include "common/map_gen.hpp"
#include "common/block_definitions.hpp"
#include "common/constants.hpp"

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

MAP_GEN_QUAL float mg_inverse_lerp( const float min, const float max, const float value ) {
    if ( value < min ) {
        return 0.0f;
    }
    if ( value > max ) {
        return 1.0f;
    }
    return ( value - min ) / ( max - min );
}

// ---------------------------------------------------------------------------
// 2D transforms: pure functions of perlin noise (which returns [0, 1]).
// Kept separate from the samplers so MapGen::maxTerrainHeight() can compute
// the exact max of each by evaluating across the [0, 1] noise range.
// ---------------------------------------------------------------------------

// Continentalness: large gentle drift. Linear so mid-range slopes produce
// real elevation change everywhere instead of saturating into flat plateaus.
MAP_GEN_QUAL float mg_level_transform( const float noise ) {
    return ( noise - 0.5f ) * 24.0f;
}

// Rolling terrain: medium-scale undulation between the continental and hill
// wavelengths, so flat regions still read as landscapes, not slabs.
MAP_GEN_QUAL float mg_rolling_transform( const float noise ) {
    return ( noise - 0.5f ) * 12.0f;
}

MAP_GEN_QUAL float mg_hills_transform( const float noise ) {
    return ( noise - 0.5f ) * 15;
}

MAP_GEN_QUAL float mg_ground_transform( const float noise ) {
    return ( noise - 0.5f ) * 1.6f;
}

// Mountain mask: flat [0,1] gate so mountains form distinct ranges instead of
// appearing everywhere. Ramps up across noise in [0.55, 0.80].
MAP_GEN_QUAL float mg_mountain_mask( float noise ) {
    float m = ( noise - 0.55f ) / 0.25f;
    m = m < 0 ? 0 : m;
    m = m > 1 ? 1 : m;
    return m;
}

// Ridged noise: peaks where the raw noise crosses 0.5, cubed for sharp crests.
MAP_GEN_QUAL float mg_mountain_ridge( const float noise ) {
    const float r = 1.0f - fabsf( 2.0f * noise - 1.0f );
    return r * r * r;
}

// ---------------------------------------------------------------------------
// 2D field samplers (raw noise in [0, 1]). Seed map:
//   +0 hills   +1 ground    +2 level   +3 mountain mask   +4/+5 domain warp
//   +6 ridge   +7 biome     +8 weird   +9 roughness       +10 underwater
//   +11 bedrock jitter
// 3D fields use MAP_SEED + 20..27.
// ---------------------------------------------------------------------------

MAP_GEN_QUAL float mg_base_height( const int x, const int z ) {
    const float level = mg_level_transform( MAP_GEN_PERLIN2D( x, z, 0.004f, 2, MAP_SEED + 2 ) );
    const float rolling = mg_rolling_transform( MAP_GEN_PERLIN2D( x, z, 0.009f, 2, MAP_SEED + 12 ) );
    const float hills = mg_hills_transform( MAP_GEN_PERLIN2D( x, z, 0.02f, 3, MAP_SEED + 0 ) );
    const float ground = mg_ground_transform( MAP_GEN_PERLIN2D( x, z, 0.06f, 2, MAP_SEED + 1 ) );

    // Domain warp bends the ridged noise into curved ranges.
    const float wx = x + ( MAP_GEN_PERLIN2D( x, z, 0.004f, 2, MAP_SEED + 4 ) - 0.5f ) * 50.0f;
    const float wz = z + ( MAP_GEN_PERLIN2D( x, z, 0.004f, 2, MAP_SEED + 5 ) - 0.5f ) * 50.0f;
    const float mask = mg_mountain_mask( MAP_GEN_PERLIN2D( x, z, 0.003f, 2, MAP_SEED + 3 ) );
    const float ridge = mg_mountain_ridge( MAP_GEN_PERLIN2D( wx, wz, 0.006f, 4, MAP_SEED + 6 ) );
    const float mountains = mask * ridge * MAX_MOUNTAIN_HEIGHT;

    return level + rolling + hills + ground + mountains;
}

// Overhang gain: plains get a small bump factor, rough regions and mountain
// ranges get large 3D relief (cliffs, shelves, overhangs). Kept low on flat
// land so ordinary ground stays walkably smooth.
MAP_GEN_QUAL float mg_rough_gain( const int x, const int z ) {
    const float rough = MAP_GEN_PERLIN2D( x, z, 0.01f, 2, MAP_SEED + 9 );
    const float mask = mg_mountain_mask( MAP_GEN_PERLIN2D( x, z, 0.003f, 2, MAP_SEED + 3 ) );
    return 0.5f + rough * 6.0f + mask * 30.0f;
}

MAP_GEN_QUAL float mg_biome( const int x, const int z ) {
    return MAP_GEN_PERLIN2D( x, z, 0.0035f, 2, MAP_SEED + 7 );
}

MAP_GEN_QUAL float mg_weird( const int x, const int z ) {
    return MAP_GEN_PERLIN2D( x, z, 0.005f, 2, MAP_SEED + 8 );
}

MAP_GEN_QUAL float mg_underwater( const int x, const int z ) {
    return MAP_GEN_PERLIN2D( x, z, 0.2f, 2, MAP_SEED + 10 );
}

MAP_GEN_QUAL int mg_bedrock_top( const int x, const int z ) {
    const float n = MAP_GEN_PERLIN2D( x, z, 0.3f, 1, MAP_SEED + 11 );
    return BEDROCK_LEVEL + (int)( n * 4.0f );
}

MAP_GEN_QUAL MapGenColumn mg_column_info( const int x, const int z ) {
    MapGenColumn col;
    col.height = mg_base_height( x, z );
    const float hx = fabsf( mg_base_height( x + 1, z ) - col.height );
    const float hz = fabsf( mg_base_height( x, z + 1 ) - col.height );
    col.steepness = hx > hz ? hx : hz;
    col.biome = mg_biome( x, z );
    col.weird = mg_weird( x, z );
    col.rough_gain = mg_rough_gain( x, z );
    return col;
}

// ---------------------------------------------------------------------------
// 3D fields. Seed map: +20 overhang, +21/+22 main cave channel, +23/+24
// branch channel, +25 cheese caverns, +26 cave zone mask, +27 stone
// pockets, +28 coal, +29 iron, +30 gold.
// ---------------------------------------------------------------------------

// Whether a cell is solid terrain (before cave carving). Below the heightmap
// is solid except where near-surface noise notches inward; above the
// heightmap, gated 3D noise can add ledges/overhangs up to OVERHANG_MAX_RISE.
MAP_GEN_QUAL bool mg_solid_at( const int x, const int y, const int z, const MapGenColumn &col ) {
    const float dy = (float)y - col.height;
    if ( dy <= -35.0f ) {
        return true;
    }
    if ( dy >= OVERHANG_MAX_RISE ) {
        return false;
    }
    float n;
    if ( dy < 0.0f ) {
        // Below the heightmap: smooth noise notches the surface inward.
        // Low frequency + few octaves keep ordinary ground walkable.
        n = MAP_GEN_PERLIN3D( x, y, z, 0.09f, 2, MAP_SEED + 20 ) - 0.5f;
    } else {
        // Above the heightmap: ridged noise forms sheet-like walls whose
        // undersides read as cliffs, ledges and overhangs.
        const float raw = MAP_GEN_PERLIN3D( x, y, z, 0.06f, 3, MAP_SEED + 20 );
        n = 0.30f - fabsf( 2.0f * raw - 1.0f );
    }
    const float gate = 1.0f - fabsf( dy ) / 35.0f;
    return ( col.height - (float)y ) + col.rough_gain * n * gate > 0.0f;
}

// Physical distance (in blocks) to the centerline of a noise channel pair —
// the pair distance in noise units divided by the pair's combined gradient.
// Normalizing by the gradient is what makes a real tube: with a fixed noise-
// space radius, physical width would vary ~300x (the noise gradient ranges
// 0.0001..0.03 per block), producing pinches that sample into disconnected
// specks and balloons where the field runs flat. Here, carving
// `dist < width` always yields ~`width` blocks of real air around a
// continuous centerline. The gradient is forward-differenced; y is squashed
// so centerlines wander mostly horizontally.
MAP_GEN_QUAL float mg_channel_dist( const float x, const float y, const float z,
                                    const float freq, const int seed1, const int seed2 ) {
    const float C = 0.53f; // noise mean — it is not zero-centered
    // Stretching y (factor > 1) amplifies the vertical gradient components,
    // so the centerline tangent (∇a×∇b) tilts toward horizontal — walkable
    // diagonal tunnels instead of vertical shafts. It also flattens the
    // cross-section into wide galleries rather than tall cracks.
    const float ys = y * 1.75f;
    const float a0 = MAP_GEN_PERLIN3D( x, ys, z, freq, 2, seed1 ) - C;
    const float b0 = MAP_GEN_PERLIN3D( x, ys, z, freq, 2, seed2 ) - C;
    const float ax = MAP_GEN_PERLIN3D( x + 1.0f, ys, z, freq, 2, seed1 ) - C - a0;
    const float ay = MAP_GEN_PERLIN3D( x, ys + 1.75f, z, freq, 2, seed1 ) - C - a0;
    const float az = MAP_GEN_PERLIN3D( x, ys, z + 1.0f, freq, 2, seed1 ) - C - a0;
    const float bx = MAP_GEN_PERLIN3D( x + 1.0f, ys, z, freq, 2, seed2 ) - C - b0;
    const float by = MAP_GEN_PERLIN3D( x, ys + 1.75f, z, freq, 2, seed2 ) - C - b0;
    const float bz = MAP_GEN_PERLIN3D( x, ys, z + 1.0f, freq, 2, seed2 ) - C - b0;
    const float dist = sqrtf( a0 * a0 + b0 * b0 );
    const float g = sqrtf( ax * ax + ay * ay + az * az + bx * bx + by * by + bz * bz );
    // Tiny floor avoids div-by-zero; where both fields are flat there is no
    // tube anyway and the large ratio correctly fails the width test.
    return dist / ( g + 0.002f );
}

// Cave carving: two gradient-normalized channel pairs (wide winding mains +
// thinner branches) gated into bounded 3D zones, plus modest cheese rooms
// near the lava layer. The surface gate shrinks tunnels near the heightmap
// so most caves stay underground but some open to daylight.
MAP_GEN_QUAL bool mg_cave_carve( const int x, const int y, const int z, const float height ) {
    const float depth = height - (float)y;
    if ( depth < -20.0f ) {
        return false;
    }
    const float gate = 0.2f + 0.8f * mg_inverse_lerp( -6.0f, 14.0f, depth );
    // Cave zone mask: caves live in bounded regions with real rock between
    // systems — tunnels thin to nothing at a zone's edge.
    const float zone = MAP_GEN_PERLIN3D( x, y * 0.6f, z, 0.016f, 2, MAP_SEED + 26 );
    const float zoneGate = mg_inverse_lerp( 0.54f, 0.67f, zone );
    if ( zoneGate <= 0.0f ) {
        return false;
    }
    const float depthR = mg_inverse_lerp( 0.0f, 60.0f, depth );
    const float wGate = gate * zoneGate;
    // Main tunnels: low-frequency, ~3-6 blocks wide — the walkable trunks.
    const float wMain = ( 1.6f + 1.4f * depthR ) * wGate;
    if ( mg_channel_dist( x, y, z, 0.022f, MAP_SEED + 21, MAP_SEED + 22 ) < wMain ) {
        return true;
    }
    // Branch tunnels: thinner, faster-winding connectors feeding the mains.
    const float wBranch = ( 0.9f + 0.6f * depthR ) * wGate;
    if ( mg_channel_dist( x, y, z, 0.05f, MAP_SEED + 23, MAP_SEED + 24 ) < wBranch ) {
        return true;
    }
    // Small cheese rooms only just above the lava level so the deep layer
    // has occasional chambers without the whole underground going bulbous.
    if ( y < LAVA_LEVEL + 25 ) {
        const float n5 = MAP_GEN_PERLIN3D( x, y, z, 0.03f, 2, MAP_SEED + 25 );
        if ( n5 > 0.80f ) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Block selection
// ---------------------------------------------------------------------------

// Top / near-top block of a column. `exposed` means the cell directly above is
// non-solid (this is the visible surface); otherwise this is the underlayer.
MAP_GEN_QUAL BlockID mg_surface_block( const int x, const int y, const int z, const MapGenColumn &col, const bool exposed ) {
    const float dy = (float)y - col.height;
    if ( col.height < WATER_LEVEL - 1.5f ) {
        // Ocean/lake floor.
        return mg_underwater( x, z ) > 0.5f ? GRAVEL : SAND;
    }
    if ( col.height < WATER_LEVEL + 2.0f ) {
        return SAND; // Beach band around sea level.
    }
    if ( dy > 16.0f || col.steepness > 7.0f ) {
        return STONE; // High shelf tops and cliff faces stay rocky.
    }
    if ( col.height > SNOW_LINE ) {
        return exposed ? SNOW : STONE;
    }
    if ( col.height > MOUNTAIN_ROCK_LINE ) {
        return STONE;
    }
    if ( col.biome > 0.72f && col.height < 28.0f && col.weird < 0.55f ) {
        return ORANGE_SAND; // Desert: red sand several blocks deep.
    }
    if ( col.weird > 0.88f ) {
        return exposed ? MYCELIUM : DIRT; // Rare mushroom fields.
    }
    if ( col.weird > 0.75f ) {
        return exposed ? PODZEL : DIRT; // Dark forest floor.
    }
    if ( col.biome < 0.22f ) {
        return exposed ? SNOWY_GRASS : DIRT; // Snowy plains.
    }
    return exposed ? GRASS : DIRT;
}

// Deep underground: stone with dirt/gravel pockets and depth-banded ore veins.
MAP_GEN_QUAL BlockID mg_deep_block( const int x, const int y, const int z, const MapGenColumn &col ) {
    const float depth = col.height - (float)y;
    if ( depth > 4.0f ) {
        float coal = MAP_GEN_PERLIN3D( x, y, z, 0.16f, 3, MAP_SEED + 28 );
        coal = coal * coal;
        if ( coal < 0.06f ) {
            return COAL_ORE;
        }
    }
    if ( depth > 10.0f && y < 15 ) {
        float iron = MAP_GEN_PERLIN3D( x, y, z, 0.16f, 3, MAP_SEED + 29 );
        iron = iron * iron;
        if ( iron < 0.045f ) {
            return IRON_ORE;
        }
    }
    if ( y < -45 ) {
        float gold = MAP_GEN_PERLIN3D( x, y, z, 0.16f, 3, MAP_SEED + 30 );
        gold = gold * gold;
        if ( gold < 0.04f ) {
            return GOLD_ORE;
        }
    }
    const float pv = MAP_GEN_PERLIN3D( x, y, z, 0.09f, 2, MAP_SEED + 27 );
    if ( pv > 0.66f ) {
        return GRAVEL;
    }
    if ( pv < 0.33f ) {
        return DIRT;
    }
    return STONE;
}

// Full per-cell terrain pick: bedrock floor, solid vs air/water, surface vs
// deep selection, then cave carving (lava below LAVA_LEVEL).
MAP_GEN_QUAL BlockID mg_pick_block( const int x, const int y, const int z, const MapGenColumn &col ) {
    if ( y <= mg_bedrock_top( x, z ) ) {
        return BEDROCK;
    }
    if ( !mg_solid_at( x, y, z, col ) ) {
        return y < WATER_LEVEL ? WATER : AIR;
    }
    const float dy = (float)y - col.height;
    BlockID block;
    if ( !mg_solid_at( x, y + 1, z, col ) ) {
        // Exposed top of the solid column.
        block = dy > -SURFACE_BAND * 2.0f ? mg_surface_block( x, y, z, col, true ) : STONE;
    } else if ( dy > -SURFACE_BAND * 2.0f - SURFACE_BAND && !mg_solid_at( x, y + (int)SURFACE_BAND, z, col ) ) {
        // A few blocks under the visible surface: biome underlayer.
        block = mg_surface_block( x, y, z, col, false );
    } else {
        block = mg_deep_block( x, y, z, col );
    }
    if ( mg_cave_carve( x, y, z, col.height ) ) {
        return y <= LAVA_LEVEL ? LAVA : AIR;
    }
    return block;
}
