#include "chunk_loader.h"
#include "RepGame.h"
#include <math.h>
#include <stdlib.h>

#define CHUNK_RADIUS_X 13
#define CHUNK_RADIUS_Y 4
#define CHUNK_RADIUS_Z 13
#define MAX_LOADED_CHUNKS ( ( 2 * CHUNK_RADIUS_X + 1 ) * ( 2 * CHUNK_RADIUS_Y + 1 ) * ( 2 * CHUNK_RADIUS_Z + 1 ) )

void chunk_loader_init( LoadedChunks *loadedChunks ) {
    loadedChunks->loadedChunkArray = malloc( MAX_LOADED_CHUNKS * sizeof( LoadedChunk ) );
}

int chunk_loader_is_chunk_loaded( LoadedChunks *loadedChunks, int chunk_x, int chunk_y, int chunk_z ) {
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        LoadedChunk *loadedChunk = &loadedChunks->loadedChunkArray[ i ];
        if ( loadedChunk->loaded ) {
            if ( loadedChunk->chunk.chunk_x == chunk_x && loadedChunk->chunk.chunk_y == chunk_y && loadedChunk->chunk.chunk_z == chunk_z ) {
                return 1;
            }
        }
    }
    return 0;
}

void chunk_loader_load_chunk( LoadedChunks *loadedChunks, int index, int chunk_x, int chunk_y, int chunk_z ) {
    LoadedChunk *loadedChunk = &loadedChunks->loadedChunkArray[ index ];
    if ( loadedChunk->loaded ) {
        chunk_free( &loadedChunk->chunk );
    }
    if ( chunk_loader_is_chunk_loaded( loadedChunks, chunk_x, chunk_y, chunk_z ) ) {
        pr_debug( "Paul Error" );
    }
    loadedChunk->chunk.chunk_x = chunk_x;
    loadedChunk->chunk.chunk_y = chunk_y;
    loadedChunk->chunk.chunk_z = chunk_z;
    terrain_loading_thread_enqueue( int index, &loadedChunk->chunk );
}

void chunk_loader_render_chunks( LoadedChunks *loadedChunks, float camera_x, float camera_y, float camera_z ) {
    int chunk_x = floor( camera_x / CHUNK_SIZE );
    int chunk_y = floor( camera_y / CHUNK_SIZE );
    int chunk_z = floor( camera_z / CHUNK_SIZE );

    loadedChunk->loaded = 1;

    if ( ( loadedChunks->chunk_center_x != chunk_x ) || ( loadedChunks->chunk_center_y != chunk_y ) || ( loadedChunks->chunk_center_z != chunk_z ) || !loadedChunks->loaded_any ) {
        // pr_debug( "Moved into chunk x:%d y:%d z:%d", chunk_x, chunk_y, chunk_z );

        if ( !loadedChunks->loaded_any ) {
            int nextChunk = 0;
            for ( int i = -CHUNK_RADIUS_X; i <= CHUNK_RADIUS_X; i++ ) {
                for ( int j = -CHUNK_RADIUS_Y; j <= CHUNK_RADIUS_Y; j++ ) {
                    for ( int k = -CHUNK_RADIUS_Z; k <= CHUNK_RADIUS_Z; k++ ) {
                        chunk_loader_load_chunk( loadedChunks, nextChunk, chunk_x + i, chunk_y + j, chunk_z + k );
                        // pr_debug( "Initing chunk %d", nextChunk );
                        nextChunk = ( nextChunk + 1 );
                    }
                }
            }
        }

        for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
            LoadedChunk *loadedChunk = &loadedChunks->loadedChunkArray[ i ];
            if ( loadedChunk->loaded ) {
                int dist_x = loadedChunk->chunk.chunk_x - chunk_x;
                int dist_y = loadedChunk->chunk.chunk_y - chunk_y;
                int dist_z = loadedChunk->chunk.chunk_z - chunk_z;
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
                    chunk_loader_load_chunk( loadedChunks, i, loadedChunk->chunk.chunk_x - 2 * dist_x + sig_x, loadedChunk->chunk.chunk_y - 2 * dist_y + sig_y, loadedChunk->chunk.chunk_z - 2 * dist_z + sig_z );

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
        if ( loadedChunks->loadedChunkArray[ i ].loaded ) {
            chunk_draw( &loadedChunks->loadedChunkArray[ i ].chunk );
        }
    }
}

void chunk_loader_free_chunks( LoadedChunks *loadedChunks ) {
    // pr_debug( "Freeing %d chunks", loadedChunks->numLoadedChunks );
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        chunk_free( &loadedChunks->loadedChunkArray[ i ].chunk );
    }
    free( loadedChunks->loadedChunkArray );
}