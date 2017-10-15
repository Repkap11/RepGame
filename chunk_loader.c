#include "chunk_loader.h"
#include "RepGame.h"
#include <math.h>
#include <stdlib.h>

#define CHUNK_RADIUS 2
#define MAX_LOADED_CHUNKS ( CHUNK_RADIUS * CHUNK_RADIUS * CHUNK_RADIUS )

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

int nextChunk = 0;
void chunk_loader_render_chunks( LoadedChunks *loadedChunks, float camera_x, float camera_y, float camera_z ) {
    int chunk_x = floor( camera_x / CHUNK_SIZE );
    int chunk_y = floor( camera_y / CHUNK_SIZE );
    int chunk_z = floor( camera_z / CHUNK_SIZE );

    if ( ( loadedChunks->chunk_center_x != chunk_x ) || ( loadedChunks->chunk_center_y != chunk_y ) || ( loadedChunks->chunk_center_z != chunk_z ) || !loadedChunks->loaded_any ) {
        pr_debug( "Moved into chunk x:%d y:%d z:%d num:%d", chunk_x, chunk_y, chunk_z, nextChunk );
        if ( chunk_loader_is_chunk_loaded( loadedChunks, chunk_x, chunk_y, chunk_z ) ) {
            pr_debug( "Not loading chunk x:%d y:%d z:%d because its already loaded", chunk_x, chunk_y, chunk_z );
            return;
        }
        chunk_loader_load_chunk( &loadedChunks->loadedChunkArray[ nextChunk ], chunk_x, chunk_y, chunk_z );
        nextChunk = ( nextChunk + 1 ) % MAX_LOADED_CHUNKS;
        loadedChunks->chunk_center_x = chunk_x;
        loadedChunks->chunk_center_y = chunk_y;
        loadedChunks->chunk_center_z = chunk_z;
        loadedChunks->loaded_any = 1;
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