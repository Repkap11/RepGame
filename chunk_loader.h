#ifndef HEADER_CHUNK_LOADER_H
#define HEADER_CHUNK_LOADER_H
#include "chunk.h"

typedef struct {
    int loaded_any;
    int numLoadedChunks;
    int chunk_center_x, chunk_center_y, chunk_center_z;
    Chunk *chunks;
} LoadedChunks;

void chunk_loader_render_chunks( LoadedChunks *loadedChunks, int camera_x, int camera_y, int camera_z );
void chunk_loader_draw_chunks( LoadedChunks *loadedChunks );
void chunk_loader_free_chunks( LoadedChunks *loadedChunks );

#endif