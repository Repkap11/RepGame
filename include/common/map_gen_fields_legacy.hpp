#pragma once

// Legacy terrain-generation fields: the pre-overhaul generator as it existed
// at commit 101a348, written in the C++/WGSL dual dialect
// (include/common/wgsl_dialect.hpp) so one file feeds every backend —
// host C++, CUDA, HIP, and the WebGPU compute shader emitted by running this
// file through cpp with -DREPGAME_WGSL_GEN. Selected when
// REPGAME_MAP_GEN_LEGACY is 1 (see constants.hpp).
//
// Interface contract shared with map_gen_fields.hpp so the backend loops and
// MapGen methods can dispatch without knowing which generator is live:
//   mgl_base_height( x, z )                    -> terrainHeight (float)
//   mgl_pick_block( x, y, z, terrainHeight )   -> BlockID
//   mg_biome / mg_weird                        -> [0,1] fields for structures
// plus the *_transform helpers MapGen::maxTerrainHeight() sweeps.

#include "common/wgsl_dialect.hpp"
#include "common/chunk_dimensions.hpp"
#include "common/map_gen_consts.hpp"
#if !defined( REPGAME_WGSL_GEN )
#include "common/map_gen.hpp"
#include "common/block_definitions.hpp"
#endif

// Legacy-only world constants (the old map_gen.hpp values).
#define LEGACY_MOUNTAIN_CAP_HEIGHT 20
#define LEGACY_DIRT_SURFACE_THICKNESS 3
#define LEGACY_CAVE_THRESHOLD 0.3f

// Params named lo/hi (not min/max): WGSL reserves no shadowing and min/max
// are builtin names — a parameter collision would break the generated shader.
MG_FN3( float, mgl_inverse_lerp, float, lo, float, hi, float, value ) {
    if ( value < lo ) {
        return 0.0f;
    }
    if ( value > hi ) {
        return 1.0f;
    }
    return ( value - lo ) / ( hi - lo );
}

// Transforms: pure functions of perlin noise (which returns [0, 1]).
// Separated from the samplers so maxTerrainHeight() can compute the exact
// max of each by evaluating across the [0, 1] noise range.
MG_FN1( float, mgl_hills_transform, float, noise ) {
    return ( noise - 0.5f ) * 15;
}
MG_FN1( float, mgl_ground_transform, float, noise ) {
    return ( noise - 0.5f ) * 2;
}
MG_FN1( float, mgl_mountains_transform, float, noise ) {
    MG_VAR( float, n, noise - 0.5f )
    if ( n < 0 ) {
        n = 0;
    }
    return n * n * n * 1000;
}
MG_FN1( float, mgl_level_transform, float, noise ) {
    // Local named `ns`, not `noise2` — WGSL forbids shadowing module-scope
    // names and noise2 is a shared perlin function.
    MG_VAR( float, ns, ( noise - 0.5f ) * 10 )
    MG_VAR( float, n, MG_ABS( ns ) )
    n = n * ns;
    n = MG_MIN( n, 1 );
    n = MG_MAX( n, -1 );
    return n * 10;
}

// Legacy terrainHeight: level + mountains + hills + ground.
MG_FN2( float, mgl_base_height, int, x, int, z ) {
    MG_LET( float, ground, mgl_ground_transform( MAP_GEN_PERLIN2D( x, z, 0.1f, 2, MAP_SEED + 1 ) ) )
    MG_LET( float, hills, mgl_hills_transform( MAP_GEN_PERLIN2D( x, z, 0.02f, 3, MAP_SEED + 0 ) ) )
    MG_LET( float, mountains, mgl_mountains_transform( MAP_GEN_PERLIN2D( x, z, 0.008f, 3, MAP_SEED + 2 ) ) )
    MG_LET( float, level, mgl_level_transform( MAP_GEN_PERLIN2D( x, z, 0.004f, 2, MAP_SEED + 5 ) ) )
    return level + mountains + hills + ground;
}

// Structure gen picks tree species off these; legacy terrain has no biome
// fields, so sample the same frequencies the new gen uses — trees still vary.
MG_FN2( float, mg_biome, int, x, int, z ) {
    return MAP_GEN_PERLIN2D( x, z, 0.0035f, 2, MAP_SEED + 7 );
}
MG_FN2( float, mg_weird, int, x, int, z ) {
    return MAP_GEN_PERLIN2D( x, z, 0.005f, 2, MAP_SEED + 8 );
}

// The old per-block decision (formerly map_logic.hpp at 101a348), as a
// function so it can be shared by the kernel loops and MapGen::gen_block_id.
MG_FN4( BlockID, mgl_pick_block, int, x, int, y, int, z, float, terrainHeight ) {
    MG_VAR( BlockID, finalBlockId, AIR )
    if ( MG_F32( y ) < terrainHeight ) {
        if ( MG_F32( y + LEGACY_DIRT_SURFACE_THICKNESS ) < terrainHeight ) { // Deep underground
            MG_VAR( float, should_be_iron_ore, MAP_GEN_PERLIN3D( x, y, z, 0.2f, 4, MAP_SEED + 7 ) )
            should_be_iron_ore = should_be_iron_ore * should_be_iron_ore;
            if ( should_be_iron_ore < 0.05f ) {
                finalBlockId = IRON_ORE;
            } else {
                MG_VAR( float, should_be_coal_ore, MAP_GEN_PERLIN3D( x, y, z, 0.2f, 4, MAP_SEED + 8 ) )
                should_be_coal_ore = should_be_coal_ore * should_be_coal_ore;
                if ( should_be_coal_ore < 0.05f ) {
                    finalBlockId = COAL_ORE;
                } else {
                    MG_VAR( float, should_be_gold_ore, MAP_GEN_PERLIN3D( x, y, z, 0.2f, 4, MAP_SEED + 9 ) )
                    should_be_gold_ore = should_be_gold_ore * should_be_gold_ore;
                    finalBlockId = MG_TERN( should_be_gold_ore < 0.05f, GOLD_ORE, STONE );
                }
            }
        } else { // Near the surface
            finalBlockId = DIRT;
            if ( -2.3f + WATER_LEVEL < terrainHeight && terrainHeight < 0.3f + WATER_LEVEL ) {
                finalBlockId = SAND;
            } else if ( terrainHeight < WATER_LEVEL + 0.3f ) {
                MG_LET( float, under_water, MAP_GEN_PERLIN2D( x, z, 0.2f, 2, MAP_SEED + 4 ) )
                finalBlockId = MG_TERN( under_water > 0.5f, GRAVEL, SAND );
            } else if ( terrainHeight > LEGACY_MOUNTAIN_CAP_HEIGHT ) { // Tall enough to be a mountian
                MG_LET( float, diff, terrainHeight - LEGACY_MOUNTAIN_CAP_HEIGHT )
                MG_VAR( float, depth_of_dirt, LEGACY_DIRT_SURFACE_THICKNESS - ( diff / 30.0f ) )
                if ( depth_of_dirt < 1 ) {
                    depth_of_dirt = 1;
                }
                if ( MG_F32( y ) + depth_of_dirt < terrainHeight ) { // Not the top block of a mountian
                    finalBlockId = STONE;
                } else { // Surface blocks of mountian
                    MG_LET( float, mountian_block, MAP_GEN_PERLIN2D( x, z, 0.4f, 2, MAP_SEED + 3 ) )
                    if ( mountian_block * ( terrainHeight - LEGACY_MOUNTAIN_CAP_HEIGHT ) > 20 ) {
                        finalBlockId = MG_TERN( MG_F32( y + 1 ) < terrainHeight, DIRT, SNOW ); // Only snow on very top
                    } else if ( depth_of_dirt == 1 && mountian_block * ( terrainHeight - LEGACY_MOUNTAIN_CAP_HEIGHT ) > 10 ) {
                        finalBlockId = DIRT;
                    } else {
                        finalBlockId = MG_TERN( MG_F32( y + 1 ) < terrainHeight, DIRT, GRASS ); // Only grass on very top
                    }
                }
            } else if ( MG_F32( y + 1 ) >= terrainHeight ) {
                finalBlockId = GRASS;
            }
        } // End near the surface
        if ( y > -100 ) {
            // See if we need a cave
            MG_LET( float, cave_dencity, MAP_GEN_PERLIN3D( x, MG_F32( y ) * 2.5f, z, 0.03f, 3, MAP_SEED + 6 ) )
            MG_LET( float, cave_lerp, 1.0f - mgl_inverse_lerp( -20.0f, 40.0f, MG_F32( y ) - terrainHeight ) )
            if ( cave_dencity < cave_lerp * LEGACY_CAVE_THRESHOLD ) {
                finalBlockId = AIR;
            }
        } else {
            // Super low is bedrock, and no caves.
            finalBlockId = BEDROCK;
        }
    } else {
        // There should not be a block here, but water is still possible at low height
        if ( y < WATER_LEVEL ) {
            finalBlockId = WATER;
        }
    }
    return finalBlockId;
}
