#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>

#include "common/chunk.hpp"

struct TerrainWorkItem {
    Chunk *chunk;
    glm::ivec3 new_chunk_pos;
    int persist;
};

// Async server-diff remesh jobs (docs/chunks.md "Async server-diff remesh").
// One member per 26-connected dirty chunk: the render thread snapshots
// cells/extras into pooled buffers plus the chunk's cell_version and
// chunk_pos; a worker runs Chunk::build_mesh into `result` while the render
// thread keeps ownership of every buffer (freed at publish/discard).
struct MeshJobMember {
    Chunk *chunk;
    glm::ivec3 chunk_pos;
    uint32_t cell_version;
    BlockCell *cells;   // pooled snapshot or nullptr (all-air chunk)
    BlockExtra *extras; // pooled snapshot or nullptr
    ChunkMeshResult result;
};
struct MeshJob {
    uint32_t seq;
    std::vector<MeshJobMember> members;
};
// Runs build_mesh on every member of a job — called by the terrain workers;
// a free function so unit tests can drive a job inline without threads.
void mesh_job_run( MeshJob &job );
// Frees a job's remaining snapshot + result buffers. Render thread only —
// the buffers are always allocated and freed there.
void mesh_job_free_buffers( MeshJob &job );

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
    // Mesh jobs ride the same workers/mutex but pop BEFORE terrain items —
    // they are latency-sensitive (visible seams) and much cheaper than a
    // full terrain gen.
    std::deque<MeshJob> mesh_work_queue;
    bool stopping = false;
    mutable std::mutex result_mutex;
    std::deque<TerrainWorkItem> result_queue;
    std::deque<MeshJob> mesh_result_queue;
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

    // Async remesh plumbing: the render thread enqueues jobs, workers run
    // mesh_job_run and push to the result deque the render thread drains
    // non-blocking. mesh_pop_work lets tests run a queued job inline.
    void mesh_enqueue( MeshJob &&job );
    bool mesh_pop_work( MeshJob *out );
    bool mesh_pop_result( MeshJob *out );
    void mesh_queue_sizes( int *work, int *results ) const;
    // False where no worker pool exists (WASM without pthreads, or start()
    // failed) — the async remesh path force-disables in that case.
    bool has_workers( ) const;
};
