#pragma once

// Shared nether terrain generator, included by every map_gen backend right
// after map_gen_fields.hpp (same dialect contract: MG_QUAL,
// MAP_GEN_PERLIN2D/3D, wgsl_dialect.hpp). map_logic.hpp dispatches to
// mgn_pick_block when the encoded block y lands in the nether band.
// The map_gen backends define MG_QUAL + MAP_GEN_PERLIN2D/3D first, exactly
// as map_gen_fields.hpp requires. A standalone host include (clangd, tests,
// ad-hoc tools) instead falls back to the CPU dialect map_gen.cpp uses, so
// this header stays self-contained and mgn_pick_block is callable from host
// code. A backend context missing the perlin macros is still an error.
#if defined( MG_QUAL ) || defined( REPGAME_WGSL_GEN )
#if !defined( MAP_GEN_PERLIN2D ) || !defined( MAP_GEN_PERLIN3D )
#error "map_gen_nether.hpp requires MAP_GEN_PERLIN2D and MAP_GEN_PERLIN3D"
#endif
#else
#include "common/perlin_noise.hpp"
#define MG_QUAL static inline
#define MAP_GEN_PERLIN2D( x, z, f, d, s ) perlin_noise2d( x, z, f, d, s )
#define MAP_GEN_PERLIN3D( x, y, z, f, d, s ) perlin_noise3d( x, y, z, f, d, s )
#endif
//
// Layout (local y = encoded y - DIM_BY_MIN_N, [0, DIM_BLOCK_SPAN)):
//   ly   0..~5    jittered bedrock floor
//   ly  ~5..floor_h   netherrack floor mass (cave worms + ore pockets)
//   ly  floor_h..roof_base  open cavern; air at/below MGN_LAVA_SEA is lava
//   ly  roof_base..+MGN_ROOF_THICK  netherrack roof mass (glowstone clusters
//                         near its base, exposed where carving opens them)
//   ly  > roof_base+MGN_ROOF_THICK+jitter  solid bedrock — the remaining
//                         ~3.7k blocks of band are dead filler, not caves

#include "common/wgsl_dialect.hpp"
#include "common/map_gen_consts.hpp"
#include "common/dimension.hpp"
#if !defined( REPGAME_WGSL_GEN )
#include "common/map_gen.hpp"
#include "common/block_definitions.hpp"
#endif

// Band layout constants (MGN_LAVA_SEA/MGN_FLOOR_MID/MGN_ROOF_MID) live in
// map_gen_consts.hpp so plain host code can see them too.

// 2D floor height: rolling wastes with ridges, local-y ~[15, 95].
MG_FN2( float, mgn_floor_height, int, x, int, z ) {
    return MGN_FLOOR_MID + ( MAP_GEN_PERLIN2D( x, z, 0.01f, 3, MAP_SEED + 42 ) - 0.5f ) * 60.0f +
           ( MAP_GEN_PERLIN2D( x, z, 0.05f, 2, MAP_SEED + 43 ) - 0.5f ) * 20.0f;
}

// Underside of the roof mass, local-y ~[170, 210].
MG_FN2( float, mgn_roof_base, int, x, int, z ) {
    return MGN_ROOF_MID + ( MAP_GEN_PERLIN2D( x, z, 0.015f, 3, MAP_SEED + 44 ) - 0.5f ) * 40.0f;
}

MG_FN3( BlockID, mgn_pick_block, int, x, int, y, int, z ) {
    MG_LET( int, ly, y - DIM_BY_MIN_N )

    // Jittered bedrock floor and ceiling cap the band.
    MG_LET( float, bedrock_floor, MAP_GEN_PERLIN2D( x, z, 0.6f, 1, MAP_SEED + 40 ) )
    if ( MG_F32( ly ) <= 1.0f + bedrock_floor * 4.0f ) {
        return BEDROCK;
    }
    MG_LET( float, floor_h, mgn_floor_height( x, z ) )
    MG_LET( float, roof_base, mgn_roof_base( x, z ) )

    // Jittered bedrock ceiling a bounded thickness above the roof base. The
    // band is ~4000 tall but the cavern only needs ~240 — everything past
    // this cap is unreachable solid bedrock filler up to the band top (and
    // this early-out skips the carving/ore noise for all of it).
    MG_LET( float, bedrock_roof, MAP_GEN_PERLIN2D( x, z, 0.6f, 1, MAP_SEED + 41 ) )
    if ( MG_F32( ly ) >= roof_base + MGN_ROOF_THICK + bedrock_roof * 4.0f ) {
        return BEDROCK;
    }

    MG_VAR( int, solid, 0 )
    if ( MG_F32( ly ) <= floor_h || MG_F32( ly ) >= roof_base ) {
        solid = 1;
    }
    // Cave worms chew through the floor and roof masses. Keep a thin rock
    // skin over the bedrock so caverns never open onto the void band below.
    MG_LET( float, cave, MAP_GEN_PERLIN3D( x, y, z, 0.045f, 3, MAP_SEED + 45 ) )
    if ( solid != 0 && cave < 0.30f && ly > 8 ) {
        solid = 0;
    }
    // Large low-frequency hollows open up the cavern volume.
    MG_LET( float, hollow, MAP_GEN_PERLIN3D( x, y, z, 0.008f, 2, MAP_SEED + 46 ) )
    if ( solid != 0 && hollow < 0.42f && MG_F32( ly ) > 20.0f && MG_F32( ly ) < roof_base + 60.0f ) {
        solid = 0;
    }

    if ( solid == 0 ) {
        return MG_TERN( ly <= MGN_LAVA_SEA, LAVA, AIR );
    }

    MG_VAR( BlockID, id, NETHERRACK )

    // Biome patches: basalt deltas are basalt through the whole column; soul
    // sand valleys swap the surface band to soul sand over soul soil.
    MG_LET( float, biome, MAP_GEN_PERLIN2D( x, z, 0.004f, 2, MAP_SEED + 47 ) )
    if ( biome >= 0.80f ) {
        id = BASALT;
    } else if ( biome >= 0.60f && biome < 0.70f ) {
        id = MG_TERN( MG_F32( ly ) > floor_h - 3.0f, SOUL_SAND, SOUL_SOIL );
    }
    // Fungal forests live on the surface crust only.
    if ( MG_F32( ly ) > floor_h - 2.5f && MG_F32( ly ) <= floor_h ) {
        MG_LET( float, fungi, MAP_GEN_PERLIN2D( x, z, 0.03f, 2, MAP_SEED + 48 ) )
        if ( fungi > 0.70f && fungi <= 0.78f ) {
            id = CRIMSON_NYLIUM;
        } else if ( fungi > 0.78f ) {
            id = WARPED_NYLIUM;
        }
    }
    // Blackstone seams in the deep floor rock.
    if ( MG_F32( ly ) < floor_h - 30.0f && biome > 0.35f && biome < 0.45f ) {
        id = BLACKSTONE;
    }

    // Ore pockets (squared noise keeps them clustered and sparse).
    MG_LET( float, quartz, MAP_GEN_PERLIN3D( x, y, z, 0.25f, 4, MAP_SEED + 49 ) )
    if ( quartz * quartz < 0.03f ) {
        id = QUARTZ_ORE;
    }
    MG_LET( float, gold, MAP_GEN_PERLIN3D( x, y, z, 0.25f, 4, MAP_SEED + 50 ) )
    if ( gold * gold < 0.025f ) {
        id = NETHER_GOLD_ORE;
    }
    if ( ly < 60 ) {
        MG_LET( float, debris, MAP_GEN_PERLIN3D( x, y, z, 0.30f, 4, MAP_SEED + 51 ) )
        if ( debris * debris < 0.003f ) {
            id = ANCIENT_DEBRIS;
        }
    }

    // Glowstone clusters embedded around the roof base — carving exposes them
    // as lit ceiling blobs above the cavern.
    if ( MG_F32( ly ) > roof_base - 10.0f && MG_F32( ly ) < roof_base + 30.0f ) {
        MG_LET( float, glow, MAP_GEN_PERLIN3D( x, y, z, 0.35f, 4, MAP_SEED + 52 ) )
        if ( glow * glow < 0.02f ) {
            id = GLOWSTONE;
        }
    }
    // Magma pockets near the lava sea line.
    if ( MG_F32( ly ) > 18.0f && MG_F32( ly ) < floor_h ) {
        MG_LET( float, mag, MAP_GEN_PERLIN3D( x, y, z, 0.30f, 4, MAP_SEED + 53 ) )
        if ( mag * mag < 0.015f ) {
            id = MAGMA;
        }
    }
    return id;
}
