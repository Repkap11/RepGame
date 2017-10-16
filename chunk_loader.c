#include "chunk_loader.h"
#include "RepGame.h"
#include <math.h>
#include <stdlib.h>

#define CHUNK_RADIUS 5
#define MAX_LOADED_CHUNKS ( ( 2 * CHUNK_RADIUS + 1 ) * ( 2 * CHUNK_RADIUS + 1 ) * ( 2 * CHUNK_RADIUS + 1 ) )

void chunk_loader_init( LoadedChunks *loadedChunks ) {
    loadedChunks->loadedChunkArray = malloc( MAX_LOADED_CHUNKS * sizeof( LoadedChunk ) );
}

void chunk_loader_load_chunk( LoadedChunk *loadedChunk, int chunk_x, int chunk_y, int chunk_z ) {
    if ( loadedChunk->loaded ) {
        chunk_free( &loadedChunk->chunk );
    }
    loadedChunk->chunk.chunk_x = chunk_x;
    loadedChunk->chunk.chunk_y = chunk_y;
    loadedChunk->chunk.chunk_z = chunk_z;
    chunk_load( &loadedChunk->chunk );
    loadedChunk->loaded = 1;
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

void chunk_loader_render_chunks( LoadedChunks *loadedChunks, float camera_x, float camera_y, float camera_z ) {
    int chunk_x = floor( camera_x / CHUNK_SIZE );
    int chunk_y = floor( camera_y / CHUNK_SIZE );
    int chunk_z = floor( camera_z / CHUNK_SIZE );

    if ( ( loadedChunks->chunk_center_x != chunk_x ) || ( loadedChunks->chunk_center_y != chunk_y ) || ( loadedChunks->chunk_center_z != chunk_z ) || !loadedChunks->loaded_any ) {
        pr_debug( "\nMoved into chunk x:%d y:%d z:%d", chunk_x, chunk_y, chunk_z );
        //}

        if ( !loadedChunks->loaded_any ) {
            int nextChunk = 0;
            for ( int i = -CHUNK_RADIUS; i <= CHUNK_RADIUS; i++ ) {
                for ( int j = -CHUNK_RADIUS; j <= CHUNK_RADIUS; j++ ) {
                    for ( int k = -CHUNK_RADIUS; k <= CHUNK_RADIUS; k++ ) {
                        chunk_loader_load_chunk( &loadedChunks->loadedChunkArray[ nextChunk ], chunk_x + i, chunk_y + j, chunk_z + k );
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
                if ( dist_x_abs > CHUNK_RADIUS || //
                     dist_y_abs > CHUNK_RADIUS || //
                     dist_z_abs > CHUNK_RADIUS ) {
                    int sig_x = dist_x_abs == 0 ? 0 : dist_x / dist_x_abs;
                    int sig_y = dist_y_abs == 0 ? 0 : dist_y / dist_y_abs;
                    int sig_z = dist_z_abs == 0 ? 0 : dist_z / dist_z_abs;
                    pr_debug( "sig x:%d y:%d z:%d", sig_x, sig_y, sig_z );

                    int sig_x2 = ( dist_x_abs > CHUNK_RADIUS ) * ( sig_x );
                    int sig_y2 = ( dist_y_abs > CHUNK_RADIUS ) * ( sig_y );
                    int sig_z2 = ( dist_z_abs > CHUNK_RADIUS ) * ( sig_z );

                    pr_debug( "si2 x:%d y:%d z:%d", sig_x2, sig_y2, sig_z2 );
                    // pr_debug( "change x:%d y:%d z:%d", dist_x, dist_y, dist_z );

                    // if ( !loadedChunks->loaded_any ) {
                    // if ( chunk_x == 0 && chunk_y == 0 && chunk_z == 0 ) {
                    pr_debug( "Freeing chunk  x:%d y:%d z:%d", loadedChunk->chunk.chunk_x, loadedChunk->chunk.chunk_y, loadedChunk->chunk.chunk_z );
                    // pr_debug( " Freed because x:%d y:%d z:%d", dist_x, dist_y, dist_z );
                    pr_debug( "Alloc   chunk  x:%d y:%d z:%d", loadedChunk->chunk.chunk_x - 2 * dist_x + sig_x2, loadedChunk->chunk.chunk_y - 2 * dist_y + sig_y2, loadedChunk->chunk.chunk_z - 2 * dist_z + sig_z2 );
                    //}
                    //}
                    // chunk_free( &loadedChunk->chunk );
                    chunk_loader_load_chunk( &loadedChunks->loadedChunkArray[ i ], loadedChunk->chunk.chunk_x - 2 * dist_x + sig_x2, loadedChunk->chunk.chunk_y - 2 * dist_y + sig_y2, loadedChunk->chunk.chunk_z - 2 * dist_z + sig_z2 );

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