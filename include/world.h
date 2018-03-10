#ifndef HEADER_WORLD_H
#define HEADER_WORLD_H

#include "block_definitions.h"
#include "RepGame.h"

BlockID world_get_block( LoadedChunks *loadedChunks, int block_x, int block_y, int block_z );
void world_set_block( LoadedChunks *loadedChunks, int block_x, int block_y, int block_z, BlockID blockID );
void world_init( LoadedChunks *loadedChunks );
void world_render( LoadedChunks *loadedChunks, float camera_x, float camera_y, float camera_z );
void world_draw( LoadedChunks *loadedChunks );
void world_cleanup( LoadedChunks *loadedChunks );

#endif