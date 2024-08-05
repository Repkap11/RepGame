#ifndef HEADER_CHUNK_LOADER_H
#define HEADER_CHUNK_LOADER_H
#include "chunk.hpp"
#include "abstract/renderer.hpp"
#include "mouse_selection.hpp"
#include "sky_box.hpp"

typedef struct {
    glm::ivec3 chunk_center;
    Chunk *chunkArray;
    Shader shader;
    struct {
        VertexBuffer vb_block;
    } solid;
    struct {
        VertexBuffer vb_block;
    } water;
} LoadedChunks;

#define MAX_LOADED_CHUNKS ( ( 2 * CHUNK_RADIUS_X + 1 ) * ( 2 * CHUNK_RADIUS_Y + 1 ) * ( 2 * CHUNK_RADIUS_Z + 1 ) )

void chunk_loader_init( LoadedChunks *loadedChunks, const glm::vec3 &camera_pos, VertexBufferLayout *vbl_block, VertexBufferLayout *vbl_coords );
void chunk_loader_render_chunks( LoadedChunks *loadedChunks, const glm::vec3 &camera_pos, int limit_render );
void chunk_loader_repopulate_blocks( LoadedChunks *loadedChunks );
void chunk_loader_calculate_cull( LoadedChunks *loadedChunks, const glm::mat4 &mvp, bool saveAsReflection );
void chunk_loader_draw_chunks( LoadedChunks *loadedChunks, const glm::mat4 &mvp, Renderer *renderer, const Texture *texture, bool reflect_only, bool draw_reflect );
Chunk *chunk_loader_get_chunk( LoadedChunks *loadedChunks, const glm::ivec3 &pointed );
void chunk_loader_cleanup( LoadedChunks *loadedChunks );

#endif