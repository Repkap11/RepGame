#ifndef HEADER_TERRAIN_LOADING_H
#define HEADER_TERRAIN_LOADING_H

#include "common/chunk.hpp"
#include "common/RepGame.hpp"
#include "common/utils/linked_list.hpp"

class TerrainLoadingThread {

    static void process_value( LinkedListValue *value );
    static void *process_background_tasks( void *arg );

  public:
    static int start( );
    static void enqueue( Chunk *chunk, const glm::ivec3 &new_chunk_pos, int persist );
    static Chunk *dequeue( );
    static void stop( );
};

#endif