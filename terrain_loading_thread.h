#ifndef HEADER_TERRAIN_LOADING_H
#define HEADER_TERRAIN_LOADING_H

#include "chunk.h"

int terrain_loading_thread_start( );
void terrain_loading_thread_enqueue( Chunk *chunk );
Chunk *terrain_loading_thread_dequeue( );
void terrain_loading_thread_stop( );

#endif