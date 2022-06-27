#ifndef HEADER_TERRAIN_LOADING_H
#define HEADER_TERRAIN_LOADING_H

#include "common/chunk.hpp"
#include "common/RepGame.hpp"

int terrain_loading_thread_start( );
void terrain_loading_thread_enqueue( Chunk *chunk, const glm::ivec3 &new_chunk_pos, int persist );
Chunk *terrain_loading_thread_dequeue( );
void terrain_loading_thread_stop( );

#endif