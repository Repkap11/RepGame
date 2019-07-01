#ifndef HEADER_CHUNK_LOADER_H
#define HEADER_CHUNK_LOADER_H
#include "chunk.hpp"
#include "abstract/renderer.hpp"
#include "mouse_selection.hpp"
#include "sky_box.hpp"

typedef struct {
    int chunk_center_x, chunk_center_y, chunk_center_z;
    Chunk *chunkArray;
    Renderer renderer;
    Shader shader;
    MouseSelection mouseSelection;
    struct {
        VertexBuffer vb_block;
    } solid;
    struct {
        VertexBuffer vb_block;
    } water;
    struct {
        VertexBuffer vb_block;
    } flowers;
    VertexBufferLayout vbl_block;
    VertexBufferLayout vbl_coords;
    SkyBox skyBox;
} LoadedChunks;

void chunk_loader_init( LoadedChunks *loadedChunks );
void chunk_loader_render_chunks( LoadedChunks *loadedChunks, float camera_x, float camera_y, float camera_z );
void chunk_loader_calculate_cull( LoadedChunks *loadedChunks, glm::mat4 &mvp );
void chunk_loader_draw_chunks( LoadedChunks *loadedChunks, glm::mat4 &mvp );
Chunk *chunk_loader_get_chunk( LoadedChunks *loadedChunks, int pointed_x, int pointed_y, int pointed_z );
void chunk_loader_cleanup( LoadedChunks *loadedChunks );

void chunk_loader_set_selected_block( LoadedChunks *loadedChunks, int selected_x, int selected_y, int selected_z, int shouldDraw );
void chunk_loader_draw_mouse_selection( LoadedChunks *loadedChunks );

#endif