#include "RepGame.h"
#include "map_gen.h"
#include "block_definitions.h"
#include "perlin_noise.h"
#include "chunk_loader.h"
#include "chunk.h"
#include <math.h>

#include <stdlib.h>
#define MOUNTAN_CAP_HEIGHT 24

void map_gen_load_block( Chunk *chunk ) {
    int chunk_offset_x = chunk->chunk_x * CHUNK_SIZE;
    int chunk_offset_y = chunk->chunk_y * CHUNK_SIZE;
    int chunk_offset_z = chunk->chunk_z * CHUNK_SIZE;
    for ( int x = -1; x < CHUNK_SIZE_INTERNAL - 1; x++ ) {
        for ( int z = -1; z < CHUNK_SIZE_INTERNAL - 1; z++ ) {
            float ground_noise = perlin_noise_ground_noise( chunk_offset_x + x, chunk_offset_z + z );
            float hills = perlin_noise_hills( chunk_offset_x + x, chunk_offset_z + z );
            float mountians = perlin_noise_mountians( chunk_offset_x + x, chunk_offset_z + z );
            mountians = pow( mountians, 5 );
            float terrainHeight = ground_noise + hills + mountians;
            // terrainHeight = biome;

            for ( int y = -1; y < CHUNK_SIZE_INTERNAL - 1; y++ ) {

                int index = chunk_get_index_from_coords( x, y, z );

                // for ( int index = 0; index < CHUNK_BLOCK_SIZE; index++ ) {
                // int x, y, z;
                int terrainShouldHaveBlock = ( chunk_offset_y + y ) < terrainHeight; // Default to no block
                BlockID finalBlockId = AIR;                                          // base block type is grass I guess
                                                                                     // chunk_get_coords_from_index( index, &x, &y, &z );

                if ( terrainShouldHaveBlock == 1 ) {
                    finalBlockId = GRASS;
                    if ( -2.3 < terrainHeight && terrainHeight < 0.3 ) {
                        finalBlockId = SAND;
                    } else if ( terrainHeight < 0.3 ) {
                        float under_water = perlin_noise_under_water_block( chunk_offset_x + x, chunk_offset_z + z );
                        finalBlockId = under_water > 0.5 ? GRAVEL : SAND;
                    } else if ( terrainHeight > MOUNTAN_CAP_HEIGHT ) {
                        float mountian_block = perlin_noise_mountian_block( chunk_offset_x + x, chunk_offset_z + z );
                        if ( mountian_block * ( terrainHeight - MOUNTAN_CAP_HEIGHT ) > 25 ) {
                            finalBlockId = SNOW;
                        } else if ( mountian_block * ( terrainHeight - MOUNTAN_CAP_HEIGHT ) > 10 ) {
                            finalBlockId = STONE;
                        }
                    }
                } else {
                    // There should not be a block here, but water is still possible at low height
                    if ( chunk_offset_y + y < 0 ) {
                        finalBlockId = WATER;
                    }
                }
                chunk->blocks[ index ].blockDef = block_definition_get_definition( finalBlockId );
            }
        }
    }
}