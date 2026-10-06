#pragma once

// Legacy terrain-generation fields: the pre-overhaul generator as it existed
// at commit 101a348, ported onto the same MAP_GEN_QUAL / MAP_GEN_PERLIN2D /
// MAP_GEN_PERLIN3D macro pattern so one file covers all three backends.
// Selected when REPGAME_MAP_GEN_LEGACY is 1 (see constants.hpp).
//
// Interface contract shared with map_gen_fields.hpp so the backend loops and
// MapGen methods can dispatch without knowing which generator is live:
//   mgl_base_height( x, z )                    -> terrainHeight (float)
//   mgl_pick_block( x, y, z, terrainHeight )   -> BlockID
//   mg_biome / mg_weird                        -> [0,1] fields for structures
// plus the *_transform helpers MapGen::maxTerrainHeight() sweeps.

#include "common/map_gen.hpp"
#include "common/block_definitions.hpp"
#include "common/constants.hpp"

// Legacy-only world constants (the old map_gen.hpp values).
#define LEGACY_MOUNTAIN_CAP_HEIGHT 20
#define LEGACY_DIRT_SURFACE_THICKNESS 3
#define LEGACY_CAVE_THRESHOLD 0.3f

MAP_GEN_QUAL float mgl_inverse_lerp( const float min, const float max, const float value ) {
    if ( value < min ) {
        return 0.0f;
    }
    if ( value > max ) {
        return 1.0f;
    }
    return ( value - min ) / ( max - min );
}

// Transforms: pure functions of perlin noise (which returns [0, 1]).
// Separated from the samplers so maxTerrainHeight() can compute the exact
// max of each by evaluating across the [0, 1] noise range.
MAP_GEN_QUAL float mgl_hills_transform( const float noise ) {
    return ( noise - 0.5f ) * 15;
}
MAP_GEN_QUAL float mgl_ground_transform( const float noise ) {
    return ( noise - 0.5f ) * 2;
}
MAP_GEN_QUAL float mgl_mountains_transform( float noise ) {
    noise = noise - 0.5f;
    if ( noise < 0 ) {
        noise = 0;
    }
    return noise * noise * noise * 1000;
}
MAP_GEN_QUAL float mgl_level_transform( float noise ) {
    noise = ( noise - 0.5f ) * 10;
    float n = fabs( noise );
    n = n * noise;
    n = n > 1 ? 1 : n;
    n = n < -1 ? -1 : n;
    return n * 10;
}

// Legacy terrainHeight: level + mountains + hills + ground.
MAP_GEN_QUAL float mgl_base_height( const int x, const int z ) {
    const float ground = mgl_ground_transform( MAP_GEN_PERLIN2D( x, z, 0.1f, 2, MAP_SEED + 1 ) );
    const float hills = mgl_hills_transform( MAP_GEN_PERLIN2D( x, z, 0.02f, 3, MAP_SEED + 0 ) );
    const float mountains = mgl_mountains_transform( MAP_GEN_PERLIN2D( x, z, 0.008f, 3, MAP_SEED + 2 ) );
    const float level = mgl_level_transform( MAP_GEN_PERLIN2D( x, z, 0.004f, 2, MAP_SEED + 5 ) );
    return level + mountains + hills + ground;
}

// Structure gen picks tree species off these; legacy terrain has no biome
// fields, so sample the same frequencies the new gen uses — trees still vary.
MAP_GEN_QUAL float mg_biome( const int x, const int z ) {
    return MAP_GEN_PERLIN2D( x, z, 0.0035f, 2, MAP_SEED + 7 );
}
MAP_GEN_QUAL float mg_weird( const int x, const int z ) {
    return MAP_GEN_PERLIN2D( x, z, 0.005f, 2, MAP_SEED + 8 );
}

// The old per-block decision (formerly map_logic.hpp at 101a348), as a
// function so it can be shared by the kernel loops and MapGen::gen_block_id.
MAP_GEN_QUAL BlockID mgl_pick_block( const int x, const int y, const int z, const float terrainHeight ) {
    BlockID finalBlockId = AIR;
    if ( y < terrainHeight ) {
        if ( y + LEGACY_DIRT_SURFACE_THICKNESS < terrainHeight ) { // Deep underground
            float should_be_iron_ore = MAP_GEN_PERLIN3D( x, y, z, 0.2f, 4, MAP_SEED + 7 );
            should_be_iron_ore = should_be_iron_ore * should_be_iron_ore;
            if ( should_be_iron_ore < 0.05f ) {
                finalBlockId = IRON_ORE;
            } else {
                float should_be_coal_ore = MAP_GEN_PERLIN3D( x, y, z, 0.2f, 4, MAP_SEED + 8 );
                should_be_coal_ore = should_be_coal_ore * should_be_coal_ore;
                if ( should_be_coal_ore < 0.05f ) {
                    finalBlockId = COAL_ORE;
                } else {
                    float should_be_gold_ore = MAP_GEN_PERLIN3D( x, y, z, 0.2f, 4, MAP_SEED + 9 );
                    should_be_gold_ore = should_be_gold_ore * should_be_gold_ore;
                    finalBlockId = should_be_gold_ore < 0.05f ? GOLD_ORE : STONE;
                }
            }
        } else { // Near the surface
            finalBlockId = DIRT;
            if ( -2.3f + WATER_LEVEL < terrainHeight && terrainHeight < 0.3f + WATER_LEVEL ) {
                finalBlockId = SAND;
            } else if ( terrainHeight < WATER_LEVEL + 0.3f ) {
                float under_water = MAP_GEN_PERLIN2D( x, z, 0.2f, 2, MAP_SEED + 4 );
                finalBlockId = under_water > 0.5f ? GRAVEL : SAND;
            } else if ( terrainHeight > LEGACY_MOUNTAIN_CAP_HEIGHT ) { // Tall enough to be a mountian
                float diff = terrainHeight - LEGACY_MOUNTAIN_CAP_HEIGHT;
                float depth_of_dirt = LEGACY_DIRT_SURFACE_THICKNESS - ( diff / 30.0f );
                if ( depth_of_dirt < 1 ) {
                    depth_of_dirt = 1;
                }
                if ( y + depth_of_dirt < terrainHeight ) { // Not the top block of a mountian
                    finalBlockId = STONE;
                } else { // Surface blocks of mountian
                    float mountian_block = MAP_GEN_PERLIN2D( x, z, 0.4f, 2, MAP_SEED + 3 );
                    if ( mountian_block * ( terrainHeight - LEGACY_MOUNTAIN_CAP_HEIGHT ) > 20 ) {
                        finalBlockId = y + 1 < terrainHeight ? DIRT : SNOW; // Only snow on very top
                    } else if ( depth_of_dirt == 1 && mountian_block * ( terrainHeight - LEGACY_MOUNTAIN_CAP_HEIGHT ) > 10 ) {
                        finalBlockId = DIRT;
                    } else {
                        finalBlockId = y + 1 < terrainHeight ? DIRT : GRASS; // Only grass on very top
                    }
                }
            } else if ( y + 1 >= terrainHeight ) {
                finalBlockId = GRASS;
            }
        } // End near the surface
        if ( y > -100 ) {
            // See if we need a cave
            float cave_dencity = MAP_GEN_PERLIN3D( x, y * 2.5f, z, 0.03f, 3, MAP_SEED + 6 );
            float cave_lerp = 1.0f - mgl_inverse_lerp( -20.0f, 40.0f, y - terrainHeight );
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
