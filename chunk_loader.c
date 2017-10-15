#include "chunk_loader.h"
#include "RepGame.h"
#include <stdlib.h>

int maxChunks = 20;
int loaded = 0;
void chunk_loader_render_chunks( LoadedChunks *loadedChunks, int camera_x, int camera_y, int camera_z ) {
    int chunk_x = ( camera_x ) / CHUNK_SIZE;
    int chunk_y = ( camera_y ) / CHUNK_SIZE;
    int chunk_z = ( camera_z ) / CHUNK_SIZE;
    if ( !loaded ) {

        loadedChunks->chunks = malloc( maxChunks * sizeof( Chunk ) );
        loadedChunks->numLoadedChunks = 0;
        loaded = 1;
    }

    if ( ( loadedChunks->chunk_center_x != chunk_x ) || ( loadedChunks->chunk_center_y != chunk_y ) || ( loadedChunks->chunk_center_z != chunk_z ) || !loadedChunks->loaded_any ) {
        pr_debug( "Moved into chunk x:%d y:%d z:%d num:%d", chunk_x, chunk_y, chunk_z, loadedChunks->numLoadedChunks );
        loadedChunks->chunks[ loadedChunks->numLoadedChunks ].chunk_x = chunk_x;
        loadedChunks->chunks[ loadedChunks->numLoadedChunks ].chunk_y = chunk_y;
        loadedChunks->chunks[ loadedChunks->numLoadedChunks ].chunk_z = chunk_z;
        chunk_load( &loadedChunks->chunks[ loadedChunks->numLoadedChunks ] );
        chunk_create_display_list( &loadedChunks->chunks[ loadedChunks->numLoadedChunks ] );
        loadedChunks->numLoadedChunks = loadedChunks->numLoadedChunks + 1;

        loadedChunks->chunk_center_x = chunk_x;
        loadedChunks->chunk_center_y = chunk_y;
        loadedChunks->chunk_center_z = chunk_z;
        loadedChunks->loaded_any = 1;
        //}
    }
}

void chunk_loader_draw_chunks( LoadedChunks *loadedChunks ) {
    // pr_debug( "Drawing %d chunks", loadedChunks->numLoadedChunks );
    for ( int i = 0; i < loadedChunks->numLoadedChunks; i++ ) {
        // pr_debug( "Drawing chunk %d", i );
        chunk_draw( &loadedChunks->chunks[ i ] );
    }
}

void chunk_loader_free_chunks( LoadedChunks *loadedChunks ) {
    // pr_debug( "Freeing %d chunks", loadedChunks->numLoadedChunks );
    for ( int i = 0; i < loadedChunks->numLoadedChunks; i++ ) {
        chunk_free( &loadedChunks->chunks[ i ] );
    }
    free( loadedChunks->chunks );
}