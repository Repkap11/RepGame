#ifndef HEADER_CHUNK_LOADER_H
#define HEADER_CHUNK_LOADER_H
#include "chunk.h"
#include "abstract/renderer.h"

typedef struct {
    int chunk_center_x, chunk_center_y, chunk_center_z;
    Chunk *chunkArray;
    Renderer renderer;
    Shader shader;
    Texture blocksTexture;
    struct {
        VertexBuffer vb_block;
    } solid;
    struct {
        VertexBuffer vb_block;
    } water;
    VertexBufferLayout vbl_block;
} LoadedChunks;

void chunk_loader_init( LoadedChunks *loadedChunks );
void chunk_loader_render_chunks( LoadedChunks *loadedChunks, float camera_x, float camera_y, float camera_z );
void chunk_loader_draw_chunks_solid( LoadedChunks *loadedChunks, glm::mat4 &mvp );
void chunk_loader_draw_chunks_liquid( LoadedChunks *loadedChunks, glm::mat4 &mvp );
Chunk *chunk_loader_get_chunk( LoadedChunks *loadedChunks, int pointed_x, int pointed_y, int pointed_z );
void chunk_loader_cleanup( LoadedChunks *loadedChunks );

#endif