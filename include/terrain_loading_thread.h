#ifndef HEADER_TERRAIN_LOADING_H
#define HEADER_TERRAIN_LOADING_H

#include "chunk.h"
#include "RepGame.h"

int terrain_loading_thread_start( );
void terrain_loading_thread_enqueue( Chunk *chunk, TRIP_ARGS(int new_chunk_) , int persist);
Chunk *terrain_loading_thread_dequeue( );
void terrain_loading_thread_stop( );

#endif