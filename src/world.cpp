#include "world.h"
#include "RepGame.h"
#include "chunk.h"
#include "chunk_loader.h"
#include <math.h>

void world_init( LoadedChunks *loadedChunks ) {
    chunk_loader_init( loadedChunks );
}
void world_render( LoadedChunks *loadedChunks, TRIP_ARGS( float camera_ ) ) {
    chunk_loader_render_chunks( loadedChunks, TRIP_ARGS( camera_ ) );
}
void world_draw( LoadedChunks *loadedChunks , glm::mat4 &mvp) {
    chunk_loader_draw_chunks( loadedChunks, mvp);
}
void world_cleanup( LoadedChunks *loadedChunks ) {
    chunk_loader_cleanup( loadedChunks );
}

void fixup_chunk( LoadedChunks *loadedChunks, Chunk *chunk, TRIP_ARGS( int offset_ ), TRIP_ARGS( int pos_ ), BlockID blockID ) {
    // pr_debug( "                                                               Fixup Offset: %d %d %d", x, y, z );
    Chunk *fixupChunk = chunk_loader_get_chunk( loadedChunks, chunk->chunk_x + offset_x, chunk->chunk_y + offset_y, chunk->chunk_z + offset_z );
    if ( fixupChunk ) {
        //chunk_destroy_display_list( fixupChunk );
        //chunk_set_block( fixupChunk, TRIP_ARGS( pos_ ), blockID );
        //chunk_calculate_sides( fixupChunk );
        fixupChunk->ditry = 1;
        //chunk_create_display_list( fixupChunk );
    }
}

Chunk *world_get_loaded_chunk( LoadedChunks *loadedChunks, TRIP_ARGS( int block_ ) ) {
    int chunk_x = floor( block_x / ( float )CHUNK_SIZE );
    int chunk_y = floor( block_y / ( float )CHUNK_SIZE );
    int chunk_z = floor( block_z / ( float )CHUNK_SIZE );
    return chunk_loader_get_chunk( loadedChunks, TRIP_ARGS( chunk_ ) );
}

Block *world_get_block( Chunk *chunk, TRIP_ARGS( int block_ ) ) {
    int diff_x = block_x - chunk->chunk_x * CHUNK_SIZE;
    int diff_y = block_y - chunk->chunk_y * CHUNK_SIZE;
    int diff_z = block_z - chunk->chunk_z * CHUNK_SIZE;
    return chunk_get_block( chunk, TRIP_ARGS( diff_ ) );
}

Block *world_get_loaded_block( LoadedChunks *loadedChunks, TRIP_ARGS( int block_ ) ) {
    Chunk *chunk = world_get_loaded_chunk( loadedChunks, TRIP_ARGS( block_ ) );
    Block *block = world_get_block( chunk, TRIP_ARGS( block_ ) );
    return block;
}

void world_set_block( LoadedChunks *loadedChunks, TRIP_ARGS( int block_ ), BlockID blockID ) {
    int chunk_x = floor( block_x / ( float )CHUNK_SIZE );
    int chunk_y = floor( block_y / ( float )CHUNK_SIZE );
    int chunk_z = floor( block_z / ( float )CHUNK_SIZE );

    Chunk *chunk = chunk_loader_get_chunk( loadedChunks, TRIP_ARGS( chunk_ ) );
    if ( chunk ) {
        //chunk_destroy_display_list( chunk );
        int diff_x = block_x - chunk_x * CHUNK_SIZE;
        int diff_y = block_y - chunk_y * CHUNK_SIZE;
        int diff_z = block_z - chunk_z * CHUNK_SIZE;
        // pr_debug( "Orig Offset: %d %d %d", diff_x, diff_y, diff_z );

        for ( int i = -1; i < 2; i++ ) {
            for ( int j = -1; j < 2; j++ ) {
                for ( int k = -1; k < 2; k++ ) {

                    int needs_update_x = ( ( i != 1 && diff_x == 0 ) || ( i != -1 && diff_x == ( CHUNK_SIZE - 1 ) ) ) || i == 0; //
                    int needs_update_y = ( ( j != 1 && diff_y == 0 ) || ( j != -1 && diff_y == ( CHUNK_SIZE - 1 ) ) ) || j == 0; //
                    int needs_update_z = ( ( k != 1 && diff_z == 0 ) || ( k != -1 && diff_z == ( CHUNK_SIZE - 1 ) ) ) || k == 0;

                    int new_i = i * needs_update_x;
                    int new_j = j * needs_update_y;
                    int new_k = k * needs_update_z;

                    int needs_update = TRIP_AND( needs_update_ ) && !( i == 0 && j == 0 && k == 0 );
                    // pr_debug( "Chunk Dir: %d %d %d:%d", i, j, k, needs_update );
                    // pr_debug( "                    Needs Updates: %d %d %d:%d", needs_update_x, needs_update_y, needs_update_z, needs_update );
                    // pr_debug( "                                New Offset: %d %d %d:%d", new_i, new_j, new_k, needs_update );

                    if ( needs_update ) {
                        fixup_chunk( loadedChunks, chunk, i, j, k, diff_x - CHUNK_SIZE * new_i, diff_y - CHUNK_SIZE * new_j, diff_z - CHUNK_SIZE * new_k, blockID );
                    }
                }
            }
        }
        chunk_set_block( chunk, TRIP_ARGS( diff_ ), blockID );
        //chunk_calculate_sides( chunk );
        chunk->ditry = 1;
        //chunk_create_display_list( chunk );
    } else {
        // This just means mouse is not pointing at a block
        // pr_debug( "Could not find the pointed to chunk" );
    }
}