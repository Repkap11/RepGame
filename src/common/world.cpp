#include "common/world.hpp"
#include "common/RepGame.hpp"
#include "common/chunk.hpp"
#include "common/chunk_loader.hpp"
#include <math.h>

void world_init( LoadedChunks *loadedChunks ) {
    chunk_loader_init( loadedChunks );
    sky_box_init( &loadedChunks->skyBox );
}
void world_render( LoadedChunks *loadedChunks, TRIP_ARGS( float camera_ ) ) {
    chunk_loader_render_chunks( loadedChunks, TRIP_ARGS( camera_ ) );
}
void world_draw( LoadedChunks *loadedChunks, Texture *blocksTexture, glm::mat4 &mvp, glm::mat4 &mvp_sky, int debug ) {
    sky_box_draw( &loadedChunks->skyBox, &loadedChunks->renderer, mvp_sky );

    shader_set_uniform1i( &loadedChunks->shader, "u_Texture", blocksTexture->slot );
    shader_set_uniform_mat4f( &loadedChunks->shader, "u_MVP", mvp );
    float debug_block_scale;
    if ( debug ) {
        debug_block_scale = BLOCK_SCALE_OFFSET;
    } else {
        debug_block_scale = 0.0f;
    }
    shader_set_uniform3f( &loadedChunks->shader, "u_DebugScaleOffset", debug_block_scale, debug_block_scale, debug_block_scale );
    chunk_loader_calculate_cull( loadedChunks, mvp );
    chunk_loader_draw_mouse_selection( loadedChunks );
    chunk_loader_draw_chunks( loadedChunks, mvp );
}
void world_cleanup( LoadedChunks *loadedChunks ) {
    chunk_loader_cleanup( loadedChunks );
    sky_box_destroy( &loadedChunks->skyBox );
}

void fixup_chunk( LoadedChunks *loadedChunks, Chunk *chunk, TRIP_ARGS( int offset_ ), TRIP_ARGS( int pos_ ), BlockID blockID ) {
    // pr_debug( "Fixup Offset: %d %d %d", x, y, z );
    Chunk *fixupChunk = chunk_loader_get_chunk( loadedChunks, chunk->chunk_x + offset_x, chunk->chunk_y + offset_y, chunk->chunk_z + offset_z );
    if ( fixupChunk ) {
        chunk_unprogram_terrain( chunk );
        chunk_set_block( fixupChunk, TRIP_ARGS( pos_ ), blockID );
        fixupChunk->ditry = 1;
        chunk_calculate_popupated_blocks( fixupChunk );
        chunk_program_terrain( fixupChunk );
    }
}

Chunk *world_get_loaded_chunk( LoadedChunks *loadedChunks, TRIP_ARGS( int block_ ) ) {
    int chunk_x = floor( block_x / ( float )CHUNK_SIZE );
    int chunk_y = floor( block_y / ( float )CHUNK_SIZE );
    int chunk_z = floor( block_z / ( float )CHUNK_SIZE );
    return chunk_loader_get_chunk( loadedChunks, TRIP_ARGS( chunk_ ) );
}

BlockID world_get_block( Chunk *chunk, TRIP_ARGS( int block_ ) ) {
    int diff_x = block_x - chunk->chunk_x * CHUNK_SIZE;
    int diff_y = block_y - chunk->chunk_y * CHUNK_SIZE;
    int diff_z = block_z - chunk->chunk_z * CHUNK_SIZE;
    return chunk_get_block( chunk, TRIP_ARGS( diff_ ) );
}

BlockID world_get_loaded_block( LoadedChunks *loadedChunks, TRIP_ARGS( int block_ ) ) {
    Chunk *chunk = world_get_loaded_chunk( loadedChunks, TRIP_ARGS( block_ ) );
    if ( chunk ) {
        if ( chunk->is_loading ) {
            return LAST_BLOCK_ID;
        }
        BlockID blockID = world_get_block( chunk, TRIP_ARGS( block_ ) );
        return blockID;
    }
    return LAST_BLOCK_ID;
}

void world_set_block( LoadedChunks *loadedChunks, TRIP_ARGS( int block_ ), BlockID blockID ) {
    int chunk_x = floor( block_x / ( float )CHUNK_SIZE );
    int chunk_y = floor( block_y / ( float )CHUNK_SIZE );
    int chunk_z = floor( block_z / ( float )CHUNK_SIZE );

    Chunk *chunk = chunk_loader_get_chunk( loadedChunks, TRIP_ARGS( chunk_ ) );
    if ( chunk ) {
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
        chunk_unprogram_terrain( chunk );
        chunk_set_block( chunk, TRIP_ARGS( diff_ ), blockID );
        chunk->ditry = 1;
        chunk_calculate_popupated_blocks( chunk );
        chunk_program_terrain( chunk );
    } else {
        // This just means mouse is not pointing at a block
        // pr_debug( "Could not find the pointed to chunk" );
    }
}