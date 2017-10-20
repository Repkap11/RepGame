#include "chunk_loader.h"
#include "RepGame.h"
#include "terrain_loading_thread.h"
#include <math.h>
#include <stdlib.h>

#define MAX_LOADED_CHUNKS ( ( 2 * CHUNK_RADIUS_X + 1 ) * ( 2 * CHUNK_RADIUS_Y + 1 ) * ( 2 * CHUNK_RADIUS_Z + 1 ) )

void chunk_loader_init( LoadedChunks *loadedChunks ) {
    loadedChunks->chunkArray = calloc( MAX_LOADED_CHUNKS, sizeof( Chunk ) );
}

// int chunk_loader_is_chunk_loaded( LoadedChunks *loadedChunks, int chunk_x, int chunk_y, int chunk_z ) {
//     for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
//         Chunk *loadedChunk = &loadedChunks->chunkArray[ i ];
//         if ( loadedChunk->loaded ) {
//             if ( loadedChunk->chunk_x == chunk_x && loadedChunk->chunk_y == chunk_y && loadedChunk->chunk_z == chunk_z ) {
//                 return 1;
//             }
//         }
//     }
//     return 0;
// }

// void chunk_loader_load_chunk( LoadedChunks *loadedChunks, int index, int chunk_x, int chunk_y, int chunk_z ) {
//     Chunk *loadedChunk = &loadedChunks->chunkArray[ index ];
//     // if ( loadedChunk->loaded ) {
// }

void chunk_loader_render_chunks( LoadedChunks *loadedChunks, float camera_x, float camera_y, float camera_z ) {
    int chunk_x = floor( camera_x / ( float )CHUNK_SIZE );
    int chunk_y = floor( camera_y / ( float )CHUNK_SIZE );
    int chunk_z = floor( camera_z / ( float )CHUNK_SIZE );

    Chunk *chunk;
    int count = 0;
    do {
        chunk = terrain_loading_thread_dequeue( );
        if ( chunk ) {
            // pr_debug( "Paul Loading terrain x:%d y%d: z:%d", chunk->chunk_x, chunk->chunk_y, chunk->chunk_z );
            chunk_create_display_list( chunk );
            chunk->loaded = 1;
        }
        count += 1;
    } while ( chunk && count < CHUNK_RENDERS_PER_FRAME );

    if ( ( loadedChunks->chunk_center_x != chunk_x ) || ( loadedChunks->chunk_center_y != chunk_y ) || ( loadedChunks->chunk_center_z != chunk_z ) || !loadedChunks->loaded_any ) {
        // pr_debug( "Moved into chunk x:%d y:%d z:%d", chunk_x, chunk_y, chunk_z );

        if ( !loadedChunks->loaded_any ) {
            int nextChunk = 0;
            for ( int i = -CHUNK_RADIUS_X; i <= CHUNK_RADIUS_X; i++ ) {
                int new_i = ( i + CHUNK_RADIUS_X );
                new_i = ( ( new_i * ( new_i % 2 ? 1 : -1 ) ) + ( ( new_i % 2 ) ? 1 : 0 ) ) / 2;
                // pr_debug( "i:%d new_i:%d", i, new_i );
                for ( int j = -CHUNK_RADIUS_Y; j <= CHUNK_RADIUS_Y; j++ ) {
                    int new_j = ( j + CHUNK_RADIUS_Y );
                    new_j = ( ( new_j * ( new_j % 2 ? 1 : -1 ) ) + ( ( new_j % 2 ) ? 1 : 0 ) ) / 2;
                    for ( int k = -CHUNK_RADIUS_Z; k <= CHUNK_RADIUS_Z; k++ ) {
                        int new_k = ( k + CHUNK_RADIUS_Z );
                        new_k = ( ( new_k * ( new_k % 2 ? 1 : -1 ) ) + ( ( new_k % 2 ) ? 1 : 0 ) ) / 2;

                        // pr_debug( "Initing chunk %d", nextChunk );

                        loadedChunks->chunkArray[ nextChunk ].chunk_x = chunk_x + new_i;
                        loadedChunks->chunkArray[ nextChunk ].chunk_y = chunk_y + new_j;
                        loadedChunks->chunkArray[ nextChunk ].chunk_z = chunk_z + new_k;
                        terrain_loading_thread_enqueue( &loadedChunks->chunkArray[ nextChunk ] );
                        nextChunk = ( nextChunk + 1 );
                    }
                }
            }
        }

        for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
            Chunk *loadedChunk = &loadedChunks->chunkArray[ i ];
            if ( loadedChunk->loaded ) {
                int dist_x = loadedChunk->chunk_x - chunk_x;
                int dist_y = loadedChunk->chunk_y - chunk_y;
                int dist_z = loadedChunk->chunk_z - chunk_z;
                int dist_x_abs = abs( dist_x );
                int dist_y_abs = abs( dist_y );
                int dist_z_abs = abs( dist_z );

                // if ( dist_x_abs * dist_x_abs + dist_y_abs * dist_y_abs + dist_z_abs * dist_z_abs > CHUNK_RADIUS * CHUNK_RADIUS ) {
                if ( dist_x_abs > CHUNK_RADIUS_X || //
                     dist_y_abs > CHUNK_RADIUS_Y || //
                     dist_z_abs > CHUNK_RADIUS_Z ) {

                    int sig_x = loadedChunks->chunk_center_x - chunk_x;
                    int sig_y = loadedChunks->chunk_center_y - chunk_y;
                    int sig_z = loadedChunks->chunk_center_z - chunk_z;
                    // pr_debug( "sig x:%d y:%d z:%d", sig_x, sig_y, sig_z );

                    // int sig_x2 = ( dist_x_abs > CHUNK_RADIUS ) * ( sig_x );
                    // int sig_y2 = ( dist_y_abs > CHUNK_RADIUS ) * ( sig_y );
                    // int sig_z2 = ( dist_z_abs > CHUNK_RADIUS ) * ( sig_z );

                    // pr_debug( "change x:%d y:%d z:%d", dist_x, dist_y, dist_z );

                    // if ( !loadedChunks->loaded_any ) {
                    // if ( chunk_x == 0 && chunk_y == 0 && chunk_z == 0 ) {
                    // pr_debug( "Freeing chunk  x:%d y:%d z:%d", loadedChunk->chunk.chunk_x, loadedChunk->chunk.chunk_y, loadedChunk->chunk.chunk_z );
                    // pr_debug( " Freed because x:%d y:%d z:%d", dist_x, dist_y, dist_z );
                    // pr_debug( "Alloc   chunk  x:%d y:%d z:%d", loadedChunk->chunk.chunk_x - 2 * dist_x + sig_x, loadedChunk->chunk.chunk_y - 2 * dist_y + sig_y, loadedChunk->chunk.chunk_z - 2 * dist_z + sig_z );
                    //}
                    //}
                    // chunk_free( &loadedChunk->chunk );
                    // chunk_loader_load_chunk( loadedChunks, i, , , );

                    chunk_destroy_display_list( loadedChunk );
                    // chunk_free_terrain( loadedChunk );
                    //}
                    // if ( chunk_loader_is_chunk_loaded( loadedChunks, chunk_x, chunk_y, chunk_z ) ) {
                    //    pr_debug( "Paul Error" );
                    //}
                    loadedChunk->chunk_x = loadedChunk->chunk_x - 2 * dist_x + sig_x;
                    loadedChunk->chunk_y = loadedChunk->chunk_y - 2 * dist_y + sig_y;
                    loadedChunk->chunk_z = loadedChunk->chunk_z - 2 * dist_z + sig_z;
                    terrain_loading_thread_enqueue( loadedChunk );
                    // chunk_x = 10 //camera
                    // loadedChunk->chunk.chunk_x = 5 // chunk
                    // = 2 * chunk_x - loadedChunk->chunk.chunk_x + sig( loadedChunk->chunk.chunk_y - chunk )

                    // = 2 * 10 - 5 + -1
                    //  = 20 - 5 + -1
                    // loadedChunk->loaded = 0;
                }
            }
        }
        loadedChunks->loaded_any = 1;

        // pr_debug( "Done with chunk cycle" );
        // if ( !chunk_loader_is_chunk_loaded( loadedChunks, chunk_x, chunk_y, chunk_z ) ) {
        //     chunk_loader_load_chunk( &loadedChunks->loadedChunkArray[ nextChunk ], chunk_x, chunk_y, chunk_z );
        // } else {
        //     pr_debug( "Not loading chunk x:%d y:%d z:%d because its already loaded", chunk_x, chunk_y, chunk_z );
        //     return;
        // }
        loadedChunks->chunk_center_x = chunk_x;
        loadedChunks->chunk_center_y = chunk_y;
        loadedChunks->chunk_center_z = chunk_z;
    }
}

void chunk_loader_draw_chunks( LoadedChunks *loadedChunks ) {
    // pr_debug( "Drawing %d chunks", loadedChunks->numLoadedChunks );
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        // pr_debug( "Drawing chunk %d", i );
        chunk_draw( &loadedChunks->chunkArray[ i ], 1 );
    }
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        // pr_debug( "Drawing chunk %d", i );
        chunk_draw( &loadedChunks->chunkArray[ i ], 0 );
    }
}

void chunk_loader_free_chunks( LoadedChunks *loadedChunks ) {
    // pr_debug( "Freeing %d chunks", loadedChunks->numLoadedChunks );
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        chunk_destroy_display_list( &loadedChunks->chunkArray[ i ] );
        chunk_free_terrain( &loadedChunks->chunkArray[ i ] );
    }
    free( loadedChunks->chunkArray );
}