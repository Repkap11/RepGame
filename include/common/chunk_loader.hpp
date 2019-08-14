#ifndef HEADER_CHUNK_LOADER_H
#define HEADER_CHUNK_LOADER_H
#include "chunk.hpp"
#include "abstract/renderer.hpp"
#include "mouse_selection.hpp"
#include "sky_box.hpp"

typedef struct {
    int chunk_center_x, chunk_center_y, chunk_center_z;
    Chunk *chunkArray;
    Shader shader;
    struct {
        VertexBuffer vb_block;
    } solid;
    struct {
        VertexBuffer vb_block;
    } water;
} LoadedChunks;

void chunk_loader_init( LoadedChunks *loadedChunks, float camera_x, float camera_y, float camera_z, VertexBufferLayout *vbl_block, VertexBufferLayout *vbl_coords );
void chunk_loader_render_chunks( LoadedChunks *loadedChunks, float camera_x, float camera_y, float camera_z, int limit_render );
void chunk_loader_calculate_cull( LoadedChunks *loadedChunks, glm::mat4 &mvp );
void chunk_loader_draw_chunks( LoadedChunks *loadedChunks, glm::mat4 &mvp, Renderer *renderer, bool reflect_only );
Chunk *chunk_loader_get_chunk( LoadedChunks *loadedChunks, int pointed_x, int pointed_y, int pointed_z );
void chunk_loader_cleanup( LoadedChunks *loadedChunks );

#endif