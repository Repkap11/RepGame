#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>

#include "common/chunk.hpp"

struct TerrainWorkItem {
    Chunk *chunk;
    glm::ivec3 new_chunk_pos;
    int persist;
};

class TerrainLoadingThread {
    // Points at the world's MapStorage (bound in start) rather than copying
    // it: the chunk_io_enabled gate must be shared — a private copy would
    // keep doing file IO after multiplayer flips the gate.
    MapStorage *map_storage = nullptr;
    // Work queue: the render thread pushes and workers pop; work_cv lets
    // idle workers sleep until work arrives instead of polling. Result
    // queue: workers push, the render thread pops non-blocking. Both FIFO.
    mutable std::mutex work_mutex;
    std::condition_variable work_cv;
    std::deque<TerrainWorkItem> work_queue;
    bool stopping = false;
    mutable std::mutex result_mutex;
    std::deque<TerrainWorkItem> result_queue;
    // Loader-focus chunk column (render thread's chunk_center), so workers can
    // retarget stale requests instead of loading a position the camera has
    // already left. x/y/z packed via three atomics — a torn read just picks a
    // slightly-off center, which the dequeue-time bounds check still catches.
    // focus_radius is the active horizontal render radius: retargets that
    // land outside the window are dead work — generation is skipped and the
    // dequeue-time bounds check parks the slot.
    std::atomic<int> focus_x{ 0 }, focus_y{ 0 }, focus_z{ 0 }, focus_radius{ 0 };
    std::atomic<int> focus_valid{ 0 }; // 0 → retarget falls back to new_chunk_pos

    void process_value( TerrainWorkItem &item );
    static void *process_background_tasks( void *arg );

  public:
    int start( MapStorage &map_storage );
    void set_focus( const glm::ivec3 &chunk_pos, int render_radius );
    void enqueue( Chunk *chunk, const glm::ivec3 &new_chunk_pos, int persist );
    Chunk *dequeue( );
    void stop( );
    void queue_sizes( int *work, int *results ) const;
};
