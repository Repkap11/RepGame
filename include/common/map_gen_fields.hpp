#pragma once

// Shared terrain-generation fields, included by every map_gen backend.
// Written in the C++/WGSL dual dialect (include/common/wgsl_dialect.hpp) so
// one file feeds host C++, CUDA, HIP, and the WebGPU compute shader emitted
// by preprocessing this file with -DREPGAME_WGSL_GEN.
// The includer must first define:
//   MG_QUAL         - function qualifier: "static inline" on CPU,
//                     "__device__" on CUDA/HIP (unused under REPGAME_WGSL_GEN)
//   MAP_GEN_PERLIN2D(x, z, freq, depth, seed) - perlin sampler in [0, 1]
//   MAP_GEN_PERLIN3D(x, y, z, freq, depth, seed)
// Because every backend includes this same file, all generated worlds are
// identical by construction (modulo float rounding).
#if !defined( MG_QUAL ) && !defined( REPGAME_WGSL_GEN )
#error "map_gen_fields.hpp requires MG_QUAL (or REPGAME_WGSL_GEN), MAP_GEN_PERLIN2D and MAP_GEN_PERLIN3D"
#endif
#if !defined( MAP_GEN_PERLIN2D ) || !defined( MAP_GEN_PERLIN3D )
#error "map_gen_fields.hpp requires MAP_GEN_PERLIN2D and MAP_GEN_PERLIN3D"
#endif

#include "common/wgsl_dialect.hpp"
#include "common/chunk_dimensions.hpp"
#include "common/map_gen_consts.hpp"
#if !defined( REPGAME_WGSL_GEN )
#include "common/map_gen.hpp"
#include "common/block_definitions.hpp"
#endif

#if REPGAME_MAP_GEN_LEGACY
// The pre-overhaul generator (fields + block picker live in the legacy file).
#include "common/map_gen_fields_legacy.hpp"
#else

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

MG_FN3( float, mg_inverse_lerp, float, lo, float, hi, float, value ) {
    if ( value < lo ) {
        return 0.0f;
    }
    if ( value > hi ) {
        return 1.0f;
    }
    return ( value - lo ) / ( hi - lo );
}

// ---------------------------------------------------------------------------
// 2D transforms: pure functions of perlin noise (which returns [0, 1]).
// Kept separate from the samplers so MapGen::maxTerrainHeight() can compute
// the exact max of each by evaluating across the [0, 1] noise range.
// ---------------------------------------------------------------------------

// Continentalness: very large gentle drift — oceans and highlands span many
// hundreds of blocks. Linear so mid-range slopes produce real elevation
// change everywhere instead of saturating into flat plateaus.
MG_FN1( float, mg_level_transform, float, noise ) {
    return ( noise - 0.5f ) * 30.0f;
}

// Rolling terrain: medium-scale undulation between the continental and hill
// wavelengths, so flat regions still read as landscapes, not slabs.
MG_FN1( float, mg_rolling_transform, float, noise ) {
    return ( noise - 0.5f ) * 17.0f;
}

MG_FN1( float, mg_hills_transform, float, noise ) {
    return ( noise - 0.5f ) * 20;
}

MG_FN1( float, mg_ground_transform, float, noise ) {
    return ( noise - 0.5f ) * 1.6f;
}

// Mountain mask: flat [0,1] gate so mountains form distinct ranges instead of
// appearing everywhere. Ramps up across noise in [0.52, 0.80]; the sampler
// runs at continent scale so ranges stretch for hundreds of blocks.
MG_FN1( float, mg_mountain_mask, float, noise ) {
    MG_VAR( float, m, ( noise - 0.52f ) / 0.28f )
    m = MG_MAX( m, 0 );
    m = MG_MIN( m, 1 );
    return m;
}

// Ridged noise: peaks where the raw noise crosses 0.5, cubed for sharp crests.
MG_FN1( float, mg_mountain_ridge, float, noise ) {
    MG_LET( float, r, 1.0f - MG_ABS( 2.0f * noise - 1.0f ) )
    return r * r * r;
}

// ---------------------------------------------------------------------------
// Biome fields: large domain-warped blobs (wavelength ~800 blocks) so biomes
// are continents, not patches. Warping the sample position makes borders
// jagged instead of smooth perlin ellipses.
// ---------------------------------------------------------------------------

MG_FN2( float, mg_biome, int, x, int, z ) {
    MG_LET( float, bx, MG_F32( x ) + ( MAP_GEN_PERLIN2D( x, z, 0.0022f, 2, MAP_SEED + 13 ) - 0.5f ) * 320.0f )
    MG_LET( float, bz, MG_F32( z ) + ( MAP_GEN_PERLIN2D( x, z, 0.0022f, 2, MAP_SEED + 14 ) - 0.5f ) * 320.0f )
    return MAP_GEN_PERLIN2D( bx, bz, 0.0011f, 3, MAP_SEED + 7 );
}

MG_FN2( float, mg_weird, int, x, int, z ) {
    MG_LET( float, wx, MG_F32( x ) + ( MAP_GEN_PERLIN2D( x, z, 0.0026f, 2, MAP_SEED + 15 ) - 0.5f ) * 260.0f )
    MG_LET( float, wz, MG_F32( z ) + ( MAP_GEN_PERLIN2D( x, z, 0.0026f, 2, MAP_SEED + 16 ) - 0.5f ) * 260.0f )
    return MAP_GEN_PERLIN2D( wx, wz, 0.0015f, 3, MAP_SEED + 8 );
}

// Hot+dry desertness [0,1]: shared by the mesa terracing in mg_base_height
// and the sandy surface pick, so mesa walls stay desert-colored.
MG_FN2( float, mg_desert, int, x, int, z ) {
    MG_LET( float, hot, mg_inverse_lerp( 0.68f, 0.80f, mg_biome( x, z ) ) )
    MG_LET( float, dry, 1.0f - mg_inverse_lerp( 0.52f, 0.64f, mg_weird( x, z ) ) )
    return hot * dry;
}

// Pale-garden field [0,1]: rare spooky woods inside the dark-forest/mushroom
// latitudes. mg_is_pale is the gate shared by the surface pick and structure
// gen — much rarer than the cherry-grove band.
MG_FN2( float, mg_pale, int, x, int, z ) {
    return MAP_GEN_PERLIN2D( x, z, 0.0013f, 3, MAP_SEED + 43 );
}
MG_FN3( bool, mg_is_pale, int, x, int, z, float, weird ) {
    return weird > 0.82f && mg_pale( x, z ) > 0.72f;
}

// Mangrove-swamp gate shared by the surface pick and structure gen: warm,
// wet-ish lowlands right at sea level — mangroves take over coasts and lake
// edges in the right biomes.
MG_FN3( bool, mg_is_swamp, float, biome, float, weird, float, height ) {
    return biome > 0.45f && biome < 0.68f && weird > 0.30f && weird < 0.55f &&
        height > WATER_LEVEL - 6.5f && height < WATER_LEVEL + 3.0f;
}

// Floating-island strength [0,1]: rare continent-scale archipelagos. The 3D
// island field in mg_solid_at is only sampled where this is > 0.
MG_FN2( float, mg_island_strength, int, x, int, z ) {
    return mg_inverse_lerp( 0.72f, 0.93f, MAP_GEN_PERLIN2D( x, z, 0.0016f, 2, MAP_SEED + 17 ) );
}

// Island band center above the heightmap, jittered so islands in one
// archipelago stack at different altitudes and bend slightly along their
// length. Keep max(dy0 + half_thickness) under OVERHANG_MAX_RISE:
// 28 + 4 jitter + 27 max half = 59 < 60.
MG_FN2( float, mg_island_dy0, int, x, int, z ) {
    return 28.0f + ( MAP_GEN_PERLIN2D( x, z, 0.006f, 2, MAP_SEED + 18 ) - 0.5f ) * 8.0f;
}

// Island footprint: a warped 2D blob field at boulder scale (~40-block
// features) so islands come out as individual floating rocks, not wide
// sheets. The value past the rim threshold acts as "inland-ness" — island
// thickness scales with it, so each rock is a thick domed core tapering to a
// thin rim. Returns a thickness factor in [0, 1]; 0 = outside any island.
MG_FN2( float, mg_island_footprint, int, x, int, z ) {
    MG_LET( float, fx, MG_F32( x ) + ( MAP_GEN_PERLIN2D( x, z, 0.03f, 1, MAP_SEED + 41 ) - 0.5f ) * 35.0f )
    MG_LET( float, fz, MG_F32( z ) + ( MAP_GEN_PERLIN2D( x, z, 0.03f, 1, MAP_SEED + 42 ) - 0.5f ) * 35.0f )
    return mg_inverse_lerp( 0.54f, 0.66f, MAP_GEN_PERLIN2D( fx, fz, 0.022f, 3, MAP_SEED + 40 ) );
}

// ---------------------------------------------------------------------------
// 2D field samplers (raw noise in [0, 1]). Seed map:
//   +0 hills   +1 ground    +2 level   +3 mountain mask   +4/+5 domain warp
//   +6 ridge   +7 biome     +8 weird   +9 roughness       +10 underwater
//   +11 bedrock jitter   +12 rolling   +13/+14 biome warp  +15/+16 weird warp
//   +17 island mask      +18 island band   +34 canyon band   +35 canyon patch
//   +36 geode zone mask  +40 island footprint  +41/+42 footprint warp
//   +43 pale garden      +49 strata wobble     +50 strata palette
// 3D fields use MAP_SEED + 20..33, +37..+39, +44 copper, +45 lapis,
//   +46 redstone, +47 diamond, +48 emerald.
// ---------------------------------------------------------------------------

MG_FN2( float, mg_base_height, int, x, int, z ) {
    MG_LET( float, level, mg_level_transform( MAP_GEN_PERLIN2D( x, z, 0.0016f, 2, MAP_SEED + 2 ) ) )
    MG_LET( float, rolling, mg_rolling_transform( MAP_GEN_PERLIN2D( x, z, 0.0045f, 2, MAP_SEED + 12 ) ) )
    MG_LET( float, hills, mg_hills_transform( MAP_GEN_PERLIN2D( x, z, 0.011f, 3, MAP_SEED + 0 ) ) )
    MG_LET( float, ground, mg_ground_transform( MAP_GEN_PERLIN2D( x, z, 0.06f, 2, MAP_SEED + 1 ) ) )

    // Domain warp bends the ridged noise into long curved ranges.
    MG_LET( float, wx, MG_F32( x ) + ( MAP_GEN_PERLIN2D( x, z, 0.002f, 2, MAP_SEED + 4 ) - 0.5f ) * 130.0f )
    MG_LET( float, wz, MG_F32( z ) + ( MAP_GEN_PERLIN2D( x, z, 0.002f, 2, MAP_SEED + 5 ) - 0.5f ) * 130.0f )
    MG_LET( float, mask, mg_mountain_mask( MAP_GEN_PERLIN2D( x, z, 0.0011f, 2, MAP_SEED + 3 ) ) )
    MG_LET( float, ridge, mg_mountain_ridge( MAP_GEN_PERLIN2D( wx, wz, 0.0032f, 4, MAP_SEED + 6 ) ) )
    MG_LET( float, mountains, mask * ridge * MAX_MOUNTAIN_HEIGHT )

    MG_VAR( float, h, level + rolling + hills + ground + mountains )

    // Deserts terrace into mesas: the accumulated height itself is
    // quantized into 7-block strata steps — flat treads with ramped cliff
    // risers — so terrace lines follow the terrain's own contours instead
    // of the desert field's iso-rings. The plateau lift is a whole number
    // of steps (keeps mg_mesa_riser exact), `ground` stays unquantized so
    // treads keep some texture, and the desertness blend tapers the
    // terraces out at the biome edge. Max lift ~28 < MESA_MAX_RISE.
    MG_LET( float, desert, mg_desert( x, z ) )
    if ( desert > 0.0f ) {
        MG_LET( float, hf, ( h - ground + desert * 21.0f ) / 7.0f )
        MG_LET( float, t, MG_FLOOR( hf ) )
        MG_LET( float, f, hf - t )
        MG_LET( float, qh, t * 7.0f + mg_inverse_lerp( 0.55f, 0.95f, f ) * 7.0f + ground )
        h = qh * desert + h * ( 1.0f - desert );
    }

    // Slot canyons: rare long gorges winding through the land. A narrow band
    // around the midline of a very-low-frequency field traces the slot; a
    // patch mask keeps canyons regional instead of a continuous web. Cutting
    // stops a few blocks under sea level, so deep stretches flood into
    // canyon rivers instead of trenching to bedrock.
    if ( h > WATER_LEVEL + 3.0f ) {
        MG_LET( float, cn, MAP_GEN_PERLIN2D( x, z, 0.0013f, 2, MAP_SEED + 34 ) )
        MG_LET( float, band, 1.0f - mg_inverse_lerp( 0.012f, 0.04f, MG_ABS( cn - 0.5f ) ) )
        if ( band > 0.0f ) {
            MG_LET( float, cpatch, mg_inverse_lerp( 0.72f, 0.84f, MAP_GEN_PERLIN2D( x, z, 0.0045f, 1, MAP_SEED + 35 ) ) )
            MG_LET( float, cut, band * band * cpatch * CANYON_MAX_DEPTH )
            MG_LET( float, max_cut, h - ( WATER_LEVEL - 6.0f ) )
            h -= MG_MIN( cut, max_cut );
        }
    }

    return h;
}

// Position within a mesa stratum step: 0 on the flat treads, ramping to 1
// across the cliff risers. Derived from the quantized height — the plateau
// lift is a whole number of steps, so at full desert the fractional part of
// (height - ground)/7 is exactly the riser ramp; at partial desert it
// degrades into a band position on the half-formed steps, which still reads
// as eroded badlands. mg_surface_block exposes this as bare strata.
MG_FN4( float, mg_mesa_riser, int, x, int, z, float, height, float, desert ) {
    if ( desert <= 0.0f ) {
        return 0.0f;
    }
    MG_LET( float, ground, mg_ground_transform( MAP_GEN_PERLIN2D( x, z, 0.06f, 2, MAP_SEED + 1 ) ) )
    MG_LET( float, f, ( height - ground ) / 7.0f )
    return f - MG_FLOOR( f );
}

// Overhang gain: plains get a small bump factor, rough regions and mountain
// ranges get large 3D relief (cliffs, shelves, overhangs, arches). Kept low
// on flat land so ordinary ground stays walkably smooth.
MG_FN2( float, mg_rough_gain, int, x, int, z ) {
    MG_LET( float, rough, MAP_GEN_PERLIN2D( x, z, 0.01f, 2, MAP_SEED + 9 ) )
    MG_LET( float, mask, mg_mountain_mask( MAP_GEN_PERLIN2D( x, z, 0.0011f, 2, MAP_SEED + 3 ) ) )
    return 0.5f + rough * 8.0f + mask * 55.0f;
}

MG_FN2( float, mg_underwater, int, x, int, z ) {
    return MAP_GEN_PERLIN2D( x, z, 0.2f, 2, MAP_SEED + 10 );
}

MG_FN2( int, mg_bedrock_top, int, x, int, z ) {
    MG_LET( float, n, MAP_GEN_PERLIN2D( x, z, 0.3f, 1, MAP_SEED + 11 ) )
    return BEDROCK_LEVEL + MG_I32( n * 4.0f );
}

MG_FN2( MapGenColumn, mg_column_info, int, x, int, z ) {
    MG_DECL( MapGenColumn, col )
    col.height = mg_base_height( x, z );
    MG_LET( float, hx, MG_ABS( mg_base_height( x + 1, z ) - col.height ) )
    MG_LET( float, hz, MG_ABS( mg_base_height( x, z + 1 ) - col.height ) )
    col.steepness = MG_MAX( hx, hz );
    col.biome = mg_biome( x, z );
    col.weird = mg_weird( x, z );
    col.rough_gain = mg_rough_gain( x, z );
    col.island = mg_island_strength( x, z );
    col.island_dy0 = MG_TERN( col.island > 0.0f, mg_island_dy0( x, z ), 0.0f );
    col.island_tin = MG_TERN( col.island > 0.0f, mg_island_footprint( x, z ), 0.0f );
    col.desert = mg_desert( x, z );
    col.mesa_riser = mg_mesa_riser( x, z, col.height, col.desert );
    return col;
}

// ---------------------------------------------------------------------------
// 3D fields. Seed map: +20 overhang, +21/+22 main cave channel, +23/+24
// branch channel, +25 cheese caverns, +26 cave zone mask, +27 stone
// pockets, +28 coal, +29 iron, +30 gold, +31 island blobs, +32 lava veins,
// +33 mega caverns.
// ---------------------------------------------------------------------------

// Whether a cell is solid terrain (before cave carving). Below the heightmap
// is solid except where near-surface noise notches inward; above the
// heightmap, gated 3D noise can add ledges/overhangs and, where the island
// mask is strong, free-floating landmasses — all bounded by
// OVERHANG_MAX_RISE.
MG_FN4( bool, mg_solid_at, int, x, int, y, int, z, MapGenColumn, col ) {
    MG_LET( float, dy, MG_F32( y ) - col.height )
    if ( dy <= -35.0f ) {
        return true;
    }
    if ( dy >= OVERHANG_MAX_RISE ) {
        return false;
    }
    // Floating islands: domed landmasses whose half-thickness scales with
    // "inland-ness" of a warped 2D footprint field — deep cores with real
    // volume thinning to nothing at the rims. The inner half of the profile
    // is unconditionally solid; the outer half is a ragged 3D-noise shell,
    // so undersides come out lumpy and silhouettes stay broken.
    if ( dy > 0.0f && col.island > 0.0f ) {
        MG_LET( float, tin, col.island_tin )
        if ( tin > 0.0f ) {
            MG_VAR( float, half_thickness, ( 3.0f + col.island * 24.0f ) * tin )
            if ( half_thickness > col.island_dy0 - 4.0f ) {
                half_thickness = col.island_dy0 - 4.0f; // keep a crawlspace under every island
            }
            MG_LET( float, ab, MG_ABS( dy - col.island_dy0 ) )
            if ( ab < half_thickness * 0.5f ) {
                return true; // solid core — guaranteed volume
            }
            if ( ab < half_thickness ) {
                MG_LET( float, is3, MAP_GEN_PERLIN3D( x, MG_F32( y ) * 0.55f, z, 0.045f, 2, MAP_SEED + 31 ) - 0.5f )
                if ( is3 > ( ab / half_thickness - 0.5f ) * 0.9f ) {
                    return true;
                }
            }
        }
    }
    MG_LET( float, gate, 1.0f - MG_ABS( dy ) / MG_TERN( dy < 0.0f, 35.0f, OVERHANG_MAX_RISE ) )
    // Exact bounds on the noise terms below: n>=-0.5 under the heightmap and
    // n<=0.45 above it, so deep-enough cells are always solid and high-enough
    // cells never are — most air/deep cells skip the 3D noise eval entirely.
    if ( dy < 0.0f ) {
        if ( dy < -0.5f * col.rough_gain * gate ) {
            return true;
        }
    } else if ( dy >= 0.45f * col.rough_gain * gate ) {
        return false;
    }
    MG_DECL( float, n )
    if ( dy < 0.0f ) {
        // Below the heightmap: smooth noise notches the surface inward.
        // Low frequency + few octaves keep ordinary ground walkable.
        n = MAP_GEN_PERLIN3D( x, y, z, 0.09f, 2, MAP_SEED + 20 ) - 0.5f;
    } else {
        // Above the heightmap: ridged noise forms sheet-like walls whose
        // undersides read as cliffs, ledges and overhangs.
        MG_LET( float, raw, MAP_GEN_PERLIN3D( x, y, z, 0.06f, 3, MAP_SEED + 20 ) )
        n = 0.45f - MG_ABS( 2.0f * raw - 1.0f );
    }
    return ( col.height - MG_F32( y ) ) + col.rough_gain * n * gate > 0.0f;
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
MG_FN7( float, mg_channel_dist, float, x, float, y, float, z, float, freq, float, w, int, seed1, int, seed2 ) {
    MG_LET( float, C, 0.53f ) // noise mean — it is not zero-centered
    // Stretching y (factor > 1) amplifies the vertical gradient components,
    // so the centerline tangent (∇a×∇b) tilts toward horizontal — walkable
    // diagonal tunnels instead of vertical shafts. It also flattens the
    // cross-section into wide galleries rather than tall cracks.
    MG_LET( float, ys, y * 1.75f )
    MG_LET( float, a0, MAP_GEN_PERLIN3D( x, ys, z, freq, 2, seed1 ) - C )
    MG_LET( float, b0, MAP_GEN_PERLIN3D( x, ys, z, freq, 2, seed2 ) - C )
    MG_LET( float, len, MG_SQRT( a0 * a0 + b0 * b0 ) )
    // Exact early-out: |∂a/∂component| <= 3*freq for these depth-2 fields (the
    // smooth-interp derivative peaks at 1.5/octave) and the y sample spans
    // 1.75x, so g <= 3*freq*sqrt(4+2*1.75^2) ~= 9.55*freq. Inside the tube
    // needs len < w*g <= w*gmax — when len already exceeds that, the cell is
    // outside for certain and the six gradient samples can be skipped. The
    // returned lower bound still fails the caller's width test identically.
    MG_LET( float, gmax, 9.55f * freq + 0.002f )
    if ( len > w * gmax ) {
        return len / gmax;
    }
    MG_LET( float, ax, MAP_GEN_PERLIN3D( x + 1.0f, ys, z, freq, 2, seed1 ) - C - a0 )
    MG_LET( float, ay, MAP_GEN_PERLIN3D( x, ys + 1.75f, z, freq, 2, seed1 ) - C - a0 )
    MG_LET( float, az, MAP_GEN_PERLIN3D( x, ys, z + 1.0f, freq, 2, seed1 ) - C - a0 )
    MG_LET( float, bx, MAP_GEN_PERLIN3D( x + 1.0f, ys, z, freq, 2, seed2 ) - C - b0 )
    MG_LET( float, by, MAP_GEN_PERLIN3D( x, ys + 1.75f, z, freq, 2, seed2 ) - C - b0 )
    MG_LET( float, bz, MAP_GEN_PERLIN3D( x, ys, z + 1.0f, freq, 2, seed2 ) - C - b0 )
    MG_LET( float, g, MG_SQRT( ax * ax + ay * ay + az * az + bx * bx + by * by + bz * bz ) )
    // Tiny floor avoids div-by-zero; where both fields are flat there is no
    // tube anyway and the large ratio correctly fails the width test.
    return len / ( g + 0.002f );
}

// Cave carving: two gradient-normalized channel pairs (wide winding mains +
// thinner branches) gated into bounded 3D zones, plus modest cheese rooms
// near the lava layer. The surface gate shrinks tunnels near the heightmap
// so most caves stay underground but some open to daylight.
MG_FN4( bool, mg_cave_carve, int, x, int, y, int, z, float, height ) {
    MG_LET( float, depth, height - MG_F32( y ) )
    if ( depth < -20.0f ) {
        return false;
    }
    MG_LET( float, gate, 0.2f + 0.8f * mg_inverse_lerp( -6.0f, 14.0f, depth ) )
    // Cave zone mask: caves live in bounded regions with real rock between
    // systems — tunnels thin to nothing at a zone's edge.
    MG_LET( float, zone, MAP_GEN_PERLIN3D( x, MG_F32( y ) * 0.6f, z, 0.016f, 2, MAP_SEED + 26 ) )
    MG_LET( float, zoneGate, mg_inverse_lerp( 0.54f, 0.67f, zone ) )
    if ( zoneGate <= 0.0f ) {
        return false;
    }
    MG_LET( float, depthR, mg_inverse_lerp( 0.0f, 60.0f, depth ) )
    MG_LET( float, wGate, gate * zoneGate )
    // Main tunnels: low-frequency, ~3-6 blocks wide — the walkable trunks.
    MG_LET( float, wMain, ( 1.6f + 1.4f * depthR ) * wGate )
    if ( mg_channel_dist( MG_F32( x ), MG_F32( y ), MG_F32( z ), 0.022f, wMain, MAP_SEED + 21, MAP_SEED + 22 ) < wMain ) {
        return true;
    }
    // Branch tunnels: thinner, faster-winding connectors feeding the mains.
    MG_LET( float, wBranch, ( 0.9f + 0.6f * depthR ) * wGate )
    if ( mg_channel_dist( MG_F32( x ), MG_F32( y ), MG_F32( z ), 0.05f, wBranch, MAP_SEED + 23, MAP_SEED + 24 ) < wBranch ) {
        return true;
    }
    // Small cheese rooms only just above the lava level so the deep layer
    // has occasional chambers without the whole underground going bulbous.
    if ( y < LAVA_LEVEL + 25 ) {
        MG_LET( float, n5, MAP_GEN_PERLIN3D( x, y, z, 0.03f, 2, MAP_SEED + 25 ) )
        if ( n5 > 0.80f ) {
            return true;
        }
    }
    // Rare mega-caverns: very-low-frequency blobs at mid-depth, thriving
    // inside cave zones and tapering shut at their edges and near the
    // surface, so they read as found caverns rather than void bubbles.
    if ( depth > 12.0f && y > LAVA_LEVEL + 12 ) {
        MG_LET( float, n6, MAP_GEN_PERLIN3D( x, y, z, 0.0075f, 3, MAP_SEED + 33 ) )
        if ( n6 > 0.92f - 0.25f * zoneGate * wGate ) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Block selection
// ---------------------------------------------------------------------------

// Banded terracotta strata for desert badlands: ~4-block horizontal bands
// with a slow lateral wobble, colored by a per-band pick so mesa cliffs read
// as sedimentary strata instead of bare stone.
MG_FN3( BlockID, mg_strata, int, x, int, y, int, z ) {
    MG_LET( float, wob, ( MAP_GEN_PERLIN2D( x, z, 0.045f, 1, MAP_SEED + 49 ) - 0.5f ) * 9.0f )
    MG_LET( int, band, MG_I32( MG_FLOOR( ( MG_F32( y ) + wob ) / 4.0f ) ) )
    MG_LET( float, pick, MAP_GEN_PERLIN2D( band * 31 + 7, band * 17 + 3, 0.11f, 1, MAP_SEED + 50 ) )
    if ( pick < 0.15f ) {
        return WHITE_TERRACOTTA;
    }
    if ( pick < 0.30f ) {
        return YELLOW_TERRACOTTA;
    }
    if ( pick < 0.50f ) {
        return ORANGE_TERRACOTTA;
    }
    if ( pick < 0.72f ) {
        return RED_TERRACOTTA;
    }
    if ( pick < 0.86f ) {
        return BROWN_TERRACOTTA;
    }
    if ( pick < 0.95f ) {
        return LIGHT_GRAY_TERRACOTTA;
    }
    return GRAY_TERRACOTTA;
}

// Top / near-top block of a column. `exposed` means the cell directly above is
// non-solid (this is the visible surface); otherwise this is the underlayer.
MG_FN5( BlockID, mg_surface_block, int, x, int, y, int, z, MapGenColumn, col, bool, exposed ) {
    MG_LET( float, dy, MG_F32( y ) - col.height )
    // Floating-island tops grow meadow (or snow in cold biomes) no matter
    // what the terrain far below them is; the same rule greens the tops of
    // natural overhang sheets inside island archipelagos.
    if ( col.island > 0.0f && dy > 10.0f ) {
        if ( !exposed ) {
            return DIRT;
        }
        return MG_TERN( col.biome < 0.22f, SNOW, GRASS );
    }
    if ( col.height < WATER_LEVEL - 1.5f ) {
        // Ocean/lake floor — muddy inside swamp gates.
        if ( mg_is_swamp( col.biome, col.weird, col.height ) ) {
            return MUD;
        }
        return MG_TERN( mg_underwater( x, z ) > 0.5f, GRAVEL, SAND );
    }
    if ( col.height < WATER_LEVEL + 2.0f ) {
        if ( mg_is_swamp( col.biome, col.weird, col.height ) ) {
            return MUD; // Muddy banks where swamps meet the water.
        }
        // Beach band around sea level: sand on top, sandstone underneath.
        return MG_TERN( exposed, SAND, SANDSTONE );
    }
    if ( col.desert > 0.0f ) {
        // Desert badlands: banded terracotta strata — bare on cliff risers
        // and plateau tops, skinned with red sand over red sandstone on the
        // low flat basin floor.
        if ( col.mesa_riser > 0.0f ) {
            return mg_strata( x, y, z );
        }
        if ( col.height < 28.0f ) {
            return MG_TERN( exposed, RED_SAND, RED_SANDSTONE );
        }
        return mg_strata( x, y, z );
    }
    if ( mg_is_pale( x, z, col.weird ) ) {
        // Pale garden: ghostly moss ground inside the dark latitudes.
        return MG_TERN( exposed, PALE_MOSS_BLOCK, DIRT );
    }
    if ( dy > 16.0f || col.steepness > 7.0f ) {
        return STONE; // High shelf tops and cliff faces stay rocky.
    }
    if ( col.steepness > 4.5f ) {
        return MG_TERN( exposed, GRAVEL, STONE ); // Scree aprons under cliffs.
    }
    if ( col.height > SNOW_LINE ) {
        return MG_TERN( exposed, SNOW, STONE );
    }
    if ( col.height > MOUNTAIN_ROCK_LINE ) {
        return STONE;
    }
    if ( col.weird > 0.88f ) {
        return MG_TERN( exposed, MYCELIUM, DIRT ); // Rare mushroom fields.
    }
    if ( col.weird > 0.75f ) {
        return MG_TERN( exposed, PODZOL, DIRT ); // Dark forest floor.
    }
    if ( col.biome < 0.22f ) {
        return MG_TERN( exposed, SNOWY_GRASS, DIRT ); // Snowy plains.
    }
    return MG_TERN( exposed, GRASS, DIRT );
}

// Deep underground: stone with dirt/gravel pockets and depth-banded ore veins.
MG_FN4( BlockID, mg_deep_block, int, x, int, y, int, z, MapGenColumn, col ) {
    MG_LET( float, depth, col.height - MG_F32( y ) )
    if ( depth > 4.0f ) {
        MG_VAR( float, coal, MAP_GEN_PERLIN3D( x, y, z, 0.16f, 2, MAP_SEED + 28 ) )
        coal = coal * coal;
        if ( coal < 0.06f ) {
            return COAL_ORE;
        }
    }
    if ( depth > 10.0f && y < 15 ) {
        MG_VAR( float, iron, MAP_GEN_PERLIN3D( x, y, z, 0.16f, 2, MAP_SEED + 29 ) )
        iron = iron * iron;
        if ( iron < 0.045f ) {
            return IRON_ORE;
        }
    }
    if ( y < -45 ) {
        MG_VAR( float, gold, MAP_GEN_PERLIN3D( x, y, z, 0.16f, 2, MAP_SEED + 30 ) )
        gold = gold * gold;
        if ( gold < 0.04f ) {
            return GOLD_ORE;
        }
    }
    if ( depth > 4.0f && y < 30 ) {
        MG_VAR( float, copper, MAP_GEN_PERLIN3D( x, y, z, 0.16f, 2, MAP_SEED + 44 ) )
        copper = copper * copper;
        if ( copper < 0.05f ) {
            return COPPER_ORE;
        }
    }
    if ( depth > 8.0f && y < -5 ) {
        MG_VAR( float, lapis, MAP_GEN_PERLIN3D( x, y, z, 0.16f, 2, MAP_SEED + 45 ) )
        lapis = lapis * lapis;
        if ( lapis < 0.04f ) {
            return LAPIS_ORE;
        }
    }
    if ( y < -20 ) {
        MG_VAR( float, redstone, MAP_GEN_PERLIN3D( x, y, z, 0.16f, 2, MAP_SEED + 46 ) )
        redstone = redstone * redstone;
        if ( redstone < 0.04f ) {
            return REDSTONE_ORE;
        }
    }
    if ( y < -50 ) {
        MG_VAR( float, diamond, MAP_GEN_PERLIN3D( x, y, z, 0.16f, 2, MAP_SEED + 47 ) )
        diamond = diamond * diamond;
        if ( diamond < 0.035f ) {
            return DIAMOND_ORE;
        }
    }
    if ( depth > 6.0f && col.height > 55.0f ) {
        // Emeralds only under mountains: lower-frequency, sparser veins.
        MG_VAR( float, emerald, MAP_GEN_PERLIN3D( x, y, z, 0.10f, 2, MAP_SEED + 48 ) )
        emerald = emerald * emerald;
        if ( emerald < 0.03f ) {
            return EMERALD_ORE;
        }
    }
    if ( col.desert > 0.0f && depth < 24.0f ) {
        // Badlands crust: banded terracotta continues under the surface band
        // (past the ore checks, so veins still spawn inside the strata).
        return mg_strata( x, y, z );
    }
    MG_LET( float, pv, MAP_GEN_PERLIN3D( x, y, z, 0.09f, 2, MAP_SEED + 27 ) )
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
MG_FN4( BlockID, mg_pick_block, int, x, int, y, int, z, MapGenColumn, col ) {
    // mg_bedrock_top only reaches BEDROCK_LEVEL+4 — skip its noise eval above.
    if ( y <= BEDROCK_LEVEL + 4 && y <= mg_bedrock_top( x, z ) ) {
        return BEDROCK;
    }
    if ( !mg_solid_at( x, y, z, col ) ) {
        return MG_TERN( y < WATER_LEVEL, WATER, AIR );
    }
    // Carve before selecting the block: carving doesn't read the pick, and
    // carved cells skip the exposure/deep-block/ore noise entirely.
    if ( mg_cave_carve( x, y, z, col.height ) ) {
        return MG_TERN( y <= LAVA_LEVEL, LAVA, AIR );
    }
    MG_LET( float, dy, MG_F32( y ) - col.height )
    MG_DECL( BlockID, block )
    if ( !mg_solid_at( x, y + 1, z, col ) ) {
        // Exposed top of the solid column.
        block = MG_TERN( dy > -SURFACE_BAND * 2.0f, mg_surface_block( x, y, z, col, true ), STONE );
    } else if ( dy > -SURFACE_BAND * 2.0f - SURFACE_BAND && !mg_solid_at( x, y + MG_I32( SURFACE_BAND ), z, col ) ) {
        // A few blocks under the visible surface: biome underlayer.
        block = mg_surface_block( x, y, z, col, false );
    } else {
        block = mg_deep_block( x, y, z, col );
    }
    // Magma veins: thin noise seams in near-surface rock. Invisible on flat
    // ground, but on cliff faces (high steepness) they surface as glowing
    // cracks — cliff walls and cave mouths light up at night.
    if ( block == STONE && col.steepness > 5.0f && dy > -14.0f && MAP_GEN_PERLIN3D( x, y, z, 0.07f, 2, MAP_SEED + 32 ) > 0.88f ) {
        block = LAVA;
    }
    // Amethyst geodes: rare deep pockets — a hollow heart wrapped in a
    // basalt/calcite/amethyst shell, with crystal buds sprouting from the
    // heart floor (AMETHYST_CLUSTER emits light, so geodes still glow). A 2D
    // zone mask keeps them scarce and they live well under the surface, so
    // they appear as glowing spheres found while caving (or breaking out of
    // a cave wall) rather than terrain clutter.
    MG_LET( float, depth, col.height - MG_F32( y ) )
    if ( depth > 22.0f && y < 4 && y > BEDROCK_LEVEL + 14 ) {
        if ( MAP_GEN_PERLIN2D( x, z, 0.0035f, 1, MAP_SEED + 36 ) > 0.60f ) {
            MG_LET( float, g, MAP_GEN_PERLIN3D( x, y, z, 0.055f, 2, MAP_SEED + 37 ) )
            if ( g > 0.972f ) {
                // Hollow heart: air, with a crystal bud wherever the cell
                // below is shell material — buds only sit on geode floors,
                // never float over cave-carved gaps.
                MG_LET( float, gb, MAP_GEN_PERLIN3D( x, y - 1, z, 0.055f, 2, MAP_SEED + 37 ) )
                return MG_TERN( gb > 0.90f && gb <= 0.972f, AMETHYST_CLUSTER, AIR );
            }
            if ( g > 0.955f ) {
                // Inner amethyst shell, flecked with budding amethyst.
                return MG_TERN( MAP_GEN_PERLIN3D( x, y, z, 0.65f, 1, MAP_SEED + 38 ) < 0.15f, BUDDING_AMETHYST, AMETHYST_BLOCK );
            }
            if ( g > 0.935f ) {
                return CALCITE;
            }
            if ( g > 0.912f ) {
                return SMOOTH_BASALT;
            }
        }
    }
    return block;
}

#endif // REPGAME_MAP_GEN_LEGACY
