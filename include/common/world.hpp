#ifndef HEADER_WORLD_H
#define HEADER_WORLD_H

#include "block.hpp"
#include "RepGame.hpp"

BlockID world_get_loaded_block( LoadedChunks *loadedChunks, TRIP_ARGS( int block_ ) );
void world_set_loaded_block( LoadedChunks *loadedChunks, int block_x, int block_y, int block_z, BlockID blockID );

BlockID world_get_block_from_chunk( Chunk *chunk, TRIP_ARGS( int block_ ) );
Chunk *world_get_loaded_chunk( LoadedChunks *loadedChunks, TRIP_ARGS( int block_ ) );
void world_init( LoadedChunks *loadedChunks );
void world_render( LoadedChunks *loadedChunks, float camera_x, float camera_y, float camera_z );
void world_draw( LoadedChunks *loadedChunks, Texture *blocksTexture, glm::mat4 &mvp, glm::mat4 &mvp_sky, int debug, int draw_mouse_selection );
void world_cleanup( LoadedChunks *loadedChunks );

#endif