#ifndef HEADER_CHUNK_LOADER_H
#define HEADER_CHUNK_LOADER_H
#include "chunk.h"

typedef struct {
    int loaded;
    Chunk chunk;
} LoadedChunk;

typedef struct {
    int loaded_any;
    int chunk_center_x, chunk_center_y, chunk_center_z;
    LoadedChunk *loadedChunkArray;
} LoadedChunks;

void chunk_loader_init( LoadedChunks *loadedChunks );
void chunk_loader_render_chunks( LoadedChunks *loadedChunks, float camera_x, float camera_y, float camera_z );
void chunk_loader_draw_chunks( LoadedChunks *loadedChunks );
void chunk_loader_free_chunks( LoadedChunks *loadedChunks );

#endif