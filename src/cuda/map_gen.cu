#include "RepGame.h"
#include "map_gen.h"
#include "block_definitions.h"

__host__ void map_gen_load_block( Chunk *chunk ) {
    int chunk_offset_x = chunk->chunk_x * CHUNK_SIZE;
    int chunk_offset_y = chunk->chunk_y * CHUNK_SIZE;
    int chunk_offset_z = chunk->chunk_z * CHUNK_SIZE;
    for ( int x = -1; x < CHUNK_SIZE_INTERNAL - 1; x++ ) {
        for ( int z = -1; z < CHUNK_SIZE_INTERNAL - 1; z++ ) {
            for ( int y = -1; y < CHUNK_SIZE_INTERNAL - 1; y++ ) {
                int index = chunk_get_index_from_coords( x, y, z );
                int real_y = chunk_offset_y + y;
                BlockID finalBlockId;
                if (real_y < 0) {
                    finalBlockId = GRASS;
                } else {
                    finalBlockId = AIR;
                }
                chunk->blocks[ index ].blockDef = block_definition_get_definition( finalBlockId );
            }
        }
    }
}