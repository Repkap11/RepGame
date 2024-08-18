#pragma once

#include "common/chunk.hpp"

struct LinkedList;
struct LinkedListValue;

class TerrainLoadingThread {
    MapStorage map_storage;
    LinkedList *work_linked_list;
    LinkedList *result_linked_list;

    void process_value( LinkedListValue *value );
    static void *process_background_tasks( void *arg );

  public:
    int start( MapStorage &map_storage );
    void enqueue( Chunk *chunk, const glm::ivec3 &new_chunk_pos, int persist );
    Chunk *dequeue( );
    void stop( );
};