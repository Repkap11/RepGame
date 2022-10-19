#ifndef HEADER_WORLD_H
#define HEADER_WORLD_H

#include "block.hpp"
#include "chunk_loader.hpp"
#include "mobs.hpp"
#include "common/abstract/full_screen_quad.hpp"

typedef struct {
    LoadedChunks loadedChunks;
    Renderer renderer;
    SkyBox skyBox;
    Mobs mobs;
    VertexBufferLayout vbl_block;
    VertexBufferLayout vbl_coords;
    VertexBufferLayout vbl_object_vertex;
    VertexBufferLayout vbl_object_position;
    Texture reflectionTexture;
    Texture blockTexture;
    Texture depthStencilTexture;
    FrameBuffer frameBuffer;
    FullScreenQuad fullScreenQuad;
    MouseSelection mouseSelection;
    Shader object_shader;
} World;

BlockState world_get_loaded_block( World *world, const glm::ivec3 &block );
void world_set_loaded_block( World *world, const glm::ivec3 &block, BlockState blockState );

BlockState world_get_block_from_chunk( Chunk *chunk, const glm::ivec3 &block );
Chunk *world_get_loaded_chunk( World *world, const glm::ivec3 &block );
void world_init( World *world, const glm::vec3 &camera_pos );
void world_change_size( World *world, int width, int height );
void world_render( World *world, const glm::vec3 &camera_pos, int limit_render, const glm::mat4 &rotation );
void world_draw( World *world, Texture *blocksTexture, const glm::mat4 &mvp, const glm::mat4 &mvp_reflect, const glm::mat4 &mvp_sky, const glm::mat4 &mvp_sky_reflect, int debug, int draw_mouse_selection, float y_height, bool headInWater,
                 bool drawReflectionsIfSupported );
void world_process_random_ticks(World *world);
void world_set_selected_block( World *world, const glm::ivec3 &selected, int shouldDraw );
void world_cleanup( World *world );

#endif