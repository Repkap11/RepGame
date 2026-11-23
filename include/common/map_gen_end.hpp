#pragma once

// Shared End terrain generator, included by every map_gen backend right
// after map_gen_nether.hpp (same dialect contract: MG_QUAL,
// MAP_GEN_PERLIN2D/3D, wgsl_dialect.hpp). map_logic.hpp dispatches to
// mge_pick_block when the encoded block y lands in the end band.
// The map_gen backends define MG_QUAL + MAP_GEN_PERLIN2D/3D first, exactly
// as map_gen_fields.hpp requires. A standalone host include (clangd, tests,
// ad-hoc tools) instead falls back to the CPU dialect map_gen.cpp uses, so
// this header stays self-contained and mge_pick_block is callable from host
// code. A backend context missing the perlin macros is still an error.
#if defined( MG_QUAL ) || defined( REPGAME_WGSL_GEN )
#if !defined( MAP_GEN_PERLIN2D ) || !defined( MAP_GEN_PERLIN3D )
#error "map_gen_end.hpp requires MAP_GEN_PERLIN2D and MAP_GEN_PERLIN3D"
#endif
#else
#include "common/perlin_noise.hpp"
#define MG_QUAL static inline
#define MAP_GEN_PERLIN2D( x, z, f, d, s ) perlin_noise2d( x, z, f, d, s )
#define MAP_GEN_PERLIN3D( x, y, z, f, d, s ) perlin_noise3d( x, y, z, f, d, s )
#endif
//
// Layout (local y = encoded y - DIM_BY_MIN_E, [0, DIM_BLOCK_SPAN)):
//   the band is void — AIR everywhere except:
//   ly  ~1990..~2160, r<~110   central island: end stone dome, flat top at
//                              MGE_ISLAND_Y near x/z 0, bedrock fountain with
//                              an END_PORTAL pool, obsidian arrival pad,
//                              ten obsidian pillars on a r=43 ring
//   r > MGE_GAP_R              outer islands: noise-blob end-stone shelves
//                              drifting around MGE_ISLAND_Y-70..+40, outer
//                              surfaces grow CHORUS_PLANT/CHORUS_FLOWER
//                              stalks in clumps

#include "common/wgsl_dialect.hpp"
#include "common/map_gen_consts.hpp"
#include "common/dimension.hpp"
#if !defined( REPGAME_WGSL_GEN )
#include "common/map_gen.hpp"
#include "common/block_definitions.hpp"
#endif

// Band layout constants (MGE_ISLAND_Y/MGE_GAP_R/MGE_PAD_*) live in
// map_gen_consts.hpp so plain host code can see them too. Noise fields
// here use MAP_SEED + 80..87 — pick a fresh offset above 87 for new ones
// (nether seed maps end at 78, see map_gen_nether.hpp).

// Ten obsidian pillars stand on a r=43 ring around the fountain. Heights
// vary per pillar like vanilla's; roots sink into the island mass.
#define MGE_PILLAR_BASE ( MGE_ISLAND_Y - 8 )
#define MGE_PILLAR_R2 9
#define MGE_PILLAR( px, pz, top )                                        \
    if ( ( x - ( px ) ) * ( x - ( px ) ) + ( z - ( pz ) ) * ( z - ( pz ) ) <= MGE_PILLAR_R2 ) { \
        return top;                                                     \
    }
MG_FN2( int, mge_pillar_top, int, x, int, z ) {
    MGE_PILLAR( 43, 0, MGE_ISLAND_Y + 90 )
    MGE_PILLAR( 35, 25, MGE_ISLAND_Y + 76 )
    MGE_PILLAR( 13, 41, MGE_ISLAND_Y + 100 )
    MGE_PILLAR( -13, 41, MGE_ISLAND_Y + 85 )
    MGE_PILLAR( -35, 25, MGE_ISLAND_Y + 79 )
    MGE_PILLAR( -43, 0, MGE_ISLAND_Y + 103 )
    MGE_PILLAR( -35, -25, MGE_ISLAND_Y + 82 )
    MGE_PILLAR( -13, -41, MGE_ISLAND_Y + 97 )
    MGE_PILLAR( 13, -41, MGE_ISLAND_Y + 88 )
    MGE_PILLAR( 35, -25, MGE_ISLAND_Y + 94 )
    return -1;
}

MG_FN3( BlockID, mge_pick_block, int, x, int, y, int, z ) {
    MG_LET( int, ly, y - DIM_BY_MIN_E )
    // Everything in the End lives in a few hundred blocks around the island
    // layer; the rest of the ~4k-tall band is void.
    if ( ly < 1800 || ly > 2250 ) {
        return AIR;
    }
    MG_LET( int, r2, x * x + z * z )
    MG_LET( int, cheb2, MG_I32( MG_MAX( x * x, z * z ) ) )

    // The exit fountain: a 5x5 bedrock ring at the surface row holds a 3x3
    // END_PORTAL pool; four bedrock posts mark the corners one row up.
    if ( ly == MGE_ISLAND_Y ) {
        if ( cheb2 == 4 ) {
            return BEDROCK;
        }
        if ( cheb2 <= 1 ) {
            return END_PORTAL;
        }
    }
    if ( ly == MGE_ISLAND_Y + 1 && x * x == 4 && z * z == 4 ) {
        return BEDROCK;
    }
    // Obsidian arrival pad beside the fountain — end travellers always land
    // here, next to the way home.
    if ( ly == MGE_PAD_Y && x >= MGE_PAD_X0 && x <= MGE_PAD_X1 && z >= MGE_PAD_Z0 && z <= MGE_PAD_Z1 ) {
        return OBSIDIAN;
    }
    // Obsidian pillars claim their columns before the island mass — roots
    // embedded in the dome read as obsidian where carving exposes them.
    MG_LET( int, ptop, mge_pillar_top( x, z ) )
    if ( ptop >= 0 && ly >= MGE_PILLAR_BASE && ly <= ptop ) {
        return OBSIDIAN;
    }

    // Central island: a fixed end-stone dome ~70-110 blocks wide. The rim
    // wanders on a 2D field; thickness tapers quadratically from ~60 under
    // the middle to ~12 at the rim, so the underside reads as a dome, not a
    // slab. The middle is flattened to MGE_ISLAND_Y so the fountain and pad
    // sit on a level floor.
    MG_LET( float, er, 90.0f + ( MAP_GEN_PERLIN2D( x, z, 0.03f, 2, MAP_SEED + 84 ) - 0.5f ) * 40.0f )
    if ( MG_F32( r2 ) < er * er ) {
        MG_VAR( float, ctop, MG_F32( MGE_ISLAND_Y ) + ( MAP_GEN_PERLIN2D( x, z, 0.06f, 2, MAP_SEED + 85 ) - 0.5f ) * 10.0f )
        if ( r2 < 225 ) {
            ctop = MG_F32( MGE_ISLAND_Y );
        }
        MG_LET( float, d2, MG_F32( r2 ) / ( er * er ) )
        MG_LET( float, cbot, ctop - 12.0f - ( 1.0f - d2 ) * 50.0f )
        if ( MG_F32( ly ) <= ctop && MG_F32( ly ) >= cbot ) {
            return END_STONE;
        }
        return AIR;
    }

    // Outer islands start past the void moat. A low-frequency footprint
    // field thresholded high gives discrete blobs a few hundred blocks
    // across; a second field roughs the surface and the thickness grows
    // toward each blob's centre so edges feather into the void.
    if ( r2 > MGE_GAP_R * MGE_GAP_R ) {
        MG_LET( float, isl, MAP_GEN_PERLIN2D( x, z, 0.0016f, 3, MAP_SEED + 80 ) )
        if ( isl > 0.56f ) {
            MG_LET( float, otop, 1978.0f + ( isl - 0.56f ) * 320.0f +
                            ( MAP_GEN_PERLIN2D( x, z, 0.02f, 2, MAP_SEED + 81 ) - 0.5f ) * 30.0f )
            MG_LET( float, oth, 10.0f + ( isl - 0.56f ) * 50.0f )
            if ( MG_F32( ly ) <= otop && MG_F32( ly ) > otop - oth ) {
                return END_STONE;
            }
            // Chorus stalks grow in clumps on outer island tops: a clump
            // mask picks patches, a faster field sets each column's stalk
            // height so a clump reads as a thicket of varied stems topped
            // by a flower cell.
            if ( MG_F32( ly ) > otop ) {
                MG_LET( float, clump, MAP_GEN_PERLIN2D( x, z, 0.008f, 2, MAP_SEED + 82 ) )
                if ( clump > 0.60f ) {
                    MG_LET( float, ch, MAP_GEN_PERLIN2D( x, z, 0.09f, 2, MAP_SEED + 83 ) )
                    if ( ch > 0.45f ) {
                        MG_LET( int, chh, 3 + MG_I32( ch * 10.0f ) )
                        if ( MG_F32( ly ) <= otop + MG_F32( chh ) ) {
                            return CHORUS_PLANT;
                        }
                        if ( MG_F32( ly ) <= otop + MG_F32( chh ) + 1.0f ) {
                            return CHORUS_FLOWER;
                        }
                    }
                }
            }
        }
    }
    return AIR;
}
