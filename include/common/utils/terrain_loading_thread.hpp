#pragma once

#include <atomic>

#include "common/chunk.hpp"

struct LinkedList;
struct LinkedListValue;

class TerrainLoadingThread {
    MapStorage map_storage;
    LinkedList *work_linked_list;
    LinkedList *result_linked_list;
    // Loader-focus chunk column (render thread's chunk_center), so workers can
    // retarget stale requests instead of loading a position the camera has
    // already left. x/y/z packed via three atomics — a torn read just picks a
    // slightly-off center, which the dequeue-time bounds check still catches.
    // focus_radius is the active horizontal render radius: retargets that
    // land outside the window are dead work — generation is skipped and the
    // dequeue-time bounds check parks the slot.
    std::atomic<int> focus_x{ 0 }, focus_y{ 0 }, focus_z{ 0 }, focus_radius{ 0 };
    std::atomic<int> focus_valid{ 0 }; // 0 → retarget falls back to new_chunk_pos

    void process_value( LinkedListValue *value );
    static void *process_background_tasks( void *arg );

  public:
    int start( MapStorage &map_storage );
    void set_focus( const glm::ivec3 &chunk_pos, int render_radius );
    void enqueue( Chunk *chunk, const glm::ivec3 &new_chunk_pos, int persist );
    Chunk *dequeue( );
    void stop( );
    void queue_sizes( int *work, int *results ) const;
};