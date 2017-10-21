#include "map_gen.h"
#include "RepGame.h"
#include "block_definitions.h"
#include "perlin_noise.h"
#include <math.h>

#include <stdlib.h>

void map_gen_load_block( Chunk *chunk ) {
    int chunk_offset_x = chunk->chunk_x * CHUNK_SIZE;
    int chunk_offset_y = chunk->chunk_y * CHUNK_SIZE;
    int chunk_offset_z = chunk->chunk_z * CHUNK_SIZE;
    if ( chunk->blocks == NULL ) {
        chunk->blocks = calloc( CHUNK_BLOCK_SIZE, sizeof( Block ) );
    }
    for ( int index = 0; index < CHUNK_BLOCK_SIZE; index++ ) {
        int x, y, z;
        int terrainShouldHaveBlock = 0; // Default to no block
        int terrainBiomeType = 0;       // Some default biome type
        int finalBlockId = 0;           // base block type is grass I guess
        chunk_get_coords_from_index( index, &x, &y, &z );
        terrainShouldHaveBlock = perlin_noise_terrain( chunk_offset_x + x, chunk_offset_y + y, chunk_offset_z + z );
        if ( terrainShouldHaveBlock == 1 ) {
            terrainBiomeType = perlin_noise_biomes( chunk_offset_x + x, chunk_offset_y + y, chunk_offset_z + z );
            finalBlockId = GRASS;
            if ( terrainBiomeType > 4 ) {
                finalBlockId = GRASS;
            }

            if ( terrainBiomeType < 4 && terrainBiomeType > 0 ) {
                finalBlockId = SAND;
            }

            if ( terrainBiomeType > 7.5 ) {
                finalBlockId = STONE;
            }
        }

        if ( terrainShouldHaveBlock == 0 ) {
            // There should not be a block here, but water is still possible at low height
            if ( chunk_offset_y + y < 30 ) {
                finalBlockId = WATER;
            }
        }

        chunk->blocks[ index ].blockDef = block_definition_get_definition( finalBlockId );
    }
}

void map_gen_free_block( Block *blocks ) {
    free( blocks );
}
