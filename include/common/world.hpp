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
    FrameBuffer frameBuffer;
    FullScreenQuad fullScreenQuad;

    MouseSelection mouseSelection;
    Shader object_shader;
} World;

BlockState world_get_loaded_block( World *world, int block_x, int block_y, int block_z );
void world_set_loaded_block( World *world, int block_x, int block_y, int block_z, BlockState blockState );

BlockState world_get_block_from_chunk( Chunk *chunk, int block_, int block_y, int block_z );
Chunk *world_get_loaded_chunk( World *world, int block_x, int block_y, int block_z );
void world_init( World *world, float camera_x, float camera_y, float camera_z );
void world_change_size(World *world, int width, int height);
void world_render( World *world, float camera_x, float camera_y, float camera_z, int limit_render, const glm::mat4 &rotation );
void world_draw( World *world, Texture *blocksTexture, const glm::mat4 &mvp, const glm::mat4 &mvp_reflect, const glm::mat4 &mvp_sky, const glm::mat4 &mvp_sky_reflect, int debug, int draw_mouse_selection, float y_height, bool headInWater );
void world_set_selected_block( World *world, int selected_x, int selected_y, int selected_z, int shouldDraw );
void world_cleanup( World *world );

#endif