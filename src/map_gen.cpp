#include "map_gen.h"
#include "RepGame.h"
#include "block_definitions.h"
#include "perlin_noise.h"
#include "chunk_loader.h"
#include "chunk.h"
#include <math.h>

#include <stdlib.h>

void map_gen_load_block( Chunk *chunk ) {
    int chunk_offset_x = chunk->chunk_x * CHUNK_SIZE;
    int chunk_offset_y = chunk->chunk_y * CHUNK_SIZE;
    int chunk_offset_z = chunk->chunk_z * CHUNK_SIZE;
    for ( int x = -1; x < CHUNK_SIZE_INTERNAL - 1; x++ ) {
        for ( int z = -1; z < CHUNK_SIZE_INTERNAL - 1; z++ ) {
            int terrainHeight = perlin_noise_terrain( chunk_offset_x + x, chunk_offset_z + z );
            int terrainBiomeType = perlin_noise_biomes( chunk_offset_x + x, chunk_offset_z + z );

            for ( int y = -1; y < CHUNK_SIZE_INTERNAL - 1; y++ ) {

                int index = chunk_get_index_from_coords( x, y, z );

                // for ( int index = 0; index < CHUNK_BLOCK_SIZE; index++ ) {
                // int x, y, z;
                int terrainShouldHaveBlock = ( chunk_offset_y + y ) < terrainHeight; // Default to no block
                BlockID finalBlockId = AIR;                                          // base block type is grass I guess
                                                                                     // chunk_get_coords_from_index( index, &x, &y, &z );

                if ( terrainShouldHaveBlock == 1 ) {
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
    }
}